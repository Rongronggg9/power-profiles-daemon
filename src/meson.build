deps = [ gio_dep, gudev_dep ]

resources = gnome.compile_resources(
    'power-profiles-daemon-resources', 'power-profiles-daemon.gresource.xml',
    c_name: 'power_profiles_daemon',
    source_dir: '.',
    export: true
)

sources = [
  'power-profiles-daemon.c',
  'ppd-action.c',
  'ppd-action-trickle-charge.c',
  'ppd-profile-driver.c',
  'ppd-profile-driver-lenovo-dytc.c',
  'ppd-profile-driver-balanced.c',
  'ppd-profile-driver-power-saver.c',
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
