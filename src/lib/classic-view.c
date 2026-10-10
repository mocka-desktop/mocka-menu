/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include <string.h>

#include "app-menu.h"
#include "classic-view.h"
#include "menu-window.h"
#include "search.h"
#include "session.h"

/* Keeps a value inside a range even when the range itself is empty. */
static gint
clamp_into (gint value, gint low, gint high)
{
  if (high < low)
    {
      return low;
    }

  return CLAMP (value, low, high);
}

GdkRectangle
mocka_classic_view_place (const GdkRectangle *anchor, const GdkRectangle *monitor, GtkPositionType panel_side,
                          GtkRequisition want)
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

  out.width = MAX (MIN (want.width, room_width), MOCKA_CLASSIC_MIN_WIDTH);
  out.height = MAX (MIN (want.height, room_height), MOCKA_CLASSIC_MIN_HEIGHT);

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
#define FAVOURITE_ICON_SIZE 32
#define CATEGORY_ICON_SIZE 16
#define ROW_SPACING 8
#define ROW_PADDING 4

/* The settings application the Settings shortcut opens (SPEC section 6). */
#define SETTINGS_DESKTOP_ID "matecc.desktop"

/* The Preferences icon. preferences-system is Administration's. */
#define SETTINGS_ICON_NAME "preferences-desktop"

struct _MockaClassicView
{
  GtkBox parent_instance;

  MockaMenuData *data;         /* not owned */
  MockaFavourites *favourites; /* not owned */

  GtkWidget *entry;
  GtkWidget *columns;       /* the category list beside the app list */
  GtkWidget *left_column;   /* the categories, with Settings under them */
  GtkWidget *settings_list; /* the Settings shortcut, never a category */
  GtkWidget *category_list;
  GtkWidget *category_scroller;
  GtkWidget *app_list;
  GtkWidget *app_scroller;
  GtkWidget *favourites_list;
  GtkWidget *favourites_scroller;
  GtkWidget *favourites_column; /* the favourites above the session controls */
  GtkWidget *session_box;
  GtkWidget *session_buttons[MOCKA_SESSION_N_ACTIONS];
  MockaSession *session; /* owned, built with the view */

  /* Weak, while a drag is running. The dragged row can be destroyed by a
   * rebuild before the drag ends, and then it can no longer name its own
   * window, so the window is remembered here instead. */
  MockaMenuWindow *drag_window;
  gboolean favourites_pending; /* a change held back until the drag ends */

  guint app_count;     /* rows in app_list, for keyboard navigation */
  gulong toplevel_key; /* handler on the window, so the keys come first */
  gint saved_category; /* the category to put back when a search is cleared */
  gboolean rollover;
};

G_DEFINE_TYPE (MockaClassicView, mocka_classic_view, GTK_TYPE_BOX)

enum
{
  SIGNAL_APP_ACTIVATED,
  N_VIEW_SIGNALS
};

static guint view_signals[N_VIEW_SIGNALS];

static void show_apps (MockaClassicView *self, GPtrArray *apps);
static void apply_favourites_change (MockaClassicView *self);

/*
 * The icon to draw, swapped for a generic one only when the theme has none of
 * the names this icon carries (SPEC section 16).
 *
 * This cannot be decided when the icon is read. GTK searches theme by theme
 * and tries every name of an icon within each theme, so an icon carrying both
 * its own name and a generic one resolves to whichever the first theme
 * happens to have, which is usually the generic one. Applications installed
 * for one user keep their icons in hicolor, late in that search, so they
 * would lose to the generic icon every time.
 *
 * Returns a reference the caller owns.
 */
static GIcon *
icon_to_draw (GIcon *icon, const gchar *generic)
{
  GtkIconTheme *theme = gtk_icon_theme_get_default ();
  const gchar *const *names;

  if (icon == NULL)
    {
      return g_themed_icon_new (generic);
    }

  if (!G_IS_THEMED_ICON (icon))
    {
      return g_object_ref (icon);
    }

  names = g_themed_icon_get_names (G_THEMED_ICON (icon));
  for (gsize i = 0; names != NULL && names[i] != NULL; i++)
    {
      if (gtk_icon_theme_has_icon (theme, names[i]))
        {
          return g_object_ref (icon);
        }
    }

  return g_themed_icon_new (generic);
}

/* Within the menu an app travels as its desktop entry ID. */
#define APP_TARGET "application/x-mocka-menu-app"

