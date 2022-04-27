envs = environment()
envs.set ('top_builddir', meson.build_root())
envs.set ('top_srcdir', meson.source_root())

python3 = find_program('python3')
unittest_inspector = find_program('unittest_inspector.py')
r = run_command(unittest_inspector, files('integration-test.py'), check: true)
unit_tests = r.stdout().strip().split('\n')

foreach ut: unit_tests
    ut_args = files('integration-test.py')
    ut_args += ut
    test(ut,
         python3,
         args: ut_args,
         env: envs,
        )
endforeach
