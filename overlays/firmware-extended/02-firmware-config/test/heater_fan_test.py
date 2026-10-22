# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12

"""Host side checks for the fan curve support of the Klipper patch.

The patched driver is built here: the patch of this repository is applied to
the heater_fan.py of the Klipper release the firmware ships with. Both
drivers are then driven with the fan tables of the printer's own
printer.cfg, and what they do is compared. No printer is needed.

    python overlays/firmware-extended/02-firmware-config/test/heater_fan_test.py
"""
import importlib.util
import json
import os
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
PATCH_FILE = os.path.join(
    REPO, 'overlays', 'firmware-extended', '11-patch-klipper', 'patches',
    'home', 'lava', 'klipper', '09_heater_fan_fan_curves.patch')
STOCK_FILE = os.path.join(HERE, 'stock_heater_fan.py')
PRINTER_CFG = os.path.join(HERE, 'printer.cfg')
DEFAULT_PROFILE = os.path.join(
    REPO, 'overlays', 'firmware-extended', '32-feature-klipper-tweaks', 'root',
    'usr', 'local', 'share', 'firmware-config', 'tweaks', 'klipper',
    'fan_curves_default.json')
CURVE_PATH = os.path.join(tempfile.gettempdir(), 'fan-curves-active-test.json')

os.environ['FAN_CURVES_PATH'] = CURVE_PATH

CHECKS = 0
FAILURES = []


def check(name, condition, detail=''):
    global CHECKS
    CHECKS += 1
    if condition:
        print('  ok   %s' % name)
    else:
        FAILURES.append(name)
        print('  FAIL %s %s' % (name, detail))


def section(title):
    print('\n== %s' % title)


# ---------------------------------------------------------------------------
# the patch


def apply_unified_diff(original, diff_text):
    """Apply a unified diff to the text of a file and return the new text.

    Only what a patch needs is supported: hunk headers, context lines,
    removed lines and added lines.
    """
    source = original.splitlines()
    result = []
    index = 0
    lines = diff_text.splitlines()
    position = 0
    while position < len(lines):
        if not lines[position].startswith('@@'):
            position += 1
            continue
        header = lines[position].split('@@')[1].strip()
        old_start = int(header.split(' ')[0].split(',')[0][1:])
        result.extend(source[index:old_start - 1])
        index = old_start - 1
        position += 1
        while position < len(lines) and not lines[position].startswith('@@'):
            line = lines[position]
            position += 1
            if line.startswith('\\'):
                continue
            if line.startswith('+'):
                result.append(line[1:])
                continue
            if line.startswith(' '):
                expected = line[1:]
            elif line == '':
                expected = ''
            else:
                expected = line[1:]
            if index >= len(source) or source[index] != expected:
                raise ValueError('the patch does not fit at line %d' % (index + 1))
            if not line.startswith('-'):
                result.append(expected)
            index += 1
    result.extend(source[index:])
    return '\n'.join(result) + '\n'


def read_text(path):
    with open(path, 'r', encoding='utf-8', newline='') as stream:
        return stream.read().replace('\r\n', '\n')


# ---------------------------------------------------------------------------
# klipper doubles


class ConfigError(Exception):
    pass


class FakeReactor:
    NOW = 0

    def register_timer(self, callback, when):
        self.callback = callback
        return 'timer'

    def update_timer(self, timer, when):
        pass

    def monotonic(self):
        return 0.


class FakeGcode:
    def register_mux_command(self, *args, **kwargs):
        pass


class FakeHeater:
    def __init__(self, current, target):
        self.current = current
        self.target = target

    def get_temp(self, eventtime):
        return (self.current, self.target)


class FakeSensor:
    def __init__(self, current):
        self.current = current

    def get_temp(self, eventtime):
        return (self.current, 0.)


