/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include "favourites.h"
#include "menu-data.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * One app as a widget, in the three shapes the layouts need, each already
 * carrying the drag source (SPEC sections 8 and 13) and the app menu
 * (SPEC section 11.1).
 */

/* Icon and name, for the Classic app list. */
GtkWidget *mocka_app_item_new_row (MockaMenuApp *app, MockaFavourites *favourites);
/* Icon alone, for the favourites column, at the size the layout wants. */
GtkWidget *mocka_app_item_new_icon (MockaMenuApp *app, MockaFavourites *favourites, gint pixel_size);
/* Large icon with the name below, for the Launcher grid. */
GtkWidget *mocka_app_item_new_tile (MockaMenuApp *app, MockaFavourites *favourites);

/* The app a widget made here stands for, or NULL for anything else. */
MockaMenuApp *mocka_app_item_get_app (GtkWidget *widget);

/* The room a tile asks for, so a grid can work out how many of them fit. */
GtkRequisition mocka_app_item_tile_size (void);

/* What the drag carries, so a column can accept a drop from a list. */
#define MOCKA_APP_ITEM_TARGET "application/x-mocka-menu-app"
const GtkTargetEntry *mocka_app_item_drop_targets (guint *n_targets);
GdkDragAction mocka_app_item_drag_actions (void);
/* The ID in a drop, or NULL when the data is not ours. Free with g_free. */
gchar *mocka_app_item_id_from_drop (GtkSelectionData *selection);

/*
 * The one drag that can be running, since there is one pointer.
 *
 * Rebuilding a list destroys the row being dragged, and GTK then never emits
 * drag-end for it, so the menu never takes back the grab the drag broke.
 * Whatever rebuilds waits for "finished" instead.
 */
#define MOCKA_TYPE_APP_DRAG (mocka_app_drag_get_type ())
G_DECLARE_FINAL_TYPE (MockaAppDrag, mocka_app_drag, MOCKA, APP_DRAG, GObject)

MockaAppDrag *mocka_app_drag_get (void);
gboolean mocka_app_drag_is_running (void);

/* The icon to draw, swapped for a generic one only when the theme has none of
 * the names this icon carries (SPEC section 16). Caller owns the reference. */
GIcon *mocka_icon_to_draw (GIcon *icon, const gchar *generic);

G_END_DECLS
