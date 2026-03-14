backend = get_option('sbox_backend')

add_inc = include_directories('.')

# Shared library for passthrough backend
libadd = shared_library('add',
  'lib_add.c',
  install: false,
)

# Sandbox executable for process backend
add_sandbox = executable('add_sandbox',
  'lib_add.c',
  link_with: libpbox_sandbox_nocb,
  link_args: ['-rdynamic'],
  dependencies: [dl_dep],
  install: false,
)

if backend == 'process'
  add_main = executable('add_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc, add_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    install: false,
  )
  test('add', add_main,
    workdir: meson.current_build_dir(),
    depends: [add_sandbox],
  )
elif backend == 'lfi'
  lib_add_lfi = custom_target('lib_add.lfi',
    input: 'lib_add.c',
    output: 'lib_add.lfi',
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )
  add_main = executable('add_main',
    'main.cc',
    include_directories: [sbox_inc, lfi_inc, add_inc],
    cpp_args: ['-DSBOX_LFI'],
    link_with: libsbox_lfi,
    install: false,
  )
  test('add', add_main,
    workdir: meson.current_build_dir(),
    depends: [lib_add_lfi],
  )
else
  add_main = executable('add_main',
    'main.cc',
    include_directories: [sbox_inc, add_inc],
    dependencies: [dependency('dl')],
    install: false,
  )
  test('add', add_main,
    workdir: meson.current_build_dir(),
    depends: [libadd],
  )
endif
