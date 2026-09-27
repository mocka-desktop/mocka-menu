mocka_menu_headers = files(
  'mocka-menu.h',
)

mocka_menu_lib = shared_library('mocka-menu',
  'mocka-menu.c',
  include_directories: config_inc,
  dependencies: [gio_dep, gtk_dep],
  version: meson.project_version(),
  soversion: '0',
  install: true,
)

install_headers(mocka_menu_headers, subdir: 'mocka-menu')

mocka_menu_dep = declare_dependency(
  link_with: mocka_menu_lib,
  include_directories: include_directories('.'),
  dependencies: [gio_dep, gtk_dep],
)

# So Mocka Dock's menu button can link against the menu (SPEC section 13).
pkgconfig.generate(mocka_menu_lib,
  name: 'mocka-menu',
  description: 'Mocka Menu without the MATE panel applet glue',
  subdirs: 'mocka-menu',
  requires: ['gio-unix-2.0', 'gtk+-3.0'],
)
