backend = get_option('sbox_backend')

# Shared library for passthrough backend
libshm = shared_library('shm',
  'lib_shm.c',
  install: false,
)

# Sandbox executable for process backend
shm_sandbox = executable('shm_sandbox',
  'lib_shm.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

if backend == 'process'
  shm_main = executable('shm_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [ffi_dep],
    install: false,
  )
  test('shm', shm_main,
    workdir: meson.current_build_dir(),
    depends: [shm_sandbox],
  )
elif backend == 'lfi'
  if lficc.found()
    libshm_lfi = custom_target('libshm.lfi',
      input: 'lib_shm.c',
      output: 'libshm.lfi',
      command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
    )
    shm_main = executable('shm_main',
      'main.cc',
      include_directories: [sbox_inc, lfi_inc],
      cpp_args: ['-DSBOX_LFI'],
      dependencies: [lfi_linux.as_link_whole()],
      install: false,
    )
    test('shm', shm_main,
      workdir: meson.current_build_dir(),
      depends: [libshm_lfi],
    )
  endif
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
