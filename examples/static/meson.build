backend = get_option('sbox_backend')

# Static mode only works with passthrough (no dynamic loading needed)
if backend == 'passthrough'
  static_main = executable('static_main',
    'main.cc',
    'lib_static.c',
    include_directories: sbox_inc,
    install: false,
  )

  test('static', static_main)
endif
