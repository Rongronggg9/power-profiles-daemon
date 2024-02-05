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
