/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * The Classic layout (SPEC section 6): the favourites column, the category
 * list and the app list side by side, with the search entry across the top or
 * the bottom. The first two are shared with the Launcher and live in their own
 * modules, so what is left here is the arrangement, the app list and the
 * keyboard.
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "app-item.h"
#include "category-list.h"
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

struct _MockaClassicView
{
  GtkBox parent_instance;

  MockaMenuData *data;         /* not owned */
  MockaFavourites *favourites; /* not owned */

  GtkWidget *entry;
  GtkWidget *columns;
  GtkWidget *categories;
  GtkWidget *app_list;
  GtkWidget *app_scroller;
  GtkWidget *column; /* favourites above the session controls */

  guint app_count;     /* rows in app_list, for keyboard navigation */
  gulong toplevel_key; /* handler on the window, so the keys come first */
  gboolean search_pending;
};

G_DEFINE_TYPE (MockaClassicView, mocka_classic_view, GTK_TYPE_BOX)

enum
{
  SIGNAL_APP_ACTIVATED,
  N_VIEW_SIGNALS
};

static guint view_signals[N_VIEW_SIGNALS];

static void show_apps (MockaClassicView *self, GPtrArray *apps);
static void on_search_changed (GtkSearchEntry *entry, MockaClassicView *self);

static void
emit_activated (MockaClassicView *self, MockaMenuApp *app)
{
  if (app != NULL)
    {
      g_signal_emit (self, view_signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

static void
destroy_row (GtkWidget *widget, gpointer user_data)
{
  gtk_widget_destroy (widget);
}

static void
show_apps (MockaClassicView *self, GPtrArray *apps)
{
  GtkAdjustment *adjustment;

  gtk_container_foreach (GTK_CONTAINER (self->app_list), destroy_row, NULL);

  for (guint i = 0; apps != NULL && i < apps->len; i++)
    {
      gtk_container_add (GTK_CONTAINER (self->app_list),
                         mocka_app_item_new_row (g_ptr_array_index (apps, i), self->favourites));
    }

  self->app_count = apps != NULL ? apps->len : 0;
  gtk_widget_show_all (self->app_list);

  /* A new category starts at the top of its list. */
  adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->app_scroller));
  gtk_adjustment_set_value (adjustment, gtk_adjustment_get_lower (adjustment));
}

static void
on_category_selected (MockaCategoryList *categories, MockaClassicView *self)
{
  /* While there is search text the category selection is ignored
   * (SPEC section 6). */
  if (mocka_classic_view_is_searching (self))
    {
      return;
    }

  show_apps (self, mocka_category_list_get_apps (categories));
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
  GPtrArray *results;

  mocka_category_list_set_searching (MOCKA_CATEGORY_LIST (self->categories), searching);

  if (!searching)
    {
      show_apps (self, mocka_category_list_get_apps (MOCKA_CATEGORY_LIST (self->categories)));
      return;
    }

  /* Favourites come first within a rank (SPEC section 9.2). */
  results = mocka_search_run (mocka_menu_data_get_all_apps (self->data), text,
                              self->favourites != NULL ? mocka_favourites_get_ids (self->favourites) : NULL);
  show_apps (self, results);
  g_ptr_array_unref (results);
}

/*
 * A favourite changing reranks a showing search, but rebuilding the list
 * during a drag would destroy the row being dragged, and GTK then never emits
 * drag-end for it. The rerun waits for the drag to finish.
 */
static void
rerun_search (MockaClassicView *self)
{
  self->search_pending = FALSE;

  if (mocka_classic_view_is_searching (self))
    {
      on_search_changed (GTK_SEARCH_ENTRY (self->entry), self);
    }
}

static void
on_favourites_changed (MockaClassicView *self)
{
  if (mocka_app_drag_is_running ())
    {
      self->search_pending = TRUE;
      return;
    }

  rerun_search (self);
}

static void
on_drag_finished (MockaClassicView *self)
{
  if (self->search_pending)
    {
      rerun_search (self);
    }
}

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

  /* Enter launches the first result when nothing was picked (SPEC section 9.1). */
  if (row == NULL)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->app_list), 0);
    }
  if (row == NULL)
    {
      return;
    }

  emit_activated (self, mocka_app_item_get_app (GTK_WIDGET (row)));
}

/* The rows take the focus, not the box around them. */
static gboolean
focus_app_list (MockaClassicView *self)
{
  GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->app_list));

  if (row == NULL)
    {
      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->app_list), 0);
    }
  if (row == NULL)
    {
      return FALSE;
    }

  gtk_list_box_select_row (GTK_LIST_BOX (self->app_list), row);
  gtk_widget_grab_focus (GTK_WIDGET (row));

  return TRUE;
}

