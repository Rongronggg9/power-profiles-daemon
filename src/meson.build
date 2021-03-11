deps = [ gio_dep, gudev_dep, upower_dep ]

resources = gnome.compile_resources(
    'power-profiles-daemon-resources', 'power-profiles-daemon.gresource.xml',
    c_name: 'power_profiles_daemon',
    source_dir: '.',
    export: true
)

sources = [
  'ppd-profile.c',
  'ppd-utils.c',
  'ppd-action.c',
  'ppd-driver.c',
  resources,
]

enums = 'ppd-enums'
sources += gnome.mkenums(
  enums,
  sources: 'ppd-profile.h',
  c_template: enums + '.c.in',
  h_template: enums + '.h.in'
)

lib_libpower_profiles_daemon = shared_library(
  'libppd',
  sources,
  dependencies: deps,
  install: false
)

libpower_profiles_daemon_dep = declare_dependency(
  dependencies: deps,
  link_with: lib_libpower_profiles_daemon,
)

sources += [
  'power-profiles-daemon.c',
  'ppd-action-trickle-charge.c',
  'ppd-driver-intel-pstate.c',
  'ppd-driver-platform-profile.c',
  'ppd-driver-balanced.c',
  'ppd-driver-power-saver.c',
  'ppd-driver-fake.c',
]

executable('power-profiles-daemon',
  sources,
  dependencies: deps,
  install: true,
  install_dir: libexecdir
)

python = import('python')
py_installation = python.find_installation('python3', required: true)

ppd_conf = configuration_data()
ppd_conf.set('VERSION', meson.project_version())
ppd_conf.set('PYTHON3', py_installation.path())

configure_file(
  input: 'powerprofilesctl.in',
  output: 'powerprofilesctl',
  configuration: ppd_conf,
  install_dir: get_option('bindir')
)
