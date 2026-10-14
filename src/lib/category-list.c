/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "app-item.h"
#include "category-list.h"

#define CATEGORY_ICON_SIZE 16
#define ROW_SPACING 8
#define ROW_PADDING 4

/* The settings application the Settings shortcut opens (SPEC section 6). */
#define SETTINGS_DESKTOP_ID "matecc.desktop"

/* The Preferences icon. preferences-system is Administration's. */
#define SETTINGS_ICON_NAME "preferences-desktop"

struct _MockaCategoryList
{
  GtkBox parent_instance;

  MockaMenuData *data; /* not owned */
  GtkWidget *scroller;
  GtkWidget *list;
  GtkWidget *settings_list; /* the Settings shortcut, never a category */

  gint saved_selection; /* the row to put back when a search is cleared */
  gboolean rollover;
};

G_DEFINE_TYPE (MockaCategoryList, mocka_category_list, GTK_TYPE_BOX)

enum
{
  SIGNAL_CATEGORY_SELECTED,
  SIGNAL_APP_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
destroy_row (GtkWidget *widget, gpointer user_data)
{
  gtk_widget_destroy (widget);
}

/* icon is borrowed: the image takes its own reference. */
static GtkWidget *
make_row (const gchar *name, GIcon *icon)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROW_SPACING);
  GtkWidget *label = gtk_label_new (name);

  if (icon != NULL)
    {
      GIcon *drawn = mocka_icon_to_draw (icon, "folder");
      GtkWidget *image = gtk_image_new_from_gicon (drawn, GTK_ICON_SIZE_MENU);

      g_object_unref (drawn);
      gtk_image_set_pixel_size (GTK_IMAGE (image), CATEGORY_ICON_SIZE);
      gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
    }

  gtk_label_set_xalign (GTK_LABEL (label), 0.0F);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (box), ROW_PADDING);
  gtk_container_add (GTK_CONTAINER (row), box);

  return row;
}

static MockaMenuApp *
find_settings_app (MockaCategoryList *self)
{
  GPtrArray *all = mocka_menu_data_get_all_apps (self->data);

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);

      if (g_strcmp0 (mocka_menu_app_get_id (app), SETTINGS_DESKTOP_ID) == 0)
        {
          return app;
        }
    }

  return NULL;
}

GPtrArray *
mocka_category_list_get_apps (MockaCategoryList *self)
{
  GtkListBoxRow *row;
  MockaMenuCategory *category;

  g_return_val_if_fail (MOCKA_IS_CATEGORY_LIST (self), NULL);

  row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->list));
  category = row != NULL ? g_object_get_data (G_OBJECT (row), "category") : NULL;

  if (category != NULL)
    {
      return mocka_menu_category_get_apps (category);
    }

  return mocka_menu_data_get_all_apps (self->data);
}

static void
on_selected (GtkListBox *list, GtkListBoxRow *row, MockaCategoryList *self)
{
  if (row == NULL)
    {
      return;
    }

  g_signal_emit (self, signals[SIGNAL_CATEGORY_SELECTED], 0);
}

static void
on_settings_activated (GtkListBox *list, GtkListBoxRow *row, MockaCategoryList *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "settings-app");

  if (app != NULL)
    {
      g_signal_emit (self, signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

/* The pointer is inside our own window here, so the coordinates are the
 * list's own. */
static gboolean
on_motion (GtkWidget *widget, GdkEventMotion *event, MockaCategoryList *self)
{
  GtkListBoxRow *row;

  if (!self->rollover)
    {
      return GDK_EVENT_PROPAGATE;
    }

  row = gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->list), (gint)event->y);

  /* The Settings shortcut is not selectable, so hovering it selects nothing. */
  if (row != NULL && gtk_list_box_row_get_selectable (row))
    {
      gtk_list_box_select_row (GTK_LIST_BOX (self->list), row);
    }

  return GDK_EVENT_PROPAGATE;
}

void
mocka_category_list_set_rollover (MockaCategoryList *self, gboolean rollover)
{
  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));
  self->rollover = rollover;
}

void
mocka_category_list_set_searching (MockaCategoryList *self, gboolean searching)
{
  GtkListBoxRow *row;

  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));

  gtk_widget_set_sensitive (self->scroller, !searching);

  if (searching && self->saved_selection < 0)
    {
      row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->list));
      self->saved_selection = row != NULL ? gtk_list_box_row_get_index (row) : 0;
      gtk_list_box_unselect_all (GTK_LIST_BOX (self->list));
    }
  else if (!searching && self->saved_selection >= 0)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list), self->saved_selection);
      if (row != NULL)
        {
          gtk_list_box_select_row (GTK_LIST_BOX (self->list), row);
        }
      self->saved_selection = -1;
    }
}

gboolean
mocka_category_list_is_sensitive (MockaCategoryList *self)
{
  g_return_val_if_fail (MOCKA_IS_CATEGORY_LIST (self), FALSE);
  return gtk_widget_get_sensitive (self->scroller);
}

