# brightnessctl

This program allows you read and control device brightness on Linux. Devices, by default, include backlight and LEDs (searched for in corresponding classes). If omitted, the first found device is selected.

It can also preserve current brightness before applying the operation (allowing for usecases like disabling backlight on lid close).

## Installation

The program is available in:
* [Alpine Linux](https://pkgs.alpinelinux.org/packages?name=brightnessctl&branch=edge)
* [Arch Linux](https://www.archlinux.org/packages/community/x86_64/brightnessctl/)
* [Void Linux](https://github.com/void-linux/void-packages/blob/master/srcpkgs/brightnessctl/template)
* [Debian](https://packages.debian.org/testing/source/brightnessctl) - starting with Buster (and derivatives)
* [Ubuntu](https://packages.ubuntu.com/source/bionic/brightnessctl) - starting with 18.04 (and derivatives)
* [openSUSE](https://build.opensuse.org/package/show/utilities/brightnessctl) - available in Tumbleweed, use OBS `utilities/brightnessctl` devel project for Leap < 15.1
* [Fedora](https://src.fedoraproject.org/rpms/brightnessctl) - available in Fedora 31+
* [NixOS/nix](https://nixos.org/nixos/packages.html?attr=brightnessctl) - starting with 17.09, please see the [NixOS Wiki page](https://nixos.wiki/wiki/Backlight#brightnessctl) for the "best-practice" configuration file based installation

One can build and install the program using `./configure && make install`. Consult `./configure --help` for relevant build-time options.

## Permissions

Modifying brightness requires write permissions for device files or systemd support. `brightnessctl` accomplishes this (without using `sudo`/`su`/etc.) by either of the following means:

1) installing relevant udev rules to add permissions to backlight class devices for users in `video` and leds for users in `input`. (done by default)

2) installing `brightnessctl` as a suid binary.

3) using the `systemd-logind` API.

## FAQ

#### I'd like to configure the brightness of an external monitor

Use the [ddcci-driver-linux](https://gitlab.com/ddcci-driver-linux/ddcci-driver-linux) kernel module to expose external monitor brightness controls to `brightnessctl`. Available in repositories of [AUR](https://aur.archlinux.org/packages/ddcci-driver-linux-dkms/), [Debian](https://packages.debian.org/stable/ddcci-dkms), [Nix](https://github.com/NixOS/nixpkgs/blob/master/pkgs/os-specific/linux/ddcci/default.nix), [Ubuntu](https://packages.ubuntu.com/bionic/admin/ddcci-dkms), [Void](https://github.com/void-linux/void-packages/tree/master/srcpkgs/ddcci-dkms).

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
  percentage value		Example: 50%
  specific delta		Example: 50- or +10
  percentage delta		Example: 50%- or +10%
 ```
