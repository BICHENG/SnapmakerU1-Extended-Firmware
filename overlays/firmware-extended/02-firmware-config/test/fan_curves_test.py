# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware
# SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12

"""Host side checks for the profile file model of the fan curves.

The real fan_curves.py of this repository is driven against a temporary
config directory: the shipped profile takes the place of an empty one, a
profile is drawn, renamed, applied and deleted, and the rules the module
enforces are checked.
No printer is needed.

    python overlays/firmware-extended/02-firmware-config/test/fan_curves_test.py
"""
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
MODULE_PATH = os.path.join(
    REPO, 'overlays', 'firmware-extended', '02-firmware-config', 'root',
    'usr', 'local', 'bin', 'fan_curves.py')
SHIPPED_DIR = os.path.join(
    REPO, 'overlays', 'firmware-extended', '32-feature-klipper-tweaks', 'root',
    'usr', 'local', 'share', 'firmware-config', 'tweaks', 'klipper')

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


def load_module(config_dir):
    os.environ['FAN_CURVES_CONFIG_DIR'] = config_dir
    os.environ['FAN_CURVES_SHIPPED_DIR'] = SHIPPED_DIR
    spec = importlib.util.spec_from_file_location('fan_curves_test', MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_json(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return json.load(stream)


def write_json(path, document):
    with open(path, 'w', encoding='utf-8') as stream:
        json.dump(document, stream)


def refuses(module, curves):
    """The module does not accept these curves."""
    try:
        module.normalize_curves(curves)
    except module.FanCurveError:
        return True
    return False


def check_the_shipped_profile():
    section('the profile the firmware ships')
    name = fan.seed_placeholder()
    check('the seed puts the shipped profile in', name == 'custom',
          '(name=%s)' % name)
    check('the shipped profile is the only profile',
          fan.available_profiles() == ['custom'],
          '(profiles=%s)' % fan.available_profiles())
    check('the seed leaves nothing but the profile in the directory',
          os.listdir(profile_dir) == ['fan_curves_custom.json'],
          '(files=%s)' % os.listdir(profile_dir))

    document = read_json(os.path.join(profile_dir, 'fan_curves_custom.json'))
    check('the shipped profile carries no rule under the curve',
          'min_cool_temperature' not in document
          and 'min_nozzle_duty' not in document,
          '(document=%s)' % document)
    check('the nozzle curve of the shipped profile starts at the stock step',
          document['curves']['nozzle'][0] == [45, 0.3],
          '(curve=%s)' % (document['curves']['nozzle'][0],))
    check('the seed runs only once', fan.seed_placeholder() is None)


def check_the_rules():
    section('the rules a curve has to follow')
    check('a curve with two points is accepted',
          not refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                            'nozzle': [[45, 0.3], [260, 0.9]]}))
    check('a curve with one point is refused',
          refuses(fan, {'power': [[45, 0.6]], 'nozzle': [[45, 0.3], [260, 0.9]]}))
    check('a curve with seventeen points is refused',
          refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                        'nozzle': [[45 + 10 * index, 0.5] for index in range(17)]}))
    check('a curve with sixteen points is accepted',
          not refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                            'nozzle': [[45 + 10 * index, 0.5] for index in range(16)]}))
    check('a temperature above 300C is refused',
          refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                        'nozzle': [[45, 0.3], [301, 0.9]]}))
    check('a duty above 100% is refused',
          refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                        'nozzle': [[45, 0.3], [260, 1.1]]}))
    check('a duty below 0% is refused',
          refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                        'nozzle': [[45, -0.1], [260, 0.9]]}))
    check('temperatures that do not ascend are refused',
          refuses(fan, {'power': [[45, 0.6], [260, 0.6]],
                        'nozzle': [[100, 0.3], [60, 0.9]]}))
    check('curves that are not an object are refused',
          refuses(fan, {'power': 'no', 'nozzle': [[45, 0.3], [260, 0.9]]}))


