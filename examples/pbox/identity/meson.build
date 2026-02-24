executable('identity_pbox_sandbox',
  'lib_identity.c',
  link_with: libpbox_sandbox,
  link_args: ['-rdynamic'],
  include_directories: pbox_inc,
  dependencies: [ffi_dep, dl_dep],
)

identity_pbox_host = executable('identity_pbox_host',
  'host.c',
  link_with: libpbox,
  include_directories: pbox_inc,
)

test('pbox_identity', identity_pbox_host, workdir: meson.current_build_dir())
