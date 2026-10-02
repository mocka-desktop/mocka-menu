/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * SPEC section 4. The window behaves like a popup menu: it takes the pointer
 * and the keyboard while it is open and gives them back when it closes, and
 * it starts from the same state every time it opens.
 *
 * Where it is placed, and what it holds, belong to the layouts.
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "classic-view.h"
#include "menu-window.h"

/*
 * A click on the panel button arrives as a click outside the window, which
 * closes it, and then as the button's own click, which would open it again.
 * Toggles this soon after such a close are the same click, so they are
 * ignored and the menu stays closed.
 */
#define REOPEN_GUARD_MS 300

struct _MockaMenuWindow
{
  GtkWindow parent_instance;

  GtkWidget *content_area;
  GdkSeat *held_seat; /* while open, the seat we grabbed */
  GtkWidget *anchor;  /* the panel button, not owned */
  GtkPositionType panel_side;
  gboolean open;
  guint32 closed_at; /* event time of the last close on the anchor */
};

G_DEFINE_TYPE (MockaMenuWindow, mocka_menu_window, GTK_TYPE_WINDOW)

enum
{
  SIGNAL_OPENED,
  SIGNAL_CLOSED,
  /* Emitted before the window is shown, so the layout can start clean. */
  SIGNAL_RESET,
  /*
   * Emitted on Escape. A handler that returns TRUE has dealt with it, by
   * clearing the search for instance, and the menu stays open. With nothing
   * to deal with, Escape closes it (SPEC section 12.2).
   */
  SIGNAL_ESCAPE,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/*
 * The widget's rectangle in root coordinates.
 *
 * A button has no window of its own, so gtk_widget_get_window() hands back an
 * ancestor's, and adding the allocation to that origin lands somewhere else
 * entirely. Translating to the toplevel first is what actually works.
 */
static gboolean
widget_root_rect (GtkWidget *widget, GdkRectangle *out)
{
  GtkWidget *toplevel;
  GdkWindow *window;
  GtkAllocation alloc;
  gint top_x = 0;
  gint top_y = 0;
  gint origin_x = 0;
  gint origin_y = 0;

  if (widget == NULL || !gtk_widget_get_realized (widget))
    {
      return FALSE;
    }

  toplevel = gtk_widget_get_toplevel (widget);
  if (toplevel == NULL)
    {
      return FALSE;
    }

  window = gtk_widget_get_window (toplevel);
  if (window == NULL)
    {
      return FALSE;
    }

  if (!gtk_widget_translate_coordinates (widget, toplevel, 0, 0, &top_x, &top_y))
    {
      return FALSE;
    }

  gdk_window_get_origin (window, &origin_x, &origin_y);
  gtk_widget_get_allocation (widget, &alloc);

  out->x = origin_x + top_x;
  out->y = origin_y + top_y;
  out->width = alloc.width;
  out->height = alloc.height;
  return TRUE;
}

/*
 * A click anywhere else closes the menu (SPEC section 4).
 *
 * The grab is taken with owner_events, so a press over another window of this
 * process, the panel button included, arrives here with its coordinates still
 * relative to that window rather than to this one. Only the root coordinates
 * mean the same thing in both, so the test has to be made in those.
 */
static gboolean
on_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  MockaMenuWindow *self = MOCKA_MENU_WINDOW (widget);
  GdkRectangle rect;
  gint root_x = (gint)event->x_root;
  gint root_y = (gint)event->y_root;

  if (!widget_root_rect (GTK_WIDGET (self), &rect))
    {
      return GDK_EVENT_PROPAGATE;
    }

  if (root_x >= rect.x && root_x < rect.x + rect.width && root_y >= rect.y && root_y < rect.y + rect.height)
    {
      return GDK_EVENT_PROPAGATE;
    }

  /* The same click may reach the panel button next, which would open the
   * menu again, so disarm that. */
  self->closed_at = event->time;
  mocka_menu_window_close (self);

  return GDK_EVENT_STOP;
}

static gboolean
on_key_press (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      gboolean handled = FALSE;

      /* The layout clears its search first, if it has one to clear. */
      g_signal_emit (widget, signals[SIGNAL_ESCAPE], 0, &handled);
      if (!handled)
        {
          mocka_menu_window_close (MOCKA_MENU_WINDOW (widget));
        }

      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

/* Something else took the pointer or the keyboard, so we are no longer a menu. */
static gboolean
on_grab_broken (GtkWidget *widget, GdkEventGrabBroken *event, gpointer user_data)
{
  MockaMenuWindow *self = MOCKA_MENU_WINDOW (widget);

  self->held_seat = NULL; /* the grab is already gone */
  mocka_menu_window_close (self);
  return GDK_EVENT_PROPAGATE;
}

static void
mocka_menu_window_init (MockaMenuWindow *self)
{
  GtkWindow *window = GTK_WINDOW (self);

  gtk_window_set_decorated (window, FALSE);
  gtk_window_set_resizable (window, FALSE);
  /* A bottom panel until the applet says otherwise: the common case. */
  self->panel_side = GTK_POS_BOTTOM;
  /* Never in taskbars, pagers, or window switchers (SPEC section 4). */
  gtk_window_set_skip_taskbar_hint (window, TRUE);
  gtk_window_set_skip_pager_hint (window, TRUE);
  gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_POPUP_MENU);

  gtk_widget_add_events (GTK_WIDGET (self), GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);

  self->content_area = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (self), self->content_area);
  gtk_widget_show (self->content_area);

  g_signal_connect (self, "button-press-event", G_CALLBACK (on_button_press), NULL);
  g_signal_connect (self, "key-press-event", G_CALLBACK (on_key_press), NULL);
  g_signal_connect (self, "grab-broken-event", G_CALLBACK (on_grab_broken), NULL);
}

