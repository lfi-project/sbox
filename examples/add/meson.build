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
  link_with: libpbox_sandbox,
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
