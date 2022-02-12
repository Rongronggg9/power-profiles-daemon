#!/usr/bin/python3

# power-profiles-daemon integration test suite
#
# Run in built tree to test local built binaries, or from anywhere else to test
# system installed binaries.
#
# Copyright: (C) 2011 Martin Pitt <martin.pitt@ubuntu.com>
# (C) 2020 Bastien Nocera <hadess@hadess.net>
# (C) 2021 David Redondo <kde@david-redondo.de>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import os
import sys
import dbus
import tempfile
import subprocess
import unittest
import time

try:
    import gi
    from gi.repository import GLib
    from gi.repository import Gio
except ImportError as e:
    sys.stderr.write('Skipping tests, PyGobject not available for Python 3, or missing GI typelibs: %s\n' % str(e))
    sys.exit(0)

try:
    gi.require_version('UMockdev', '1.0')
    from gi.repository import UMockdev
except ImportError:
    sys.stderr.write('Skipping tests, umockdev not available (https://github.com/martinpitt/umockdev)\n')
    sys.exit(0)

try:
    import dbusmock
except ImportError:
    sys.stderr.write('Skipping tests, python-dbusmock not available (http://pypi.python.org/pypi/python-dbusmock).\n')
    sys.exit(0)


PP = 'net.hadess.PowerProfiles'
PP_PATH = '/net/hadess/PowerProfiles'
PP_INTERFACE = 'net.hadess.PowerProfiles'