def check_the_life_of_a_profile():
    section('the life of a profile')
    fan.create_profile('my_cool')
    document = fan._load_profile('my_cool')
    check('a new profile starts with the stock ends',
          document['curves']['nozzle'] == [[45, 0.3], [260, 0.9]],
          '(curve=%s)' % (document['curves']['nozzle'],))
    check('the power curve of a new profile starts with the stock ends',
          document['curves']['power'] == [[45, 0.6], [260, 0.6]],
          '(curve=%s)' % (document['curves']['power'],))

    # Saving keeps the settings a change of the curve does not touch.
    path = os.path.join(profile_dir, 'fan_curves_my_cool.json')
    document = read_json(path)
    document['heavy_load'] = {'temperature': 200, 'heater_count': 2,
                              'hot_count': 1, 'duty': 0.8}
    write_json(path, document)
    fan.save_profile('my_cool', document['curves'])
    saved = fan._load_profile('my_cool')
    check('saving keeps the heavy load rule',
          saved['heavy_load']['temperature'] == 200,
          '(rule=%s)' % (saved['heavy_load'],))
    check('a saved profile carries no rule under the curve',
          'min_cool_temperature' not in saved
          and 'min_nozzle_duty' not in saved,
          '(document=%s)' % saved)

    state = fan.get_state()
    check('the state carries no rule under the curve',
          'min_cool_temperature' not in state['files']['my_cool']
          and 'min_nozzle_duty' not in state['files']['my_cool'])
    check('the state carries the label of the profile',
          state['labels']['my_cool'] == 'My Cool',
          '(label=%s)' % state['labels'].get('my_cool'))
    check('the state carries the warning', state['notice'] == fan.RISK_NOTICE)

    # Applying writes the curve and the rule to the file the driver reads.
    fan.apply_profile('my_cool', saved['curves'], restart=False)
    active = read_json(os.path.join(config_dir, 'fan-curves-active.json'))
    check('the applied profile runs', active['profile'] == 'my_cool',
          '(profile=%s)' % active.get('profile'))
    check('the running curve is the curve of the profile',
          active['curves']['nozzle'] == saved['curves']['nozzle'],
          '(curve=%s)' % (active['curves']['nozzle'],))
    check('the running profile is reported', fan.current_profile() == 'my_cool')

    # Renaming keeps the running profile and its curve.
    fan.rename_profile('my_cool', 'quiet')
    check('the profile was renamed', fan.available_profiles() == ['custom', 'quiet'],
          '(profiles=%s)' % fan.available_profiles())
    check('the running profile follows the rename', fan.current_profile() == 'quiet')
    active = read_json(os.path.join(config_dir, 'fan-curves-active.json'))
    check('the rename keeps the running curve',
          active['curves']['nozzle'] == saved['curves']['nozzle'],
          '(curve=%s)' % (active['curves']['nozzle'],))

    # A running profile is deleted as well, and the printer goes back to the
    # stock curves.
    fan.delete_profile('quiet', restart=False)
    check('a running profile is deleted', fan.available_profiles() == ['custom'],
          '(profiles=%s)' % fan.available_profiles())
    check('the printer goes back to the stock curves',
          fan.current_profile() == 'stock')


def check_two_changes_at_the_same_time():
    section('two changes at the same time')
    # The init script of Klipper refuses a restart that runs next to another
    # one. The module has to let the second change wait instead of running
    # two restarts at once, or the second change fails and takes the running
    # profile back with it.
    curves = {'power': [[45, 0.6], [260, 0.6]], 'nozzle': [[45, 0.3], [260, 0.9]]}
    fan.create_profile('busy')
    fan.save_profile('busy', curves)

    running = []
    overlaps = []

    def restart():
        running.append(1)
        if len(running) > 1:
            overlaps.append(len(running))
        time.sleep(0.3)
        running.pop()
        return 0

    original = fan._restart_klipper
    fan._restart_klipper = restart
    results = {}

    def work(name, function):
        try:
            results[name] = ('ok', function())
        except Exception as error:
            results[name] = ('error', str(error))

    first = threading.Thread(target=work,
                              args=('apply', lambda: fan.apply_profile('busy', curves)))
    second = threading.Thread(target=work,
                               args=('delete', lambda: fan.delete_profile('busy')))
    try:
        first.start()
        time.sleep(0.05)
        second.start()
        first.join()
        second.join()
    finally:
        fan._restart_klipper = original

    check('the change that came first succeeded', results.get('apply', ('error',))[0] == 'ok',
          '(result=%s)' % (results.get('apply'),))
    check('the change that came second succeeded', results.get('delete', ('error',))[0] == 'ok',
          '(result=%s)' % (results.get('delete'),))
    check('the two changes did not run at the same time', not overlaps,
          '(overlaps=%s)' % overlaps)
    check('the profile is gone', 'busy' not in fan.available_profiles(),
          '(profiles=%s)' % fan.available_profiles())
    check('the printer is back on the stock curves', fan.current_profile() == 'stock',
          '(profile=%s)' % fan.current_profile())


