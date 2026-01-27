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

# Process backend (requires pbox subproject)
pbox_subproj = subproject('pbox')
libpbox = pbox_subproj.get_variable('libpbox')
libpbox_sandbox = pbox_subproj.get_variable('libpbox_sandbox')
pbox_inc = pbox_subproj.get_variable('inc')
ffi_dep = pbox_subproj.get_variable('ffi_dep')
dl_dep = pbox_subproj.get_variable('dl_dep')

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

# LFI backend
lfi_subproj = subproject('lfi-runtime', default_options: ['enable_linux=true'])
lfi_linux = lfi_subproj.get_variable('lfi_linux')
lfi_inc = include_directories('subprojects/lfi-runtime/linux/include',
                               'subprojects/lfi-runtime/core/include')

# LFI test (only if lficc compiler is available)
lficc_name = target_machine.cpu_family() == 'aarch64' ? 'aarch64_lfi-linux-musl-clang' : 'x86_64_lfi-linux-musl-clang'
lficc = find_program(lficc_name, required: false)
if lficc.found()
  testlib_lfi = custom_target('testlib.lfi',
    input: 'test/testlib.c',
    output: 'testlib.lfi',
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )

  test_lfi = executable('test_lfi',
    'test/test_lfi.cc',
    include_directories: [sbox_inc, lfi_inc],
    dependencies: [lfi_linux.as_link_whole()],
    install: false,
  )

  test('lfi', test_lfi,
    workdir: meson.current_build_dir(),
    depends: [testlib_lfi],
  )
endif

# Examples
subdir('examples/add')
subdir('examples/callback')
subdir('examples/bench')
subdir('examples/shm')
subdir('examples/threads')
subdir('examples/inout')
subdir('examples/static')
