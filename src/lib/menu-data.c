/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * SPEC section 5. libmate-menu does the reading, so the user's menu edits and
 * the freedesktop.org rules for hidden entries, OnlyShowIn and NotShowIn are
 * honoured by the tree itself rather than re-implemented here.
 *
 * On top of that this adds what the menu needs: one category per top level
 * directory, an app listed once per category however deep it sits, "All" with
 * every app exactly once, and sorting by the displayed name in the user's
 * locale.
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#define MATEMENU_I_KNOW_THIS_IS_UNSTABLE
#include <matemenu-tree.h>

#include "menu-data.h"

/* The menus the desktop ships. Settings tools live in the second one. */
static const gchar *const default_menus[] = {
  "mate-applications.menu",
  "mate-settings.menu",
  NULL,
};

struct _MockaMenuApp
{
  GObject parent_instance;

  gchar *id;
  gchar *name;
  gchar *comment;
  gchar *collate_key; /* sorting, built once per app */
  GIcon *icon;
  GDesktopAppInfo *app_info;
};

G_DEFINE_TYPE (MockaMenuApp, mocka_menu_app, G_TYPE_OBJECT)

static void
mocka_menu_app_finalize (GObject *object)
{
  MockaMenuApp *self = MOCKA_MENU_APP (object);

  g_free (self->id);
  g_free (self->name);
  g_free (self->comment);
  g_free (self->collate_key);
  g_clear_object (&self->icon);
  g_clear_object (&self->app_info);

  G_OBJECT_CLASS (mocka_menu_app_parent_class)->finalize (object);
}

static void
mocka_menu_app_class_init (MockaMenuAppClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = mocka_menu_app_finalize;
}

static void
mocka_menu_app_init (MockaMenuApp *self)
{
}

/*
 * The icon to show. A name is left exactly as the entry gave it: GTK looks
 * for icons theme by theme, trying every name of an icon within each theme
 * before moving on, so adding a generic name here would let a theme that has
 * the generic one win over hicolor, where many application icons live. The
 * generic icon is chosen when it is drawn instead, once the theme is known.
 *
 * Only what can be judged without a theme is settled here: no icon at all,
 * and an entry pointing at an icon file that is no longer there.
 */
static GIcon *
icon_or_generic (GIcon *icon, const gchar *generic)
{
  if (icon == NULL)
    {
      return g_themed_icon_new (generic);
    }

  if (G_IS_FILE_ICON (icon))
    {
      GFile *file = g_file_icon_get_file (G_FILE_ICON (icon));

      if (file == NULL || !g_file_query_exists (file, NULL))
        {
          return g_themed_icon_new (generic);
        }
    }

  return g_object_ref (icon);
}

static MockaMenuApp *
mocka_menu_app_new (const gchar *entry_id, GDesktopAppInfo *info)
{
  MockaMenuApp *self = g_object_new (MOCKA_TYPE_MENU_APP, NULL);
  GAppInfo *base = G_APP_INFO (info);
  GIcon *icon;

  self->id = g_strdup (entry_id);
  self->app_info = g_object_ref (info);
  self->name = g_strdup (g_app_info_get_display_name (base));
  self->comment = g_strdup (g_app_info_get_description (base));

  icon = g_app_info_get_icon (base);
  self->icon = icon_or_generic (icon, "application-x-executable");

  /* Collation keys sort the way the user's language expects, unlike the
   * bytes of the name. */
  self->collate_key = self->name != NULL ? g_utf8_collate_key_for_filename (self->name, -1) : g_strdup ("");

  return self;
}

const gchar *
mocka_menu_app_get_id (MockaMenuApp *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_APP (self), NULL);
  return self->id;
}

const gchar *
mocka_menu_app_get_name (MockaMenuApp *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_APP (self), NULL);
  return self->name;
}

const gchar *
mocka_menu_app_get_comment (MockaMenuApp *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_APP (self), NULL);
  return self->comment;
}

GIcon *
mocka_menu_app_get_icon (MockaMenuApp *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_APP (self), NULL);
  return self->icon;
}

GDesktopAppInfo *
mocka_menu_app_get_app_info (MockaMenuApp *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_APP (self), NULL);
  return self->app_info;
}

struct _MockaMenuCategory
{
  GObject parent_instance;

  gchar *id;
  gchar *name;
  GIcon *icon;
  GPtrArray *apps;     /* MockaMenuApp* */
  GHashTable *app_ids; /* the ids already in apps */
};

G_DEFINE_TYPE (MockaMenuCategory, mocka_menu_category, G_TYPE_OBJECT)

