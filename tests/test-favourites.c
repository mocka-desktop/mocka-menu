/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* SPEC section 8, against the tree in tests/menus/ and settings in memory. */

#include <locale.h>

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>

#include "favourites.h"

#define MENU_SCHEMA "org.mocka_desktop.Menu"
#define MISSING_ID "zzz-not-installed.desktop"

static const gchar *const test_menus[] = { "mocka-test.menu", NULL };

static MockaMenuData *
load_test_menu (void)
{
  MockaMenuData *data = mocka_menu_data_new_for_menus (test_menus);
  GError *error = NULL;

  g_assert_true (mocka_menu_data_load (data, &error));
  g_assert_no_error (error);

  return data;
}

/* In memory, so the tests never touch the user's own settings. */
static GSettings *
isolated_settings (const gchar *const *ids)
{
  GSettingsBackend *backend = g_memory_settings_backend_new ();
  GSettings *settings = g_settings_new_with_backend (MENU_SCHEMA, backend);

  g_settings_set_strv (settings, "favourite-apps", ids);
  g_object_unref (backend);

  return settings;
}

static const gchar *
app_id_at (MockaFavourites *favourites, guint index)
{
  GPtrArray *apps = mocka_favourites_get_apps (favourites);

  g_assert_cmpuint (index, <, apps->len);
  return mocka_menu_app_get_id (g_ptr_array_index (apps, index));
}

static gchar **
stored_ids (GSettings *settings)
{
  return g_settings_get_strv (settings, "favourite-apps");
}

/* An entry that is not installed is left out, and left in the setting. */
static void
test_missing_is_skipped_and_kept (void)
{
  const gchar *const ids[] = { "alpha.desktop", MISSING_ID, "beta.desktop", NULL };
  MockaMenuData *data = load_test_menu ();
  GSettings *settings = isolated_settings (ids);
  MockaFavourites *favourites = mocka_favourites_new (data, settings);
  gchar **stored;

  g_assert_cmpuint (mocka_favourites_get_apps (favourites)->len, ==, 2);
  g_assert_cmpstr (app_id_at (favourites, 0), ==, "alpha.desktop");
  g_assert_cmpstr (app_id_at (favourites, 1), ==, "beta.desktop");

  /* Still a favourite, so it returns if its application is installed again. */
  g_assert_true (mocka_favourites_contains (favourites, MISSING_ID));

  stored = stored_ids (settings);
  g_assert_cmpuint (g_strv_length (stored), ==, 3);
  g_strfreev (stored);

  g_object_unref (favourites);
  g_object_unref (settings);
  g_object_unref (data);
}

static void
test_add_and_remove (void)
{
  const gchar *const ids[] = { "alpha.desktop", NULL };
  MockaMenuData *data = load_test_menu ();
  GSettings *settings = isolated_settings (ids);
  MockaFavourites *favourites = mocka_favourites_new (data, settings);

  mocka_favourites_add (favourites, "beta.desktop", -1);
  g_assert_cmpuint (mocka_favourites_get_apps (favourites)->len, ==, 2);
  g_assert_cmpstr (app_id_at (favourites, 1), ==, "beta.desktop");

  /* Adding one twice changes nothing. */
  mocka_favourites_add (favourites, "beta.desktop", -1);
  g_assert_cmpuint (mocka_favourites_get_apps (favourites)->len, ==, 2);

  mocka_favourites_remove (favourites, "alpha.desktop");
  g_assert_cmpuint (mocka_favourites_get_apps (favourites)->len, ==, 1);
  g_assert_cmpstr (app_id_at (favourites, 0), ==, "beta.desktop");

  g_object_unref (favourites);
  g_object_unref (settings);
  g_object_unref (data);
}

/*
 * Positions count what is shown, so an entry that is not installed sitting in
 * between must not shift where a new favourite lands.
 */
static void
test_position_skips_missing (void)
{
  const gchar *const ids[] = { "alpha.desktop", MISSING_ID, "beta.desktop", NULL };
  MockaMenuData *data = load_test_menu ();
  GSettings *settings = isolated_settings (ids);
  MockaFavourites *favourites = mocka_favourites_new (data, settings);
  gchar **stored;

  mocka_favourites_add (favourites, "dual.desktop", 1);

  g_assert_cmpstr (app_id_at (favourites, 0), ==, "alpha.desktop");
  g_assert_cmpstr (app_id_at (favourites, 1), ==, "dual.desktop");
  g_assert_cmpstr (app_id_at (favourites, 2), ==, "beta.desktop");

  /* The one that is not installed is still there, and still in order. */
  stored = stored_ids (settings);
  g_assert_cmpuint (g_strv_length (stored), ==, 4);
  g_assert_cmpstr (stored[0], ==, "alpha.desktop");
  g_assert_cmpstr (stored[1], ==, "dual.desktop");
  g_assert_cmpstr (stored[2], ==, MISSING_ID);
  g_assert_cmpstr (stored[3], ==, "beta.desktop");
  g_strfreev (stored);

  g_object_unref (favourites);
  g_object_unref (settings);
  g_object_unref (data);
}

static void
test_move (void)
{
  const gchar *const ids[] = { "alpha.desktop", "beta.desktop", "dual.desktop", NULL };
  MockaMenuData *data = load_test_menu ();
  GSettings *settings = isolated_settings (ids);
  MockaFavourites *favourites = mocka_favourites_new (data, settings);

  /* Last to first. */
  mocka_favourites_move (favourites, "dual.desktop", 0);
  g_assert_cmpstr (app_id_at (favourites, 0), ==, "dual.desktop");
  g_assert_cmpstr (app_id_at (favourites, 1), ==, "alpha.desktop");
  g_assert_cmpstr (app_id_at (favourites, 2), ==, "beta.desktop");

  /* First to last. */
  mocka_favourites_move (favourites, "dual.desktop", -1);
  g_assert_cmpstr (app_id_at (favourites, 2), ==, "dual.desktop");

  g_object_unref (favourites);
  g_object_unref (settings);
  g_object_unref (data);
}

static void
on_changed (MockaFavourites *favourites, gpointer data)
{
  guint *count = data;

  (*count)++;
}

/* A change made elsewhere shows at once (SPEC section 8). */
static void
test_changed_by_someone_else (void)
{
  const gchar *const ids[] = { "alpha.desktop", NULL };
  const gchar *const other[] = { "beta.desktop", "dual.desktop", NULL };
  MockaMenuData *data = load_test_menu ();
  GSettings *settings = isolated_settings (ids);
  MockaFavourites *favourites = mocka_favourites_new (data, settings);
  guint count = 0;

  g_signal_connect (favourites, "changed", G_CALLBACK (on_changed), &count);

  g_settings_set_strv (settings, "favourite-apps", other);

  g_assert_cmpuint (count, >, 0);
  g_assert_cmpuint (mocka_favourites_get_apps (favourites)->len, ==, 2);
  g_assert_cmpstr (app_id_at (favourites, 0), ==, "beta.desktop");

  g_object_unref (favourites);
  g_object_unref (settings);
  g_object_unref (data);
}

int
main (int argc, char **argv)
{
  (void)setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/favourites/missing-skipped-and-kept", test_missing_is_skipped_and_kept);
  g_test_add_func ("/favourites/add-and-remove", test_add_and_remove);
  g_test_add_func ("/favourites/position-skips-missing", test_position_skips_missing);
  g_test_add_func ("/favourites/move", test_move);
  g_test_add_func ("/favourites/changed-elsewhere", test_changed_by_someone_else);

  return g_test_run ();
}