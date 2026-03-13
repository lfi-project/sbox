backend = get_option('sbox_backend')

thread_dep = dependency('threads')

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
  dependencies: [dl_dep],
  install: false,
)

if backend == 'process'
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [thread_dep],
    install: false,
  )
elif backend == 'lfi'
  lib_bench_lfi = custom_target('lib_bench.lfi',
    input: 'lib_bench.c',
    output: 'lib_bench.lfi',
    build_by_default: true,
    command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
  )
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: [sbox_inc, lfi_inc],
    cpp_args: ['-DSBOX_LFI'],
    dependencies: [lfi_linux.as_link_whole(), thread_dep],
    install: false,
  )
else
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl'), thread_dep],
    install: false,
  )
endif
