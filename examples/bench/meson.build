backend = get_option('sbox_backend')

# Shared library for passthrough backend
libbench = shared_library('bench',
  'lib_bench.c',
  install: false,
)

# Sandbox executable for process backend
bench_sandbox = executable('bench_sandbox',
  'lib_bench.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

if backend == 'process'
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [ffi_dep],
    install: false,
  )
else
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl')],
    install: false,
  )
endif
