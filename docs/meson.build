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
  sources: meson.source_root() / 'src' / 'net.hadess.PowerProfiles.xml',
  interface_prefix: 'net.hadess',
  namespace: 'PowerProfiles',
  docbook: 'docs',
  build_by_default: true,
)

gnome.gtkdoc(
  meson.project_name(),
  main_xml: meson.project_name() + '-docs.xml',
  content_files: content_files,
  src_dir: [
    meson.source_root() /'src',
    meson.build_root() / 'src',
  ],
  ignore_headers: ['ppd-action.h', 'ppd-mode-driver.h'],
  scan_args: ['--rebuild-sections'],
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