enum
{
  TARGET_APP,
  TARGET_URI_LIST
};

/*
 * Dragged out, an app is the URI of its desktop entry, which is what the
 * desktop, a panel and Mocka Dock all make a launcher from (SPEC section 13).
 */
static const GtkTargetEntry drag_targets[] = {
  { (gchar *)APP_TARGET, GTK_TARGET_SAME_APP, TARGET_APP },
  { (gchar *)"text/uri-list", 0, TARGET_URI_LIST },
};

/* The column takes our own apps only, never files from elsewhere. */
static const GtkTargetEntry drop_targets[] = {
  { (gchar *)APP_TARGET, GTK_TARGET_SAME_APP, TARGET_APP },
};

// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) two actions combined is not one named value
static const GdkDragAction APP_DRAG_ACTIONS = GDK_ACTION_COPY | GDK_ACTION_MOVE;
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) the drag-data-get signal fixes this signature
static void
on_drag_data_get (GtkWidget *row, GdkDragContext *context, GtkSelectionData *selection, guint info, guint time,
                  gpointer user_data)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "app");
  const gchar *file;
  gchar *uris[2] = { NULL, NULL };

  if (app == NULL)
    {
      return;
    }

  if (info == TARGET_APP)
    {
      gtk_selection_data_set (selection, gdk_atom_intern_static_string (APP_TARGET), 8,
                              (const guchar *)mocka_menu_app_get_id (app), (gint)strlen (mocka_menu_app_get_id (app)));
      return;
    }

  file = g_desktop_app_info_get_filename (mocka_menu_app_get_app_info (app));
  if (file == NULL)
    {
      return;
    }

  uris[0] = g_filename_to_uri (file, NULL, NULL);
  if (uris[0] != NULL)
    {
      gtk_selection_data_set_uris (selection, uris);
    }

  g_free (uris[0]);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static void
on_drag_begin (GtkWidget *row, GdkDragContext *context, MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "app");
  GIcon *icon = app != NULL ? mocka_menu_app_get_icon (app) : NULL;
  MockaMenuWindow *window = mocka_menu_window_of (row);

  if (icon != NULL)
    {
      gtk_drag_set_icon_gicon (context, icon, 0, 0);
    }

  if (window == NULL || self->drag_window != NULL)
    {
      return;
    }

  self->drag_window = window;
  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *)&self->drag_window);
  mocka_menu_window_begin_grab_handover (window);
}

static void
on_drag_end (GtkWidget *row, GdkDragContext *context, MockaClassicView *self)
{
  MockaMenuWindow *window = self->drag_window;

  if (window == NULL)
    {
      return;
    }

  g_object_remove_weak_pointer (G_OBJECT (window), (gpointer *)&self->drag_window);
  self->drag_window = NULL;
  mocka_menu_window_end_grab_handover (window);

  /* Safe now: the row this ran on is no longer needed. */
  if (self->favourites_pending)
    {
      apply_favourites_change (self);
    }
}

static gboolean
on_row_button_press (GtkWidget *events, GdkEventButton *event, MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (events), "app");

  if (event->button != GDK_BUTTON_SECONDARY || app == NULL || self->favourites == NULL)
    {
      return GDK_EVENT_PROPAGATE;
    }

  mocka_app_menu_popup (app, self->favourites, events, (const GdkEvent *)event);

  return GDK_EVENT_STOP;
}

/* GTK emits this for the Menu key and Shift + F10 (SPEC section 12.2). */
static gboolean
on_row_popup_menu (GtkWidget *row, MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "app");

  if (app == NULL || self->favourites == NULL)
    {
      return FALSE;
    }

  mocka_app_menu_popup (app, self->favourites, row, NULL);

  return TRUE;
}

/*
 * A list row has no window of its own, so a drag source on it never sees the
 * button events. The content goes inside an event box, which has one, and
 * that is what drags and what catches the right click.
 */
