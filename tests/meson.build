envs = environment()
envs.set('PPD_TEST_VERBOSE', 'true')
envs.set ('top_builddir', meson.project_build_root())
envs.set ('top_srcdir', meson.project_source_root())

unittest_inspector = find_program('unittest_inspector.py')
integration_tests = files('integration_test.py')
r = run_command(python3, unittest_inspector, integration_tests, check: true)
unit_tests = r.stdout().strip().split('\n')

valgrind = find_program('valgrind', required: false)
if valgrind.found()
    glib_share = glib_dep.get_variable('prefix') / 'share' / glib_dep.name()
    glib_suppressions = glib_share + '/valgrind/glib.supp'
    libfprint_wrapper = [
        valgrind.full_path(),
        '--tool=memcheck',
        '--leak-check=full',
        '--leak-resolution=high',
        '--error-exitcode=1',
        '--errors-for-leak-kinds=definite',
        '--track-origins=yes',
        '--show-leak-kinds=definite,possible',
        '--show-error-list=yes',
        '--gen-suppressions=all',
        '--suppressions=' + glib_suppressions,
    ]
    add_test_setup('valgrind',
        timeout_multiplier: 5,
        env: [
            'G_SLICE=always-malloc',
            'UNDER_VALGRIND=1',
            'PPD_TEST_WRAPPER=' + ' '.join(libfprint_wrapper),
        ])
endif

preloaded_libs = []
ppd_tests_ld_preload = []

if address_sanitizer
    # ASAN has to be the first in list
    preloaded_libs += 'asan'
endif

foreach libname: preloaded_libs
    lib = run_command(meson.get_compiler('c'),
        '-print-file-name=lib@0@.so'.format(libname)
    ).stdout().strip()

    # Support linker script files
    if run_command('grep', '-qI', '^INPUT', files(lib)).returncode() == 0
        out = run_command('cat', lib).stdout()
        lib = out.split('(')[1].split(')')[0].strip()
    endif

    if lib != '' and lib[0] == '/'
        message('Found library @0@ as @1@'.format(libname, lib))
        ppd_tests_ld_preload += '@0@'.format(files(lib)[0])
    else
        tests = []
        warning('No library found for ' + libname + ', skipping PAM tests')
    endif
endforeach

envs.set('PPD_LD_PRELOAD', ' '.join(ppd_tests_ld_preload))

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
