threads_pbox = executable('threads_pbox',
  'lib_threads.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
)

thread_dep = dependency('threads')

threads_pbox_main = executable('threads_pbox_main',
  'main.c',
  link_with: libpbox,
  include_directories: pbox_inc,
  dependencies: [thread_dep],
)

test('pbox_threads', threads_pbox_main, workdir: meson.current_build_dir())
