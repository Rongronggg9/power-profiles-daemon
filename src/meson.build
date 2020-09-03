deps = [ gio_dep, gudev_dep, upower_dep ]

resources = gnome.compile_resources(
    'power-profiles-daemon-resources', 'power-profiles-daemon.gresource.xml',
    c_name: 'power_profiles_daemon',
    source_dir: '.',
    export: true
)

sources = [
  'power-profiles-daemon.c',
  'ppd-profile.c',
  'ppd-utils.c',
  'ppd-action.c',
  'ppd-action-trickle-charge.c',
  'ppd-driver.c',
  'ppd-driver-intel-pstate.c',
  'ppd-driver-lenovo-dytc.c',
  'ppd-driver-balanced.c',
  'ppd-driver-power-saver.c',
  'ppd-driver-fake.c',
  resources,
]

enums = 'ppd-enums'
sources += gnome.mkenums(
  enums,
  sources: 'ppd-profile.h',
  c_template: enums + '.c.in',
  h_template: enums + '.h.in'
)

executable('power-profiles-daemon',
  sources,
  dependencies: deps,
  install: true,
  install_dir: libexecdir
)