/* Focus on a GtkListBox does nothing: the rows hold it. */
gboolean
mocka_category_list_grab_focus_on_row (MockaCategoryList *self)
{
  GtkListBoxRow *row;

  g_return_val_if_fail (MOCKA_IS_CATEGORY_LIST (self), FALSE);

  if (!gtk_widget_get_sensitive (self->scroller))
    {
      return FALSE;
    }

  row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->list));
  if (row == NULL)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list), 0);
      if (row == NULL)
        {
          return FALSE;
        }
      gtk_list_box_select_row (GTK_LIST_BOX (self->list), row);
    }

  gtk_widget_grab_focus (GTK_WIDGET (row));

  return TRUE;
}

void
mocka_category_list_reset (MockaCategoryList *self, gboolean take_focus)
{
  GtkListBoxRow *first;
  GtkAdjustment *adjustment;

  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));

  gtk_widget_set_sensitive (self->scroller, TRUE);
  self->saved_selection = -1;

  first = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list), 0);
  if (first != NULL)
    {
      gtk_list_box_select_row (GTK_LIST_BOX (self->list), first);
      if (take_focus)
        {
          gtk_widget_grab_focus (GTK_WIDGET (first));
        }
    }

  adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroller));
  gtk_adjustment_set_value (adjustment, gtk_adjustment_get_lower (adjustment));
}

void
mocka_category_list_rebuild (MockaCategoryList *self)
{
  GPtrArray *categories;
  GtkWidget *row;
  MockaMenuApp *settings;

  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));

  gtk_container_foreach (GTK_CONTAINER (self->list), destroy_row, NULL);

  /* "All" first, then the categories in menu order (SPEC section 6). */
  row = make_row (_ ("All"), NULL);
  gtk_container_add (GTK_CONTAINER (self->list), row);

  categories = mocka_menu_data_get_categories (self->data);
  for (guint i = 0; i < categories->len; i++)
    {
      MockaMenuCategory *category = g_ptr_array_index (categories, i);

      row = make_row (mocka_menu_category_get_name (category), mocka_menu_category_get_icon (category));
      g_object_set_data_full (G_OBJECT (row), "category", g_object_ref (category), g_object_unref);
      gtk_container_add (GTK_CONTAINER (self->list), row);
    }

  /*
   * The Settings shortcut sits under the categories but is not one of them,
   * so it lives in its own list (SPEC section 6). That also keeps it usable
   * while a search greys the categories out.
   */
  gtk_container_foreach (GTK_CONTAINER (self->settings_list), destroy_row, NULL);

  settings = find_settings_app (self);
  if (settings != NULL)
    {
      const gchar *comment = mocka_menu_app_get_comment (settings);
      GIcon *settings_icon = g_themed_icon_new (SETTINGS_ICON_NAME);

      row = make_row (_ ("Settings"), settings_icon);
      g_object_unref (settings_icon);
      gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
      g_object_set_data_full (G_OBJECT (row), "settings-app", g_object_ref (settings), g_object_unref);
      if (comment != NULL && *comment != '\0')
        {
          gtk_widget_set_tooltip_text (row, comment);
        }
      gtk_container_add (GTK_CONTAINER (self->settings_list), row);
    }

  gtk_widget_set_visible (self->settings_list, settings != NULL);
  gtk_widget_show_all (self->list);
  if (settings != NULL)
    {
      gtk_widget_show_all (self->settings_list);
    }

  mocka_category_list_reset (self, FALSE);
}

static void
mocka_category_list_init (MockaCategoryList *self)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
  self->saved_selection = -1;

  self->list = gtk_list_box_new ();
  /* Single rather than browse, so the selection can be dropped while a search
   * is showing. Browse always keeps one. */
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->list), GTK_SELECTION_SINGLE);

  self->scroller = gtk_scrolled_window_new (NULL, NULL);
  /* A scrollbar only when it does not fit (SPEC section 6). */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  /*
   * The categories ask for their whole width and height, so the column is as
   * wide as the longest name and tall enough to show every category.
   */
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
  gtk_container_add (GTK_CONTAINER (self->scroller), self->list);

  self->settings_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->settings_list), GTK_SELECTION_NONE);

  gtk_box_pack_start (GTK_BOX (self), self->scroller, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (self), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self), self->settings_list, FALSE, FALSE, 0);

  gtk_widget_add_events (self->list, GDK_POINTER_MOTION_MASK);
  g_signal_connect (self->list, "motion-notify-event", G_CALLBACK (on_motion), self);
  g_signal_connect (self->list, "row-selected", G_CALLBACK (on_selected), self);
  g_signal_connect (self->settings_list, "row-activated", G_CALLBACK (on_settings_activated), self);
}

static void
mocka_category_list_class_init (MockaCategoryListClass *klass)
{
  signals[SIGNAL_CATEGORY_SELECTED] = g_signal_new ("category-selected", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                                    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_APP_ACTIVATED] = g_signal_new ("app-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                                NULL, NULL, G_TYPE_NONE, 1, MOCKA_TYPE_MENU_APP);
}

GtkWidget *
mocka_category_list_new (MockaMenuData *data)
{
  MockaCategoryList *self;

  g_return_val_if_fail (MOCKA_IS_MENU_DATA (data), NULL);

  self = g_object_new (MOCKA_TYPE_CATEGORY_LIST, NULL);
  self->data = data;
  mocka_category_list_rebuild (self);

  return GTK_WIDGET (self);
}