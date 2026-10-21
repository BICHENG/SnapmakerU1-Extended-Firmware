import json
import math
import os
import subprocess
import tempfile


CONFIG_DIR = os.environ.get(
    'FAN_CURVES_CONFIG_DIR', '/oem/printer_data/config/extended')
ACTIVE_PATH = os.path.join(CONFIG_DIR, 'fan-curves-active.json')
CUSTOM_PATH = os.path.join(CONFIG_DIR, 'fan-curves.json')
PRESET_DIR = os.environ.get(
    'FAN_CURVES_PRESET_DIR', '/usr/local/share/firmware-config/tweaks/klipper')
# Personal curves live in their own directory. The user directory wins over
# the built-in one, so a personal file can replace a shipped preset.
USER_PRESET_DIR = os.path.join(CONFIG_DIR, 'fan-curves-presets')
PRESET_PREFIX = 'fan_curves_'
PRESET_SUFFIX = '.json'
STOCK_PROFILE = 'stock'
CUSTOM_PROFILE = 'custom'
DEFAULT_PROFILE = 'optimal'
MAX_POINTS = 16
MIN_POINTS = 2
MAX_TEMPERATURE = 300
# The stock nozzle fan reaches 90%, so an ordinary curve has to be allowed to
# go that high. The heavy-load rule is what reaches 100%.
MAX_DUTY = 1.0
DEFAULT_HYSTERESIS = 5.0
# A nozzle fan must not be held off once the hotend is warm enough to matter.
# The stock stepped table starts at 45C, so the first curve node has to be at
# or below this temperature and the node that covers it has to keep the fan
# turning.
MIN_COOL_TEMPERATURE = 50
MIN_NOZZLE_DUTY = 0.05


class FanCurveError(ValueError):
    pass


def _finite_number(value, name):
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise FanCurveError('%s must be a number' % name)
    if not math.isfinite(number):
        raise FanCurveError('%s must be finite' % name)
    return number


def normalize_curves(value):
    if not isinstance(value, dict):
        raise FanCurveError('curves must be an object')
    normalized = {}
    for name in ('power', 'nozzle'):
        points = value.get(name)
        if not isinstance(points, list):
            raise FanCurveError('%s must be an array' % name)
        if len(points) < MIN_POINTS or len(points) > MAX_POINTS:
            raise FanCurveError(
                '%s must contain %d-%d nodes' %
                (name, MIN_POINTS, MAX_POINTS))
        output = []
        previous_temperature = None
        for index, point in enumerate(points):
            if not isinstance(point, (list, tuple)) or len(point) != 2:
                raise FanCurveError('%s node %d must be [temperature, duty]' %
                                    (name, index + 1))
            temperature = _finite_number(point[0], '%s temperature' % name)
            duty = _finite_number(point[1], '%s duty' % name)
            if temperature != int(temperature) or not 0 <= temperature <= MAX_TEMPERATURE:
                raise FanCurveError('%s temperature must be an integer from 0 to %d' %
                                    (name, MAX_TEMPERATURE))
            if not 0 <= duty <= MAX_DUTY:
                raise FanCurveError('%s duty must be between 0 and %.2f' %
                                    (name, MAX_DUTY))
            temperature = int(temperature)
            if previous_temperature is not None and temperature <= previous_temperature:
                raise FanCurveError('%s temperatures must increase strictly' % name)
            output.append([temperature, round(duty, 3)])
            previous_temperature = temperature
        if name == 'nozzle':
            if output[0][0] > MIN_COOL_TEMPERATURE:
                raise FanCurveError(
                    'nozzle curve must start at or below %dC'
                    % MIN_COOL_TEMPERATURE)
            # The curve holds the duty of the last node at or below the
            # cooling temperature, so that node and every node above it have
            # to keep the fan turning.
            cool_index = 0
            for index, (temperature, _) in enumerate(output):
                if temperature > MIN_COOL_TEMPERATURE:
                    break
                cool_index = index
            for index in range(cool_index, len(output)):
                if output[index][1] < MIN_NOZZLE_DUTY:
                    raise FanCurveError(
                        'nozzle node %d leaves the fan off above %dC'
                        % (index + 1, MIN_COOL_TEMPERATURE))
        normalized[name] = output
    return normalized


