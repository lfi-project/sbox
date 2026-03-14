project('sbox', 'cpp', 'c',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=release',
  ]
)

# Header-only library
sbox_inc = include_directories('include')

# Test libraries (shared objects for passthrough)
testlib = shared_library('testlib',
  'test/testlib.c',
  install: false,
)

testlib2 = shared_library('testlib2',
  'test/testlib2.c',
  install: false,
)

# Test include directory (for test_helpers.hh)
test_inc = include_directories('test')

# Dynamic function call library
dyfn_inc = include_directories('src/dyfn')

if host_machine.cpu_family() == 'x86_64'
  dyfn_asm = files('src/dyfn/dyfn_x86_64.S')
elif host_machine.cpu_family() == 'aarch64'
  dyfn_asm = files('src/dyfn/dyfn_aarch64.S')
else
  error('Unsupported architecture for dyfn')
endif

dyfn_sources = [files('src/dyfn/dyfn.c', 'src/dyfn/dyfn_offsets_check.c'), dyfn_asm]

# Process backend (pbox)
dl_dep = meson.get_compiler('c').find_library('dl')

pbox_inc = include_directories('src/pbox')

libpbox = library('pbox',
  'src/pbox/pbox.c',
  'src/pbox/pbox_procmaps.c',
  include_directories: pbox_inc,
  install: false,
)

pbox_sandbox_sources = files('src/pbox/pbox_sandbox.c', 'src/pbox/pbox_seccomp.c')

libpbox_sandbox = static_library('pbox_sandbox',
  pbox_sandbox_sources,
  dyfn_sources,
  include_directories: [pbox_inc, dyfn_inc],
  dependencies: [dl_dep],
  install: false,
)

libpbox_sandbox_nocb = static_library('pbox_sandbox_nocb',
  pbox_sandbox_sources,
  dyfn_sources,
  include_directories: [pbox_inc, dyfn_inc],
  c_args: ['-DSBOX_NO_CALLBACKS'],
  dependencies: [dl_dep],
  install: false,
)

# Test sandbox executables (run in child process)
test_sandbox = executable('test_sandbox',
  'test/testlib.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [dl_dep],
  install: false,
)

test_sandbox2 = executable('test_sandbox2',
  'test/testlib2.c',
  link_with: libpbox_sandbox_nocb,
  link_args: ['-rdynamic'],
  dependencies: [dl_dep],
  install: false,
)

# Shared test categories (each has a .inc.cc included by per-backend drivers)
test_categories = ['arithmetic', 'fn_handles', 'misc', 'structs', 'pointers', 'memory', 'strings', 'callbacks']

dl_dep = dependency('dl')

foreach cat : test_categories
  exe = executable('test_passthrough_' + cat,
    'test/test_passthrough_' + cat + '.cc',
    include_directories: [sbox_inc, test_inc],
    dependencies: [dl_dep],
    install: false,
  )
  test('passthrough_' + cat, exe,
    workdir: meson.current_build_dir(),
    depends: [testlib],
    protocol: 'tap',
  )
endforeach

foreach cat : test_categories
  exe = executable('test_process_' + cat,
    'test/test_process_' + cat + '.cc',
    include_directories: [sbox_inc, pbox_inc, test_inc],
    link_with: libpbox,
    install: false,
  )
  test('process_' + cat, exe,
    workdir: meson.current_build_dir(),
    depends: [test_sandbox],
    protocol: 'tap',
  )
endforeach

# Passthrough-specific tests
passthrough_extra_tests = ['multi', 'reentrant']

foreach t : passthrough_extra_tests
  exe = executable('test_passthrough_' + t,
    'test/test_passthrough_' + t + '.cc',
    include_directories: [sbox_inc, test_inc],
    dependencies: [dl_dep],
    install: false,
  )
  test('passthrough_' + t, exe,
    workdir: meson.current_build_dir(),
    depends: [testlib],
    protocol: 'tap',
  )
endforeach

# Process-specific tests
process_extra_tests = ['specific', 'multi']

foreach t : process_extra_tests
  exe = executable('test_process_' + t,
    'test/test_process_' + t + '.cc',
    include_directories: [sbox_inc, pbox_inc, test_inc],
    link_with: libpbox,
    install: false,
  )
  test('process_' + t, exe,
    workdir: meson.current_build_dir(),
    depends: [test_sandbox],
    protocol: 'tap',
  )
endforeach

