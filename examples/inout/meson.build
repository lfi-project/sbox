backend = get_option('sbox_backend')

# Shared library for passthrough backend
libinout = shared_library('inout',
  'lib_inout.c',
  install: false,
)

# Sandbox executable for process backend
inout_sandbox = executable('inout_sandbox',
  'lib_inout.c',
  link_with: libpbox_sandbox_nocb,
  link_args: ['-rdynamic'],
  dependencies: [dl_dep],
  install: false,
)

if backend == 'process'
  inout_main = executable('inout_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    install: false,
  )
  test('inout', inout_main,
    workdir: meson.current_build_dir(),
    depends: [inout_sandbox],
  )
elif backend == 'lfi'
  lib_inout_lfi = custom_target('lib_inout.lfi',
    input: 'lib_inout.c',
    output: 'lib_inout.lfi',
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )
  inout_main = executable('inout_main',
    'main.cc',
    include_directories: [sbox_inc, lfi_inc],
    cpp_args: ['-DSBOX_LFI'],
    link_with: libsbox_lfi,
    install: false,
  )
  test('inout', inout_main,
    workdir: meson.current_build_dir(),
    depends: [lib_inout_lfi],
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