static GtkWidget *
draggable_holder (GtkWidget *row, MockaMenuApp *app, MockaClassicView *self)
{
  GtkWidget *events = gtk_event_box_new ();

  gtk_event_box_set_visible_window (GTK_EVENT_BOX (events), FALSE);
  gtk_container_add (GTK_CONTAINER (row), events);

  /* The row holds the reference; this one only borrows it. */
  g_object_set_data (G_OBJECT (events), "app", app);

  gtk_drag_source_set (events, GDK_BUTTON1_MASK, drag_targets, G_N_ELEMENTS (drag_targets), APP_DRAG_ACTIONS);
  g_signal_connect (events, "drag-data-get", G_CALLBACK (on_drag_data_get), NULL);
  g_signal_connect (events, "drag-begin", G_CALLBACK (on_drag_begin), self);
  g_signal_connect (events, "drag-end", G_CALLBACK (on_drag_end), self);
  g_signal_connect (events, "button-press-event", G_CALLBACK (on_row_button_press), self);
  g_signal_connect (row, "popup-menu", G_CALLBACK (on_row_popup_menu), self);

  return events;
}

/* gtk_widget_destroy does not have the shape gtk_container_foreach wants. */
static void
destroy_row (GtkWidget *widget, gpointer user_data)
{
  gtk_widget_destroy (widget);
}

/* One row: icon, name, and the app's comment as its tooltip. */
static GtkWidget *
make_app_row (MockaMenuApp *app, MockaClassicView *self)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROW_SPACING);
  GIcon *icon = icon_to_draw (mocka_menu_app_get_icon (app), "application-x-executable");
  GtkWidget *image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
  GtkWidget *label = gtk_label_new (mocka_menu_app_get_name (app));
  const gchar *comment = mocka_menu_app_get_comment (app);

  g_object_unref (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (image), APP_ICON_SIZE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0F);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (box), ROW_PADDING);

  if (comment != NULL && *comment != '\0')
    {
      gtk_widget_set_tooltip_text (row, comment);
    }

  g_object_set_data_full (G_OBJECT (row), "app", g_object_ref (app), g_object_unref);
  gtk_container_add (GTK_CONTAINER (draggable_holder (row, app, self)), box);

  return row;
}

/* One favourite: its icon, with its name as the tooltip (SPEC section 8). */
static GtkWidget *
make_favourite_row (MockaMenuApp *app, MockaClassicView *self)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GIcon *icon = icon_to_draw (mocka_menu_app_get_icon (app), "application-x-executable");
  GtkWidget *image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DND);

  g_object_unref (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (image), FAVOURITE_ICON_SIZE);
  gtk_widget_set_margin_top (image, ROW_PADDING);
  gtk_widget_set_margin_bottom (image, ROW_PADDING);
  gtk_widget_set_margin_start (image, ROW_PADDING);
  gtk_widget_set_margin_end (image, ROW_PADDING);

  gtk_widget_set_tooltip_text (row, mocka_menu_app_get_name (app));
  g_object_set_data_full (G_OBJECT (row), "app", g_object_ref (app), g_object_unref);
  gtk_container_add (GTK_CONTAINER (draggable_holder (row, app, self)), image);

  return row;
}

/* icon is borrowed: the image takes its own reference. */
static GtkWidget *
make_category_row (const gchar *name, GIcon *icon)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROW_SPACING);
  GtkWidget *label = gtk_label_new (name);

  if (icon != NULL)
    {
      GIcon *drawn = icon_to_draw (icon, "folder");
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

/*
 * With rollover on, moving over a category selects it, so its apps show
 * without a click (SPEC section 6). The pointer is inside our own window
 * here, so the event coordinates are the list's own.
 */
static gboolean
on_category_motion (GtkWidget *widget, GdkEventMotion *event, MockaClassicView *self)
{
  GtkListBoxRow *row;

  if (!self->rollover)
    {
      return GDK_EVENT_PROPAGATE;
    }

  row = gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->category_list), (gint)event->y);

  /* The Settings shortcut is not selectable, so hovering it selects nothing. */
  if (row != NULL && gtk_list_box_row_get_selectable (row))
    {
      gtk_list_box_select_row (GTK_LIST_BOX (self->category_list), row);
    }

  return GDK_EVENT_PROPAGATE;
}

/* The apps of whichever category is selected, or all of them. */
static GPtrArray *
apps_of_selected_category (MockaClassicView *self)
{
  GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->category_list));
  MockaMenuCategory *category = row != NULL ? g_object_get_data (G_OBJECT (row), "category") : NULL;

  if (category != NULL)
    {
      return mocka_menu_category_get_apps (category);
    }

  return mocka_menu_data_get_all_apps (self->data);
}