static void
mocka_menu_window_class_init (MockaMenuWindowClass *klass)
{
  signals[SIGNAL_OPENED]
      = g_signal_new ("opened", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_CLOSED]
      = g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_RESET]
      = g_signal_new ("reset", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_ESCAPE] = g_signal_new ("escape", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                                         g_signal_accumulator_true_handled, NULL, NULL, G_TYPE_BOOLEAN, 0);
}

GtkWidget *
mocka_menu_window_new (void)
{
  /* A popup window, the type a menu uses, not a managed toplevel. */
  return g_object_new (MOCKA_TYPE_MENU_WINDOW, "type", GTK_WINDOW_POPUP, NULL);
}

GtkWidget *
mocka_menu_window_get_content_area (MockaMenuWindow *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_WINDOW (self), NULL);
  return self->content_area;
}

gboolean
mocka_menu_window_is_open (MockaMenuWindow *self)
{
  g_return_val_if_fail (MOCKA_IS_MENU_WINDOW (self), FALSE);
  return self->open;
}

void
mocka_menu_window_set_panel_side (MockaMenuWindow *self, GtkPositionType side)
{
  g_return_if_fail (MOCKA_IS_MENU_WINDOW (self));
  self->panel_side = side;
}

/* Next to the button, inside the monitor holding it (SPEC section 6). */
static void
place_near_anchor (MockaMenuWindow *self, GtkWidget *anchor)
{
  GdkRectangle anchor_rect;
  GdkRectangle monitor_rect;
  GdkRectangle placed;
  GdkDisplay *display;
  GdkMonitor *monitor;
  GtkRequisition natural;
  GtkRequisition want;

  if (!widget_root_rect (anchor, &anchor_rect))
    {
      return;
    }

  display = gtk_widget_get_display (anchor);
  monitor = gdk_display_get_monitor_at_window (display, gtk_widget_get_window (anchor));
  if (monitor == NULL)
    {
      monitor = gdk_display_get_primary_monitor (display);
    }
  if (monitor == NULL)
    {
      return;
    }

  gdk_monitor_get_geometry (monitor, &monitor_rect);

  /*
   * The height the contents want, so the whole category list shows without
   * scrolling when the monitor has room for it. The app list does not ask for
   * its full height, or hundreds of apps would want a window taller than any
   * screen; it scrolls instead.
   */
  gtk_widget_get_preferred_size (self->content_area, NULL, &natural);

  want.width = MAX (natural.width, MOCKA_CLASSIC_WANT_WIDTH);
  want.height = MAX (natural.height, MOCKA_CLASSIC_WANT_HEIGHT);
  placed = mocka_classic_view_place (&anchor_rect, &monitor_rect, self->panel_side, want);

  gtk_widget_set_size_request (GTK_WIDGET (self), placed.width, placed.height);
  gtk_window_resize (GTK_WINDOW (self), placed.width, placed.height);
  gtk_window_move (GTK_WINDOW (self), placed.x, placed.y);
}

void
mocka_menu_window_open (MockaMenuWindow *self, GtkWidget *anchor)
{
  GdkSeat *seat;
  GdkGrabStatus status;

  g_return_if_fail (MOCKA_IS_MENU_WINDOW (self));

  if (self->open)
    {
      return;
    }

  self->anchor = anchor;

  /* Same state every time it opens (SPEC section 4). */
  g_signal_emit (self, signals[SIGNAL_RESET], 0);

  /* Placed before it is shown, so it never appears in the wrong spot. */
  place_near_anchor (self, anchor);
  gtk_widget_show (GTK_WIDGET (self));

  /* The device grab uses owner_events, so clicks on the panel and its other
   * applets reach them, not us. A GTK grab sends those here too. */
  gtk_grab_add (GTK_WIDGET (self));

  seat = gdk_display_get_default_seat (gtk_widget_get_display (GTK_WIDGET (self)));
  status = gdk_seat_grab (seat, gtk_widget_get_window (GTK_WIDGET (self)), GDK_SEAT_CAPABILITY_ALL, TRUE, NULL, NULL,
                          NULL, NULL);
  if (status == GDK_GRAB_SUCCESS)
    {
      self->held_seat = seat;
    }
  else
    {
      g_warning ("mocka-menu: could not grab the pointer and keyboard");
    }

  self->open = TRUE;
  g_signal_emit (self, signals[SIGNAL_OPENED], 0);
}

void
mocka_menu_window_close (MockaMenuWindow *self)
{
  g_return_if_fail (MOCKA_IS_MENU_WINDOW (self));

  if (!self->open)
    {
      return;
    }

  if (self->held_seat != NULL)
    {
      gdk_seat_ungrab (self->held_seat);
      self->held_seat = NULL;
    }

  gtk_grab_remove (GTK_WIDGET (self));

  gtk_widget_hide (GTK_WIDGET (self));
  self->open = FALSE;
  g_signal_emit (self, signals[SIGNAL_CLOSED], 0);
}

void
mocka_menu_window_toggle (MockaMenuWindow *self, GtkWidget *anchor)
{
  g_return_if_fail (MOCKA_IS_MENU_WINDOW (self));

  if (self->open)
    {
      mocka_menu_window_close (self);
      return;
    }

  /* The click that just closed us is not a click to open us again. */
  if (self->closed_at != 0 && gtk_get_current_event_time () - self->closed_at < REOPEN_GUARD_MS)
    {
      self->closed_at = 0;
      return;
    }

  mocka_menu_window_open (self, anchor);
}
