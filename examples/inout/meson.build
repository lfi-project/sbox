backend = get_option('sbox_backend')

# Shared library for passthrough backend
libinout = shared_library('inout',
  'lib_inout.c',
  install: false,
)

# Sandbox executable for process backend
inout_sandbox = executable('inout_sandbox',
  'lib_inout.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

if backend == 'process'
  inout_main = executable('inout_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [ffi_dep],
    install: false,
  )
  test('inout', inout_main,
    workdir: meson.current_build_dir(),
    depends: [inout_sandbox],
  )
else
  inout_main = executable('inout_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl')],
    install: false,
  )
  test('inout', inout_main,
    workdir: meson.current_build_dir(),
    depends: [libinout],
  )
endif