def check_the_placeholder():
    section('the shipped profile takes the place of an empty directory')
    fan.delete_profile('custom', restart=False)
    check('the profile was deleted', fan.available_profiles() == [],
          '(profiles=%s)' % fan.available_profiles())
    name = fan.seed_placeholder()
    check('the seed brings the shipped profile back', name == 'custom',
          '(name=%s)' % name)
    check('the shipped profile is back', fan.available_profiles() == ['custom'],
          '(profiles=%s)' % fan.available_profiles())

    # A profile of the user is never touched.
    fan.create_profile('mine')
    fan.delete_profile('custom', restart=False)
    name = fan.seed_placeholder()
    check('the seed leaves a profile of the user alone', name is None,
          '(name=%s)' % name)
    check('the profile of the user is still there',
          fan.available_profiles() == ['mine'],
          '(profiles=%s)' % fan.available_profiles())


def check_the_shipped_profile_is_a_profile():
    section('the shipped profile is a profile like any other')
    # It is a file in the profile directory, so it can be renamed like
    # any other, and a profile of the user keeps it from coming back.
    fan.rename_profile('mine', 'quiet')
    check('the profile was renamed', fan.available_profiles() == ['quiet'],
          '(profiles=%s)' % fan.available_profiles())
    name = fan.seed_placeholder()
    check('the seed does not bring the shipped profile back', name is None,
          '(name=%s)' % name)
    check('the renamed profile is still there',
          fan.available_profiles() == ['quiet'],
          '(profiles=%s)' % fan.available_profiles())


def check_an_older_install():
    section('an older install keeps its curve')
    write_json(os.path.join(config_dir, 'fan-curves.json'),
               {'nozzle': [[50, 0.1], [260, 0.9]],
                'power': [[45, 0.4], [260, 0.4]]})
    os.remove(os.path.join(config_dir, 'fan-curves-active.json'))
    fan.migrate_legacy('custom')
    check('the old custom curve became a profile',
          'custom' in fan.available_profiles(),
          '(profiles=%s)' % fan.available_profiles())
    check('the old profile is the running one', fan.current_profile() == 'custom')

    os.remove(os.path.join(config_dir, 'fan-curves-active.json'))
    fan.migrate_legacy('something_else')
    check('an unknown old profile falls back to the shipped profile',
          fan.current_profile() == 'custom',
          '(profile=%s)' % fan.current_profile())


def main():
    global fan, config_dir, profile_dir
    config_dir = tempfile.mkdtemp(prefix='fan-curves-test-')
    profile_dir = os.path.join(config_dir, 'fan-curves-profiles')
    try:
        fan = load_module(config_dir)
        check('there is no profile before the seed', fan.available_profiles() == [],
              '(profiles=%s)' % fan.available_profiles())
        check('stock is the running profile', fan.current_profile() == 'stock')
        check_the_shipped_profile()
        check_the_rules()
        check_the_life_of_a_profile()
        check_two_changes_at_the_same_time()
        check_the_placeholder()
        check_the_shipped_profile_is_a_profile()
        check_an_older_install()
    finally:
        shutil.rmtree(config_dir, ignore_errors=True)
    print('\n%d checks, %d failures' % (CHECKS, len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == '__main__':
    sys.exit(main())