static void
mocka_menu_category_finalize (GObject *object)
{
  MockaMenuCategory *self = MOCKA_MENU_CATEGORY (object);

  g_free (self->id);
  g_free (self->name);
  g_clear_object (&self->icon);
  g_clear_pointer (&self->apps, g_ptr_array_unref);
  g_clear_pointer (&self->app_ids, g_hash_table_unref);

  G_OBJECT_CLASS (mocka_menu_category_parent_class)->finalize (object);
}

static void
mocka_menu_category_class_init (MockaMenuCategoryClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = mocka_menu_category_finalize;
}

static void
mocka_menu_category_init (MockaMenuCategory *self)
{
  self->apps = g_ptr_array_new_with_free_func (g_object_unref);
  self->app_ids = g_hash_table_new (g_str_hash, g_str_equal);
}

const gchar *
mocka_menu_category_get_id (MockaMenuCategory *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_CATEGORY (self), NULL);
  return self->id;
}

const gchar *
mocka_menu_category_get_name (MockaMenuCategory *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_CATEGORY (self), NULL);
  return self->name;
}

GIcon *
mocka_menu_category_get_icon (MockaMenuCategory *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_CATEGORY (self), NULL);
  return self->icon;
}

GPtrArray *
mocka_menu_category_get_apps (MockaMenuCategory *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_CATEGORY (self), NULL);
  return self->apps;
}

struct _MockaMenuData
{
  GObject parent_instance;

  gchar **menus;           /* menu file basenames to load */
  GPtrArray *categories;   /* MockaMenuCategory*, menu order */
  GPtrArray *all_apps;     /* MockaMenuApp*, every app once */
  GHashTable *all_app_ids; /* id -> MockaMenuApp*, so an app is made once */
};

G_DEFINE_TYPE (MockaMenuData, mocka_menu_data, G_TYPE_OBJECT)

static void
mocka_menu_data_finalize (GObject *object)
{
  MockaMenuData *self = MOCKA_MENU_DATA (object);

  g_strfreev (self->menus);
  g_clear_pointer (&self->categories, g_ptr_array_unref);
  g_clear_pointer (&self->all_apps, g_ptr_array_unref);
  g_clear_pointer (&self->all_app_ids, g_hash_table_unref);

  G_OBJECT_CLASS (mocka_menu_data_parent_class)->finalize (object);
}

static void
mocka_menu_data_class_init (MockaMenuDataClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = mocka_menu_data_finalize;
}

static void
mocka_menu_data_init (MockaMenuData *self)
{
  self->categories = g_ptr_array_new_with_free_func (g_object_unref);
  self->all_apps = g_ptr_array_new_with_free_func (g_object_unref);
  self->all_app_ids = g_hash_table_new (g_str_hash, g_str_equal);
}

MockaMenuData *
mocka_menu_data_new (void)
{
  return mocka_menu_data_new_for_menus (default_menus);
}

MockaMenuData *
mocka_menu_data_new_for_menus (const gchar *const *basenames)
{
  MockaMenuData *self = g_object_new (MOCKA_TYPE_MENU_DATA, NULL);

  self->menus = g_strdupv ((gchar **)basenames);
  return self;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) GCompareFunc fixes this signature
