/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

#include "menu-data.h"

G_BEGIN_DECLS

/* The Classic layout (SPEC section 6). */

#define MOCKA_TYPE_CLASSIC_VIEW (mocka_classic_view_get_type ())
G_DECLARE_FINAL_TYPE (MockaClassicView, mocka_classic_view, MOCKA, CLASSIC_VIEW,
                      GtkBox)

/* The category list and the app list. Does not own the data. */
GtkWidget *mocka_classic_view_new     (MockaMenuData    *data);

/* Back to "All", scrolled to the top, nothing selected (SPEC section 4). */
void       mocka_classic_view_reset   (MockaClassicView *self);

/* Rebuilt from the data, keeping nothing. */
void       mocka_classic_view_rebuild (MockaClassicView *self);

/* Hovering a category selects it, off by default (SPEC section 6). */
void       mocka_classic_view_set_rollover (MockaClassicView *self,
                                            gboolean          rollover);

/* The size the menu asks for before the monitor is taken into account. */
#define MOCKA_CLASSIC_WANT_WIDTH  480
#define MOCKA_CLASSIC_WANT_HEIGHT 480

/* It never shrinks below this, even on a very small monitor. */
#define MOCKA_CLASSIC_MIN_WIDTH   240
#define MOCKA_CLASSIC_MIN_HEIGHT  240

/*
 * Where the menu window goes: next to the panel button, on the side facing
 * the screen, kept inside the monitor holding the button, and reduced when
 * the monitor is too small (SPEC section 6).
 *
 * panel_side is the screen edge the panel sits on, so a bottom panel opens
 * the menu upwards and a left panel opens it to the right.
 *
 * Pure geometry, so it is tested without a panel or a display.
 */
GdkRectangle mocka_classic_view_place (const GdkRectangle *anchor,
                                       const GdkRectangle *monitor,
                                       GtkPositionType     panel_side,
                                       gint                want_width,
                                       gint                want_height);

G_END_DECLS
