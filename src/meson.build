deps = [
  gio_dep,
  gio_unix_dep,
  gudev_dep,
  polkit_gobject_dep,
  upower_dep,
]

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

powerprofilesctl = files('powerprofilesctl')
install_data(powerprofilesctl,
             install_dir: get_option('bindir')
)
if pylint.found()
  test('pylint-powerprofilesctl',
       pylint,
       args: pylint_flags + [ powerprofilesctl ],
       env: nomalloc,
       )
endif

if argparse_manpage.found()
  argparse_features = run_command(argparse_manpage, '--help',
    check: true).stdout().strip()

  install_man(configure_file(
    command: [
      argparse_manpage,
      '--pyfile', powerprofilesctl,
      '--function', 'get_parser',
      argparse_features.contains('--author') ?
        ['--author', 'Bastien Nocera', '--author', 'Mario Limonciello'] : [],
      argparse_features.contains('--author-email') ?
        ['--author-email', 'hadess@hadess.net', '--author-email', 'mario.limonciello@amd.com'] : [],
      argparse_features.contains('--project-name') ?
        ['--project-name', meson.project_name()] : [],
      argparse_features.contains('--version') ?
        ['--version', meson.project_version()] : [],
      argparse_features.contains('--url') ?
        ['--url', 'https://gitlab.freedesktop.org/upower/power-profiles-daemon'] : [],
      argparse_features.contains('--description') ?
        ['--manual-title', 'Power Profiles Daemon Control Program'] : [],
      argparse_features.contains('--description') ?
        ['--description', 'Command line utility to control Power Profiles Daemon'] : [],
      argparse_features.contains('--format') ?
        ['--format', 'single-commands-section'] : [],
    ],
    capture: true,
    output: 'powerprofilesctl.1',
  ))
endif

generate_completion = [python3, powerprofilesctl, '--print-completion']
if bashcomp.found()
  completions_dir = bashcomp.get_variable(pkgconfig: 'completionsdir',
    pkgconfig_define: bashcomp.version().version_compare('>= 2.10') ?
      ['datadir', get_option('datadir')] : ['prefix', prefix],
  )

  custom_target('bash-completion',
    output: 'powerprofilesctl',
    command: [
      generate_completion,
      'bash',
    ],
    capture: true,
    install: true,
    install_dir: completions_dir,
  )
endif

if zshcomp
  custom_target('zsh-completion',
    output: '_powerprofilesctl',
    command: [
      generate_completion,
      'zsh',
    ],
    capture: true,
    install: true,
    install_dir: get_option('zshcomp'),
  )
endif
