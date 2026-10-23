# Fan curve host side checks

These checks drive the fan curve support of the firmware on a development
machine. They need a Python 3 and no printer.

```bash
./overlays/firmware-extended/02-firmware-config/test/run.sh
```

Every check prints what it did. A failing check prints what it expected
instead, and the run ends with a non zero exit code.

## What is covered

`fan_curves_test.py` drives the real `fan_curves.py` of this repository
against a temporary config directory:

- the profile the firmware ships takes the place of an empty profile
  directory, and a profile of the user is never touched
- the shipped profile is a profile like any other: it can be renamed
- a new profile starts close to the stock curve
- saving a curve keeps the settings a change of the curve does not touch: the
  heavy load rule of the profile
- the state the editor reads carries the rule, the label and the warning of
  every profile
- applying a profile writes the curve and its rule to the file the driver
  reads
- renaming a profile keeps the running profile and its rule
- deleting the profile that is running puts the printer back on the stock
  curves
- the rules a curve has to follow: two to sixteen points, temperatures that
  ascend and stay below 300C, duties between 0% and 100%
- an older install keeps the curve it drew, and an unknown profile of an
  older install falls back to the profile the firmware ships

`heater_fan_test.py` builds the patched driver by applying the patch of this
repository to the `heater_fan.py` of the Klipper release the firmware ships
with, and drives both drivers with the fan tables of the printer's own
`printer.cfg`:

- the stock profile gives the same duty as the Klipper release at every
  temperature, for the nozzle fan and for the power fan, with four
  toolheads and with the cavity sensor of the printer
- a curve that asks for 0% gets 0%, the driver runs the curve as it is
  written
- the duty between two points is the line between them
- the faulty toolhead bypass keeps its fan on the stock stepped table
- a bogus toolhead reading does not raise the power fan
- probing locks the nozzle fan to its `probe_speed`
- a hot chamber forces the nozzle fan to 100%
- the heavy load rule of the stock power fan holds
- the profile the firmware ships never cools less than the stock fans

## The fixtures

- `stock_heater_fan.py` is the `klippy/extras/heater_fan.py` of the Klipper
  release the firmware ships with. It keeps its own copyright header.
- `printer.cfg` holds the six `heater_fan` sections of the factory
  configuration of the Snapmaker U1, copied verbatim: the power fan and the
  four nozzle fans. The rest of the factory configuration belongs to the
  printer, not to the fan driver.

Both fixtures are the real thing. The checks read the driver and the patch
of this repository, they do not keep a copy of them.

## What is not covered

These need the machine:

- applying a profile from the Firmware Config page and Klipper restarting
  without a curve error
- the fans reporting the expected duty at a known temperature
- a long print with the shipped profile, with the temperatures and the fan
  duties watched
- a long print with a chamber heated, to watch the chamber and the
  electronics
- a profile of your own: draw it, save it, apply it and watch it run
- deleting the profile that is running and watching the printer fall back to
  the stock curves

The checks that drive the curve editor in a headless browser need a browser.
They are not part of this run.
