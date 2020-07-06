power-profiles-daemon
=====================

Makes power profiles handling available over D-Bus.

Installation
------------
```sh
$ meson _build -Dprefix=/usr
$ ninja -v -C _build install
```
It requires libgudev and systemd (>= 233 for the accelerometer quirks).

Introduction
------------

power-profiles-daemon offers to modify system behaviour based upon user-selected
power profiles. There are 3 different power profiles, a "balanced" default mode,
a "power-saver" mode, as well as a "performance" mode. The first 2 of those are
available on every system. The "performance" mode is only available on select
systems and is implemented by different "drivers" based on the system or
systems it targets.

In addition to those 2 or 3 modes (depending on the system), "actions" can be hooked
up to change the behaviour of a particular device. For example, this can be used
to disable the fast-charging for some USB devices when in power-saver mode.

GNOME's Settings and shell both include interfaces to select the current mode, but
they are also expected to adjust the behaviour of the desktop depending on the mode,
such as turning the screen off after inaction more aggressively when in power-saver
mode.

Note that power-profiles-daemon does not save the currently active profile across
system restarts and will always start with the "balanced" profile selected.

Debugging
---------

You can now check which mode is in use, and which ones are available by running:
```
gdbus introspect --system --dest net.hadess.PowerProfiles --object-path /net/hadess/PowerProfiles
```

If that doesn't work, please file an issue, make sure any running power-profiles-daemon
has been stopped:
`systemctl stop power-profiles-daemon.service`
and attach the output of:
`G_MESSAGES_DEBUG=all /usr/libexec/power-profiles-daemon`
running as ```root```.

References
----------

- [Use Low Power Mode to save battery life on your iPhone (iOS)](https://support.apple.com/en-us/HT205234)
- [lowPowerModeEnabled (iOS)](https://developer.apple.com/documentation/foundation/nsprocessinfo/1617047-lowpowermodeenabled?language=objc)
- [React to Low Power Mode on iPhones (iOS)](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/EnergyGuide-iOS/LowPowerMode.html#//apple_ref/doc/uid/TP40015243-CH31)
- [[S]ettings that use less battery (Android)](https://support.google.com/android/answer/7664692?hl=en&visit_id=637297348326801871-2263015427&rd=1)
- [EnergySaverStatus Enum (Windows)](https://docs.microsoft.com/en-us/uwp/api/windows.system.power.energysaverstatus?view=winrt-19041)
