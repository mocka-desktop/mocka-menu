/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * The categories, down a column for Classic and along a row for the Launcher
 * (SPEC sections 6 and 7). A list box is vertical only, so the row is a flow
 * box instead, and the handful of places that differ go through the helpers
 * below.
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "app-item.h"
#include "category-list.h"

#define CATEGORY_ICON_SIZE 16
#define ROW_SPACING 8
#define ROW_PADDING 4

struct _MockaCategoryList
{
  GtkBox parent_instance;

  MockaMenuData *data; /* not owned */
  GtkWidget *scroller;
  GtkWidget *items; /* a list box down a column, a flow box along a row */
  gboolean horizontal;

  gint saved_selection; /* the item to put back when a search is cleared */
  gboolean rollover;
};

G_DEFINE_TYPE (MockaCategoryList, mocka_category_list, GTK_TYPE_BOX)

enum
{
  SIGNAL_CATEGORY_SELECTED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static GtkWidget *
nth_item (MockaCategoryList *self, gint index)
{
  if (self->horizontal)
    {
      return GTK_WIDGET (gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (self->items), index));
    }

  return GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->items), index));
}

static GtkWidget *
selected_item (MockaCategoryList *self)
{
  GList *selected;
  GtkWidget *item;

  if (!self->horizontal)
    {
      return GTK_WIDGET (gtk_list_box_get_selected_row (GTK_LIST_BOX (self->items)));
    }

  selected = gtk_flow_box_get_selected_children (GTK_FLOW_BOX (self->items));
  item = selected != NULL ? selected->data : NULL;
  g_list_free (selected);

  return item;
}

static gint
item_index (MockaCategoryList *self, GtkWidget *item)
{
  if (item == NULL)
    {
      return -1;
    }

  if (self->horizontal)
    {
      return gtk_flow_box_child_get_index (GTK_FLOW_BOX_CHILD (item));
    }

  return gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (item));
}

static void
select_item (MockaCategoryList *self, GtkWidget *item)
{
  if (item == NULL)
    {
      return;
    }

  if (self->horizontal)
    {
      gtk_flow_box_select_child (GTK_FLOW_BOX (self->items), GTK_FLOW_BOX_CHILD (item));
    }
  else
    {
      gtk_list_box_select_row (GTK_LIST_BOX (self->items), GTK_LIST_BOX_ROW (item));
    }
}

static void
unselect_items (MockaCategoryList *self)
{
  if (self->horizontal)
    {
      gtk_flow_box_unselect_all (GTK_FLOW_BOX (self->items));
    }
  else
    {
      gtk_list_box_unselect_all (GTK_LIST_BOX (self->items));
    }
}

static void
destroy_row (GtkWidget *widget, gpointer user_data)
{
  gtk_widget_destroy (widget);
}

/* icon is borrowed: the image takes its own reference. */
static GtkWidget *
make_content (const gchar *name, GIcon *icon)
{
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

  return box;
}

/* Returns the item the selection works with, which the content goes inside. */
static GtkWidget *
append_item (MockaCategoryList *self, const gchar *name, GIcon *icon)
{
  GtkWidget *item = self->horizontal ? gtk_flow_box_child_new () : gtk_list_box_row_new ();

  gtk_container_add (GTK_CONTAINER (item), make_content (name, icon));
  gtk_container_add (GTK_CONTAINER (self->items), item);

  return item;
}

GPtrArray *
mocka_category_list_get_apps (MockaCategoryList *self)
{
  GtkWidget *item;
  MockaMenuCategory *category;

  g_return_val_if_fail (MOCKA_IS_CATEGORY_LIST (self), NULL);

  item = selected_item (self);
  category = item != NULL ? g_object_get_data (G_OBJECT (item), "category") : NULL;

  if (category != NULL)
    {
      return mocka_menu_category_get_apps (category);
    }

  return mocka_menu_data_get_all_apps (self->data);
}

static void
emit_selected (MockaCategoryList *self)
{
  if (selected_item (self) != NULL)
    {
      g_signal_emit (self, signals[SIGNAL_CATEGORY_SELECTED], 0);
    }
}

static void
on_row_selected (GtkListBox *list, GtkListBoxRow *row, MockaCategoryList *self)
{
  emit_selected (self);
}

static void
on_children_changed (GtkFlowBox *box, MockaCategoryList *self)
{
  emit_selected (self);
}

/* The pointer is inside our own window here, so the coordinates are the
 * list's own. */
static gboolean
on_motion (GtkWidget *widget, GdkEventMotion *event, MockaCategoryList *self)
{
  GtkWidget *item;

  if (!self->rollover)
    {
      return GDK_EVENT_PROPAGATE;
    }

  if (self->horizontal)
    {
      item = GTK_WIDGET (gtk_flow_box_get_child_at_pos (GTK_FLOW_BOX (self->items), (gint)event->x, (gint)event->y));
    }
  else
    {
      item = GTK_WIDGET (gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->items), (gint)event->y));
    }

  select_item (self, item);

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
  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));

  gtk_widget_set_sensitive (self->scroller, !searching);

  if (searching && self->saved_selection < 0)
    {
      gint index = item_index (self, selected_item (self));

      self->saved_selection = index >= 0 ? index : 0;
      unselect_items (self);
    }
  else if (!searching && self->saved_selection >= 0)
    {
      select_item (self, nth_item (self, self->saved_selection));
      self->saved_selection = -1;
    }
}

