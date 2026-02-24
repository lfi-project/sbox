bench_pbox = executable('bench_pbox',
  'lib_bench.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
)

thread_dep = dependency('threads')

executable('bench_pbox_main',
  'main.c',
  link_with: libpbox,
  include_directories: pbox_inc,
  dependencies: [thread_dep],
)