static void
on_category_selected (GtkListBox *list, GtkListBoxRow *row, MockaClassicView *self)
{
  if (row == NULL)
    {
      return;
    }

  /* While there is search text the category selection is ignored
   * (SPEC section 6). */
  if (mocka_classic_view_is_searching (self))
    {
      return;
    }

  show_apps (self, apps_of_selected_category (self));
}

/*
 * Results replace the category's apps while there is text, and the category
 * comes back when the text goes (SPEC section 9.1).
 */
static void
on_search_changed (GtkSearchEntry *entry, MockaClassicView *self)
{
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (entry));
  gboolean searching = text != NULL && *text != '\0';
  GtkListBoxRow *row;
  GPtrArray *results;

  /* The categories are greyed while searching, since the selection counts for
   * nothing until the search is cleared (SPEC section 6). The selection goes
   * with them, and is put back when the search is cleared. */
  gtk_widget_set_sensitive (self->category_scroller, !searching);

  if (searching && self->saved_category < 0)
    {
      row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->category_list));
      self->saved_category = row != NULL ? gtk_list_box_row_get_index (row) : 0;
      gtk_list_box_unselect_all (GTK_LIST_BOX (self->category_list));
    }
  else if (!searching && self->saved_category >= 0)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->category_list), self->saved_category);
      if (row != NULL)
        {
          gtk_list_box_select_row (GTK_LIST_BOX (self->category_list), row);
        }
      self->saved_category = -1;
    }

  if (!searching)
    {
      show_apps (self, apps_of_selected_category (self));
      return;
    }

  /* Favourites come first within a rank (SPEC section 9.2). */
  results = mocka_search_run (mocka_menu_data_get_all_apps (self->data), text,
                              self->favourites != NULL ? mocka_favourites_get_ids (self->favourites) : NULL);
  show_apps (self, results);
  g_ptr_array_unref (results);
}

/*
 * Typing anywhere in the menu goes to the search entry (SPEC section 9.1).
 * Key events reach the focused widget first and then travel up to here, so
 * this only sees what the lists did not use, such as ordinary text.
 */
/* Keeps a row on screen without taking the focus off wherever it is. */
static void
scroll_app_row_into_view (MockaClassicView *self, GtkListBoxRow *row)
{
  GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->app_scroller));
  GtkAllocation alloc;
  gdouble value = gtk_adjustment_get_value (adjustment);
  gdouble page = gtk_adjustment_get_page_size (adjustment);

  gtk_widget_get_allocation (GTK_WIDGET (row), &alloc);

  if (alloc.y < value)
    {
      gtk_adjustment_set_value (adjustment, alloc.y);
    }
  else if (alloc.y + alloc.height > value + page)
    {
      gtk_adjustment_set_value (adjustment, alloc.y + alloc.height - page);
    }
}

/* How many rows fit, so Page Up and Page Down move by a screenful. */
static gint
app_rows_per_page (MockaClassicView *self)
{
  GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->app_scroller));
  GtkListBoxRow *first = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->app_list), 0);
  GtkAllocation alloc;

  if (first == NULL)
    {
      return 1;
    }

  gtk_widget_get_allocation (GTK_WIDGET (first), &alloc);
  if (alloc.height <= 0)
    {
      return 1;
    }

  return MAX (1, (gint)(gtk_adjustment_get_page_size (adjustment) / alloc.height));
}

static void
select_app_at (MockaClassicView *self, gint index)
{
  GtkListBoxRow *row;

  if (self->app_count == 0)
    {
      return;
    }

  index = CLAMP (index, 0, (gint)self->app_count - 1);
  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->app_list), index);
  if (row == NULL)
    {
      return;
    }

  gtk_list_box_select_row (GTK_LIST_BOX (self->app_list), row);
  scroll_app_row_into_view (self, row);
}

static void
activate_selected_app (MockaClassicView *self)
{
  GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->app_list));
  MockaMenuApp *app;

  /* Enter launches the first result when nothing was picked (SPEC section 9.1). */
  if (row == NULL)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->app_list), 0);
    }
  if (row == NULL)
    {
      return;
    }

  app = g_object_get_data (G_OBJECT (row), "app");
  if (app != NULL)
    {
      g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

/* Left and Right move between the two lists (SPEC section 12.2). */
static gboolean
focus_list (MockaClassicView *self, guint keyval)
{
  GtkWidget *list;
  GtkListBoxRow *row;

  if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)
    {
      list = self->category_list;
    }
  else if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)
    {
      list = self->app_list;
    }
  else
    {
      return FALSE;
    }

  /* The categories are insensitive while a search is showing. */
  if (!gtk_widget_is_sensitive (list))
    {
      return FALSE;
    }

  /* The rows take the focus, not the box around them. */
  row = gtk_list_box_get_selected_row (GTK_LIST_BOX (list));
  if (row == NULL)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0);
    }
  if (row == NULL)
    {
      return FALSE;
    }

  gtk_list_box_select_row (GTK_LIST_BOX (list), row);
  gtk_widget_grab_focus (GTK_WIDGET (row));

  return TRUE;
}

