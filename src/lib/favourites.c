/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * SPEC section 8. The stored list is the truth and is never filtered on the
 * way back: an ID whose application is missing is simply not shown. Positions
 * given to this module count what is shown, so they are translated into the
 * stored list before anything is written.
 */

#include "config.h"

#include "favourites.h"

#define FAVOURITES_KEY "favourite-apps"

struct _MockaFavourites
{
  GObject parent_instance;

  MockaMenuData *data; /* not owned */
  GSettings *settings; /* not owned */
  gchar **ids;         /* the stored list, as read */
  GPtrArray *apps;     /* the installed ones, in stored order */
  GHashTable *id_set;  /* every stored ID */
};

G_DEFINE_TYPE (MockaFavourites, mocka_favourites, G_TYPE_OBJECT)

enum
{
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static MockaMenuApp *
find_app (MockaFavourites *self, const gchar *id)
{
  GPtrArray *all = mocka_menu_data_get_all_apps (self->data);

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);

      if (g_strcmp0 (mocka_menu_app_get_id (app), id) == 0)
        {
          return app;
        }
    }

  return NULL;
}

/* Reads the setting and works out which of its IDs can be shown. */
static void
reload (MockaFavourites *self)
{
  /* The set borrows its keys from the array, so it is emptied first. */
  g_hash_table_remove_all (self->id_set);
  g_ptr_array_set_size (self->apps, 0);
  g_strfreev (self->ids);

  self->ids = g_settings_get_strv (self->settings, FAVOURITES_KEY);

  for (gsize i = 0; self->ids[i] != NULL; i++)
    {
      MockaMenuApp *app = find_app (self, self->ids[i]);

      g_hash_table_add (self->id_set, self->ids[i]);

      /* Not installed: kept in the list, left out of the column. */
      if (app != NULL)
        {
          g_ptr_array_add (self->apps, g_object_ref (app));
        }
    }
}

static void
on_settings_changed (MockaFavourites *self)
{
  reload (self);
  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
write_ids (MockaFavourites *self, GPtrArray *ids)
{
  g_ptr_array_add (ids, NULL);
  g_settings_set_strv (self->settings, FAVOURITES_KEY, (const gchar *const *)ids->pdata);
  g_ptr_array_unref (ids);
}

/* The stored list, as a fresh array that can be rearranged. */
static GPtrArray *
stored_copy (MockaFavourites *self)
{
  GPtrArray *ids = g_ptr_array_new_with_free_func (g_free);

  for (gsize i = 0; self->ids != NULL && self->ids[i] != NULL; i++)
    {
      g_ptr_array_add (ids, g_strdup (self->ids[i]));
    }

  return ids;
}

/*
 * Where a position among the shown favourites falls in the stored list. The
 * two differ whenever an ID in between is not installed.
 */
static guint
stored_index_for (MockaFavourites *self, gint position)
{
  gint seen = 0;

  if (position < 0)
    {
      return self->ids != NULL ? g_strv_length (self->ids) : 0;
    }

  for (gsize i = 0; self->ids != NULL && self->ids[i] != NULL; i++)
    {
      if (seen == position)
        {
          return (guint)i;
        }

      if (find_app (self, self->ids[i]) != NULL)
        {
          seen++;
        }
    }

  return self->ids != NULL ? g_strv_length (self->ids) : 0;
}

static void
mocka_favourites_finalize (GObject *object)
{
  MockaFavourites *self = MOCKA_FAVOURITES (object);

  g_strfreev (self->ids);
  g_clear_pointer (&self->apps, g_ptr_array_unref);
  g_clear_pointer (&self->id_set, g_hash_table_unref);

  G_OBJECT_CLASS (mocka_favourites_parent_class)->finalize (object);
}

static void
mocka_favourites_class_init (MockaFavouritesClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = mocka_favourites_finalize;

  signals[SIGNAL_CHANGED]
      = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
mocka_favourites_init (MockaFavourites *self)
{
  self->apps = g_ptr_array_new_with_free_func (g_object_unref);
  self->id_set = g_hash_table_new (g_str_hash, g_str_equal);
}

MockaFavourites *
mocka_favourites_new (MockaMenuData *data, GSettings *settings)
{
  MockaFavourites *self;

  g_return_val_if_fail (MOCKA_IS_MENU_DATA (data), NULL);
  g_return_val_if_fail (G_IS_SETTINGS (settings), NULL);

  self = g_object_new (MOCKA_TYPE_FAVOURITES, NULL);
  self->data = data;
  self->settings = settings;

  reload (self);

  /*
   * Changes made by anything else show at once (SPEC section 8). The settings
   * are shared and outlive this, so the handler has to go when this does,
   * which connect_object does for us.
   */
  g_signal_connect_object (settings, "changed::" FAVOURITES_KEY, G_CALLBACK (on_settings_changed), self,
                           G_CONNECT_SWAPPED);

  return self;
}

GPtrArray *
mocka_favourites_get_apps (MockaFavourites *self)
{
  g_return_val_if_fail (MOCKA_IS_FAVOURITES (self), NULL);
  return self->apps;
}

GHashTable *
mocka_favourites_get_ids (MockaFavourites *self)
{
  g_return_val_if_fail (MOCKA_IS_FAVOURITES (self), NULL);
  return self->id_set;
}

gboolean
mocka_favourites_contains (MockaFavourites *self, const gchar *id)
{
  g_return_val_if_fail (MOCKA_IS_FAVOURITES (self), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  return g_hash_table_contains (self->id_set, id);
}

void
mocka_favourites_add (MockaFavourites *self, const gchar *id, gint position)
{
  GPtrArray *ids;

  g_return_if_fail (MOCKA_IS_FAVOURITES (self));
  g_return_if_fail (id != NULL);

  if (mocka_favourites_contains (self, id))
    {
      return;
    }

  ids = stored_copy (self);
  g_ptr_array_insert (ids, (gint)stored_index_for (self, position), g_strdup (id));
  write_ids (self, ids);
}

void
mocka_favourites_remove (MockaFavourites *self, const gchar *id)
{
  GPtrArray *ids;

  g_return_if_fail (MOCKA_IS_FAVOURITES (self));
  g_return_if_fail (id != NULL);

  /* Every copy of it: the stored list can hold the same ID twice, since
   * anything may write the setting. */
  ids = stored_copy (self);
  for (guint i = ids->len; i > 0; i--)
    {
      if (g_strcmp0 (g_ptr_array_index (ids, i - 1), id) == 0)
        {
          g_ptr_array_remove_index (ids, i - 1);
        }
    }

  write_ids (self, ids);
}

void
mocka_favourites_move (MockaFavourites *self, const gchar *id, gint position)
{
  GPtrArray *ids;
  guint target;

  g_return_if_fail (MOCKA_IS_FAVOURITES (self));
  g_return_if_fail (id != NULL);

  if (!mocka_favourites_contains (self, id))
    {
      return;
    }

  /* Worked out before the move, while the stored list still matches. */
  target = stored_index_for (self, position);

  ids = stored_copy (self);
  for (guint i = 0; i < ids->len; i++)
    {
      if (g_strcmp0 (g_ptr_array_index (ids, i), id) == 0)
        {
          g_ptr_array_remove_index (ids, i);
          if (target > i)
            {
              target--;
            }
          break;
        }
    }

  g_ptr_array_insert (ids, (gint)MIN (target, ids->len), g_strdup (id));
  write_ids (self, ids);
}