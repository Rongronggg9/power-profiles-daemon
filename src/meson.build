deps = [ gio_dep, gudev_dep, upower_dep, polkit_gobject_dep ]

config_h = configuration_data()
config_h.set_quoted('VERSION', meson.project_version())
config_h.set('POLKIT_HAS_AUTOPOINTERS', polkit_gobject_dep.version().version_compare('>= 0.114'))
config_h_files = configure_file(
  output: 'config.h',
  configuration: config_h
)

dbus_xml = {}
dbus_xml_sources = []
resources_contents = []

foreach name, path: bus_names
  config = {
    'dbus_name': name,
    'dbus_iface': name,
    'dbus_path': path,
  }

  xml_source = configure_file(
    input: 'power-profiles-daemon.dbus.xml.in',
    output: name + '.xml',
    configuration: config,
  )
  dbus_xml_sources += xml_source
  dbus_xml += {name: xml_source}

  resources_contents += '<file preprocess="xml-stripblanks">@0@.xml</file>'.format(name)
endforeach

resources_xml = configure_file(
  input: 'power-profiles-daemon.gresource.xml.in',
  output: '@BASENAME@',
  configuration: {
    'prefix': bus_names['org.freedesktop.UPower.PowerProfiles'],
    'contents': '\n'.join(resources_contents),
  },
)

resources = gnome.compile_resources(
  'power-profiles-daemon-resources', resources_xml,
  c_name: 'power_profiles_daemon',
  dependencies: dbus_xml_sources,
  source_dir: [
    meson.current_source_dir(),
    meson.current_build_dir(),
  ],
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
  'ppd-action-amdgpu-panel-power.c',
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

script = 'powerprofilesctl'
install_data(script,
             install_dir: get_option('bindir')
)
script = join_paths(meson.current_source_dir(), script)
if get_option('pylint')
  test('pylint-powerprofilesctl',
       pylint,
       args: pylint_flags + [ script ],
       env: nomalloc,
       )
endif