gboolean
mocka_category_list_is_sensitive (MockaCategoryList *self)
{
  g_return_val_if_fail (MOCKA_IS_CATEGORY_LIST (self), FALSE);
  return gtk_widget_get_sensitive (self->scroller);
}

/* Focus on the box itself does nothing: the items hold it. */
gboolean
mocka_category_list_grab_focus_on_row (MockaCategoryList *self)
{
  GtkWidget *item;

  g_return_val_if_fail (MOCKA_IS_CATEGORY_LIST (self), FALSE);

  if (!gtk_widget_get_sensitive (self->scroller))
    {
      return FALSE;
    }

  item = selected_item (self);
  if (item == NULL)
    {
      item = nth_item (self, 0);
      if (item == NULL)
        {
          return FALSE;
        }
      select_item (self, item);
    }

  gtk_widget_grab_focus (item);

  return TRUE;
}

void
mocka_category_list_reset (MockaCategoryList *self, gboolean take_focus)
{
  GtkWidget *first;
  GtkAdjustment *adjustment;

  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));

  gtk_widget_set_sensitive (self->scroller, TRUE);
  self->saved_selection = -1;

  first = nth_item (self, 0);
  if (first != NULL)
    {
      select_item (self, first);
      if (take_focus)
        {
          gtk_widget_grab_focus (first);
        }
    }

  adjustment = self->horizontal ? gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scroller))
                                : gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroller));
  gtk_adjustment_set_value (adjustment, gtk_adjustment_get_lower (adjustment));
}

void
mocka_category_list_rebuild (MockaCategoryList *self)
{
  GPtrArray *categories;

  g_return_if_fail (MOCKA_IS_CATEGORY_LIST (self));

  gtk_container_foreach (GTK_CONTAINER (self->items), destroy_row, NULL);

  /* "All" first, then the categories in menu order (SPEC section 6). */
  append_item (self, _ ("All"), NULL);

  categories = mocka_menu_data_get_categories (self->data);
  for (guint i = 0; i < categories->len; i++)
    {
      MockaMenuCategory *category = g_ptr_array_index (categories, i);
      GtkWidget *item
          = append_item (self, mocka_menu_category_get_name (category), mocka_menu_category_get_icon (category));

      g_object_set_data_full (G_OBJECT (item), "category", g_object_ref (category), g_object_unref);
    }

  /*
   * One line, by asking for exactly as many per line as there are. A flow box
   * left to itself asks for the width of a single child, which centring then
   * grants, and they come out stacked in a column.
   */
  if (self->horizontal)
    {
      guint count = categories->len + 1;

      gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->items), count);
      gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->items), count);
    }

  gtk_widget_show_all (self->items);

  mocka_category_list_reset (self, FALSE);
}

static void
mocka_category_list_init (MockaCategoryList *self)
{
  self->saved_selection = -1;
}

static void
mocka_category_list_class_init (MockaCategoryListClass *klass)
{
  signals[SIGNAL_CATEGORY_SELECTED] = g_signal_new ("category-selected", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                                    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

GtkWidget *
mocka_category_list_new (MockaMenuData *data, GtkOrientation orientation)
{
  MockaCategoryList *self;

  g_return_val_if_fail (MOCKA_IS_MENU_DATA (data), NULL);

  self = g_object_new (MOCKA_TYPE_CATEGORY_LIST, NULL);
  self->data = data;
  self->horizontal = orientation == GTK_ORIENTATION_HORIZONTAL;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);

  if (self->horizontal)
    {
      self->items = gtk_flow_box_new ();
      gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->items), GTK_SELECTION_SINGLE);
      gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (self->items), ROW_SPACING);
      gtk_widget_set_halign (self->items, GTK_ALIGN_CENTER);
    }
  else
    {
      self->items = gtk_list_box_new ();
      /* Single rather than browse, so the selection can be dropped while a
       * search is showing. Browse always keeps one. */
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->items), GTK_SELECTION_SINGLE);
    }

  self->scroller = gtk_scrolled_window_new (NULL, NULL);
  /* A scrollbar only when it does not fit (SPEC section 6). */
  if (self->horizontal)
    {
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
      gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
    }
  else
    {
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
      /*
       * The categories ask for their whole width and height, so the column is
       * as wide as the longest name and tall enough to show every category.
       */
      gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
      gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (self->scroller), TRUE);
    }
  gtk_container_add (GTK_CONTAINER (self->scroller), self->items);
  gtk_box_pack_start (GTK_BOX (self), self->scroller, TRUE, TRUE, 0);

  gtk_widget_add_events (self->items, GDK_POINTER_MOTION_MASK);
  g_signal_connect (self->items, "motion-notify-event", G_CALLBACK (on_motion), self);
  if (self->horizontal)
    {
      g_signal_connect (self->items, "selected-children-changed", G_CALLBACK (on_children_changed), self);
    }
  else
    {
      g_signal_connect (self->items, "row-selected", G_CALLBACK (on_row_selected), self);
    }

  mocka_category_list_rebuild (self);

  return GTK_WIDGET (self);
}