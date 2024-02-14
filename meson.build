project('power-profiles-daemon', [ 'c' ],
        version: '0.13',
        license: 'GPLv3+',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=1',
          'c_std=c99',
        ],
        meson_version: '>= 0.59.0')

cc = meson.get_compiler('c')

common_cflags = cc.get_supported_arguments([
    '-fgnu89-inline',
    '-Wundef',
    '-Wunused',
    '-Wstrict-prototypes',
    '-Werror-implicit-function-declaration',
    '-Wno-pointer-sign',
    '-Wshadow',
    '-Wno-sign-compare',
    '-Wno-cast-function-type',
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
    '-Wno-type-limits',
])

prefix = get_option('prefix')
libexecdir = prefix / get_option('libexecdir')
bindir = get_option('bindir')
dbusconfdir = get_option('datadir') / 'dbus-1' / 'system.d'
dbusservicedir = get_option('datadir') / 'dbus-1' / 'system-services'

systemd_system_unit_dir = get_option('systemdsystemunitdir')
if systemd_system_unit_dir == 'auto'
    systemd_dep = dependency('systemd')
    systemd_system_unit_dir = systemd_dep.get_variable('systemdsystemunitdir')
endif
gio_dep = dependency('gio-2.0')
gudev_dep = dependency('gudev-1.0', version: '>= 234')
upower_dep = dependency('upower-glib')
polkit_gobject_dep = dependency('polkit-gobject-1', version: '>= 0.91')
polkit_policy_directory = polkit_gobject_dep.get_variable('policydir')

gnome = import('gnome')

add_global_arguments('-D_GNU_SOURCE=1', language: 'c')
add_global_arguments(common_cflags, language: 'c')

if get_option('pylint')
    pylint = find_program('pylint-3', 'pylint3', 'pylint', required: true)
    nomalloc = environment({'MALLOC_PERTURB_': '0'})
    pylint_flags = ['-d', 'C0116', '-d', 'C0114', '-d', 'W0707', '-d', 'W0706' ]
endif
xmllint = find_program('xmllint', required: false)

bus_names = {
  'org.freedesktop.UPower.PowerProfiles': '/org/freedesktop/UPower/PowerProfiles',
  'net.hadess.PowerProfiles': '/net/hadess/PowerProfiles',
}

address_sanitizer = get_option('b_sanitize') == 'address' or \
  get_option('b_sanitize') == 'address,undefined' or \
  get_option('b_sanitize') == 'leak'

subdir('src')
subdir('data')

if get_option('gtk_doc')
  # Make COPYING available in the build root for docs
  configure_file(
    input: 'COPYING',
    output: 'COPYING',
    copy: true,
  )
  subdir('docs')
endif

if get_option('tests')
  # Python 3 required modules
  python3_required_modules = ['dbusmock', 'gi']
  gi_required_modules = {
    'GLib': '2.0',
    'Gio': '2.0',
    'UMockdev': '1.0',
  }

  python = import('python')
  python3 = python.find_installation('python3')
  foreach p : python3_required_modules
    # Source: https://docs.python.org/3/library/importlib.html#checking-if-a-module-can-be-imported
    script = 'import importlib.util; import sys; exit(1) if importlib.util.find_spec(\''+ p +'\') is None else exit(0)'
    if run_command(python3, '-c', script, check: false).returncode() != 0
      error('Python3 module \'' + p + '\' required for running tests but not found')
    endif
  endforeach

  foreach module, version : gi_required_modules
    script = 'import gi; gi.require_version("@0@", "@1@")'.format(module, version)
    if run_command(python3, '-c', script, check: false).returncode() != 0
      error('''GObject Introspection module '@0@' version @1@ required for running tests but not found'''.format(
        module, version))
    endif
  endforeach

  subdir('tests')
endif

meson.add_dist_script(
  find_program('check-news.sh').full_path(),
  '@0@'.format(meson.project_version())
)
