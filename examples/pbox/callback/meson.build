callback_pbox_host = executable('callback_pbox_host',
  'host.c',
  include_directories: pbox_inc,
  link_with: libpbox,
)

executable('callback_pbox_sandbox',
  'lib_callback.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  dependencies: [ffi_dep, dl_dep],
)

test('pbox_callback', callback_pbox_host, workdir: meson.current_build_dir())
