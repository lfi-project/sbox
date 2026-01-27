backend = get_option('sbox_backend')

# Shared library for passthrough backend
libadd = shared_library('add',
  'lib_add.c',
  install: false,
)

# Sandbox executable for process backend
add_sandbox = executable('add_sandbox',
  'lib_add.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

if backend == 'process'
  add_main = executable('add_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [ffi_dep],
    install: false,
  )
  test('add', add_main,
    workdir: meson.current_build_dir(),
    depends: [add_sandbox],
  )
elif backend == 'lfi'
  if lficc.found()
    libadd_lfi = custom_target('libadd.lfi',
      input: 'lib_add.c',
      output: 'libadd.lfi',
      command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
    )
    add_main = executable('add_main',
      'main.cc',
      include_directories: [sbox_inc, lfi_inc],
      cpp_args: ['-DSBOX_LFI'],
      dependencies: [lfi_linux.as_link_whole()],
      install: false,
    )
    test('add', add_main,
      workdir: meson.current_build_dir(),
      depends: [libadd_lfi],
    )
  endif
else
  add_main = executable('add_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl')],
    install: false,
  )
  test('add', add_main,
    workdir: meson.current_build_dir(),
    depends: [libadd],
  )
endif
