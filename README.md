# brightnessctl

This program allows you read and control device brightness. Devices, by default, include backlight and LEDs (searched for in corresponding classes). If omitted, the first found device is selected.

It can also preserve current brightness before applying the operation (allowing for usecases like disabling backlight on lid close).

## Installation

The program is available in:
* [Alpine Linux](https://pkgs.alpinelinux.org/package/edge/community/x86_64/brightnessctl) - starting with 3.11 and Edge
* [Arch Linux](https://www.archlinux.org/packages/community/x86_64/brightnessctl/)
* [Void Linux](https://github.com/void-linux/void-packages/blob/master/srcpkgs/brightnessctl/template)
* [Debian](https://packages.debian.org/testing/source/brightnessctl) - starting with Buster (and derivatives)
* [Ubuntu](https://packages.ubuntu.com/source/bionic/brightnessctl) - starting with 18.04 (and derivatives)
* [openSUSE](https://build.opensuse.org/package/show/utilities/brightnessctl) - available in Tumbleweed, use OBS `utilities/brightnessctl` devel project for Leap < 15.1
* [Fedora/EPEL](https://apps.fedoraproject.org/packages/brightnessctl) (orphaned, deleted since F30, maintainer wanted)
* [NixOS/nix](https://nixos.org/nixos/packages.html?attr=brightnessctl) - starting with 17.09, please see the [NixOS Wiki page](https://nixos.wiki/wiki/Backlight#brightnessctl) for the "best-practice" configuration file based installation

One can build and install the program using `make install`. Consult the Makefile for relevant build-time options.

## Permissions

Modifying brightness requires write permissions for device files or systemd support. `brightnessctl` accomplishes this (without using `sudo`/`su`/etc.) by either of the following means:

1) installing relevant udev rules to add permissions to backlight class devices for users in `video` and leds for users in `input`. (done by default)

2) installing `brightnessctl` as a suid binary.

3) using the `systemd-logind` API.

The behavior is controlled by the `INSTALL_UDEV_RULES` flag (setting it to `1` installs the udev rules, it is the default value).

The systemd support (since v243) is controlled by the `ENABLE_SYSTEMD` flag (udev rules will not be installed by default).

## Usage
```
Usage: brightnessctl [options] [operation] [value]

Options:
  -l, --list			list devices with available brightness controls.
  -q, --quiet			suppress output.
  -p, --pretend			do not perform write operations.
  -m, --machine-readable	produce machine-readable output.
  -n, --min-value		set minimum brightness, defaults to 1.
  -e, --exponent[=K]		changes percentage curve to exponential.
  -s, --save			save previous state in a temporary file.
  -r, --restore			restore previous saved state.
  -h, --help			print this help.
  -d, --device=DEVICE		specify device name (can be a wildcard).
  -c, --class=CLASS		specify device class.
  -V, --version			print version and exit.

Operations:
  i, info			get device info.
  g, get			get current brightness of the device.
  m, max			get maximum brightness of the device.
  s, set VALUE			set brightness of the device.

Valid values:
  specific value		Example: 500
  percentage value		Example: 50.5%
  specific delta		Example: 50- or +10
  percentage delta		Example: 50%- or +10%
 ```
