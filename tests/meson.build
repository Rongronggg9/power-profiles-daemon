integration_test = find_program('integration-test')

envs = environment()
envs.set ('top_builddir', meson.build_root())

test('power-profiles-daemon-integration-test',
  integration_test,
  env: envs
  )
