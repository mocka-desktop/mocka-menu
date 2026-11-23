/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include "app-model.h"

static const gchar *
id_of (GPtrArray *apps, guint index)
{
  return mocka_menu_app_get_id (g_ptr_array_index (apps, index));
}

static void
add_step (GArray *steps, gboolean insert, guint position, MockaMenuApp *app)
{
  MockaAppStep step = { insert, position, app };

  g_array_append_val (steps, step);
}

/* Where an app ends up in the new list, or -1 when it is not in it. */
static GHashTable *
places_in (GPtrArray *apps)
{
  GHashTable *places = g_hash_table_new (g_str_hash, g_str_equal);

  for (guint i = 0; i < apps->len; i++)
    {
      g_hash_table_insert (places, (gpointer)id_of (apps, i), GINT_TO_POINTER ((gint)i + 1));
    }

  return places;
}

static gint
place_of (GHashTable *places, const gchar *id)
{
  gpointer found = g_hash_table_lookup (places, id);

  return found != NULL ? GPOINTER_TO_INT (found) - 1 : -1;
}

/*
 * Walks the two lists together, keeping a copy of the showing one as the
 * steps change it, so every position is the one the step will be applied at.
 */
GArray *
mocka_app_model_diff (GPtrArray *from, GPtrArray *to)
{
  GArray *steps = g_array_new (FALSE, FALSE, sizeof (MockaAppStep));
  GPtrArray *now = g_ptr_array_new ();
  GHashTable *places;
  guint at = 0;

  g_return_val_if_fail (from != NULL && to != NULL, steps);

  for (guint i = 0; i < from->len; i++)
    {
      g_ptr_array_add (now, g_ptr_array_index (from, i));
    }

  places = places_in (to);

  while (at < to->len)
    {
      if (at < now->len)
        {
          gint wanted = place_of (places, id_of (now, at));

          if (wanted == (gint)at)
            {
              at++;
              continue;
            }

          /* Gone from the new list, or wanted somewhere already passed. */
          if (wanted < 0 || wanted < (gint)at)
            {
              add_step (steps, FALSE, at, NULL);
              g_ptr_array_remove_index (now, at);
              continue;
            }
        }

      add_step (steps, TRUE, at, g_ptr_array_index (to, at));
      g_ptr_array_insert (now, (gint)at, g_ptr_array_index (to, at));
      at++;
    }

  while (now->len > to->len)
    {
      add_step (steps, FALSE, to->len, NULL);
      g_ptr_array_remove_index (now, to->len);
    }

  g_hash_table_unref (places);
  g_ptr_array_unref (now);

  return steps;
}