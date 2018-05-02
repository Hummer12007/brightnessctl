# brightnessctl

This program allows you read and control device brightness. Devices, by default, include backlight and LEDs (searched for in corresponding classes). If omitted, the first found device is selected.

It can also preserve current brightness before applying the operation (allowing for usecases like disabling backlight on lid close).

## Installation

The program is available in:
* [Fedora/EPEL](https://apps.fedoraproject.org/packages/brightnessctl)
* [Arch Linux (AUR)](https://aur.archlinux.org/packages/brightnessctl)
* [Void Linux](https://github.com/voidlinux/void-packages/blob/master/srcpkgs/brightnessctl/template)
* [Debian](https://packages.debian.org/testing/source/brightnessctl) - starting with Buster (and derivatives)
* [Ubuntu](https://packages.ubuntu.com/source/bionic/brightnessctl) - starting with 18.04 (and derivatives)

One can build and install the program using `make install`. Consult the Makefile for relevant build-time options.

## Permissions

Modifying brightness requires write permissions for device files. `brightnessctl` accomplishes this (without using `sudo`/`su`/etc.) by either of the following means:

1) installing `brightnessctl` as a suid binary (done by default)

2) installing relevant udev rules to add permissions to backlight class devices for users in `video` and leds for users in `input`.

The behavior is controlled by the `INSTALL_UDEV_RULES` flag (setting it to `1` installs the udev rules, `0` is the default value).

## Usage
```
Usage: brightnessctl [options] [operation] [value]

Options:
  -l, --list			List devices with available brightness controls.
  -q, --quiet			Suppress output.
  -p, --pretend			Do not perform write operations.
  -m, --machine-readable	Produce machine-readable output.
  -s, --save			Save previous state in a temporary file.
  -r, --restore			Restore previous saved state.
  -h, --help			Print this help.
  -d, --device=DEVICE		Specify device name (can be a wildcard).
  -c, --class=CLASS		Specify device class.
  -V, --version			Print version and exit.

Operations:
  i, info			Get device info.
  g, get			Get current brightness of the device.
  m, max			Get maximum brightness of the device.
  s, set VALUE			Set brightness of the device.

Valid values:
  specific value		Example: 500
  percentage value		Example: 50%
  specific delta		Example: 50- or +10
  percentage delta		Example: 50%- or +10%
 ```