class FakePrinter:
    def __init__(self, heaters, sensors=None):
        self.reactor = FakeReactor()
        self.heaters = heaters
        self.sensors = sensors or {}
        self.gcode = FakeGcode()

    def load_object(self, config, name):
        if name == 'heaters':
            return self
        return self.sensors[name]

    def get_reactor(self):
        return self.reactor

    def lookup_object(self, name):
        if name == 'gcode':
            return self.gcode
        if name == 'heaters':
            return self
        raise KeyError(name)

    def lookup_heater(self, name):
        return self.heaters[name]

    def register_event_handler(self, name, callback):
        pass


class FakeConfig:
    def __init__(self, printer, name, options):
        self.printer = printer
        self.name = name
        self.options = options

    def get_printer(self):
        return self.printer

    def get_reactor(self):
        return self.printer.reactor

    def get_name(self):
        return self.name

    def get(self, option, default=None, **kwargs):
        return self.options.get(option, default)

    def getfloat(self, option, default=None, minval=None, maxval=None):
        value = self.options.get(option, default)
        if value is None:
            return None
        return float(value)

    def getlist(self, option, default=None, seps=(',', '\n'), count=None,
                parser=None):
        value = self.options.get(option, default)
        if value is None:
            return None
        if isinstance(value, (list, tuple)):
            return list(value)
        parts = [part.strip() for part in str(value).split(',')]
        if parser is not None:
            parts = [parser(part) for part in parts]
        return parts

    def getlists(self, option, default=None, seps=(',', '\n'), count=None,
                 parser=None):
        value = self.options.get(option, default)
        if value is None:
            return None
        rows = []
        for line in str(value).split('\n'):
            line = line.strip()
            if not line:
                continue
            parts = [part.strip() for part in line.split(',')]
            if count is not None and len(parts) != count:
                raise ConfigError('%s needs %d values' % (option, count))
            if parser is not None:
                parts = [parser(part) for part in parts]
            rows.append(parts)
        return rows

    def error(self, message):
        raise ConfigError(message)


def install_klipper_package():
    """The klippy.extras package the driver imports its fan from."""
    package = types.ModuleType('klippy')
    package.__path__ = []
    extras = types.ModuleType('klippy.extras')
    extras.__path__ = []
    fan_module = types.ModuleType('klippy.extras.fan')

    class Fan:
        def __init__(self, config, default_shutdown_speed=1.):
            self.speed = None

        def get_status(self, eventtime):
            return {'speed': self.speed}

        def get_mcu(self):
            return types.SimpleNamespace(estimated_print_time=lambda t: t)

        def set_speed(self, print_time, speed):
            self.speed = speed

    fan_module.Fan = Fan
    extras.fan = fan_module
    sys.modules['klippy'] = package
    sys.modules['klippy.extras'] = extras
    sys.modules['klippy.extras.fan'] = fan_module