/* The app list, driven from the search entry (SPEC section 12.2). */
static gboolean
navigate_app_list (MockaClassicView *self, guint keyval)
{
  GtkListBoxRow *selected = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->app_list));
  gint current = selected != NULL ? gtk_list_box_row_get_index (selected) : -1;

  switch (keyval)
    {
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
      select_app_at (self, current + 1);
      return TRUE;
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
      select_app_at (self, current <= 0 ? 0 : current - 1);
      return TRUE;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
      select_app_at (self, current + app_rows_per_page (self));
      return TRUE;
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
      select_app_at (self, current - app_rows_per_page (self));
      return TRUE;
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
      select_app_at (self, 0);
      return TRUE;
    case GDK_KEY_End:
    case GDK_KEY_KP_End:
      select_app_at (self, (gint)self->app_count - 1);
      return TRUE;
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
      return focus_list (self, keyval);
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      activate_selected_app (self);
      return TRUE;
    default:
      return FALSE;
    }
}

/*
 * On the window, because the entry binds Home, End and the Page keys for its
 * own cursor and would swallow them first. Only while the entry has focus:
 * the lists do their own navigation.
 */
static gboolean
on_toplevel_key_press (GtkWidget *toplevel, GdkEventKey *event, MockaClassicView *self)
{
  if (!gtk_widget_has_focus (self->entry))
    {
      return GDK_EVENT_PROPAGATE;
    }

  return navigate_app_list (self, event->keyval) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) the signal fixes this signature
static void
on_hierarchy_changed (GtkWidget *widget, GtkWidget *previous_toplevel, MockaClassicView *self)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);

  if (self->toplevel_key != 0 && previous_toplevel != NULL)
    {
      g_signal_handler_disconnect (previous_toplevel, self->toplevel_key);
      self->toplevel_key = 0;
    }

  if (gtk_widget_is_toplevel (toplevel))
    {
      self->toplevel_key = g_signal_connect (toplevel, "key-press-event", G_CALLBACK (on_toplevel_key_press), self);
    }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static gboolean
on_key_press (GtkWidget *widget, GdkEventKey *event, MockaClassicView *self)
{
  if (gtk_widget_has_focus (self->entry))
    {
      return GDK_EVENT_PROPAGATE;
    }

  /* With focus in a list, Left and Right still swap between them. */
  if (focus_list (self, event->keyval))
    {
      return GDK_EVENT_STOP;
    }

  if (!gtk_search_entry_handle_event (GTK_SEARCH_ENTRY (self->entry), (GdkEvent *)event))
    {
      return GDK_EVENT_PROPAGATE;
    }

  /* It took the key, so the cursor belongs there too. */
  gtk_widget_grab_focus (self->entry);
  gtk_editable_set_position (GTK_EDITABLE (self->entry), -1);

  return GDK_EVENT_STOP;
}

