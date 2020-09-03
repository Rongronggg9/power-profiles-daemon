project('power-profiles-daemon', [ 'c' ],
        version: '0.1',
        license: 'GPLv3+',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=1',
          'c_std=c99',
        ],
        meson_version: '>= 0.54.0')

cc = meson.get_compiler('c')

common_cflags = cc.get_supported_arguments([
    '-fgnu89-inline',
    '-fvisibility=hidden',
    '-std=gnu99',
    '-Wall',
    '-Wundef',
    '-Wunused',
    '-Wstrict-prototypes',
    '-Werror-implicit-function-declaration',
    '-Wno-pointer-sign',
    '-Wshadow'
])

prefix = get_option('prefix')
libexecdir = prefix / get_option('libexecdir')
bindir = get_option('bindir')
dbusconfdir = get_option('sysconfdir') / 'dbus-1' / 'system.d'
dbusservicedir = get_option('datadir') / 'dbus-1' / 'system-services'

systemd_system_unit_dir = get_option('systemdsystemunitdir')
if systemd_system_unit_dir == 'auto'
    systemd_dep = dependency('systemd')
    systemd_system_unit_dir = systemd_dep.get_pkgconfig_variable('systemdsystemunitdir')
endif
gio_dep = dependency('gio-2.0')
gudev_dep = dependency('gudev-1.0', version: '>= 232')
upower_dep = dependency('upower-glib')

gnome = import('gnome')

add_global_arguments('-D_GNU_SOURCE=1', language: 'c')

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

subdir('tests')
