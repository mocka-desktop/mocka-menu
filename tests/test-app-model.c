/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* The diff that keeps a showing list in step with a new one (SPEC section 14). */

#include <locale.h>

#include "app-model.h"

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
app_named (MockaMenuData *data, const gchar *id)
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

static GPtrArray *
list_of (MockaMenuData *data, const gchar *const *ids)
{
  GPtrArray *apps = g_ptr_array_new ();

  for (gsize i = 0; ids[i] != NULL; i++)
    {
      g_ptr_array_add (apps, app_named (data, ids[i]));
    }

  return apps;
}

/* Applies the steps to a copy of `from` and insists it becomes `to`. */
static guint
apply_and_check (GPtrArray *from, GPtrArray *to)
{
  GArray *steps = mocka_app_model_diff (from, to);
  GPtrArray *now = g_ptr_array_new ();

  for (guint i = 0; i < from->len; i++)
    {
      g_ptr_array_add (now, g_ptr_array_index (from, i));
    }

  for (guint i = 0; i < steps->len; i++)
    {
      MockaAppStep *step = &g_array_index (steps, MockaAppStep, i);

      g_assert_cmpuint (step->position, <=, now->len);

      if (step->insert)
        {
          g_ptr_array_insert (now, (gint)step->position, step->app);
        }
      else
        {
          g_assert_cmpuint (step->position, <, now->len);
          g_ptr_array_remove_index (now, step->position);
        }
    }

  g_assert_cmpuint (now->len, ==, to->len);
  for (guint i = 0; i < to->len; i++)
    {
      g_assert_cmpstr (mocka_menu_app_get_id (g_ptr_array_index (now, i)), ==,
                       mocka_menu_app_get_id (g_ptr_array_index (to, i)));
    }

  g_ptr_array_unref (now);

  return steps->len;
}

typedef struct
{
  MockaMenuData *data;
} Fixture;

static void
fixture_set_up (Fixture *fixture, gconstpointer data)
{
  fixture->data = load_test_menu ();
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer data)
{
  g_object_unref (fixture->data);
}

static void
check (Fixture *fixture, const gchar *const *before, const gchar *const *after, guint expected_steps)
{
  GPtrArray *from = list_of (fixture->data, before);
  GPtrArray *to = list_of (fixture->data, after);

  g_assert_cmpuint (apply_and_check (from, to), ==, expected_steps);

  g_ptr_array_unref (from);
  g_ptr_array_unref (to);
}

/* Nothing changed, so nothing moves and no widget is rebuilt. */
static void
test_same_list_has_no_steps (Fixture *fixture, gconstpointer data)
{
  const gchar *const ids[] = { "alpha.desktop", "beta.desktop", "dual.desktop", NULL };

  check (fixture, ids, ids, 0);
}

static void
test_one_app_installed (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { "alpha.desktop", "dual.desktop", NULL };
  const gchar *const after[] = { "alpha.desktop", "beta.desktop", "dual.desktop", NULL };

  check (fixture, before, after, 1);
}

static void
test_one_app_removed (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { "alpha.desktop", "beta.desktop", "dual.desktop", NULL };
  const gchar *const after[] = { "alpha.desktop", "dual.desktop", NULL };

  check (fixture, before, after, 1);
}

static void
test_first_and_last (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { "beta.desktop", NULL };
  const gchar *const after[] = { "alpha.desktop", "beta.desktop", "dual.desktop", NULL };

  check (fixture, before, after, 2);
}

static void
test_emptied (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { "alpha.desktop", "beta.desktop", NULL };
  const gchar *const after[] = { NULL };

  check (fixture, before, after, 2);
}

static void
test_filled_from_empty (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { NULL };
  const gchar *const after[] = { "alpha.desktop", "beta.desktop", NULL };

  check (fixture, before, after, 2);
}

/* Search results come in ranking order, so a change can reorder them. */
static void
test_reordered (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { "alpha.desktop", "beta.desktop", "dual.desktop", NULL };
  const gchar *const after[] = { "dual.desktop", "alpha.desktop", "beta.desktop", NULL };

  /* Correctness is what the check asserts; the step count only pins the
   * shape, one move being a removal and an insertion. */
  check (fixture, before, after, 2);
}

static void
test_replaced_entirely (Fixture *fixture, gconstpointer data)
{
  const gchar *const before[] = { "alpha.desktop", "beta.desktop", NULL };
  const gchar *const after[] = { "dual.desktop", "editeur.desktop", NULL };

  check (fixture, before, after, 4);
}

int
main (int argc, char **argv)
{
  (void)setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

#define ADD(path, func) g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)
  ADD ("/app-model/same-list-has-no-steps", test_same_list_has_no_steps);
  ADD ("/app-model/one-app-installed", test_one_app_installed);
  ADD ("/app-model/one-app-removed", test_one_app_removed);
  ADD ("/app-model/first-and-last", test_first_and_last);
  ADD ("/app-model/emptied", test_emptied);
  ADD ("/app-model/filled-from-empty", test_filled_from_empty);
  ADD ("/app-model/reordered", test_reordered);
  ADD ("/app-model/replaced-entirely", test_replaced_entirely);
#undef ADD

  return g_test_run ();
}