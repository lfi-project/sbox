shm_pbox = executable('shm_pbox',
  'lib_shm.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
)

shm_pbox_main = executable('shm_pbox_main',
  'main.c',
  link_with: libpbox,
  include_directories: pbox_inc,
)

test('pbox_shm', shm_pbox_main, workdir: meson.current_build_dir())
