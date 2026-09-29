/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "classic-view.h"

/* Keeps a value inside a range even when the range itself is empty. */
static gint
clamp_into (gint value, gint low, gint high)
{
  if (high < low)
    return low;

  return CLAMP (value, low, high);
}

GdkRectangle
mocka_classic_view_place (const GdkRectangle *anchor,
                          const GdkRectangle *monitor,
                          GtkPositionType     panel_side,
                          gint                want_width,
                          gint                want_height)
{
  GdkRectangle out;
  gint room_width;
  gint room_height;

  /*
   * How much room there is between the button and the far edge of the
   * monitor, on the side the menu opens towards. The panel is on the other
   * side of the button, so this never runs under the panel.
   */
  switch (panel_side)
    {
    case GTK_POS_BOTTOM:
      room_width = monitor->width;
      room_height = anchor->y - monitor->y;
      break;
    case GTK_POS_TOP:
      room_width = monitor->width;
      room_height = (monitor->y + monitor->height) - (anchor->y + anchor->height);
      break;
    case GTK_POS_LEFT:
      room_width = (monitor->x + monitor->width) - (anchor->x + anchor->width);
      room_height = monitor->height;
      break;
    case GTK_POS_RIGHT:
    default:
      room_width = anchor->x - monitor->x;
      room_height = monitor->height;
      break;
    }

  out.width = MAX (MIN (want_width, room_width), MOCKA_CLASSIC_MIN_WIDTH);
  out.height = MAX (MIN (want_height, room_height), MOCKA_CLASSIC_MIN_HEIGHT);

  /* Against the button, on the side facing the screen. */
  switch (panel_side)
    {
    case GTK_POS_BOTTOM:
      out.x = anchor->x;
      out.y = anchor->y - out.height;
      break;
    case GTK_POS_TOP:
      out.x = anchor->x;
      out.y = anchor->y + anchor->height;
      break;
    case GTK_POS_LEFT:
      out.x = anchor->x + anchor->width;
      out.y = anchor->y;
      break;
    case GTK_POS_RIGHT:
    default:
      out.x = anchor->x - out.width;
      out.y = anchor->y;
      break;
    }

  /* Never off the monitor that holds the button. */
  out.x = clamp_into (out.x, monitor->x, monitor->x + monitor->width - out.width);
  out.y = clamp_into (out.y, monitor->y, monitor->y + monitor->height - out.height);

  return out;
}

#define APP_ICON_SIZE 24
#define CATEGORY_ICON_SIZE 16
#define ROW_SPACING 8
#define ROW_PADDING 4

/* The settings application the Settings shortcut opens (SPEC section 6). */
#define SETTINGS_DESKTOP_ID "matecc.desktop"

struct _MockaClassicView
{
  GtkBox parent_instance;

  MockaMenuData *data;        /* not owned */

  GtkWidget *category_list;
  GtkWidget *category_scroller;
  GtkWidget *app_list;
  GtkWidget *app_scroller;

  gboolean rollover;
};

G_DEFINE_TYPE (MockaClassicView, mocka_classic_view, GTK_TYPE_BOX)

enum {
  SIGNAL_APP_ACTIVATED,
  N_VIEW_SIGNALS
};

static guint view_signals[N_VIEW_SIGNALS];

static void
show_apps (MockaClassicView *self, GPtrArray *apps);

/* gtk_widget_destroy does not have the shape gtk_container_foreach wants. */
static void
destroy_row (GtkWidget *widget, gpointer user_data)
{
  gtk_widget_destroy (widget);
}

/* One row: icon, name, and the app's comment as its tooltip. */
static GtkWidget *
make_app_row (MockaMenuApp *app)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROW_SPACING);
  GtkWidget *image = gtk_image_new_from_gicon (mocka_menu_app_get_icon (app),
                                               GTK_ICON_SIZE_LARGE_TOOLBAR);
  GtkWidget *label = gtk_label_new (mocka_menu_app_get_name (app));
  const gchar *comment = mocka_menu_app_get_comment (app);

  gtk_image_set_pixel_size (GTK_IMAGE (image), APP_ICON_SIZE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (box), ROW_PADDING);
  gtk_container_add (GTK_CONTAINER (row), box);

  if (comment != NULL && *comment != '\0')
    gtk_widget_set_tooltip_text (row, comment);

  g_object_set_data_full (G_OBJECT (row), "app", g_object_ref (app),
                          g_object_unref);

  return row;
}

