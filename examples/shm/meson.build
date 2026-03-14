backend = get_option('sbox_backend')

# Shared library for passthrough backend
libshm = shared_library('shm',
  'lib_shm.c',
  install: false,
)

# Sandbox executable for process backend
shm_sandbox = executable('shm_sandbox',
  'lib_shm.c',
  link_with: libpbox_sandbox_nocb,
  link_args: ['-rdynamic'],
  dependencies: [dl_dep],
  install: false,
)

if backend == 'process'
  shm_main = executable('shm_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    install: false,
  )
  test('shm', shm_main,
    workdir: meson.current_build_dir(),
    depends: [shm_sandbox],
  )
elif backend == 'lfi'
  lib_shm_lfi = custom_target('lib_shm.lfi',
    input: 'lib_shm.c',
    output: 'lib_shm.lfi',
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )
  shm_main = executable('shm_main',
    'main.cc',
    include_directories: [sbox_inc, lfi_inc],
    cpp_args: ['-DSBOX_LFI'],
    link_with: libsbox_lfi,
    install: false,
  )
  test('shm', shm_main,
    workdir: meson.current_build_dir(),
    depends: [lib_shm_lfi],
  )
else
  shm_main = executable('shm_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl')],
    install: false,
  )
  test('shm', shm_main,
    workdir: meson.current_build_dir(),
    depends: [libshm],
  )
endif