/* The Settings shortcut is not a category: it launches (SPEC section 6). */
static void
on_category_activated (GtkListBox *list, GtkListBoxRow *row, MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "settings-app");

  if (app != NULL)
    {
      g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

static void
show_favourites (MockaClassicView *self)
{
  GPtrArray *apps;

  if (self->favourites == NULL)
    {
      return;
    }

  gtk_container_foreach (GTK_CONTAINER (self->favourites_list), destroy_row, NULL);

  apps = mocka_favourites_get_apps (self->favourites);
  for (guint i = 0; i < apps->len; i++)
    {
      gtk_container_add (GTK_CONTAINER (self->favourites_list), make_favourite_row (g_ptr_array_index (apps, i), self));
    }

  /*
   * Shown even with nothing in it. The column stays for the session controls
   * at its bottom (SPEC section 10), and an empty list is still the drop
   * target, which is the only way to put the first favourite back.
   */
  gtk_widget_show_all (self->favourites_list);
  gtk_widget_show (self->favourites_scroller);
}

static void
apply_favourites_change (MockaClassicView *self)
{
  self->favourites_pending = FALSE;
  show_favourites (self);

  /* Favourites come first within a rank, so a showing search is now stale
   * (SPEC section 9.2). */
  if (mocka_classic_view_is_searching (self))
    {
      on_search_changed (GTK_SEARCH_ENTRY (self->entry), self);
    }
}

/*
 * A drop on the column changes the favourites while the drag is still
 * running, and rebuilding now would destroy the row being dragged. GTK then
 * never emits drag-end for it, the menu never takes its grab back, and the
 * keyboard is left pointing somewhere else. So the rebuild waits for the drag
 * to finish.
 */
static void
on_favourites_changed (MockaClassicView *self)
{
  if (self->drag_window != NULL)
    {
      self->favourites_pending = TRUE;
      return;
    }

  apply_favourites_change (self);
}

static void
on_session_button (GtkWidget *button, MockaClassicView *self)
{
  MockaSessionAction action = (MockaSessionAction)GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "action"));
  MockaMenuWindow *window = mocka_menu_window_of (button);

  /* The window closes at once, without waiting for the call (SPEC section 10). */
  if (window != NULL)
    {
      mocka_menu_window_close (window);
    }

  mocka_session_run (self->session, action);
}

/* A service that is not there hides its button (SPEC section 10). */
static void
show_session_buttons (MockaClassicView *self)
{
  gboolean any = FALSE;

  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      gboolean can = mocka_session_can (self->session, i);

      gtk_widget_set_visible (self->session_buttons[i], can);
      any = any || can;
    }

  gtk_widget_set_visible (self->session_box, any);
}

static void
build_session_buttons (MockaClassicView *self)
{
  self->session_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      GtkWidget *button = gtk_button_new_from_icon_name (mocka_session_action_icon (i), GTK_ICON_SIZE_LARGE_TOOLBAR);

      gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
      gtk_widget_set_tooltip_text (button, mocka_session_action_name (i));
      g_object_set_data (G_OBJECT (button), "action", GUINT_TO_POINTER (i));
      g_signal_connect (button, "clicked", G_CALLBACK (on_session_button), self);

      /* The applet shows the whole view at once, which would reveal a button
       * whose service is missing. */
      gtk_widget_set_no_show_all (button, TRUE);

      self->session_buttons[i] = button;
      gtk_box_pack_start (GTK_BOX (self->session_box), button, FALSE, FALSE, 0);
    }

  gtk_widget_set_no_show_all (self->session_box, TRUE);

  self->session = mocka_session_new ();
  g_signal_connect_swapped (self->session, "changed", G_CALLBACK (show_session_buttons), self);
  show_session_buttons (self);
}

/*
 * Where a drop lands, counting rows from the top. Past the middle of a row
 * means after it, which is how dropping at the very bottom appends.
 */
static gint
favourite_drop_position (MockaClassicView *self, gint y)
{
  GtkListBoxRow *row = gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->favourites_list), y);
  GtkAllocation alloc;

  if (row == NULL)
    {
      return -1;
    }

  gtk_widget_get_allocation (GTK_WIDGET (row), &alloc);

  return gtk_list_box_row_get_index (row) + (y > alloc.y + alloc.height / 2 ? 1 : 0);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) the drag-data-received signal fixes this signature