static GtkWidget *
make_category_row (const gchar *name, GIcon *icon)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROW_SPACING);
  GtkWidget *label = gtk_label_new (name);

  if (icon != NULL)
    {
      GtkWidget *image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);

      gtk_image_set_pixel_size (GTK_IMAGE (image), CATEGORY_ICON_SIZE);
      gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
    }

  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (box), ROW_PADDING);
  gtk_container_add (GTK_CONTAINER (row), box);

  return row;
}

/*
 * With rollover on, moving over a category selects it, so its apps show
 * without a click (SPEC section 6). The pointer is inside our own window
 * here, so the event coordinates are the list's own.
 */
static gboolean
on_category_motion (GtkWidget        *widget,
                    GdkEventMotion   *event,
                    MockaClassicView *self)
{
  GtkListBoxRow *row;

  if (!self->rollover)
    return GDK_EVENT_PROPAGATE;

  row = gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->category_list),
                                   (gint) event->y);

  /* The Settings shortcut is not selectable, so hovering it selects nothing. */
  if (row != NULL && gtk_list_box_row_get_selectable (row))
    gtk_list_box_select_row (GTK_LIST_BOX (self->category_list), row);

  return GDK_EVENT_PROPAGATE;
}

static void
on_category_selected (GtkListBox       *list,
                      GtkListBoxRow    *row,
                      MockaClassicView *self)
{
  MockaMenuCategory *category;

  if (row == NULL)
    return;

  category = g_object_get_data (G_OBJECT (row), "category");
  if (category != NULL)
    show_apps (self, mocka_menu_category_get_apps (category));
  else
    show_apps (self, mocka_menu_data_get_all_apps (self->data));
}

/* The Settings shortcut is not a category: it launches (SPEC section 6). */
static void
on_category_activated (GtkListBox       *list,
                       GtkListBoxRow    *row,
                       MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "settings-app");

  if (app != NULL)
    g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
}

static void
on_app_activated (GtkListBox       *list,
                  GtkListBoxRow    *row,
                  MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "app");

  if (app != NULL)
    g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
}

static void
show_apps (MockaClassicView *self, GPtrArray *apps)
{
  GtkAdjustment *adjustment;

  gtk_container_foreach (GTK_CONTAINER (self->app_list), destroy_row, NULL);

  for (guint i = 0; apps != NULL && i < apps->len; i++)
    {
      GtkWidget *row = make_app_row (g_ptr_array_index (apps, i));

      gtk_container_add (GTK_CONTAINER (self->app_list), row);
    }

  gtk_widget_show_all (self->app_list);

  /* A new category starts at the top of its list. */
  adjustment = gtk_scrolled_window_get_vadjustment (
      GTK_SCROLLED_WINDOW (self->app_scroller));
  gtk_adjustment_set_value (adjustment, gtk_adjustment_get_lower (adjustment));
}

/* The app the Settings shortcut opens, or NULL when it is not installed. */
static MockaMenuApp *
find_settings_app (MockaClassicView *self)
{
  GPtrArray *all = mocka_menu_data_get_all_apps (self->data);

  for (guint i = 0; i < all->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (all, i);

      if (g_strcmp0 (mocka_menu_app_get_id (app), SETTINGS_DESKTOP_ID) == 0)
        return app;
    }

  return NULL;
}

