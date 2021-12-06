power-profiles-daemon
=====================

Makes power profiles handling available over D-Bus.

Installation
------------
```sh
$ meson _build -Dprefix=/usr
$ ninja -v -C _build install
```
It requires libgudev, systemd and polkit-gobject.

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

How to use
----------

There are interfaces to switch profiles in the latest versions of KDE and GNOME. Those
desktops also include more thorough integration with its low-power mode. Please check
the user guides for each of them for details.

power-profiles-daemon also ships with a command-line utility called `powerprofilesctl`
which can be used for scripting, as it allows getting and setting the active profile,
listing the available profiles, and launching commands while holding the performance
or the power-saver profile.

For example, this will be useful to avoid manual switching profiles while compiling
large projects:
```sh
powerprofilesctl launch make
```

If you're a developer, you might also want to use GLib's [`GPowerProfileMonitor`](https://docs.gtk.org/gio/iface.PowerProfileMonitor.html)
through C, or one of its bindings, so your application can react to the user requesting
a low-power mode.

Conflicts
---------

If `power-profiles-daemon` refuses to start, it's likely that you have [a conflicting
service installed and running](data/power-profiles-daemon.service.in#L3).

```sh
systemctl unmask power-profiles-daemon.service
systemctl start power-profiles-daemon.service
```

Debugging
---------

You can now check which mode is in use, and which ones are available by running:

```sh
powerprofilesctl
```

You can change the selected profile by running (change `power-saver` for the
chosen profile):

```sh
powerprofilesctl set power-saver
```

You can check the current configuration which will be restored on
reboot in `/var/lib/power-profiles-daemon/state.ini`.

Those commands are also available through the D-Bus interface:

```
gdbus introspect --system --dest net.hadess.PowerProfiles --object-path /net/hadess/PowerProfiles
gdbus call --system --dest net.hadess.PowerProfiles --object-path /net/hadess/PowerProfiles --method org.freedesktop.DBus.Properties.Set 'net.hadess.PowerProfiles' 'ActiveProfile' "<'power-saver'>"
```

If that doesn't work, please file an issue, attach the output of:

```sh
sudo G_MESSAGES_DEBUG=all /usr/libexec/power-profiles-daemon -r -v
```

Testing
-------

If you don't have hardware that can support the performance mode, or the degraded mode
you can manually run the `power-profiles-daemon` binary as `root` with the environment
variable `POWER_PROFILE_DAEMON_FAKE_DRIVER` set to 1. For example:

```sh
sudo POWER_PROFILE_DAEMON_FAKE_DRIVER=1 /usr/libexec/power-profiles-daemon -r -v
```

References
----------

- [Use Low Power Mode to save battery life on your iPhone (iOS)](https://support.apple.com/en-us/HT205234)
- [lowPowerModeEnabled (iOS)](https://developer.apple.com/documentation/foundation/nsprocessinfo/1617047-lowpowermodeenabled?language=objc)
- [React to Low Power Mode on iPhones (iOS)](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/EnergyGuide-iOS/LowPowerMode.html#//apple_ref/doc/uid/TP40015243-CH31)
- [[S]ettings that use less battery (Android)](https://support.google.com/android/answer/7664692?hl=en&visit_id=637297348326801871-2263015427&rd=1)
- [EnergySaverStatus Enum (Windows)](https://docs.microsoft.com/en-us/uwp/api/windows.system.power.energysaverstatus?view=winrt-19041)

Why power-profiles-daemon
-------------------------

The power-profiles-daemon project was created to help provide a solution for
two separate use cases, for desktops, laptops, and other devices running a
“traditional Linux desktop”.

The first one is a "Low Power" mode, that users could toggle themselves, or
have the system toggle for them, with the intent to save battery. Mobile
devices running iOS and Android have had a similar feature available to
end-users and application developers alike.

The second use case was to allow a "Performance" mode on systems where the
hardware maker would provide and design such a mode. The idea is that the
Linux kernel would provide a way to access this mode which usually only
exists as a configuration option in some machines' "UEFI Setup" screen.

This second use case is the reason why we didn't implement the "Low Power"
mode in UPower, [as was originally discussed](https://gitlab.freedesktop.org/upower/upower/-/issues/102).

As the daemon would change kernel settings, we would need to run it as root,
and make its API available over D-Bus, as has been customary for more than
10 years. We would also design that API to be as easily usable to build
graphical interfaces as possible.

Why not...
----------

This section will contain explanations of why this new daemon was written
rather than re-using, or modifying an existing one. Each project obviously
has its own goals and needs, and those comparisons are not meant as a slight
on the project.

As the code bases for both those projects listed and power-profiles-daemon are
ever evolving, the comments were understood to be correct when made.

### [thermald](https://01.org/linux-thermal-daemon/documentation/introduction-thermal-daemon)

thermald only works on Intel CPUs, and is very focused on allowing maximum
performance based on a "maximum temperature" for the system. As such, it
could be seen as complementary to power-profiles-daemon.

### [tuned](https://github.com/redhat-performance/tuned/) and [TLP](https://linrunner.de/tlp/)

Both projects have similar goals, allowing for tweaks to be applied, for
a variety of workloads that goes far beyond the workloads and use cases
that power-profiles-daemon targets.

A fair number of the tweaks that could apply to devices running GNOME or
another free desktop are either potentially destructive (eg. some of the
SATA power-saving mode resulting in corrupted data), or working well
enough to be put into place by default (eg. audio codec power-saving), even
if we need to disable the power saving on some hardware that reacts
badly to it.

Both are good projects to use for the purpose of experimenting with particular
settings to see if they'd be something that can be implemented by default,
or to put some fine-grained, static, policies in place on server-type workloads
which are not as fluid and changing as desktop workloads can be.

### [auto-cpufreq](https://github.com/AdnanHodzic/auto-cpufreq)

It doesn't take user-intent into account, doesn't have a D-Bus interface and
seems to want to work automatically by monitoring the CPU usage, which kind
of goes against a user's wishes as a user might still want to conserve as
much energy as possible under high-CPU usage.

### [slimbookbattery](https://launchpad.net/~slimbook)
This is **not** free software (*Source code available but not modifiable
without express authorization.*). The application does a lot of things in
addition to the "3 profiles" selection:

- replaces part of the suspend mechanism with its own hybrid sleep implementation
  (systemd already implements one)
- implements charging limits for batteries
- implements some power saving tricks, which could also be implemented

A lot of those power-saving tricks could be analysed and used, but we
obviously can't rely on "source available" software for our free desktops.

### [system76-power](https://github.com/pop-os/system76-power)

Very similar project to power-profiles-daemon but goes much more into the weeds
in terms of power-saving/performance implementation.

It has a D-Bus API for choosing different power profiles, and applies a number
of settings based on the profile selected. Most of the interesting settings are
already upstreamed ([SATA power tweaks](https://hansdegoede.livejournal.com/18412.html)),
should be upstreamed to the vanilla kernel if possible (PCI power-savings), or
are things we already implement (Intel P-State).

It could without a doubt have been used as a base for power-profiles-daemon if it
was more of an upstream project instead of a PopOS!/System76 project.

### [asusctl](https://gitlab.com/asus-linux/asusctl/)

It provides an interface to a number of ASUS-specific features which isn't directly
relevant to power-profiles-daemon like handling keyboard LED settings, or setting
battery charge limits. The functionality that was relevant got moved to the asus-wmi
kernel driver during the 5.14 kernel development cycle, where power-profiles-daemon
can consume it. The 2 daemons are now complementary.
