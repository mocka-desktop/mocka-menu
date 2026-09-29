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
G_DECLARE_FINAL_TYPE (MockaMenuWindow, mocka_menu_window, MOCKA, MENU_WINDOW,
                      GtkWindow)

GtkWidget *mocka_menu_window_new              (void);

/* Anchor is the panel button the menu belongs to. */
void       mocka_menu_window_open             (MockaMenuWindow *self,
                                               GtkWidget       *anchor);
void       mocka_menu_window_close            (MockaMenuWindow *self);
void       mocka_menu_window_toggle           (MockaMenuWindow *self,
                                               GtkWidget       *anchor);
gboolean   mocka_menu_window_is_open          (MockaMenuWindow *self);

/* Where the layout packs itself. */
GtkWidget *mocka_menu_window_get_content_area (MockaMenuWindow *self);

G_END_DECLS
