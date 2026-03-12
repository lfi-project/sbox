backend = get_option('sbox_backend')
threads_dep = dependency('threads')

# Shared library for passthrough backend
libthreads = shared_library('threads',
  'lib_threads.c',
  install: false,
)

# Sandbox executable for process backend
threads_sandbox = executable('threads_sandbox',
  'lib_threads.c',
  link_with: libpbox_sandbox_nocb,
  link_args: ['-rdynamic'],
  dependencies: [dl_dep],
  install: false,
)

if backend == 'process'
  threads_main = executable('threads_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [threads_dep],
    install: false,
  )
  test('threads', threads_main,
    workdir: meson.current_build_dir(),
    depends: [threads_sandbox],
  )
else
  threads_main = executable('threads_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl'), threads_dep],
    install: false,
  )
  test('threads', threads_main,
    workdir: meson.current_build_dir(),
    depends: [libthreads],
  )
endif
