project('mocka-menu', 'c',
  version: '0.0.1',
  license: 'BSD-3-Clause',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

gnome = import('gnome')
i18n = import('i18n')
pkgconfig = import('pkgconfig')

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
includedir = prefix / get_option('includedir')
libdir = prefix / get_option('libdir')
applet_libdir = libdir / 'mate-panel'
localedir = prefix / get_option('localedir')

gettext_package = meson.project_name()

# Clang is the only supported compiler (PLAN.md "Toolchain"). The flags still
# go through get_supported_arguments() so older Clang versions do not fail on
# one they do not know.
cc = meson.get_compiler('c')
add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
  ]),
  language: 'c',
)

gio_dep = dependency('gio-unix-2.0')
gtk_dep = dependency('gtk+-3.0', version: '>= 3.24.20')
applet_dep = dependency('libmatepanelapplet-4.0', version: '>= 1.26')

# Checked here so a missing dependency shows up at configure time, on this
# milestone, rather than on the one that starts linking against it.
mate_menu_dep = dependency('libmate-menu', version: '>= 1.26')  # M1
x11_dep = dependency('x11')                                     # M2
xi_dep = dependency('xi', version: '>= 1.5')                    # M2, XInput 2.1

conf = configuration_data()
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
conf.set_quoted('MATELOCALEDIR', localedir)
conf.set('ENABLE_NLS', 1)
configure_file(output: 'config.h', configuration: conf)

config_inc = include_directories('.')

subdir('data')
subdir('src' / 'lib')
subdir('src' / 'applet')
subdir('tests')
subdir('po')

gnome.post_install(glib_compile_schemas: true)
