backend = get_option('sbox_backend')

# Shared library for passthrough backend
libcallback = shared_library('callback',
  'lib_callback.c',
  install: false,
)

# Sandbox executable for process backend
callback_sandbox = executable('callback_sandbox',
  'lib_callback.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

if backend == 'process'
  callback_main = executable('callback_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [ffi_dep],
    install: false,
  )
  test('callback', callback_main,
    workdir: meson.current_build_dir(),
    depends: [callback_sandbox],
  )
elif backend == 'lfi'
  if lficc.found()
    libcallback_lfi = custom_target('libcallback.lfi',
      input: 'lib_callback.c',
      output: 'libcallback.lfi',
      command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
    )
    callback_main = executable('callback_main',
      'main.cc',
      include_directories: [sbox_inc, lfi_inc],
      cpp_args: ['-DSBOX_LFI'],
      dependencies: [lfi_linux.as_link_whole()],
      install: false,
    )
    test('callback', callback_main,
      workdir: meson.current_build_dir(),
      depends: [libcallback_lfi],
    )
  endif
else
  callback_main = executable('callback_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl')],
    install: false,
  )
  test('callback', callback_main,
    workdir: meson.current_build_dir(),
    depends: [libcallback],
  )
endif
