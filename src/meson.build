deps = [ gio_dep, gudev_dep, upower_dep, polkit_gobject_dep ]

config_h = configuration_data()
config_h.set_quoted('VERSION', meson.project_version())
config_h_files = configure_file(
  output: 'config.h',
  configuration: config_h
)

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
  'ppd-driver-cpu.c',
  'ppd-driver-platform.c',
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
  'ppd-driver-amd-pstate.c',
  'ppd-driver-platform-profile.c',
  'ppd-driver-placeholder.c',
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

script = configure_file(
  input: 'powerprofilesctl.in',
  output: 'powerprofilesctl',
  configuration: ppd_conf,
  install_dir: get_option('bindir')
)

if get_option('pylint')
  test('pylint-powerprofilesctl',
       pylint,
       args: pylint_flags + [ script ],
       env: nomalloc,
       )
endif
