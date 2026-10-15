import json
import math
import os
import re
import subprocess
import tempfile


CONFIG_DIR = os.environ.get(
    'FAN_CURVES_CONFIG_DIR', '/oem/printer_data/config/extended')
CUSTOM_PATH = os.path.join(CONFIG_DIR, 'fan-curves.json')
ACTIVE_PATH = os.path.join(CONFIG_DIR, 'klipper', '20_fan_curves.cfg')
PRESET_DIR = os.environ.get(
    'FAN_CURVES_PRESET_DIR', '/usr/local/share/firmware-config/tweaks/klipper')
PRESETS = {
    'quiet': 'fan_curves_quiet.cfg',
    'very_quiet': 'fan_curves_quiet_experimental.cfg',
}
MAX_POINTS = 16
MIN_POINTS = 2
MAX_TEMPERATURE = 300
MAX_DUTY = 0.60


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
            duty = round(duty, 3)
            if previous_temperature is not None and temperature <= previous_temperature:
                raise FanCurveError('%s temperatures must increase strictly' % name)
            output.append([temperature, duty])
            previous_temperature = temperature
        normalized[name] = output
    return normalized


def _atomic_bytes(path, data):
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.fan-curves-', dir=directory)
    try:
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


def load_custom():
    try:
        with open(CUSTOM_PATH, 'r', encoding='utf-8') as stream:
            return normalize_curves(json.load(stream))
    except FileNotFoundError:
        return {'power': [], 'nozzle': []}
    except (OSError, ValueError, TypeError) as error:
        raise FanCurveError('cannot load Custom curves: %s' % error)


def save_custom(curves):
    normalized = normalize_curves(curves)
    _atomic_json(CUSTOM_PATH, {'version': 1, **normalized})
    return normalized


def _section(text, name):
    match = re.search(
        r'^\[' + re.escape(name) + r'\]\s*$\n(.*?)(?=^\[|\Z)',
        text, re.MULTILINE | re.DOTALL)
    return match.group(1) if match else ''


def _preset_path(profile):
    path = os.path.join(PRESET_DIR, PRESETS[profile])
    if not os.path.exists(path):
        path = os.path.abspath(os.path.join(
            os.path.dirname(__file__), '..', '..', '..', '..', '..',
            '32-feature-klipper-tweaks', 'root', 'usr', 'local', 'share',
            'firmware-config', 'tweaks', 'klipper', PRESETS[profile]))
    return path


def _preset_curves(profile):
    path = _preset_path(profile)
    try:
        with open(path, 'r', encoding='utf-8') as stream:
            text = stream.read()
    except OSError as error:
        raise FanCurveError('cannot load %s preset: %s' % (profile, error))

    curves = {}
    for curve_name, section_name in (
            ('power', 'heater_fan power_fan'),
            ('nozzle', 'heater_fan e0_nozzle_fan')):
        block = _section(text, section_name)
        table = re.search(
            r'^temp_speed_table:\s*\n((?:^[ \t]*[-+0-9.eE]+\s*,.*\n?)+)',
            block, re.MULTILINE)
        points = []
        if table:
            for line in table.group(1).splitlines():
                values = [item.strip() for item in line.split(',')]
                if len(values) == 5:
                    points.append([int(float(values[0])), float(values[4])])
        curves[curve_name] = list(reversed(points))
    return normalize_curves(curves)


def _profile_marker():
    try:
        with open(ACTIVE_PATH, 'r', encoding='utf-8') as stream:
            marker = re.search(r'^# fan-curves:\s*(\S+)', stream.read(), re.MULTILINE)
    except FileNotFoundError:
        return 'stock'
    except OSError as error:
        raise FanCurveError('cannot read active fan curves: %s' % error)
    value = marker.group(1) if marker else 'stock'
    return {
        'quiet-experimental': 'very_quiet',
        'lazy': 'very_quiet',
        'balanced': 'quiet',
    }.get(value, value if value in ('quiet', 'very_quiet', 'custom') else 'stock')


def get_state():
    presets = {
        'stock': {'semantic': 'stock', 'power': [], 'nozzle': []},
        'quiet': {'semantic': 'actual', **_preset_curves('quiet')},
        'very_quiet': {'semantic': 'actual', **_preset_curves('very_quiet')},
    }
    return {
        'version': 1,
        'current': _profile_marker(),
        'presets': presets,
        'custom': load_custom(),
        'limits': {
            'max_points': MAX_POINTS,
            'temperature': [0, MAX_TEMPERATURE],
            'duty': [0, MAX_DUTY],
        },
        'heavy_load': {'heater_count': 2, 'temperature': 200, 'duty': 1.0},
    }


def _cfg_table(points):
    return '\n'.join(
        '  %d, 9999, 1, 0, %.3f' % (temperature, duty)
        for temperature, duty in reversed(points))


def _generated_config(curves):
    power_start, power_speed = curves['power'][0]
    sections = [
        '# fan-curves: custom',
        '',
        '[heater_fan power_fan]',
        'fan_curve_mode: actual',
        'temp_speed_table:',
        _cfg_table(curves['power']),
        'temp_hysteresis: 3.0',
        'target_temp_threshold: %.1f' % power_start,
        'target_temp_speed: %.3f' % power_speed,
        'heavy_load_target_temp_threshold: 200.0',
        'heavy_load_target_temp_heater_count: 2',
        'heavy_load_target_temp_speed: 1.00',
    ]
    for index in range(4):
        sections.extend([
            '',
            '[heater_fan e%d_nozzle_fan]' % index,
            'fan_curve_mode: actual',
            'temp_speed_table:',
            _cfg_table(curves['nozzle']),
            'temp_hysteresis: 3.0',
        ])
    return ('\n'.join(sections) + '\n').encode('utf-8')


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
    profile = {'quiet_experimental': 'very_quiet', 'lazy': 'very_quiet',
               'balanced': 'quiet'}.get(profile, profile)
    if profile not in ('stock', 'quiet', 'very_quiet', 'custom'):
        raise FanCurveError('unknown fan curve profile: %s' % profile)
    if profile == 'custom':
        normalized = save_custom(curves if curves is not None else load_custom())
        content = _generated_config(normalized)
    elif profile == 'stock':
        content = None
    else:
        path = _preset_path(profile)
        try:
            with open(path, 'rb') as stream:
                content = stream.read()
        except OSError as error:
            raise FanCurveError('cannot load %s preset: %s' % (profile, error))

    try:
        with open(ACTIVE_PATH, 'rb') as stream:
            previous = stream.read()
    except FileNotFoundError:
        previous = None
    if content is None:
        try:
            os.unlink(ACTIVE_PATH)
        except FileNotFoundError:
            pass
    else:
        _atomic_bytes(ACTIVE_PATH, content)

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


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('apply', 'save'))
    parser.add_argument('profile', nargs='?')
    args = parser.parse_args()
    if args.command == 'save':
        raise SystemExit('save is available through the API')
    apply_profile(args.profile)


if __name__ == '__main__':
    main()
