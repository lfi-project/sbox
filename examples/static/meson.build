backend = get_option('sbox_backend')

if backend != 'process'
  static_main = executable('static_main',
    'main.cc',
    'lib_static.c',
    include_directories: sbox_inc,
    install: false,
  )

  test('static', static_main)
endif
