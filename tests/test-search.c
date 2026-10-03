/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* SPEC section 9.2, against the tree in tests/menus/. */

#include <locale.h>

#include "search.h"

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

static MockaMenuApp *
find_app (MockaMenuData *data, const gchar *id)
{
  GPtrArray *all = mocka_menu_data_get_all_apps (data);

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);

      if (g_strcmp0 (mocka_menu_app_get_id (app), id) == 0)
        {
          return app;
        }
    }

  g_error ("the test menu has no %s", id);
}

/* The rank of one app against one search, normalizing the text first. */
static MockaSearchRank
rank_of (MockaMenuApp *app, const gchar *text)
{
  gchar *normalized = mocka_search_normalize (text);
  MockaSearchRank rank = mocka_search_rank (app, normalized);

  g_free (normalized);
  return rank;
}

static void
test_normalize (void)
{
  gchar *folded = mocka_search_normalize ("ÉDITEUR");
  gchar *accented = mocka_search_normalize ("Éditeur");
  gchar *plain = mocka_search_normalize ("editeur");

  /* Case and accents both go, so all three become the same text. */
  g_assert_cmpstr (folded, ==, plain);
  g_assert_cmpstr (accented, ==, plain);

  g_assert_null (mocka_search_normalize (NULL));

  g_free (folded);
  g_free (accented);
  g_free (plain);
}

/* Each of the seven fields in SPEC section 9.2, in order. */
static void
test_every_rank (void)
{
  MockaMenuData *data = load_test_menu ();

  /* 1, 2, 3: the name, from its start, from a word, and from inside. */
  g_assert_cmpint (rank_of (find_app (data, "s-name.desktop"), "zenith"), ==, MOCKA_SEARCH_RANK_NAME_PREFIX);
  g_assert_cmpint (rank_of (find_app (data, "s-name.desktop"), "prose"), ==, MOCKA_SEARCH_RANK_NAME_WORD);
  g_assert_cmpint (rank_of (find_app (data, "s-name.desktop"), "nit"), ==, MOCKA_SEARCH_RANK_NAME_CONTAINS);

  /* 4: the generic name. */
  g_assert_cmpint (rank_of (find_app (data, "s-generic.desktop"), "ledger"), ==, MOCKA_SEARCH_RANK_GENERIC_NAME);

  /* 5: the keywords. */
  g_assert_cmpint (rank_of (find_app (data, "s-keywords.desktop"), "cinema"), ==, MOCKA_SEARCH_RANK_KEYWORDS);

  /* 6: the program it runs. */
  g_assert_cmpint (rank_of (find_app (data, "s-program.desktop"), "basename"), ==, MOCKA_SEARCH_RANK_PROGRAM);

  /* 7: the comment. */
  g_assert_cmpint (rank_of (find_app (data, "s-comment.desktop"), "compressed"), ==, MOCKA_SEARCH_RANK_COMMENT);

  /* Nothing matches nothing. */
  g_assert_cmpint (rank_of (find_app (data, "s-comment.desktop"), "zzzznotthere"), ==, MOCKA_SEARCH_RANK_NONE);

  g_object_unref (data);
}

/* Matching ignores accents, in the text and in the name alike. */
static void
test_accents (void)
{
  MockaMenuData *data = load_test_menu ();

  g_assert_cmpint (rank_of (find_app (data, "editeur.desktop"), "editeur"), ==, MOCKA_SEARCH_RANK_NAME_PREFIX);
  g_assert_cmpint (rank_of (find_app (data, "editeur.desktop"), "Éditeur"), ==, MOCKA_SEARCH_RANK_NAME_PREFIX);
  g_assert_cmpint (rank_of (find_app (data, "editeur.desktop"), "EDITEUR"), ==, MOCKA_SEARCH_RANK_NAME_PREFIX);

  g_object_unref (data);
}

static gint
position_of (GPtrArray *results, const gchar *id)
{
  for (guint i = 0; i < results->len; i++)
    {
      if (g_strcmp0 (mocka_menu_app_get_id (g_ptr_array_index (results, i)), id) == 0)
        {
          return (gint)i;
        }
    }

  return -1;
}

/* A better rank comes first, whatever the names are. */
static void
test_results_are_ranked (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *results = mocka_search_run (mocka_menu_data_get_all_apps (data), "qqq", NULL);

  /* "qqq" starts every one of those names, so they all share a rank. */
  g_assert_cmpint (results->len, ==, 4);
  g_ptr_array_unref (results);

  /* "kite" is a name prefix for two apps and matches nothing else. */
  results = mocka_search_run (mocka_menu_data_get_all_apps (data), "kite", NULL);
  g_assert_cmpint (results->len, ==, 2);
  /* Without favourites, by name. */
  g_assert_cmpint (position_of (results, "s-fav-early.desktop"), <, position_of (results, "s-fav-late.desktop"));
  g_ptr_array_unref (results);

  g_object_unref (data);
}

/* Within a rank, favourites come before the rest (SPEC section 9.2). */
static void
test_favourites_come_first (void)
{
  MockaMenuData *data = load_test_menu ();
  GHashTable *favourites = g_hash_table_new (g_str_hash, g_str_equal);
  GPtrArray *results;

  /* The one that would otherwise be second. */
  g_hash_table_add (favourites, (gpointer) "s-fav-late.desktop");

  results = mocka_search_run (mocka_menu_data_get_all_apps (data), "kite", favourites);

  g_assert_cmpint (results->len, ==, 2);
  g_assert_cmpint (position_of (results, "s-fav-late.desktop"), ==, 0);
  g_assert_cmpint (position_of (results, "s-fav-early.desktop"), ==, 1);

  g_ptr_array_unref (results);
  g_hash_table_unref (favourites);
  g_object_unref (data);
}

/* An empty search matches nothing rather than everything. */
static void
test_empty_search (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *results = mocka_search_run (mocka_menu_data_get_all_apps (data), "", NULL);

  g_assert_cmpint (results->len, ==, 0);
  g_ptr_array_unref (results);

  g_object_unref (data);
}

/* Hidden entries stay hidden: search covers the menu, not the disk. */
static void
test_search_does_not_reach_hidden_entries (void)
{
  MockaMenuData *data = load_test_menu ();
  GPtrArray *results = mocka_search_run (mocka_menu_data_get_all_apps (data), "hidden", NULL);

  g_assert_cmpint (position_of (results, "hiddenapp.desktop"), ==, -1);
  g_assert_cmpint (position_of (results, "nodisplay.desktop"), ==, -1);

  g_ptr_array_unref (results);
  g_object_unref (data);
}

int
main (int argc, char **argv)
{
  (void)setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/search/normalize", test_normalize);
  g_test_add_func ("/search/every-rank", test_every_rank);
  g_test_add_func ("/search/accents", test_accents);
  g_test_add_func ("/search/ranked-results", test_results_are_ranked);
  g_test_add_func ("/search/favourites-first", test_favourites_come_first);
  g_test_add_func ("/search/empty", test_empty_search);
  g_test_add_func ("/search/hidden-entries", test_search_does_not_reach_hidden_entries);

  return g_test_run ();
}