def load_driver(name, source):
    """Load a driver from source text under the klippy.extras package."""
    path = os.path.join(tempfile.gettempdir(), 'fan-curve-test-%s.py' % name)
    with open(path, 'w', encoding='utf-8', newline='\n') as stream:
        stream.write(source)
    spec = importlib.util.spec_from_file_location(
        'klippy.extras.heater_fan_%s' % name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


# ---------------------------------------------------------------------------
# the printer the firmware ships with


def printer_config():
    """The sections of the printer.cfg of the Snapmaker U1.

    A value that continues on the next line belongs to the key above it,
    so the fan tables come out the way the printer reads them.
    """
    sections = {}
    name = None
    key = None
    with open(PRINTER_CFG, 'r', encoding='utf-8') as stream:
        for raw in stream:
            line = raw.rstrip('\n')
            stripped = line.strip()
            if not stripped or stripped.startswith('#'):
                continue
            if not line[0].isspace():
                if stripped.startswith('[') and stripped.endswith(']'):
                    name = stripped[1:-1].strip()
                    sections.setdefault(name, {})
                    key = None
                    continue
                if ':' not in stripped:
                    key = None
                    continue
                key, value = stripped.split(':', 1)
                key = key.strip()
                # A comment behind a value belongs to the comment, the way
                # the printer reads it.
                sections[name][key] = value.split('#', 1)[0].strip()
                continue
            if name is not None and key is not None:
                value = stripped.split('#', 1)[0].strip()
                if not value:
                    continue
                sections[name][key] = (sections[name][key] + '\n' + value).strip()
    return sections


def nozzle_options(name='e0_nozzle_fan'):
    section = printer_config()['heater_fan %s' % name]
    return {
        'heater': [part.strip() for part in section['heater'].split(',')],
        'heater_temp': float(section['heater_temp']),
        'fan_speed': float(section['fan_speed']),
        'probe_speed': float(section['probe_speed']),
        'stepped_temp_table': section['stepped_temp_table'],
        'external_temp_sensor': section['external_temp_sensor'],
        'external_temp_guard_range': section['external_temp_guard_range'],
        'external_temp_guard_fan_speed': float(
            section['external_temp_guard_fan_speed']),
    }


def power_options():
    section = printer_config()['heater_fan power_fan']
    return {
        'heater': [part.strip() for part in section['heater'].split(',')],
        'shutdown_speed': float(section['shutdown_speed']),
        'temp_speed_table': section['temp_speed_table'],
    }


def make_fan(module, heaters, options, name, sensors=None):
    printer = FakePrinter(heaters, sensors or {})
    config = FakeConfig(printer, 'heater_fan %s' % name, dict(options))
    fan = module.PrinterHeaterFan(config)
    fan.handle_ready()
    return fan


def make_nozzle_fan(module, heaters, options=None, sensor=None,
                    name='e0_nozzle_fan'):
    merged = nozzle_options(name)
    merged.update(options or {})
    # The cavity sensor of the printer is always there. Its reading only
    # matters when a check asks for another one.
    sensors = {'temperature_sensor cavity': sensor if sensor is not None
               else FakeSensor(20.)}
    return make_fan(module, heaters, merged, name, sensors)


def make_power_fan(module, heaters, options=None):
    merged = power_options()
    merged.update(options or {})
    return make_fan(module, heaters, merged, 'power_fan')


def four_heaters(temperature):
    """The four toolheads of the printer, all at the same temperature."""
    return {name: FakeHeater(temperature, temperature) for name in
            ('extruder', 'extruder1', 'extruder2', 'extruder3')}


def speed_of(fan):
    fan.callback(0.)
    return fan.last_speed


# ---------------------------------------------------------------------------
# the curve file


def write_curve(profile, power, nozzle, hysteresis=5.0, heavy_load=None,
                floor=None):
    document = {
        'version': 1,
        'profile': profile,
        'curves': {'power': power, 'nozzle': nozzle},
        'hysteresis': hysteresis,
    }
    document['heavy_load'] = heavy_load or {
        'temperature': 260, 'heater_count': 3, 'hot_count': 1, 'duty': 1.0}
    if floor is not None:
        document['min_cool_temperature'] = floor[0]
        document['min_nozzle_duty'] = floor[1]
    with open(CURVE_PATH, 'w', encoding='utf-8') as stream:
        json.dump(document, stream)
    return document


def remove_curve():
    if os.path.exists(CURVE_PATH):
        os.unlink(CURVE_PATH)


def curve_duty(points, temp):
    """The duty the driver gives: the line between the points of a curve."""
    if not points or temp < points[0][0]:
        return 0.0
    if temp >= points[-1][0]:
        return points[-1][1]
    for index in range(1, len(points)):
        low_temperature, low_duty = points[index - 1]
        high_temperature, high_duty = points[index]
        if temp < high_temperature:
            span = high_temperature - low_temperature
            if span <= 0:
                return high_duty
            return low_duty + (high_duty - low_duty) * (temp - low_temperature) / span
    return points[-1][1]


def stepped(points, temp):
    """The duty of a stock stepped table."""
    duty = 0.0
    for threshold, value in points:
        if temp >= threshold:
            duty = value
        else:
            break
    return duty


# ---------------------------------------------------------------------------
# the drivers


install_klipper_package()
PATCHED = load_driver('patched', apply_unified_diff(
    read_text(STOCK_FILE), read_text(PATCH_FILE)))
STOCK = load_driver('stock', read_text(STOCK_FILE))


# ---------------------------------------------------------------------------
# checks


def check_the_patch():
    section('the patch')
    check('the patch applies to the driver of the Klipper release',
          'FAN_CURVES_PATH' in read_text(PATCH_FILE))
    check('the patched driver reads a curve file',
          hasattr(PATCHED, '_load_curve'))
    check('the stock driver does not',
          not hasattr(STOCK, '_load_curve'))


def check_stock_is_stock():
    section('stock is stock')
    remove_curve()
    differences = []
    for temp in range(0, 301, 5):
        heaters = {'extruder': FakeHeater(temp, temp)}
        theirs = speed_of(make_nozzle_fan(STOCK, heaters))
        ours = speed_of(make_nozzle_fan(PATCHED, heaters))
        if abs(ours - theirs) > 1e-9:
            differences.append('nozzle %dC: %.2f != %.2f' % (temp, ours, theirs))
    check('the nozzle fan gives the stock duty at every temperature',
          not differences, '(differences=%s)' % differences[:5])

    differences = []
    for temp in range(0, 301, 5):
        heaters = four_heaters(temp)
        theirs = speed_of(make_power_fan(STOCK, heaters))
        ours = speed_of(make_power_fan(PATCHED, heaters))
        if abs(ours - theirs) > 1e-9:
            differences.append('power %dC: %.2f != %.2f' % (temp, ours, theirs))
    check('the power fan gives the stock duty at every temperature',
          not differences, '(differences=%s)' % differences[:5])

    # The stock profile of a curve file is the stock behaviour as well.
    write_curve('stock', [[45, 0.6], [260, 0.6]], [[45, 0.3], [260, 0.9]])
    differences = []
    for temp in range(0, 301, 5):
        heaters = {'extruder': FakeHeater(temp, temp)}
        theirs = speed_of(make_nozzle_fan(STOCK, heaters))
        ours = speed_of(make_nozzle_fan(PATCHED, heaters))
        if abs(ours - theirs) > 1e-9:
            differences.append('nozzle %dC: %.2f != %.2f' % (temp, ours, theirs))
    check('the stock profile of a curve file gives the stock duty',
          not differences, '(differences=%s)' % differences[:5])
    remove_curve()

    # Four toolheads, the heavy load of the power fan.
    heaters = {
        'extruder': FakeHeater(265, 265),
        'extruder1': FakeHeater(265, 265),
        'extruder2': FakeHeater(265, 265),
        'extruder3': FakeHeater(265, 265),
    }
    theirs = speed_of(make_power_fan(STOCK, heaters))
    ours = speed_of(make_power_fan(PATCHED, heaters))
    check('four toolheads above 260C give the stock duty',
          abs(ours - theirs) < 1e-9 and ours == 1.0,
          '(stock=%.2f patched=%.2f)' % (theirs, ours))

    # The cavity sensor of the printer.
    for chamber in (20., 50.):
        heaters = {'extruder': FakeHeater(210, 210)}
        sensor = FakeSensor(chamber)
        theirs = speed_of(make_nozzle_fan(STOCK, heaters, sensor=sensor))
        ours = speed_of(make_nozzle_fan(PATCHED, heaters, sensor=sensor))
        check('the cavity sensor at %.0fC gives the stock duty' % chamber,
              abs(ours - theirs) < 1e-9,
              '(stock=%.2f patched=%.2f)' % (theirs, ours))


def check_the_curve_is_what_runs():
    section('the curve is what the driver runs')
    power = [[70, 0.2], [260, 0.6]]
    write_curve('custom', power, [[50, 0.0], [120, 0.0], [260, 0.6]])
    hot = {'extruder': FakeHeater(80, 80)}
    fan = make_nozzle_fan(PATCHED, hot)
    check('a curve that asks for 0% gets 0%',
          abs(speed_of(fan)) < 1e-9, '(speed=%s)' % speed_of(fan))

    warm = {'extruder': FakeHeater(190, 190)}
    fan = make_nozzle_fan(PATCHED, warm)
    check('the driver follows the curve between its points',
          abs(speed_of(fan) - 0.3) < 1e-9, '(speed=%s)' % speed_of(fan))

    cold = {'extruder': FakeHeater(30, 30)}
    fan = make_nozzle_fan(PATCHED, cold)
    check('a cold nozzle fan is left alone',
          abs(speed_of(fan)) < 1e-9, '(speed=%s)' % speed_of(fan))

    # A rule under the curve in the file is not followed. The driver runs the
    # curve and nothing else.
    write_curve('custom', power, [[50, 0.0], [120, 0.0], [260, 0.6]],
                floor=(60, 0.12))
    warm = {'extruder': FakeHeater(70, 70)}
    fan = make_nozzle_fan(PATCHED, warm)
    check('a rule under the curve in the file is ignored',
          abs(speed_of(fan)) < 1e-9, '(speed=%s)' % speed_of(fan))

    with open(DEFAULT_PROFILE, 'r', encoding='utf-8') as stream:
        default = json.load(stream)
    check('the profile the firmware ships carries no rule under the curve',
          'min_cool_temperature' not in default
          and 'min_nozzle_duty' not in default,
          '(document=%s)' % default)
    remove_curve()


def check_the_curve_is_a_line():
    section('the curve is the line between its points')
    write_curve('custom', [[70, 0.2], [260, 0.6]],
                [[50, 0.4], [100, 0.45], [260, 0.9]])
    for temp, expected in ((50, 0.4), (75, 0.425), (100, 0.45),
                           (180, 0.675), (260, 0.9)):
        heaters = {'extruder': FakeHeater(temp, temp)}
        fan = make_nozzle_fan(PATCHED, heaters)
        check('the duty at %dC is the line between the points' % temp,
              abs(speed_of(fan) - expected) < 1e-9,
              '(speed=%s expected=%s)' % (speed_of(fan), expected))
    remove_curve()


def check_the_stock_rules():
    section('the stock rules keep working')
    power = [[70, 0.2], [260, 0.6]]
    nozzle = [[50, 0.05], [150, 0.2], [260, 0.9]]
    write_curve('custom', power, nozzle)

    # The faulty toolhead bypass raises the heater_temp of the fan it turns
    # off. Such a fan stays on the stock stepped table.
    heaters = {'extruder': FakeHeater(210, 210)}
    bypassed = make_nozzle_fan(PATCHED, heaters, options={'heater_temp': 999})
    stock_bypassed = make_nozzle_fan(STOCK, heaters, options={'heater_temp': 999})
    check('the faulty toolhead bypass keeps the fan on the stock table',
          abs(speed_of(bypassed) - speed_of(stock_bypassed)) < 1e-9,
          '(patched=%s stock=%s)' % (speed_of(bypassed),
                                     speed_of(stock_bypassed)))

    # A faulty toolhead reports a bogus current temperature but no target.
    # The power fan follows the targets only, so the bogus reading changes
    # nothing.
    bogus = {'extruder': FakeHeater(400, 210), 'extruder1': FakeHeater(0, 0),
             'extruder2': FakeHeater(0, 0), 'extruder3': FakeHeater(0, 0)}
    honest = {'extruder': FakeHeater(210, 210), 'extruder1': FakeHeater(0, 0),
              'extruder2': FakeHeater(0, 0), 'extruder3': FakeHeater(0, 0)}
    fan = make_power_fan(PATCHED, bogus)
    reference = make_power_fan(PATCHED, honest)
    check('a bogus toolhead reading does not raise the power fan',
          abs(speed_of(fan) - speed_of(reference)) < 1e-9,
          '(bogus=%s honest=%s)' % (speed_of(fan), speed_of(reference)))

    # Probing locks the nozzle fan to its probe speed.
    heaters = {'extruder': FakeHeater(210, 210)}
    fan = make_nozzle_fan(PATCHED, heaters)
    fan.set_probe_speed()
    check('probing locks the nozzle fan to probe_speed',
          abs(speed_of(fan) - 0.5) < 1e-9, '(speed=%s)' % speed_of(fan))

    # A hot chamber forces the nozzle fan to 100%, also when the curve would
    # idle it. The stock driver only guards a fan that already runs.
    heaters = {'extruder': FakeHeater(30, 0)}
    sensor = FakeSensor(60.)
    fan = make_nozzle_fan(PATCHED, heaters, sensor=sensor)
    check('a hot chamber forces the nozzle fan to 100%',
          abs(speed_of(fan) - 1.0) < 1e-9, '(speed=%s)' % speed_of(fan))

    # The heavy load rule of the stock power fan: three toolheads heat up and
    # one of them targets more than 260C.
    heaters = {'extruder': FakeHeater(265, 265), 'extruder1': FakeHeater(210, 210),
               'extruder2': FakeHeater(210, 210), 'extruder3': FakeHeater(210, 210)}
    fan = make_power_fan(PATCHED, heaters)
    stock_fan = make_power_fan(STOCK, heaters)
    check('three toolheads above 260C reach the stock duty',
          abs(speed_of(fan) - 1.0) < 1e-9
          and abs(speed_of(fan) - speed_of(stock_fan)) < 1e-9,
          '(patched=%s stock=%s)' % (speed_of(fan), speed_of(stock_fan)))

    # Two toolheads heating up are not enough for the stock rule.
    heaters = {'extruder': FakeHeater(265, 265), 'extruder1': FakeHeater(210, 210),
               'extruder2': FakeHeater(0, 0), 'extruder3': FakeHeater(0, 0)}
    fan = make_power_fan(PATCHED, heaters)
    stock_fan = make_power_fan(STOCK, heaters)
    check('two toolheads above 260C give the stock duty',
          abs(speed_of(fan) - speed_of(stock_fan)) < 1e-9
          and abs(speed_of(fan) - 1.0) > 1e-9,
          '(patched=%s stock=%s)' % (speed_of(fan), speed_of(stock_fan)))
    remove_curve()


def check_the_shipped_profile():
    section('the profile the firmware ships')
    with open(DEFAULT_PROFILE, 'r', encoding='utf-8') as stream:
        default = json.load(stream)
    nozzle = [(int(t), float(d)) for t, d in default['curves']['nozzle']]
    power = [(int(t), float(d)) for t, d in default['curves']['power']]

    table = [(45, 0.3), (60, 0.35), (70, 0.4), (100, 0.45), (140, 0.5),
             (160, 0.6), (180, 0.65), (200, 0.7), (220, 0.75), (230, 0.8),
             (240, 0.85), (260, 0.9)]
    check('the shipped nozzle curve passes through every step of the stock table',
          nozzle == table, '(nodes=%s)' % (nozzle,))

    shortfalls = []
    for temp in range(45, 301):
        duty = curve_duty(nozzle, temp)
        if duty + 1e-9 < stepped(table, temp):
            shortfalls.append('%dC: %.3f < %.3f'
                              % (temp, duty, stepped(table, temp)))
    check('the shipped nozzle curve never cools less than stock',
          not shortfalls, '(shortfalls=%s)' % shortfalls[:5])

    shortfalls = []
    for temp in range(45, 301):
        duty = curve_duty(power, temp)
        if duty + 1e-9 < 0.6:
            shortfalls.append('%dC: %.3f < 0.600' % (temp, duty))
    check('the shipped power curve never cools less than stock',
          not shortfalls, '(shortfalls=%s)' % shortfalls[:5])


def main():
    check_the_patch()
    check_stock_is_stock()
    check_the_curve_is_what_runs()
    check_the_curve_is_a_line()
    check_the_stock_rules()
    check_the_shipped_profile()
    remove_curve()
    print('\n%d checks, %d failures' % (CHECKS, len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == '__main__':
    sys.exit(main())
