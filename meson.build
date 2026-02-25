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

# Test library (shared object for passthrough)
testlib = shared_library('testlib',
  'test/testlib.c',
  install: false,
)

# Test include directory (for test_helpers.hh)
test_inc = include_directories('test')

# Process backend (pbox)
libffi_subproj = subproject('libffi', default_options: ['tests=false', 'warning_level=0', 'default_library=static'])
ffi_dep = libffi_subproj.get_variable('ffi_dep')
dl_dep = meson.get_compiler('c').find_library('dl')

pbox_inc = include_directories('src/pbox')

libpbox = library('pbox',
  'src/pbox/pbox.c',
  'src/pbox/pbox_procmaps.c',
  include_directories: pbox_inc,
  dependencies: [ffi_dep],
  install: false,
)

libpbox_sandbox = static_library('pbox_sandbox',
  'src/pbox/pbox_sandbox.c',
  'src/pbox/pbox_seccomp.c',
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

# Test sandbox executable (runs in child process)
test_sandbox = executable('test_sandbox',
  'test/testlib.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
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
  )
endforeach

foreach cat : test_categories
  exe = executable('test_process_' + cat,
    'test/test_process_' + cat + '.cc',
    include_directories: [sbox_inc, pbox_inc, test_inc],
    link_with: libpbox,
    dependencies: [ffi_dep],
    install: false,
  )
  test('process_' + cat, exe,
    workdir: meson.current_build_dir(),
    depends: [test_sandbox],
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
  )
endforeach

# Process-specific tests
process_extra_tests = ['specific', 'multi']

foreach t : process_extra_tests
  exe = executable('test_process_' + t,
    'test/test_process_' + t + '.cc',
    include_directories: [sbox_inc, pbox_inc, test_inc],
    link_with: libpbox,
    dependencies: [ffi_dep],
    install: false,
  )
  test('process_' + t, exe,
    workdir: meson.current_build_dir(),
    depends: [test_sandbox],
  )
endforeach

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
