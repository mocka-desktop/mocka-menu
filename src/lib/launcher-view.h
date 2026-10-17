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

/* The Launcher layout (SPEC section 7): the same favourites column and
 * category list as Classic, with an app grid and the search at the top. */

#define MOCKA_TYPE_LAUNCHER_VIEW (mocka_launcher_view_get_type ())
G_DECLARE_FINAL_TYPE (MockaLauncherView, mocka_launcher_view, MOCKA, LAUNCHER_VIEW, GtkEventBox)

/* Owns neither the menu nor the favourites, and both must outlive it. */
GtkWidget *mocka_launcher_view_new (MockaMenuData *data, MockaFavourites *favourites);

/* "All", scrolled to the top, nothing searched (SPEC section 4). */
void mocka_launcher_view_reset (MockaLauncherView *self);
void mocka_launcher_view_rebuild (MockaLauncherView *self);

gboolean mocka_launcher_view_is_searching (MockaLauncherView *self);
void mocka_launcher_view_clear_search (MockaLauncherView *self);

G_END_DECLS