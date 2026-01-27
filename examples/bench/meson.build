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
  dependencies: [ffi_dep, dl_dep],
  install: false,
)

if backend == 'process'
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: [sbox_inc, pbox_inc],
    link_with: libpbox,
    cpp_args: ['-DSBOX_PROCESS'],
    dependencies: [ffi_dep, thread_dep],
    install: false,
  )
elif backend == 'lfi'
  if lficc.found()
    libbench_lfi = custom_target('libbench.lfi',
      input: 'lib_bench.c',
      output: 'libbench.lfi',
      command: [lficc, '-static-pie', '-o', '@OUTPUT@', '@INPUT@', '-lboxrt', '-Wl,--export-dynamic', '-O2'],
      build_by_default: true,
    )
    bench_main = executable('bench_main',
      'main.cc',
      include_directories: [sbox_inc, lfi_inc],
      cpp_args: ['-DSBOX_LFI'],
      dependencies: [lfi_linux.as_link_whole(), thread_dep],
      install: false,
    )
  endif
else
  bench_main = executable('bench_main',
    'main.cc',
    include_directories: sbox_inc,
    dependencies: [dependency('dl'), thread_dep],
    install: false,
  )
endif