def _normalize_heavy_load(value):
    if not isinstance(value, dict):
        value = {}
    # The stock temp_speed_table reaches 100% when at least three toolheads
    # heat up and one of them targets more than 260C.
    temperature = _finite_number(value.get('temperature', 260), 'heavy-load temperature')
    heater_count = _finite_number(value.get('heater_count', 3), 'heavy-load heater count')
    duty = _finite_number(value.get('duty', 1), 'heavy-load duty')
    if temperature < 0 or temperature > MAX_TEMPERATURE:
        raise FanCurveError('heavy-load temperature must be from 0 to %d' % MAX_TEMPERATURE)
    if heater_count != int(heater_count) or heater_count < 1:
        raise FanCurveError('heavy-load heater count must be a positive integer')
    if duty < 0 or duty > 1:
        raise FanCurveError('heavy-load duty must be between 0 and 1')
    return {
        'temperature': temperature,
        'heater_count': int(heater_count),
        'duty': duty,
    }


def _canonical_profile(profile):
    return str(profile or '').strip().lower().replace('-', '_')


def _normalize_document(value, profile=None):
    if not isinstance(value, dict):
        raise FanCurveError('fan curve state must be an object')
    if value.get('version') != 1:
        raise FanCurveError('unsupported fan curve version')
    profile = _canonical_profile(profile or value.get('profile'))
    if profile == STOCK_PROFILE:
        return {'version': 1, 'profile': STOCK_PROFILE}
    if profile != CUSTOM_PROFILE and profile not in _preset_files():
        raise FanCurveError('unknown fan curve profile: %s' % profile)
    curves = normalize_curves(value.get('curves'))
    hysteresis = _finite_number(
        value.get('hysteresis', DEFAULT_HYSTERESIS), 'hysteresis')
    if not 0 <= hysteresis <= MAX_TEMPERATURE:
        raise FanCurveError('hysteresis must be from 0 to %d' % MAX_TEMPERATURE)
    return {
        'version': 1,
        'profile': profile,
        'curves': curves,
        'hysteresis': hysteresis,
        'heavy_load': _normalize_heavy_load(value.get('heavy_load')),
    }


