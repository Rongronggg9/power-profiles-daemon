content_files = files()

version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())

content_files += configure_file(
  input: 'version.xml.in',
  output: 'version.xml',
  configuration: version_conf,
)

content_files += gnome.gdbus_codegen(
  meson.project_name(),
  sources: dbus_xml['org.freedesktop.UPower.PowerProfiles'],
  interface_prefix: 'org.freedesktop.UPower',
  namespace: 'PowerProfiles',
  docbook: 'docs',
  build_by_default: true,
)

private_headers = [
  'power-profiles-daemon.h',
  'ppd-action-trickle-charge.h',
  'ppd-action-amdgpu-panel-power.h',
  'ppd-driver-fake.h',
  'ppd-driver-intel-pstate.h',
  'ppd-driver-amd-pstate.h',
  'ppd-driver-placeholder.h',
  'ppd-driver-platform-profile.h',
  'ppd-utils.h',
  'power-profiles-daemon-resources.h',
]

gnome.gtkdoc(
  meson.project_name(),
  main_xml: meson.project_name() + '-docs.xml',
  content_files: content_files,
  dependencies: libpower_profiles_daemon_dep,
  gobject_typesfile: [join_paths(meson.current_source_dir(), 'power-profiles-daemon.types')],
  src_dir: [
    meson.project_source_root() /'src',
    meson.project_build_root() / 'src',
  ],
  ignore_headers: private_headers,
)

man1_dir = join_paths(get_option('prefix'), get_option('mandir'), 'man1')

xsltproc = find_program('xsltproc', required : true)
xsltproc_command = [
  xsltproc,
  '--nonet',
  '--stringparam', 'man.output.quietly', '1',
  '--stringparam', 'funcsynopsis.style', 'ansi',
  '--stringparam', 'man.th.extra1.suppress', '1',
  '--stringparam', 'man.authors.section.enabled', '0',
  '--stringparam', 'man.copyright.section.enabled', '0',
  '-o', '@OUTPUT@',
  'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
  '@INPUT@',
]