void
mocka_classic_view_rebuild (MockaClassicView *self)
{
  GPtrArray *categories;
  GtkWidget *row;
  MockaMenuApp *settings;

  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  gtk_container_foreach (GTK_CONTAINER (self->category_list), destroy_row, NULL);

  /* "All" first, then the categories in menu order (SPEC section 6). */
  row = make_category_row (_("All"), NULL);
  gtk_container_add (GTK_CONTAINER (self->category_list), row);

  categories = mocka_menu_data_get_categories (self->data);
  for (guint i = 0; i < categories->len; i++)
    {
      MockaMenuCategory *category = g_ptr_array_index (categories, i);

      row = make_category_row (mocka_menu_category_get_name (category),
                               mocka_menu_category_get_icon (category));
      g_object_set_data_full (G_OBJECT (row), "category",
                              g_object_ref (category), g_object_unref);
      gtk_container_add (GTK_CONTAINER (self->category_list), row);
    }

  /* Then, separated from them, the Settings shortcut (SPEC section 6). */
  settings = find_settings_app (self);
  if (settings != NULL)
    {
      const gchar *comment = mocka_menu_app_get_comment (settings);

      row = gtk_list_box_row_new ();
      gtk_container_add (GTK_CONTAINER (row),
                         gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
      gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
      gtk_container_add (GTK_CONTAINER (self->category_list), row);

      row = make_category_row (_("Settings"),
                               g_themed_icon_new ("preferences-system"));
      gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
      g_object_set_data_full (G_OBJECT (row), "settings-app",
                              g_object_ref (settings), g_object_unref);
      if (comment != NULL && *comment != '\0')
        gtk_widget_set_tooltip_text (row, comment);
      gtk_container_add (GTK_CONTAINER (self->category_list), row);
    }

  gtk_widget_show_all (self->category_list);
  mocka_classic_view_reset (self);
}

void
mocka_classic_view_reset (MockaClassicView *self)
{
  GtkListBoxRow *first;
  GtkAdjustment *adjustment;

  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  first = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->category_list), 0);
  if (first != NULL)
    gtk_list_box_select_row (GTK_LIST_BOX (self->category_list), first);

  show_apps (self, mocka_menu_data_get_all_apps (self->data));

  adjustment = gtk_scrolled_window_get_vadjustment (
      GTK_SCROLLED_WINDOW (self->category_scroller));
  gtk_adjustment_set_value (adjustment, gtk_adjustment_get_lower (adjustment));
}

static void
mocka_classic_view_init (MockaClassicView *self)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_HORIZONTAL);

  self->category_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->category_list),
                                   GTK_SELECTION_BROWSE);

  self->category_scroller = gtk_scrolled_window_new (NULL, NULL);
  /* A scrollbar only when it does not fit (SPEC section 6). */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->category_scroller),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  /*
   * The categories ask for their whole width and height, so the column is as
   * wide as the longest name and tall enough to show every category. Either
   * is given up, with a scrollbar or an ellipsis, only when the monitor
   * cannot fit it.
   */
  gtk_scrolled_window_set_propagate_natural_width (
      GTK_SCROLLED_WINDOW (self->category_scroller), TRUE);
  gtk_scrolled_window_set_propagate_natural_height (
      GTK_SCROLLED_WINDOW (self->category_scroller), TRUE);
  gtk_container_add (GTK_CONTAINER (self->category_scroller), self->category_list);

  self->app_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->app_list),
                                   GTK_SELECTION_BROWSE);

  self->app_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->app_scroller),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (self->app_scroller), self->app_list);

  gtk_box_pack_start (GTK_BOX (self), self->category_scroller, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self),
                      gtk_separator_new (GTK_ORIENTATION_VERTICAL),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self), self->app_scroller, TRUE, TRUE, 0);

  gtk_widget_add_events (self->category_list, GDK_POINTER_MOTION_MASK);
  g_signal_connect (self->category_list, "motion-notify-event",
                    G_CALLBACK (on_category_motion), self);
  g_signal_connect (self->category_list, "row-selected",
                    G_CALLBACK (on_category_selected), self);
  g_signal_connect (self->category_list, "row-activated",
                    G_CALLBACK (on_category_activated), self);
  g_signal_connect (self->app_list, "row-activated",
                    G_CALLBACK (on_app_activated), self);
}

static void
mocka_classic_view_class_init (MockaClassicViewClass *klass)
{
  view_signals[SIGNAL_APP_ACTIVATED] =
    g_signal_new ("app-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, MOCKA_TYPE_MENU_APP);
}

void
mocka_classic_view_set_rollover (MockaClassicView *self, gboolean rollover)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));
  self->rollover = rollover;
}

GtkWidget *
mocka_classic_view_new (MockaMenuData *data)
{
  MockaClassicView *self = g_object_new (MOCKA_TYPE_CLASSIC_VIEW, NULL);

  self->data = data;
  mocka_classic_view_rebuild (self);

  return GTK_WIDGET (self);
}