class Tests(dbusmock.DBusTestCase):
    @classmethod
    def setUpClass(cls):
        # run from local build tree if we are in one, otherwise use system instance
        builddir = os.getenv('top_builddir', '.')
        if os.access(os.path.join(builddir, 'src', 'power-profiles-daemon'), os.X_OK):
            cls.daemon_path = os.path.join(builddir, 'src', 'power-profiles-daemon')
            print('Testing binaries from local build tree (%s)' % cls.daemon_path)
        elif os.environ.get('UNDER_JHBUILD', False):
            jhbuild_prefix = os.environ['JHBUILD_PREFIX']
            cls.daemon_path = os.path.join(jhbuild_prefix, 'libexec', 'power-profiles-daemon')
            print('Testing binaries from JHBuild (%s)' % cls.daemon_path)
        else:
            cls.daemon_path = None
            with open('/usr/lib/systemd/system/power-profiles-daemon.service') as f:
                for line in f:
                    if line.startswith('ExecStart='):
                        cls.daemon_path = line.split('=', 1)[1].strip()
                        break
            assert cls.daemon_path, 'could not determine daemon path from systemd .service file'
            print('Testing installed system binary (%s)' % cls.daemon_path)

        # fail on CRITICALs on client and server side
        GLib.log_set_always_fatal(GLib.LogLevelFlags.LEVEL_WARNING |
                                  GLib.LogLevelFlags.LEVEL_ERROR |
                                  GLib.LogLevelFlags.LEVEL_CRITICAL)
        os.environ['G_DEBUG'] = 'fatal_warnings'

        # set up a fake system D-BUS
        cls.test_bus = Gio.TestDBus.new(Gio.TestDBusFlags.NONE)
        cls.test_bus.up()
        try:
            del os.environ['DBUS_SESSION_BUS_ADDRESS']
        except KeyError:
            pass
        os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = cls.test_bus.get_bus_address()

        cls.dbus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        cls.dbus_con = cls.get_dbus(True)

    @classmethod
    def tearDownClass(cls):
        cls.test_bus.down()
        dbusmock.DBusTestCase.tearDownClass()

    def setUp(self):
        '''Set up a local umockdev testbed.

        The testbed is initially empty.
        '''
        self.testbed = UMockdev.Testbed.new()
        self.polkitd, self.obj_polkit = self.spawn_server_template(
            'polkitd', {}, stdout=subprocess.PIPE)
        self.obj_polkit.SetAllowed(['net.hadess.PowerProfiles.switch-profile',
                                    'net.hadess.PowerProfiles.hold-profile'])

        self.proxy = None
        self.log = None
        self.daemon = None

        # Used for dytc devices
        self.tp_acpi = None

    def tearDown(self):
        del self.testbed
        self.stop_daemon()

        if self.polkitd:
            try:
                self.polkitd.kill()
            except OSError:
                pass
            self.polkitd.wait()
        self.polkitd = None
        self.obj_polkit = None

        del self.tp_acpi
        try:
          os.remove(self.testbed.get_root_dir() + '/' + 'ppd_test_conf.ini')
        except Exception:
          pass

        # on failures, print daemon log
        errors = [x[1] for x in self._outcome.errors if x[1]]
        if errors and self.log:
            with open(self.log.name) as f:
                sys.stderr.write('\n-------------- daemon log: ----------------\n')
                sys.stderr.write(f.read())
                sys.stderr.write('------------------------------\n')

    #
    # Daemon control and D-BUS I/O
    #

    def start_daemon(self):
        '''Start daemon and create DBus proxy.

        When done, this sets self.proxy as the Gio.DBusProxy for power-profiles-daemon.
        '''
        env = os.environ.copy()
        env['G_DEBUG'] = 'fatal-criticals'
        env['G_MESSAGES_DEBUG'] = 'all'
        # note: Python doesn't propagate the setenv from Testbed.new(), so we
        # have to do that ourselves
        env['UMOCKDEV_DIR'] = self.testbed.get_root_dir()
        self.log = tempfile.NamedTemporaryFile()
        if os.getenv('VALGRIND') != None:
            daemon_path = ['valgrind', self.daemon_path, '-v']
        else:
            daemon_path = [self.daemon_path, '-v']

        self.daemon = subprocess.Popen(daemon_path,
                                       env=env, stdout=self.log,
                                       stderr=subprocess.STDOUT)

        # wait until the daemon gets online
        timeout = 100
        while timeout > 0:
            time.sleep(0.1)
            timeout -= 1
            try:
                self.get_dbus_property('ActiveProfile')
                break
            except GLib.GError:
                pass
        else:
            self.fail('daemon did not start in 10 seconds')

        self.proxy = Gio.DBusProxy.new_sync(
            self.dbus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None, PP,
            PP_PATH, PP, None)

        self.assertEqual(self.daemon.poll(), None, 'daemon crashed')

    def stop_daemon(self):
        '''Stop the daemon if it is running.'''

        if self.daemon:
            try:
                self.daemon.kill()
            except OSError:
                pass
            self.daemon.wait()
        self.daemon = None
        self.proxy = None

    def get_dbus_property(self, name):
        '''Get property value from daemon D-Bus interface.'''

        proxy = Gio.DBusProxy.new_sync(
            self.dbus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None, PP,
            PP_PATH, 'org.freedesktop.DBus.Properties', None)
        return proxy.Get('(ss)', PP, name)

    def set_dbus_property(self, name, value):
        '''Set property value on daemon D-Bus interface.'''

        proxy = Gio.DBusProxy.new_sync(
            self.dbus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None, PP,
            PP_PATH, 'org.freedesktop.DBus.Properties', None)
        return proxy.Set('(ssv)', PP, name, value)

    def call_dbus_method(self, name, parameters):
        '''Call a method of the daemon D-Bus interface.'''

        proxy = Gio.DBusProxy.new_sync(
            self.dbus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None, PP,
            PP_PATH, PP_INTERFACE, None)
        return proxy.call_sync(name, parameters, Gio.DBusCallFlags.NO_AUTO_START, -1, None)


    def have_text_in_log(self, text):
        return self.count_text_in_log(text) > 0

    def count_text_in_log(self, text):
        with open(self.log.name) as f:
            return f.read().count(text)

    def read_sysfs_file(self, path):
        with open(self.testbed.get_root_dir() + '/' + path, 'rb') as f:
          return f.read().rstrip()
        return None

    def read_sysfs_attr(self, device, attribute):
        return self.read_sysfs_file(device + '/' + attribute)

    def get_mtime(self, device, attribute):
        return os.path.getmtime(self.testbed.get_root_dir() + '/' + device + '/' + attribute)

    def read_file(self, path):
        with open(path, 'rb') as f:
            return f.read()
        return None

    def create_dytc_device(self):
      self.tp_acpi = self.testbed.add_device('platform', 'thinkpad_acpi', None,
          ['dytc_lapmode', '0\n'],
          [ 'DEVPATH', '/devices/platform/thinkpad_acpi' ]
      )

    def create_empty_platform_profile(self):
      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      os.makedirs(acpi_dir)
      with open(os.path.join(acpi_dir, "platform_profile"),'w') as profile:
        profile.write('\n')
      with open(os.path.join(acpi_dir, "platform_profile_choices"),'w') as choices:
        choices.write('\n')

    def create_platform_profile(self):
      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      os.makedirs(acpi_dir)
      with open(os.path.join(acpi_dir, "platform_profile"),'w') as profile:
        profile.write("performance\n")
      with open(os.path.join(acpi_dir, "platform_profile_choices"),'w') as choices:
        choices.write("low-power balanced performance\n")

    def remove_platform_profile(self):
      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      os.remove(os.path.join(acpi_dir, "platform_profile_choices"))
      os.remove(os.path.join(acpi_dir, "platform_profile"))
      os.removedirs(acpi_dir)

    def assertEventually(self, condition, message=None, timeout=50):
        '''Assert that condition function eventually returns True.

        Timeout is in deciseconds, defaulting to 50 (5 seconds). message is
        printed on failure.
        '''
        while timeout >= 0:
            context = GLib.MainContext.default()
            while context.iteration(False):
                pass
            if condition():
                break
            timeout -= 1
            time.sleep(0.1)
        else:
            self.fail(message or 'timed out waiting for ' + str(condition))

    #
    # Actual test cases
    #
    def test_dbus_startup_error(self):
      '''D-Bus startup error'''

      self.start_daemon()
      out = subprocess.run([self.daemon_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
      self.assertEqual(out.returncode, 1, "power-profile-daemon started but should have failed")
      self.stop_daemon()

    def test_no_performance_driver(self):
      '''no performance driver'''

      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.assertEqual(self.get_dbus_property('PerformanceDegraded'), '')

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 2)
      self.assertEqual(profiles[1]['Driver'], 'placeholder')
      self.assertEqual(profiles[0]['Driver'], 'placeholder')
      self.assertEqual(profiles[1]['Profile'], 'balanced')
      self.assertEqual(profiles[0]['Profile'], 'power-saver')

      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')

      # process = subprocess.Popen(['gdbus', 'introspect', '--system', '--dest', 'net.hadess.PowerProfiles', '--object-path', '/net/hadess/PowerProfiles'])
      # print (self.get_dbus_property('GPUs'))

      self.stop_daemon()

    def test_inhibited_property(self):
      '''Test that the inhibited property exists'''

      self.create_dytc_device()
      self.create_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(self.get_dbus_property('PerformanceInhibited'), '')

    def test_degraded_transition(self):
      '''Test that transitions work as expected when degraded'''

      self.create_dytc_device()
      self.create_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('performance'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      # Degraded
      self.testbed.set_attribute(self.tp_acpi, 'dytc_lapmode', '1\n')
      self.assertEventually(lambda: self.have_text_in_log('dytc_lapmode is now on'))
      self.assertEqual(self.get_dbus_property('PerformanceDegraded'), 'lap-detected')
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      # Switch to non-performance
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')

    def test_intel_pstate(self):
      '''Intel P-State driver (no UPower)'''

      # Create 2 CPUs with preferences
      dir1 = os.path.join(self.testbed.get_root_dir(), "sys/devices/system/cpu/cpufreq/policy0/")
      os.makedirs(dir1)
      with open(os.path.join(dir1, "energy_performance_preference"),'w') as prefs:
        prefs.write("performance\n")
      dir2 = os.path.join(self.testbed.get_root_dir(), "sys/devices/system/cpu/cpufreq/policy1/")
      os.makedirs(dir2)
      with open(os.path.join(dir2, "energy_performance_preference"),'w') as prefs:
        prefs.write("performance\n")

      # Create no_turbo pref
      pstate_dir = os.path.join(self.testbed.get_root_dir(), "sys/devices/system/cpu/intel_pstate")
      os.makedirs(pstate_dir)
      with open(os.path.join(pstate_dir, "no_turbo"),'w') as no_turbo:
        no_turbo.write("0\n")

      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(profiles[0]['Driver'], 'intel_pstate')
      self.assertEqual(profiles[0]['Profile'], 'power-saver')

      contents = None
      with open(os.path.join(dir2, "energy_performance_preference"), 'rb') as f:
        contents = f.read()
      self.assertEqual(contents, b'balance_performance')

      # Set performance mode
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('performance'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      contents = None
      with open(os.path.join(dir2, "energy_performance_preference"), 'rb') as f:
        contents = f.read()
      self.assertEqual(contents, b'performance')

      # Disable turbo
      with open(os.path.join(pstate_dir, "no_turbo"),'w') as no_turbo:
        no_turbo.write("1\n")

      self.assertEventually(lambda: self.have_text_in_log('File monitor change happened for '))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      self.assertEqual(self.get_dbus_property('PerformanceDegraded'), 'high-operating-temperature')

      self.stop_daemon()

      # Verify that the Lenovo DYTC driver still gets preferred
      self.create_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(profiles[0]['Driver'], 'platform_profile')

    def test_intel_pstate_balance(self):
      '''Intel P-State driver (balance)'''

      # Create CPU with preference
      dir1 = os.path.join(self.testbed.get_root_dir(), "sys/devices/system/cpu/cpufreq/policy0/")
      os.makedirs(dir1)
      with open(os.path.join(dir1, "energy_performance_preference"),'w') as prefs:
        prefs.write("performance\n")

      upowerd, obj_upower = self.spawn_server_template(
            'upower', {'DaemonVersion': '0.99', 'OnBattery': False}, stdout=subprocess.PIPE)

      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(profiles[0]['Driver'], 'intel_pstate')
      self.assertEqual(profiles[0]['Profile'], 'power-saver')

      contents = None
      with open(os.path.join(dir1, "energy_performance_preference"), 'rb') as f:
        contents = f.read()
      # This matches what's written by ppd-driver-intel-pstate.c
      self.assertEqual(contents, b'balance_performance')

      self.stop_daemon()

      upowerd.terminate()
      upowerd.wait()
      upowerd.stdout.close()

    def test_dytc_performance_driver(self):
      '''Lenovo DYTC performance driver'''

      self.create_dytc_device()
      self.create_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(profiles[0]['Driver'], 'platform_profile')
      self.assertEqual(profiles[0]['Profile'], 'power-saver')
      self.assertEqual(profiles[2]['Driver'], 'platform_profile')
      self.assertEqual(profiles[2]['Profile'], 'performance')
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('performance'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      # lapmode detected
      self.testbed.set_attribute(self.tp_acpi, 'dytc_lapmode', '1\n')
      self.assertEventually(lambda: self.get_dbus_property('PerformanceDegraded') == 'lap-detected')
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      # Reset lapmode
      self.testbed.set_attribute(self.tp_acpi, 'dytc_lapmode', '0\n')
      self.assertEventually(lambda: self.get_dbus_property('PerformanceDegraded') == '')

      # Performance mode didn't change
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      # Switch to power-saver mode
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEventually(lambda: self.read_sysfs_file("sys/firmware/acpi/platform_profile") == b'low-power')
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')

      # And mimick a user pressing a Fn+H
      with open(os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/platform_profile"), 'w') as platform_profile:
        platform_profile.write('performance\n')
      self.assertEventually(lambda: self.get_dbus_property('ActiveProfile') == 'performance')

    def test_fake_driver(self):
      '''Test that the fake driver works'''

      os.environ['POWER_PROFILE_DAEMON_FAKE_DRIVER'] = '1'
      self.start_daemon()
      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.stop_daemon()

      del os.environ['POWER_PROFILE_DAEMON_FAKE_DRIVER']
      self.start_daemon()
      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 2)

    def test_trickle_charge_system(self):
      '''Trickle power_supply charge type'''

      fastcharge = self.testbed.add_device('power_supply', 'bq24190-charger', None,
          [ 'charge_type', 'Trickle', 'scope', 'System' ],
          []
      )

      self.start_daemon()

      self.assertIn('trickle_charge', self.get_dbus_property('Actions'))

      # Verify that charge-type stays untouched
      self.assertEqual(self.read_sysfs_attr(fastcharge, 'charge_type'), b'Trickle')

      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.read_sysfs_attr(fastcharge, 'charge_type'), b'Trickle')

    def test_trickle_charge_mode_no_change(self):
      '''Trickle power_supply charge type'''

      fastcharge = self.testbed.add_device('power_supply', 'MFi Fastcharge', None,
          [ 'charge_type', 'Fast', 'scope', 'Device' ],
          []
      )

      mtime = self.get_mtime(fastcharge, 'charge_type')
      self.start_daemon()

      self.assertIn('trickle_charge', self.get_dbus_property('Actions'))

      # Verify that charge-type didn't get touched
      self.assertEqual(self.read_sysfs_attr(fastcharge, 'charge_type'), b'Fast')
      self.assertEqual(self.get_mtime(fastcharge, 'charge_type'), mtime)

    def test_trickle_charge_mode(self):
      '''Trickle power_supply charge type'''

      idevice = self.testbed.add_device('usb', 'iDevice', None,
          [],
          [ 'ID_MODEL', 'iDevice', 'DRIVER', 'apple-mfi-fastcharge' ]
      )
      fastcharge = self.testbed.add_device('power_supply', 'MFi Fastcharge', idevice,
          [ 'charge_type', 'Trickle', 'scope', 'Device' ],
          []
      )

      self.start_daemon()

      self.assertIn('trickle_charge', self.get_dbus_property('Actions'))

      # Verify that charge-type got changed to Fast on startup
      self.assertEqual(self.read_sysfs_attr(fastcharge, 'charge_type'), b'Fast')

      # Verify that charge-type got changed to Trickle when power saving
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.read_sysfs_attr(fastcharge, 'charge_type'), b'Trickle')

      # FIXME no performance mode
      # Verify that charge-type got changed to Fast in a non-default, non-power save mode
      # self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('performance'))
      # self.assertEqual(self.read_sysfs_attr(fastcharge, 'charge_type'), 'Fast')

    def test_platform_driver_late_load(self):
      '''Test that we can handle the platform_profile driver getting loaded late'''
      self.create_empty_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 2)

      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      with open(os.path.join(acpi_dir, "platform_profile_choices"),'w') as choices:
        choices.write("low-power\nbalanced\nperformance\n")
      with open(os.path.join(acpi_dir, "platform_profile"),'w') as profile:
        profile.write("performance\n")

      # Wait for profiles to get reloaded
      self.assertEventually(lambda: len(self.get_dbus_property('Profiles')) == 3)
      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      # Was set in platform_profile before we loaded the drivers
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.assertEqual(self.get_dbus_property('PerformanceDegraded'), '')

      self.stop_daemon()

    def test_hp_wmi(self):

      # Uses cool instead of low-power
      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      os.makedirs(acpi_dir)
      with open(os.path.join(acpi_dir, "platform_profile"),'w') as profile:
        profile.write("cool\n")
      with open(os.path.join(acpi_dir, "platform_profile_choices"),'w') as choices:
        choices.write("cool balanced performance\n")

      self.start_daemon()
      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(profiles[0]['Driver'], 'platform_profile')
      self.assertEqual(profiles[0]['Profile'], 'power-saver')
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.assertEqual(self.read_sysfs_file("sys/firmware/acpi/platform_profile"), b'balanced')
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.assertEqual(self.read_sysfs_file("sys/firmware/acpi/platform_profile"), b'cool')

      self.stop_daemon()

    def test_quiet(self):
      # Uses cool instead of low-power
      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      os.makedirs(acpi_dir)
      with open(os.path.join(acpi_dir, "platform_profile"),'w') as profile:
        profile.write("cool\n")
      with open(os.path.join(acpi_dir, "platform_profile_choices"),'w') as choices:
        choices.write("quiet balanced balanced-performance performance\n")

      self.start_daemon()
      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(profiles[0]['Driver'], 'platform_profile')
      self.assertEqual(profiles[0]['Profile'], 'power-saver')
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.assertEqual(self.read_sysfs_file("sys/firmware/acpi/platform_profile"), b'balanced')
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.assertEqual(self.read_sysfs_file("sys/firmware/acpi/platform_profile"), b'quiet')

      self.stop_daemon()

    def test_hold_release_profile(self):
      self.create_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)

      cookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', 'testReason', 'testApplication')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      profileHolds = self.get_dbus_property('ActiveProfileHolds')
      self.assertEqual(len(profileHolds), 1)
      self.assertEqual(profileHolds[0]["Profile"], "performance")
      self.assertEqual(profileHolds[0]["Reason"], "testReason")
      self.assertEqual(profileHolds[0]["ApplicationId"], "testApplication")

      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", cookie))
      profileHolds = self.get_dbus_property('ActiveProfileHolds')
      self.assertEqual(len(profileHolds), 0)
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      # When the profile is changed manually, holds should be released a
      self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(len(self.get_dbus_property('ActiveProfileHolds')), 1)
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')

      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('balanced'))
      self.assertEqual(len(self.get_dbus_property('ActiveProfileHolds')), 0)
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      # When all holds are released, the last manually selected profile should be activated
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      cookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", cookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')

      self.stop_daemon()

    def test_vanishing_hold(self):
      self.create_platform_profile()
      self.start_daemon()

      builddir = os.getenv('top_builddir', '.')
      tool_path = os.path.join(builddir, 'src', 'powerprofilesctl')

      launch_process = subprocess.Popen([tool_path, 'launch', '-p', 'power-saver', 'sleep', '3600'],
          stdout=sys.stdout, stderr=sys.stderr)
      assert launch_process
      time.sleep(1)
      holds = self.get_dbus_property('ActiveProfileHolds')
      self.assertEqual(len(holds), 1)
      hold = holds[0]
      self.assertEqual(hold['Profile'], 'power-saver')

      # Make sure to handle vanishing clients
      launch_process.terminate()
      launch_process.wait()

      holds = self.get_dbus_property('ActiveProfileHolds')
      self.assertEqual(len(holds), 0)

      self.stop_daemon()

    def test_hold_priority(self):
      '''power-saver should take priority over performance'''
      self.create_platform_profile()
      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      # Test every order of holding and releasing power-saver and performance
      # hold performance and then power-saver, release in the same order
      performanceCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      powerSaverCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('power-saver', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", performanceCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", powerSaverCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      # hold performance and then power-saver, but release power-saver first
      performanceCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      powerSaverCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('power-saver', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)",powerSaverCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", performanceCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      # hold power-saver and then performance, release in the same order
      powerSaverCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('power-saver', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      performanceCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)",powerSaverCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", performanceCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      # hold power-saver and then performance, but release performance first
      powerSaverCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('power-saver', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      performanceCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)",performanceCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.call_dbus_method('ReleaseProfile', GLib.Variant("(u)", powerSaverCookie))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      self.stop_daemon()

    def test_save_profile(self):
      '''save profile across runs'''

      self.create_platform_profile()

      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.stop_daemon()

      # sys.stderr.write('\n-------------- config file: ----------------\n')
      # with open(self.testbed.get_root_dir() + '/' + 'ppd_test_conf.ini') as f:
      #   sys.stderr.write(f.read())
      # sys.stderr.write('------------------------------\n')

      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      # Programmatically set profile aren't saved
      performanceCookie = self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'performance')
      self.stop_daemon()

      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'power-saver')
      self.stop_daemon()

    def test_save_deferred_load(self):
      '''save profile across runs, but kernel driver loaded after start'''

      self.create_platform_profile()
      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.set_dbus_property('ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.stop_daemon()
      self.remove_platform_profile()

      # We could verify the contents of the configuration file here

      self.create_empty_platform_profile()
      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      acpi_dir = os.path.join(self.testbed.get_root_dir(), "sys/firmware/acpi/")
      with open(os.path.join(acpi_dir, "platform_profile_choices"),'w') as choices:
        choices.write("low-power\nbalanced\nperformance\n")
      with open(os.path.join(acpi_dir, "platform_profile"),'w') as profile:
        profile.write("performance\n")

      self.assertEventually(lambda: self.get_dbus_property('ActiveProfile') == 'power-saver')
      self.stop_daemon()

    def test_not_allowed_profile(self):
      '''Check that we get errors when trying to change a profile and not allowed'''

      self.obj_polkit.SetAllowed(dbus.Array([], signature='s'))
      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      proxy = Gio.DBusProxy.new_sync(
          self.dbus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None, PP,
          PP_PATH, 'org.freedesktop.DBus.Properties', None)
      with self.assertRaises(gi.repository.GLib.GError) as cm:
          proxy.Set('(ssv)', PP, 'ActiveProfile', GLib.Variant.new_string('power-saver'))
      self.assertIn('AccessDenied', str(cm.exception))

      self.stop_daemon()

    def test_not_allowed_hold(self):
      '''Check that we get an error when trying to hold a profile and not allowed'''

      self.obj_polkit.SetAllowed(dbus.Array([], signature='s'))
      self.start_daemon()
      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')

      with self.assertRaises(gi.repository.GLib.GError) as cm:
        self.call_dbus_method('HoldProfile', GLib.Variant("(sss)", ('performance', '', '')))
      self.assertIn('AccessDenied', str(cm.exception))

      self.assertEqual(self.get_dbus_property('ActiveProfile'), 'balanced')
      self.assertEqual(len(self.get_dbus_property('ActiveProfileHolds')), 0)

      self.stop_daemon()

    def test_intel_pstate_noturbo(self):
      '''Intel P-State driver (balance)'''

      # Create CPU with preference
      dir1 = os.path.join(self.testbed.get_root_dir(), "sys/devices/system/cpu/cpufreq/policy0/")
      os.makedirs(dir1)
      with open(os.path.join(dir1, "energy_performance_preference"),'w') as prefs:
        prefs.write("performance\n")

      # Create no_turbo pref with turbo_pct
      pstate_dir = os.path.join(self.testbed.get_root_dir(), "sys/devices/system/cpu/intel_pstate")
      os.makedirs(pstate_dir)
      with open(os.path.join(pstate_dir, "no_turbo"),'w') as no_turbo:
        no_turbo.write("1\n")
      with open(os.path.join(pstate_dir, "turbo_pct"),'w') as no_turbo:
        no_turbo.write("0\n")

      self.start_daemon()

      profiles = self.get_dbus_property('Profiles')
      self.assertEqual(len(profiles), 3)
      self.assertEqual(self.get_dbus_property('PerformanceDegraded'), '')

      self.stop_daemon()

    def test_powerprofilesctl_error(self):
      '''Check that powerprofilesctl returns 1 rather than an exception on error'''

      builddir = os.getenv('top_builddir', '.')
      tool_path = os.path.join(builddir, 'src', 'powerprofilesctl')

      with self.assertRaises(subprocess.CalledProcessError) as cm:
          subprocess.check_output([tool_path, 'list'],
                                  stderr=subprocess.PIPE,
                                  universal_newlines=True)
      self.assertNotIn('Traceback', cm.exception.stderr)

      with self.assertRaises(subprocess.CalledProcessError) as cm:
          subprocess.check_output([tool_path, 'get'],
                                  stderr=subprocess.PIPE,
                                  universal_newlines=True)
      self.assertNotIn('Traceback', cm.exception.stderr)

      with self.assertRaises(subprocess.CalledProcessError) as cm:
          subprocess.check_output([tool_path, 'set', 'not-a-profile'],
                                  stderr=subprocess.PIPE,
                                  universal_newlines=True)
      self.assertNotIn('Traceback', cm.exception.stderr)

      with self.assertRaises(subprocess.CalledProcessError) as cm:
          subprocess.check_output([tool_path, 'list-holds'],
                                  stderr=subprocess.PIPE,
                                  universal_newlines=True)
      self.assertNotIn('Traceback', cm.exception.stderr)

      with self.assertRaises(subprocess.CalledProcessError) as cm:
          subprocess.check_output([tool_path, 'launch', '-p', 'power-saver', 'sleep', '1'],
                                  stderr=subprocess.PIPE,
                                  universal_newlines=True)
      self.assertNotIn('Traceback', cm.exception.stderr)

      self.start_daemon()
      with self.assertRaises(subprocess.CalledProcessError) as cm:
          subprocess.check_output([tool_path, 'set', 'not-a-profile'],
                                  stderr=subprocess.PIPE,
                                  universal_newlines=True)
      self.assertNotIn('Traceback', cm.exception.stderr)
      self.stop_daemon()

    #
    # Helper methods
    #

    @classmethod
    def _props_to_str(cls, properties):
        '''Convert a properties dictionary to uevent text representation.'''

        prop_str = ''
        if properties:
            for k, v in properties.items():
                prop_str += '%s=%s\n' % (k, v)
        return prop_str

if __name__ == '__main__':
    # run ourselves under umockdev
    if 'umockdev' not in os.environ.get('LD_PRELOAD', ''):
        os.execvp('umockdev-wrapper', ['umockdev-wrapper'] + sys.argv)

    unittest.main()
