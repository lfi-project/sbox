blocked_pbox = executable('blocked_pbox',
  'lib_blocked.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
)

blocked_pbox_main = executable('blocked_pbox_main',
  'main.c',
  link_with: libpbox,
  include_directories: pbox_inc,
)

test('pbox_blocked', blocked_pbox_main, workdir: meson.current_build_dir())
