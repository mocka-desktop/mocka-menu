/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * SPEC section 5, against the tree in tests/menus/. XDG_CONFIG_DIRS and
 * XDG_DATA_DIRS point there, so these tests never read the real menu.
 */

#include <locale.h>

#include "menu-data.h"

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

static MockaMenuCategory *
find_category (MockaMenuData *data, const gchar *id)
{
  GPtrArray *categories = mocka_menu_data_get_categories (data);

  for (guint i = 0; i < categories->len; i++)
    {
      MockaMenuCategory *category = g_ptr_array_index (categories, i);

      if (g_strcmp0 (mocka_menu_category_get_id (category), id) == 0)
        {
          return category;
        }
    }

  return NULL;
}

static gboolean
has_app (GPtrArray *apps, const gchar *id)
{
  for (guint i = 0; i < apps->len; i++)
    {
      if (g_strcmp0 (mocka_menu_app_get_id (g_ptr_array_index (apps, i)), id) == 0)
        {
          return TRUE;
        }
    }

  return FALSE;
}

static gint
index_of_name (GPtrArray *apps, const gchar *name)
{
  for (guint i = 0; i < apps->len; i++)
    {
      if (g_strcmp0 (mocka_menu_app_get_name (g_ptr_array_index (apps, i)), name) == 0)
        {
          return (gint)i;
        }
    }

  return -1;
}

static void
test_categories (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *categories = mocka_menu_data_get_categories (data);

  g_assert_cmpuint (categories->len, ==, 2);
  g_assert_nonnull (find_category (data, "Internet"));
  g_assert_nonnull (find_category (data, "Office"));
  g_assert_cmpstr (mocka_menu_category_get_name (find_category (data, "Internet")), ==, "Internet");

  g_object_unref (data);
}

/* Hidden entries and the wrong desktop are left out (SPEC section 5). */
static void
test_hidden_entries (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *all = mocka_menu_data_get_all_apps (data);

  g_assert_false (has_app (all, "nodisplay.desktop"));
  g_assert_false (has_app (all, "hiddenapp.desktop"));
  g_assert_false (has_app (all, "onlygnome.desktop"));
  g_assert_false (has_app (all, "notmate.desktop"));

  g_assert_true (has_app (all, "onlymate.desktop"));
  g_assert_true (has_app (all, "alpha.desktop"));

  g_object_unref (data);
}

/* An app in two categories is in both, and once in "All". */
static void
test_deduplication (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *all = mocka_menu_data_get_all_apps (data);
  guint seen = 0;

  g_assert_true (has_app (mocka_menu_category_get_apps (find_category (data, "Internet")), "dual.desktop"));
  g_assert_true (has_app (mocka_menu_category_get_apps (find_category (data, "Office")), "dual.desktop"));

  for (guint i = 0; i < all->len; i++)
    {
      if (g_strcmp0 (mocka_menu_app_get_id (g_ptr_array_index (all, i)), "dual.desktop") == 0)
        {
          seen++;
        }
    }
  g_assert_cmpuint (seen, ==, 1);

  g_object_unref (data);
}

/*
 * Sorted the way the language sorts, not the way the bytes fall. A byte sort
 * puts "apple sauce" after every capital letter and "Éditeur" after "zulu".
 */
static void
test_locale_sorting (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *office = mocka_menu_category_get_apps (find_category (data, "Office"));
  gint apple = index_of_name (office, "apple sauce");
  gint beta = index_of_name (office, "Beta Writer");
  gint edit = index_of_name (office, "Edit Text");
  gint editeur = index_of_name (office, "Éditeur");
  gint translated = index_of_name (office, "Translated App");

  g_assert_cmpint (apple, >=, 0);
  g_assert_cmpint (editeur, >=, 0);

  g_assert_cmpint (apple, <, beta);
  g_assert_cmpint (edit, <, editeur);
  g_assert_cmpint (editeur, <, translated);

  g_object_unref (data);
}

/*
 * Names and comments come from the user's language when the entry has one.
 * GLib settles the language list the first time it is asked, so this only
 * works in a process started in that language: meson runs the test a second
 * time with LC_ALL set to French.
 */
static void
test_translated_names (void)
{
  MockaMenuData *data;
  GPtrArray *all;
  gboolean found = FALSE;

  if (!g_strv_contains (g_get_language_names (), "fr"))
    {
      g_test_skip ("not running in French");
      return;
    }

  data = load_test_menu ();
  all = mocka_menu_data_get_all_apps (data);

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);

      if (g_strcmp0 (mocka_menu_app_get_id (app), "translated.desktop") == 0)
        {
          g_assert_cmpstr (mocka_menu_app_get_name (app), ==, "Application traduite");
          g_assert_cmpstr (mocka_menu_app_get_comment (app), ==, "A un nom français");
          found = TRUE;
        }
    }

  g_assert_true (found);
  g_object_unref (data);
}

/*
 * An entry's icon name is kept exactly as it gave it, with no generic name
 * added behind it.
 *
 * This is the regression test for a real bug. GTK searches theme by theme and
 * tries every name of an icon within each theme, so an icon carrying its own
 * name plus a generic one resolved to whichever the first theme had, which is
 * normally the generic one. Applications installed for a single user keep
 * their icons in hicolor, searched last, so every one of them drew a generic
 * icon. Choosing the generic icon is the drawing code's job, once it knows
 * what the theme actually has.
 */
static void
test_icon_name_is_untouched (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *all = mocka_menu_data_get_all_apps (data);
  gboolean checked = FALSE;

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);
      const gchar *const *names;

      if (g_strcmp0 (mocka_menu_app_get_id (app), "badicon.desktop") != 0)
        {
          continue;
        }

      names = g_themed_icon_get_names (G_THEMED_ICON (mocka_menu_app_get_icon (app)));

      g_assert_cmpstr (names[0], ==, "zzz-not-a-real-icon");
      g_assert_false (g_strv_contains ((const gchar *const *)names, "application-x-executable"));
      checked = TRUE;
    }

  g_assert_true (checked);
  g_object_unref (data);
}

/* An entry with no icon at all does get the generic one: no theme needed. */
static void
test_icon_when_absent (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *all = mocka_menu_data_get_all_apps (data);
  gboolean checked = FALSE;

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);
      GIcon *icon;

      if (g_strcmp0 (mocka_menu_app_get_id (app), "beta.desktop") != 0)
        {
          continue;
        }

      icon = mocka_menu_app_get_icon (app);
      g_assert_nonnull (icon);
      g_assert_true (G_IS_THEMED_ICON (icon));
      g_assert_true (g_strv_contains ((const gchar *const *)g_themed_icon_get_names (G_THEMED_ICON (icon)),
                                      "application-x-executable"));
      checked = TRUE;
    }

  g_assert_true (checked);
  g_object_unref (data);
}

int
main (int argc, char **argv)
{
  /* Take the language from the environment: meson sets it per test run. */
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/menu-data/categories", test_categories);
  g_test_add_func ("/menu-data/hidden-entries", test_hidden_entries);
  g_test_add_func ("/menu-data/deduplication", test_deduplication);
  g_test_add_func ("/menu-data/locale-sorting", test_locale_sorting);
  g_test_add_func ("/menu-data/translated-names", test_translated_names);
  g_test_add_func ("/menu-data/icon-name-untouched", test_icon_name_is_untouched);
  g_test_add_func ("/menu-data/icon-when-absent", test_icon_when_absent);

  return g_test_run ();
}