static gint
compare_by_name (gconstpointer left, gconstpointer right)
{
  MockaMenuApp *first = *(MockaMenuApp **)left;
  MockaMenuApp *second = *(MockaMenuApp **)right;

  return g_strcmp0 (first->collate_key, second->collate_key);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

/*
 * The app for this id, made once and shared, so the same app in several
 * categories is one object and "All" lists it once (SPEC section 5).
 */
static MockaMenuApp *
intern_app (MockaMenuData *self, MateMenuTreeEntry *entry)
{
  GDesktopAppInfo *info = matemenu_tree_entry_get_app_info (entry);
  const gchar *entry_id = matemenu_tree_entry_get_desktop_file_id (entry);
  MockaMenuApp *app;

  if (info == NULL || entry_id == NULL)
    {
      return NULL;
    }

  app = g_hash_table_lookup (self->all_app_ids, entry_id);
  if (app != NULL)
    {
      return app;
    }

  app = mocka_menu_app_new (entry_id, info);
  g_ptr_array_add (self->all_apps, app);
  g_hash_table_insert (self->all_app_ids, (gpointer)mocka_menu_app_get_id (app), app);

  return app;
}

/* Every entry under a directory, however deep, lands in its top category. */
static void
collect_entries (MockaMenuData *self, MockaMenuCategory *category, MateMenuTreeDirectory *directory)
{
  MateMenuTreeIter *iter = matemenu_tree_directory_iter (directory);
  MateMenuTreeItemType type;

  while ((type = matemenu_tree_iter_next (iter)) != MATEMENU_TREE_ITEM_INVALID)
    {
      if (type == MATEMENU_TREE_ITEM_ENTRY)
        {
          MateMenuTreeEntry *entry = matemenu_tree_iter_get_entry (iter);
          MockaMenuApp *app = intern_app (self, entry);

          if (app != NULL && !g_hash_table_contains (category->app_ids, mocka_menu_app_get_id (app)))
            {
              g_hash_table_add (category->app_ids, (gpointer)mocka_menu_app_get_id (app));
              g_ptr_array_add (category->apps, g_object_ref (app));
            }

          matemenu_tree_item_unref (entry);
        }
      else if (type == MATEMENU_TREE_ITEM_DIRECTORY)
        {
          MateMenuTreeDirectory *sub = matemenu_tree_iter_get_directory (iter);

          collect_entries (self, category, sub);
          matemenu_tree_item_unref (sub);
        }
    }

  matemenu_tree_iter_unref (iter);
}

static MockaMenuCategory *
category_for_directory (MateMenuTreeDirectory *directory)
{
  MockaMenuCategory *self = g_object_new (MOCKA_TYPE_MENU_CATEGORY, NULL);
  GIcon *icon = matemenu_tree_directory_get_icon (directory);

  self->id = g_strdup (matemenu_tree_directory_get_menu_id (directory));
  self->name = g_strdup (matemenu_tree_directory_get_name (directory));
  self->icon = icon_or_generic (icon, "folder");

  return self;
}

static gboolean
load_one_menu (MockaMenuData *self, const gchar *basename, GError **error)
{
  MateMenuTree *tree;
  MateMenuTreeDirectory *root;
  MateMenuTreeIter *iter;
  MateMenuTreeItemType type;

  tree = matemenu_tree_new (basename, MATEMENU_TREE_FLAGS_NONE);
  if (tree == NULL)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "could not open the menu %s", basename);
      return FALSE;
    }

  if (!matemenu_tree_load_sync (tree, error))
    {
      g_object_unref (tree);
      return FALSE;
    }

  root = matemenu_tree_get_root_directory (tree);
  if (root == NULL)
    {
      /* A menu with nothing in it is not an error: nothing is installed. */
      g_object_unref (tree);
      return TRUE;
    }

  iter = matemenu_tree_directory_iter (root);
  while ((type = matemenu_tree_iter_next (iter)) != MATEMENU_TREE_ITEM_INVALID)
    {
      if (type == MATEMENU_TREE_ITEM_DIRECTORY)
        {
          MateMenuTreeDirectory *dir = matemenu_tree_iter_get_directory (iter);
          MockaMenuCategory *category = category_for_directory (dir);

          collect_entries (self, category, dir);

          /* An empty category is not shown (SPEC section 6). */
          if (category->apps->len > 0)
            {
              g_ptr_array_sort (category->apps, compare_by_name);
              g_ptr_array_add (self->categories, category);
            }
          else
            {
              g_object_unref (category);
            }

          matemenu_tree_item_unref (dir);
        }
      else if (type == MATEMENU_TREE_ITEM_ENTRY)
        {
          /* An app outside every category still belongs in "All". */
          MateMenuTreeEntry *entry = matemenu_tree_iter_get_entry (iter);

          intern_app (self, entry);
          matemenu_tree_item_unref (entry);
        }
    }
  matemenu_tree_iter_unref (iter);

  matemenu_tree_item_unref (root);
  g_object_unref (tree);
  return TRUE;
}

gboolean
mocka_menu_data_load (MockaMenuData *self, GError **error)
{
  g_return_val_if_fail (MOCKA_IS_MENU_DATA (self), FALSE);

  g_ptr_array_set_size (self->categories, 0);
  g_ptr_array_set_size (self->all_apps, 0);
  g_hash_table_remove_all (self->all_app_ids);

  for (gsize i = 0; self->menus != NULL && self->menus[i] != NULL; i++)
    {
      if (!load_one_menu (self, self->menus[i], error))
        {
          return FALSE;
        }
    }

  g_ptr_array_sort (self->all_apps, compare_by_name);
  return TRUE;
}

GPtrArray *
mocka_menu_data_get_categories (MockaMenuData *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_DATA (self), NULL);
  return self->categories;
}

GPtrArray *
mocka_menu_data_get_all_apps (MockaMenuData *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_DATA (self), NULL);
  return self->all_apps;
}