static void
on_favourites_drop (GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection, guint info,
                    guint time, MockaClassicView *self)
{
  const guchar *raw = gtk_selection_data_get_data (selection);
  gint length = gtk_selection_data_get_length (selection);
  gchar *id;
  gint position;

  if (raw == NULL || length <= 0 || self->favourites == NULL)
    {
      gtk_drag_finish (context, FALSE, FALSE, time);
      return;
    }

  id = g_strndup ((const gchar *)raw, (gsize)length);
  position = favourite_drop_position (self, y);

  /* Already a favourite, so this is a reordering (SPEC section 8). */
  if (mocka_favourites_contains (self->favourites, id))
    {
      mocka_favourites_move (self->favourites, id, position);
    }
  else
    {
      mocka_favourites_add (self->favourites, id, position);
    }

  g_free (id);
  gtk_drag_finish (context, TRUE, FALSE, time);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static void
on_favourite_activated (GtkListBox *list, GtkListBoxRow *row, MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "app");

  if (app != NULL)
    {
      g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

static void
on_app_activated (GtkListBox *list, GtkListBoxRow *row, MockaClassicView *self)
{
  MockaMenuApp *app = g_object_get_data (G_OBJECT (row), "app");

  if (app != NULL)
    {
      g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

static void
show_apps (MockaClassicView *self, GPtrArray *apps)
{
  GtkAdjustment *adjustment;

  gtk_container_foreach (GTK_CONTAINER (self->app_list), destroy_row, NULL);

  for (guint i = 0; apps != NULL && i < apps->len; i++)
    {
      GtkWidget *row = make_app_row (g_ptr_array_index (apps, i), self);

      gtk_container_add (GTK_CONTAINER (self->app_list), row);
    }

  self->app_count = apps != NULL ? apps->len : 0;
  gtk_widget_show_all (self->app_list);

  /* A new category starts at the top of its list. */
  adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->app_scroller));
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
        {
          return app;
        }
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
  row = make_category_row (_ ("All"), NULL);
  gtk_container_add (GTK_CONTAINER (self->category_list), row);

  categories = mocka_menu_data_get_categories (self->data);
  for (guint i = 0; i < categories->len; i++)
    {
      MockaMenuCategory *category = g_ptr_array_index (categories, i);

      row = make_category_row (mocka_menu_category_get_name (category), mocka_menu_category_get_icon (category));
      g_object_set_data_full (G_OBJECT (row), "category", g_object_ref (category), g_object_unref);
      gtk_container_add (GTK_CONTAINER (self->category_list), row);
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

      row = make_category_row (_ ("Settings"), settings_icon);
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
  gtk_widget_show_all (self->category_list);
  if (settings != NULL)
    {
      gtk_widget_show_all (self->settings_list);
    }
  mocka_classic_view_reset (self);
}

void
mocka_classic_view_reset (MockaClassicView *self)
{
  GtkListBoxRow *first;
  GtkAdjustment *adjustment;

  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  /* Search empty with no cursor in it, "All" selected and holding the focus,
   * scrolled to the top (SPEC section 4). */
  gtk_entry_set_text (GTK_ENTRY (self->entry), "");
  gtk_widget_set_sensitive (self->category_scroller, TRUE);
  self->saved_category = -1;

  first = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->category_list), 0);
  if (first != NULL)
    {
      gtk_list_box_select_row (GTK_LIST_BOX (self->category_list), first);
      gtk_widget_grab_focus (GTK_WIDGET (first));
    }

  show_apps (self, mocka_menu_data_get_all_apps (self->data));

  adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->category_scroller));
  gtk_adjustment_set_value (adjustment, gtk_adjustment_get_lower (adjustment));
}

