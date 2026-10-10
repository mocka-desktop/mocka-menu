/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * The window the menu opens in, shared by both layouts (SPEC section 4): when
 * it opens and closes, the grabs it holds while open, the state it starts
 * from every time, and its window type. What goes inside it is the layout's
 * business, packed into the content area.
 */

#define MOCKA_TYPE_MENU_WINDOW (mocka_menu_window_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuWindow, mocka_menu_window, MOCKA, MENU_WINDOW, GtkWindow)

GtkWidget *mocka_menu_window_new (void);

/* Which screen edge the panel sits on, so the menu opens away from it. */
void mocka_menu_window_set_panel_side (MockaMenuWindow *self, GtkPositionType side);

/* Anchor is the panel button the menu belongs to. */
void mocka_menu_window_open (MockaMenuWindow *self, GtkWidget *anchor);
void mocka_menu_window_close (MockaMenuWindow *self);
void mocka_menu_window_toggle (MockaMenuWindow *self, GtkWidget *anchor);
gboolean mocka_menu_window_is_open (MockaMenuWindow *self);

/* NULL when the widget's window is something else, as in a test harness. */
MockaMenuWindow *mocka_menu_window_of (GtkWidget *widget);

/* A drag or popup menu of ours takes the pointer, breaking the menu's grab.
 * Bracket it with these so that does not close the menu. They nest. */
void mocka_menu_window_begin_grab_handover (MockaMenuWindow *self);
void mocka_menu_window_end_grab_handover (MockaMenuWindow *self);

/* Where the layout packs itself. */
GtkWidget *mocka_menu_window_get_content_area (MockaMenuWindow *self);

G_END_DECLS
