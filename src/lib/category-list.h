/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include "menu-data.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* The category list both layouts show (SPEC section 6): "All" first, then the
 * menu tree's categories in order. "category-selected" says to show its apps. */
#define MOCKA_TYPE_CATEGORY_LIST (mocka_category_list_get_type ())
G_DECLARE_FINAL_TYPE (MockaCategoryList, mocka_category_list, MOCKA, CATEGORY_LIST, GtkBox)

/* The menu data is not owned and must outlive the list. Horizontal lays the
 * categories out in one row, which is what the Launcher wants. */
GtkWidget *mocka_category_list_new (MockaMenuData *data, GtkOrientation orientation);

/* Reads the menu tree again, then selects "All". */
void mocka_category_list_rebuild (MockaCategoryList *self);

/* "All" selected, scrolled to the top, holding the keyboard focus. */
void mocka_category_list_reset (MockaCategoryList *self, gboolean take_focus);

/* The apps of the selected category, or every app for "All". */
GPtrArray *mocka_category_list_get_apps (MockaCategoryList *self);

/* Hovering selects a category (SPEC section 6). Off by default. */
void mocka_category_list_set_rollover (MockaCategoryList *self, gboolean rollover);

/*
 * Greyed while a search is showing, since the selection counts for nothing
 * until the search is cleared. The selection is put back on the way out.
 */
void mocka_category_list_set_searching (MockaCategoryList *self, gboolean searching);

/* For the layouts' own keyboard handling (SPEC section 12.2). */
gboolean mocka_category_list_grab_focus_on_row (MockaCategoryList *self);
gboolean mocka_category_list_is_sensitive (MockaCategoryList *self);

G_END_DECLS