static void
mocka_classic_view_init (MockaClassicView *self)
{
  GtkWidget *placeholder;

  /* The entry spans the width, above or below the two columns. */
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
  self->columns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  self->category_list = gtk_list_box_new ();
  self->saved_category = -1;

  /* Single rather than browse, so the selection can be dropped while a search
   * is showing. Browse always keeps one. */
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->category_list), GTK_SELECTION_SINGLE);

  self->category_scroller = gtk_scrolled_window_new (NULL, NULL);
  /* A scrollbar only when it does not fit (SPEC section 6). */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->category_scroller), GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  /*
   * The categories ask for their whole width and height, so the column is as
   * wide as the longest name and tall enough to show every category. Either
   * is given up, with a scrollbar or an ellipsis, only when the monitor
   * cannot fit it.
   */
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->category_scroller), TRUE);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (self->category_scroller), TRUE);
  gtk_container_add (GTK_CONTAINER (self->category_scroller), self->category_list);

  self->app_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->app_list), GTK_SELECTION_BROWSE);

  self->app_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->app_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (self->app_scroller), self->app_list);

  /* Shown by the list itself whenever it holds nothing (SPEC section 9.1). */
  placeholder = gtk_label_new (_ ("No results"));
  gtk_widget_set_sensitive (placeholder, FALSE);
  gtk_widget_show (placeholder);
  gtk_list_box_set_placeholder (GTK_LIST_BOX (self->app_list), placeholder);

  /* Categories, then a line, then Settings: outside the scrolled list so it
   * stays put and stays usable. */
  self->settings_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->settings_list), GTK_SELECTION_NONE);

  self->left_column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (self->left_column), self->category_scroller, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (self->left_column), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->left_column), self->settings_list, FALSE, FALSE, 0);

  /* Leftmost in both layouts (SPEC section 8). */
  self->favourites_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->favourites_list), GTK_SELECTION_NONE);

  self->favourites_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->favourites_scroller), GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->favourites_scroller), TRUE);
  gtk_widget_set_no_show_all (self->favourites_scroller, TRUE);
  gtk_container_add (GTK_CONTAINER (self->favourites_scroller), self->favourites_list);

  gtk_drag_dest_set (self->favourites_list, GTK_DEST_DEFAULT_ALL, drop_targets, G_N_ELEMENTS (drop_targets),
                     APP_DRAG_ACTIONS);
  g_signal_connect (self->favourites_list, "drag-data-received", G_CALLBACK (on_favourites_drop), self);
  g_signal_connect (self->favourites_list, "row-activated", G_CALLBACK (on_favourite_activated), self);

  build_session_buttons (self);

  /* Favourites above, session controls at the bottom (SPEC sections 8 and 10). */
  self->favourites_column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (self->favourites_column), self->favourites_scroller, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (self->favourites_column), self->session_box, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (self->columns), self->favourites_column, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), self->left_column, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), self->app_scroller, TRUE, TRUE, 0);

  self->entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->entry), _ ("Search"));

  gtk_box_pack_start (GTK_BOX (self), self->entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self), self->columns, TRUE, TRUE, 0);

  g_signal_connect (self->entry, "search-changed", G_CALLBACK (on_search_changed), self);
  g_signal_connect (self, "key-press-event", G_CALLBACK (on_key_press), self);
  g_signal_connect (self, "hierarchy-changed", G_CALLBACK (on_hierarchy_changed), self);

  gtk_widget_add_events (self->category_list, GDK_POINTER_MOTION_MASK);
  g_signal_connect (self->category_list, "motion-notify-event", G_CALLBACK (on_category_motion), self);
  g_signal_connect (self->category_list, "row-selected", G_CALLBACK (on_category_selected), self);
  g_signal_connect (self->settings_list, "row-activated", G_CALLBACK (on_category_activated), self);
  g_signal_connect (self->app_list, "row-activated", G_CALLBACK (on_app_activated), self);
}

static void
mocka_classic_view_dispose (GObject *object)
{
  MockaClassicView *self = MOCKA_CLASSIC_VIEW (object);

  if (self->drag_window != NULL)
    {
      g_object_remove_weak_pointer (G_OBJECT (self->drag_window), (gpointer *)&self->drag_window);
      self->drag_window = NULL;
    }

  g_clear_object (&self->session);

  G_OBJECT_CLASS (mocka_classic_view_parent_class)->dispose (object);
}

static void
mocka_classic_view_class_init (MockaClassicViewClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = mocka_classic_view_dispose;

  view_signals[SIGNAL_APP_ACTIVATED] = g_signal_new ("app-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                                     NULL, NULL, NULL, G_TYPE_NONE, 1, MOCKA_TYPE_MENU_APP);
}

void
mocka_classic_view_set_rollover (MockaClassicView *self, gboolean rollover)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));
  self->rollover = rollover;
}

void
mocka_classic_view_set_search_position (MockaClassicView *self, MockaClassicSearchPosition position)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  gtk_box_reorder_child (GTK_BOX (self), self->entry, position == MOCKA_CLASSIC_SEARCH_BOTTOM ? 1 : 0);
}

gboolean
mocka_classic_view_is_searching (MockaClassicView *self)
{
  const gchar *text;

  g_return_val_if_fail (MOCKA_IS_CLASSIC_VIEW (self), FALSE);

  text = gtk_entry_get_text (GTK_ENTRY (self->entry));
  return text != NULL && *text != '\0';
}

void
mocka_classic_view_clear_search (MockaClassicView *self)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  gtk_entry_set_text (GTK_ENTRY (self->entry), "");
  focus_list (self, GDK_KEY_Left);
}

GtkWidget *
mocka_classic_view_new (MockaMenuData *data, MockaFavourites *favourites)
{
  MockaClassicView *self = g_object_new (MOCKA_TYPE_CLASSIC_VIEW, NULL);

  self->data = data;
  self->favourites = favourites;

  if (favourites != NULL)
    {
      g_signal_connect_swapped (favourites, "changed", G_CALLBACK (on_favourites_changed), self);
    }
  show_favourites (self);
  mocka_classic_view_rebuild (self);

  return GTK_WIDGET (self);
}
