# The tests load the tree in tests/menus/, never the real menu.
test_env = environment()
test_env.set('XDG_CONFIG_DIRS', meson.current_source_dir() / 'menus' / 'xdg')
test_env.set('XDG_DATA_DIRS', meson.current_source_dir() / 'menus' / 'share')
test_env.set('XDG_CURRENT_DESKTOP', 'MATE')
# XDG_DATA_DIRS points only at the fixture, so GSettings would find no schemas
# at all. Give it the system ones directly instead of widening XDG_DATA_DIRS,
# which would let the real menu leak into the tests.
# Ours first, then the system ones for everything else GLib wants.
test_env.set('GSETTINGS_SCHEMA_DIR',
  schemas_build_dir + ':' +
  gio_dep.get_variable(pkgconfig: 'prefix') / 'share' / 'glib-2.0' / 'schemas')

# A language with real collation rules, so sorting is not byte order.
test_env.set('LC_ALL', 'en_US.UTF-8')

test_menu_data = executable('test-menu-data',
  'test-menu-data.c',
  dependencies: [mocka_menu_dep, mate_menu_dep],
)
test('menu-data', test_menu_data, env: test_env)

# Translated names only show in a process started in that language.
test_env_fr = test_env
test_env_fr.set('LC_ALL', 'fr_FR.UTF-8')
test_env_fr.set('LANGUAGE', 'fr')
test('menu-data-french', test_menu_data,
  args: ['-p', '/menu-data/translated-names'],
  env: test_env_fr,
)

test_search = executable('test-search',
  'test-search.c',
  dependencies: [mocka_menu_dep, mate_menu_dep],
)
test('search', test_search, env: test_env)

# Geometry only, so it needs no panel, no display and no menu tree.
test_classic_placement = executable('test-classic-placement',
  'test-classic-placement.c',
  dependencies: [mocka_menu_dep],
)
test('classic-placement', test_classic_placement)

test_favourites = executable('test-favourites',
  'test-favourites.c',
  dependencies: [mocka_menu_dep, mate_menu_dep],
)
test('favourites', test_favourites, env: test_env)

# Writes into a temporary folder of its own, so it needs no menu tree. It does
# read one fixture entry, which g_test_build_filename finds through G_TEST_SRCDIR.
pins_env = environment()
pins_env.set('G_TEST_SRCDIR', meson.current_source_dir())

test_pins = executable('test-pins',
  'test-pins.c',
  dependencies: [mocka_menu_dep],
)
test('pins', test_pins, env: pins_env)
