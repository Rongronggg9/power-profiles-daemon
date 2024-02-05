envs = environment()
envs.set ('top_builddir', meson.build_root())
envs.set ('top_srcdir', meson.source_root())

unittest_inspector = find_program('unittest_inspector.py')
integration_tests = files('integration_test.py')
r = run_command(python3, unittest_inspector, integration_tests, check: true)
unit_tests = r.stdout().strip().split('\n')

foreach ut: unit_tests
    test(ut,
         python3,
         args: [
            integration_tests,
            ut,
         ],
         env: envs,
        )
endforeach

if get_option('pylint')
  integration_pylint_flags = ['-d', 'W0511', '-d', 'C0302'] + pylint_flags
  test('pylint-integration-tests',
       pylint,
       args: integration_pylint_flags + integration_tests,
       env: nomalloc,
       )
endif