/* Left and Right move between the two lists (SPEC section 12.2). */
static gboolean
focus_list (MockaClassicView *self, guint keyval)
{
  if (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left)
    {
      return mocka_category_list_grab_focus_on_row (MOCKA_CATEGORY_LIST (self->categories));
    }

  if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right)
    {
      return focus_app_list (self);
    }

  return FALSE;
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

/*
 * Typing anywhere in the menu goes to the search entry (SPEC section 9.1).
 * Key events reach the focused widget first and then travel up to here, so
 * this only sees what the lists did not use, such as ordinary text.
 */
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

static void
on_app_activated (GtkListBox *list, GtkListBoxRow *row, MockaClassicView *self)
{
  emit_activated (self, mocka_app_item_get_app (GTK_WIDGET (row)));
}

static void
on_child_app_activated (MockaClassicView *self, MockaMenuApp *app)
{
  emit_activated (self, app);
}

void
mocka_classic_view_rebuild (MockaClassicView *self)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  mocka_category_list_rebuild (MOCKA_CATEGORY_LIST (self->categories));
  mocka_classic_view_reset (self);
}

void
mocka_classic_view_reset (MockaClassicView *self)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));

  /* Search empty with no cursor in it, "All" selected and holding the focus,
   * scrolled to the top (SPEC section 4). */
  gtk_entry_set_text (GTK_ENTRY (self->entry), "");
  mocka_category_list_reset (MOCKA_CATEGORY_LIST (self->categories), TRUE);

  show_apps (self, mocka_menu_data_get_all_apps (self->data));
}

static void
mocka_classic_view_init (MockaClassicView *self)
{
  GtkWidget *placeholder;

  /* The entry spans the width, above or below the columns. */
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
  self->columns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

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

  self->entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->entry), _ ("Search"));

  gtk_box_pack_start (GTK_BOX (self), self->entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self), self->columns, TRUE, TRUE, 0);

  g_signal_connect (self->entry, "search-changed", G_CALLBACK (on_search_changed), self);
  g_signal_connect (self, "key-press-event", G_CALLBACK (on_key_press), self);
  g_signal_connect (self, "hierarchy-changed", G_CALLBACK (on_hierarchy_changed), self);
  g_signal_connect (self->app_list, "row-activated", G_CALLBACK (on_app_activated), self);
}

static void
mocka_classic_view_class_init (MockaClassicViewClass *klass)
{
  view_signals[SIGNAL_APP_ACTIVATED] = g_signal_new ("app-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                                     NULL, NULL, NULL, G_TYPE_NONE, 1, MOCKA_TYPE_MENU_APP);
}

void
mocka_classic_view_set_rollover (MockaClassicView *self, gboolean rollover)
{
  g_return_if_fail (MOCKA_IS_CLASSIC_VIEW (self));
  mocka_category_list_set_rollover (MOCKA_CATEGORY_LIST (self->categories), rollover);
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
  GtkWidget *session = mocka_session_bar_new (data);

  self->data = data;
  self->favourites = favourites;

  /* The Settings shortcut lives there now (SPEC section 10). */
  g_signal_connect_swapped (session, "app-activated", G_CALLBACK (on_child_app_activated), self);

  /* Favourites above, session controls at the bottom (SPEC sections 8 and 10). */
  self->column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  if (favourites != NULL)
    {
      GtkWidget *column = mocka_favourites_column_new (favourites);

      gtk_box_pack_start (GTK_BOX (self->column), column, TRUE, TRUE, 0);
      g_signal_connect_swapped (column, "app-activated", G_CALLBACK (on_child_app_activated), self);
      g_signal_connect_swapped (favourites, "changed", G_CALLBACK (on_favourites_changed), self);
      g_signal_connect_object (mocka_app_drag_get (), "finished", G_CALLBACK (on_drag_finished), self,
                               G_CONNECT_SWAPPED);
    }
  gtk_box_pack_end (GTK_BOX (self->column), session, FALSE, FALSE, 0);

  self->categories = mocka_category_list_new (data, GTK_ORIENTATION_VERTICAL);
  g_signal_connect (self->categories, "category-selected", G_CALLBACK (on_category_selected), self);

  gtk_box_pack_start (GTK_BOX (self->columns), self->column, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), self->categories, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->columns), self->app_scroller, TRUE, TRUE, 0);

  mocka_classic_view_reset (self);

  return GTK_WIDGET (self);
}