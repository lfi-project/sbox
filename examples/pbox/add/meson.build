add_pbox = executable('add_pbox',
  'lib_add.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
)

add_pbox_main = executable('add_pbox_main',
  'main.c',
  link_with: libpbox,
  include_directories: pbox_inc,
)

test('pbox_add', add_pbox_main, workdir: meson.current_build_dir())