def _read_json(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return json.load(stream)


def _atomic_bytes(path, data):
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.fan-curves-', dir=directory)
    try:
        os.chmod(temporary, 0o644)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _atomic_json(path, value):
    _atomic_bytes(path, (json.dumps(value, indent=2) + '\n').encode('utf-8'))


def _preset_files():
    """Return the available presets as {profile name: file path}.

    Both the built-in directory and the personal one are scanned. A personal
    file with the same name replaces the built-in preset.
    """
    found = {}
    for directory in (PRESET_DIR, USER_PRESET_DIR):
        try:
            names = sorted(os.listdir(directory))
        except OSError:
            continue
        for name in names:
            if not name.startswith(PRESET_PREFIX) or not name.endswith(PRESET_SUFFIX):
                continue
            profile = name[len(PRESET_PREFIX):-len(PRESET_SUFFIX)]
            if not profile or profile in (STOCK_PROFILE, CUSTOM_PROFILE):
                continue
            found[profile] = os.path.join(directory, name)
    return found


def _preset_path(profile):
    files = _preset_files()
    if profile not in files:
        raise FanCurveError('unknown fan curve profile: %s' % profile)
    return files[profile]


def _load_preset(profile):
    try:
        return _normalize_document(_read_json(_preset_path(profile)), profile)
    except OSError as error:
        raise FanCurveError('cannot load %s preset: %s' % (profile, error))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise FanCurveError('invalid %s preset: %s' % (profile, error))


def load_custom():
    try:
        return normalize_curves(_read_json(CUSTOM_PATH))
    except FileNotFoundError:
        return _load_preset(DEFAULT_PROFILE)['curves']
    except OSError as error:
        raise FanCurveError('cannot load Custom curves: %s' % error)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise FanCurveError('invalid Custom curves: %s' % error)


def save_custom(curves):
    normalized = normalize_curves(curves)
    _atomic_json(CUSTOM_PATH, normalized)
    return normalized


def _load_active():
    try:
        return _normalize_document(_read_json(ACTIVE_PATH))
    except FileNotFoundError:
        return {'version': 1, 'profile': 'stock'}
    except OSError as error:
        raise FanCurveError('cannot load active fan curves: %s' % error)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise FanCurveError('invalid active fan curves: %s' % error)


def current_profile():
    try:
        return _load_active()['profile']
    except FanCurveError:
        return 'stock'


def get_state():
    files = _preset_files()
    presets = {}
    for profile in sorted(files):
        presets[profile] = {'curves': _load_preset(profile)['curves']}
    default = _load_preset(DEFAULT_PROFILE)
    return {
        'version': 1,
        'current': current_profile(),
        'profiles': [STOCK_PROFILE] + sorted(files) + [CUSTOM_PROFILE],
        'presets': presets,
        'custom': load_custom(),
        'limits': {
            'max_points': MAX_POINTS,
            'temperature': [0, MAX_TEMPERATURE],
            'duty': [0, MAX_DUTY],
        },
        'hysteresis': default['hysteresis'],
        'heavy_load': default['heavy_load'],
    }


def _active_document(profile, curves=None):
    profile = _canonical_profile(profile)
    if profile == STOCK_PROFILE:
        return {'version': 1, 'profile': STOCK_PROFILE}
    if profile == CUSTOM_PROFILE:
        base = _load_preset(DEFAULT_PROFILE)
        return _normalize_document({
            'version': 1,
            'profile': CUSTOM_PROFILE,
            'curves': curves if curves is not None else load_custom(),
            'hysteresis': base['hysteresis'],
            'heavy_load': base['heavy_load'],
        }, CUSTOM_PROFILE)
    return _load_preset(profile)


def _restart_klipper():
    return subprocess.run(
        ['/etc/init.d/S60klipper', 'restart'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=120).returncode


def _restore(path, previous):
    if previous is None:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
    else:
        _atomic_bytes(path, previous)


def apply_profile(profile, curves=None, restart=True):
    profile = _canonical_profile(profile)
    if profile == 'custom' and curves is not None:
        curves = save_custom(curves)
    document = _active_document(profile, curves)
    try:
        with open(ACTIVE_PATH, 'rb') as stream:
            previous = stream.read()
    except FileNotFoundError:
        previous = None
    _atomic_json(ACTIVE_PATH, document)
    if restart:
        try:
            result = _restart_klipper()
        except Exception:
            _restore(ACTIVE_PATH, previous)
            raise
        if result != 0:
            _restore(ACTIVE_PATH, previous)
            raise FanCurveError('Klipper restart failed')
    return profile


def migrate_legacy(profile):
    if os.path.exists(ACTIVE_PATH):
        return current_profile()
    profile = _canonical_profile(profile)
    files = _preset_files()
    if profile not in (STOCK_PROFILE, CUSTOM_PROFILE) and profile not in files:
        # The preset the old install asked for is not available any more, so
        # fall back to the shipped default instead of losing the setting.
        profile = DEFAULT_PROFILE
    document = _active_document(profile)
    _atomic_json(ACTIVE_PATH, document)
    return document['profile']


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('apply', 'migrate', 'profile'))
    parser.add_argument('profile', nargs='?')
    args = parser.parse_args()
    if args.command == 'profile':
        print(current_profile())
        return
    if args.command == 'migrate':
        migrate_legacy(args.profile)
        return
    apply_profile(args.profile)


if __name__ == '__main__':
    main()
