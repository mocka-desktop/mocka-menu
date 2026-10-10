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

/* The app menu, on a right click on an app (SPEC section 11.1).
 *
 * over is the row it belongs to, and event may be NULL for the Menu key. The
 * menu destroys itself once it is dismissed. */
void mocka_app_menu_popup (MockaMenuApp *app, MockaFavourites *favourites, GtkWidget *over, const GdkEvent *event);

G_END_DECLS