# Multi-sandbox tests
test_passthrough_multi_sandbox = executable('test_passthrough_multi_sandbox',
  'test/test_passthrough_multi_sandbox.cc',
  include_directories: [sbox_inc, test_inc],
  dependencies: [dl_dep],
  install: false,
)
test('passthrough_multi_sandbox', test_passthrough_multi_sandbox,
  workdir: meson.current_build_dir(),
  depends: [testlib, testlib2],
  protocol: 'tap',
)

test_process_multi_sandbox = executable('test_process_multi_sandbox',
  'test/test_process_multi_sandbox.cc',
  include_directories: [sbox_inc, pbox_inc, test_inc],
  link_with: libpbox,
  install: false,
)
test('process_multi_sandbox', test_process_multi_sandbox,
  workdir: meson.current_build_dir(),
  depends: [test_sandbox, test_sandbox2],
  protocol: 'tap',
)

# Threading test
thread_dep = dependency('threads')
test_threads = executable('test_threads',
  'test/test_threads.cc',
  include_directories: [sbox_inc, test_inc],
  dependencies: [dl_dep, thread_dep],
  install: false,
)
test('threads', test_threads,
  workdir: meson.current_build_dir(),
  depends: [testlib],
)

# LFI backend
lfi_subproj = subproject('lfi-runtime', default_options: ['enable_linux=true'])
lfi_linux = lfi_subproj.get_variable('lfi_linux')
lfi_inc = include_directories('subprojects/lfi-runtime/linux/include',
                               'subprojects/lfi-runtime/core/include')

libsbox_lfi = static_library('sbox_lfi',
  'src/sbox_lfi.cc',
  include_directories: [sbox_inc, lfi_inc],
  dependencies: [lfi_linux.as_link_whole()],
  install: false,
)

lficc_name = host_machine.cpu_family() == 'aarch64' ? 'aarch64_lfi-linux-musl-clang' : 'x86_64_lfi-linux-musl-clang'
lficc = find_program(lficc_name, required: false)
if lficc.found()
  testlib_lfi = custom_target('testlib.lfi',
    input: 'test/testlib.c',
    output: 'testlib.lfi',
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )

  foreach cat : test_categories
    exe = executable('test_lfi_' + cat,
      'test/test_lfi_' + cat + '.cc',
      include_directories: [sbox_inc, lfi_inc, test_inc],
      link_with: libsbox_lfi,
      install: false,
    )
    test('lfi_' + cat, exe,
      workdir: meson.current_build_dir(),
      depends: [testlib_lfi],
      protocol: 'tap',
    )
  endforeach

  testlib2_lfi = custom_target('testlib2.lfi',
    input: 'test/testlib2.c',
    output: 'testlib2.lfi',
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )

  test_lfi_multi_sandbox = executable('test_lfi_multi_sandbox',
    'test/test_lfi_multi_sandbox.cc',
    include_directories: [sbox_inc, lfi_inc, test_inc],
    link_with: libsbox_lfi,
    install: false,
  )
  test('lfi_multi_sandbox', test_lfi_multi_sandbox,
    workdir: meson.current_build_dir(),
    depends: [testlib_lfi, testlib2_lfi],
    protocol: 'tap',
  )
endif

# Compile-failure tests (verify that type errors are caught at compile time)
compile_fail_script = find_program('test/test_compile_fail.sh')
cxx_cmd = meson.get_compiler('cpp').cmd_array()

compile_fail_tests = [
  'wrong_callback_type',
  'wrong_data_ptr_type',
  'raw_pointer_arg',
  'raw_callback_arg',
]

foreach t : compile_fail_tests
  test('compile_fail_' + t, compile_fail_script,
    args: cxx_cmd + [
      '-std=c++17',
      '-I' + meson.current_source_dir() / 'include',
      '-c',
      meson.current_source_dir() / 'test' / 'compile_fail' / t + '.cc',
      '-o', '/dev/null',
    ],
  )
endforeach

# Examples
subdir('examples/add')
subdir('examples/callback')
subdir('examples/bench')
subdir('examples/shm')
subdir('examples/threads')
subdir('examples/inout')
subdir('examples/static')

# Pbox examples
subdir('examples/pbox/add')
subdir('examples/pbox/bench')
subdir('examples/pbox/blocked')
subdir('examples/pbox/callback')
subdir('examples/pbox/identity')
subdir('examples/pbox/shm')
subdir('examples/pbox/threads')
