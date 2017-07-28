# brightnessctl

This program allows you read and control device brightness. Devices, by default, include backlight and LEDs (searched for in corresponding classes). If omitted, the first found device is selected.

It can also preserve current brightness before applying the operation (allowing for usecases like disabling backlight on lid close).

## Installation

The program is available in:
* [Fedora/EPEL](https://apps.fedoraproject.org/packages/brightnessctl)
* [Arch Linux (AUR)](https://aur.archlinux.org/packages/brightnessctl)

## Permissions

brightnessctl installs udev rules to add permissions to backlights to users in
group `video` and leds to users in group ´input´.

## Usage
```
Usage: brightnessctl [options] [operation] [value]

Options:
  -l, --list			list devices with available brightness controls.
  -q, --quiet			suppress output.
  -p, --pretend			do not perform write operations.
  -m, --machine-readable	produce machine-readable output.
  -s, --save			save previous state in a temporary file.
  -r, --restore			restore previous saved state.
  -h, --help			print this help.
  -d, --device=DEVICE		specify device name.
  -c, --class=CLASS		specify device class.
  -V, --version			print version and exit.

Operations:
  i, info			get device info.
  g, get			get current brightness of the device.
  m, max			get maximum brightness of the device.
  s, set VALUE			set brightness of the device.

Valid values:
  specific value		Example: 500
  percentage value		Example: 50%
  specific delta		Example: 50- or +10
  percentage delta		Example: 50%- or +10%
 ```
