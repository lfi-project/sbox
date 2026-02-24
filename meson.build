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

# Passthrough test
test_passthrough = executable('test_passthrough',
  'test/test_passthrough.cc',
  include_directories: sbox_inc,
  dependencies: [dependency('dl')],
  install: false,
)

test('passthrough', test_passthrough,
  workdir: meson.current_build_dir(),
  depends: [testlib],
)

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

# Process test
test_process = executable('test_process',
  'test/test_process.cc',
  include_directories: [sbox_inc, pbox_inc],
  link_with: libpbox,
  dependencies: [ffi_dep],
  install: false,
)

test('process', test_process,
  workdir: meson.current_build_dir(),
  depends: [test_sandbox],
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
