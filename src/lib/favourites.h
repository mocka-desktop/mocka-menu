/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include "menu-data.h"

G_BEGIN_DECLS

/*
 * The favourites, stored as an ordered list of desktop entry IDs
 * (SPEC section 8).
 *
 * An ID whose application is not installed is skipped when the favourites are
 * listed, but kept in the list, so the favourite comes back when its
 * application does. Everything written back keeps those IDs.
 */

#define MOCKA_TYPE_FAVOURITES (mocka_favourites_get_type ())
G_DECLARE_FINAL_TYPE (MockaFavourites, mocka_favourites, MOCKA, FAVOURITES, GObject)

/* Neither the menu nor the settings are owned, and both must outlive this. */
MockaFavourites *mocka_favourites_new (MockaMenuData *data, GSettings *settings);

/* MockaMenuApp*, in the stored order, without the ones not installed. */
GPtrArray *mocka_favourites_get_apps (MockaFavourites *self);

/* Every stored ID, installed or not, for deciding what is a favourite. */
GHashTable *mocka_favourites_get_ids (MockaFavourites *self);

gboolean mocka_favourites_contains (MockaFavourites *self, const gchar *id);

/* position counts installed favourites; -1 puts it at the end. */
void mocka_favourites_add (MockaFavourites *self, const gchar *id, gint position);
void mocka_favourites_remove (MockaFavourites *self, const gchar *id);
void mocka_favourites_move (MockaFavourites *self, const gchar *id, gint position);

G_END_DECLS