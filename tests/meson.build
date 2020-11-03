integration_test = find_program('integration-test')

envs = environment()
envs.set ('top_builddir', meson.build_root())
envs.set ('top_srcdir', meson.source_root())

test('power-profiles-daemon-integration-test',
  integration_test,
  env: envs
  )

src_inc = include_directories('../src/')
executable('fake-proximity-switch',
  [ 'fake-proximity-switch.c' ],
  dependencies: [ gio_dep ],
  include_directories: [ src_inc ],
  install: false
  )
