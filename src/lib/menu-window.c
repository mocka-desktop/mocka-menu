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

#include "menu-window.h"

/*
 * A click on the panel button arrives as a click outside the window, which
 * closes it, and then as the button's own click, which would open it again.
 * Toggles this soon after such a close are the same click, so they are
 * ignored and the menu stays closed.
 */
#define REOPEN_GUARD_MS 300

/* A starting size, until the placement rules of SPEC section 6 refine it. */
#define DEFAULT_WIDTH  480
#define DEFAULT_HEIGHT 480

struct _MockaMenuWindow
{
  GtkWindow parent_instance;

  GtkWidget *content_area;
  GdkSeat   *held_seat;     /* while open, the seat we grabbed */
  GtkWidget *anchor;        /* the panel button, not owned */
  gboolean   open;
  guint32    closed_at;     /* event time of the last close on the anchor */
};

G_DEFINE_TYPE (MockaMenuWindow, mocka_menu_window, GTK_TYPE_WINDOW)

enum {
  SIGNAL_OPENED,
  SIGNAL_CLOSED,
  /* Emitted before the window is shown, so the layout can start clean. */
  SIGNAL_RESET,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Whether a click at these root coordinates landed on the panel button. */
static gboolean
click_was_on_anchor (MockaMenuWindow *self, gint root_x, gint root_y)
{
  GdkWindow *window;
  GtkAllocation alloc;
  gint origin_x, origin_y;

  if (self->anchor == NULL || !gtk_widget_get_realized (self->anchor))
    return FALSE;

  window = gtk_widget_get_window (self->anchor);
  if (window == NULL)
    return FALSE;

  gdk_window_get_origin (window, &origin_x, &origin_y);
  gtk_widget_get_allocation (self->anchor, &alloc);
  origin_x += alloc.x;
  origin_y += alloc.y;

  return root_x >= origin_x && root_x < origin_x + alloc.width
      && root_y >= origin_y && root_y < origin_y + alloc.height;
}

static gboolean
on_button_press (GtkWidget      *widget,
                 GdkEventButton *event,
                 gpointer        user_data)
{
  MockaMenuWindow *self = MOCKA_MENU_WINDOW (widget);
  gint width, height;

  gtk_window_get_size (GTK_WINDOW (self), &width, &height);

  /* The grab sends clicks elsewhere here, with coordinates outside us. */
  if (event->x < 0 || event->y < 0 || event->x > width || event->y > height)
    {
      if (click_was_on_anchor (self, (gint) event->x_root, (gint) event->y_root))
        self->closed_at = event->time;

      mocka_menu_window_close (self);
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

static gboolean
on_key_press (GtkWidget   *widget,
              GdkEventKey *event,
              gpointer     user_data)
{
  /*
   * Escape closes. Once there is a search entry it clears the search first
   * and only closes when the search is already empty (SPEC section 12.2).
   */
  if (event->keyval == GDK_KEY_Escape)
    {
      mocka_menu_window_close (MOCKA_MENU_WINDOW (widget));
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

/* Something else took the pointer or the keyboard, so we are no longer a menu. */
static gboolean
on_grab_broken (GtkWidget          *widget,
                GdkEventGrabBroken *event,
                gpointer            user_data)
{
  MockaMenuWindow *self = MOCKA_MENU_WINDOW (widget);

  self->held_seat = NULL;  /* the grab is already gone */
  mocka_menu_window_close (self);
  return GDK_EVENT_PROPAGATE;
}

static void
mocka_menu_window_init (MockaMenuWindow *self)
{
  GtkWindow *window = GTK_WINDOW (self);

  gtk_window_set_decorated (window, FALSE);
  gtk_window_set_resizable (window, FALSE);
  /* A non-resizable window takes its size from the content, so the starting
   * size has to be a request rather than a default. */
  gtk_widget_set_size_request (GTK_WIDGET (self), DEFAULT_WIDTH, DEFAULT_HEIGHT);
  /* Never in taskbars, pagers, or window switchers (SPEC section 4). */
  gtk_window_set_skip_taskbar_hint (window, TRUE);
  gtk_window_set_skip_pager_hint (window, TRUE);
  gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_POPUP_MENU);

  gtk_widget_add_events (GTK_WIDGET (self),
                         GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);

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
  signals[SIGNAL_OPENED] =
    g_signal_new ("opened", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_CLOSED] =
    g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SIGNAL_RESET] =
    g_signal_new ("reset", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
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

/* Rough for now: the placement rules of SPEC section 6 are the next step. */
static void
place_near_anchor (MockaMenuWindow *self, GtkWidget *anchor)
{
  GdkWindow *window;
  GtkAllocation alloc;
  GtkRequisition size;
  gint origin_x = 0, origin_y = 0;

  if (anchor == NULL || (window = gtk_widget_get_window (anchor)) == NULL)
    return;

  gdk_window_get_origin (window, &origin_x, &origin_y);
  gtk_widget_get_allocation (anchor, &alloc);
  gtk_widget_get_preferred_size (GTK_WIDGET (self), NULL, &size);

  gtk_window_move (GTK_WINDOW (self),
                   origin_x + alloc.x,
                   origin_y + alloc.y - size.height);
}

void
mocka_menu_window_open (MockaMenuWindow *self, GtkWidget *anchor)
{
  GdkSeat *seat;
  GdkGrabStatus status;

  g_return_if_fail (MOCKA_IS_MENU_WINDOW (self));

  if (self->open)
    return;

  self->anchor = anchor;

  /* Same state every time it opens (SPEC section 4). */
  g_signal_emit (self, signals[SIGNAL_RESET], 0);

  gtk_widget_show (GTK_WIDGET (self));
  place_near_anchor (self, anchor);

  seat = gdk_display_get_default_seat (gtk_widget_get_display (GTK_WIDGET (self)));
  status = gdk_seat_grab (seat, gtk_widget_get_window (GTK_WIDGET (self)),
                          GDK_SEAT_CAPABILITY_ALL, TRUE, NULL, NULL, NULL, NULL);
  if (status == GDK_GRAB_SUCCESS)
    self->held_seat = seat;
  else
    g_warning ("mocka-menu: could not grab the pointer and keyboard");

  self->open = TRUE;
  g_signal_emit (self, signals[SIGNAL_OPENED], 0);
}

void
mocka_menu_window_close (MockaMenuWindow *self)
{
  g_return_if_fail (MOCKA_IS_MENU_WINDOW (self));

  if (!self->open)
    return;

  if (self->held_seat != NULL)
    {
      gdk_seat_ungrab (self->held_seat);
      self->held_seat = NULL;
    }

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
  if (self->closed_at != 0
      && gtk_get_current_event_time () - self->closed_at < REOPEN_GUARD_MS)
    {
      self->closed_at = 0;
      return;
    }

  mocka_menu_window_open (self, anchor);
}
