/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

/*
 * The menu tree read through libmate-menu: the apps, the categories, and
 * "All" (SPEC section 5). The user's own menu edits are respected, because
 * they are part of the tree libmate-menu builds.
 */

#define MOCKA_TYPE_MENU_APP (mocka_menu_app_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuApp, mocka_menu_app, MOCKA, MENU_APP, GObject)

const gchar     *mocka_menu_app_get_id       (MockaMenuApp *self);
const gchar     *mocka_menu_app_get_name     (MockaMenuApp *self);
const gchar     *mocka_menu_app_get_comment  (MockaMenuApp *self);
GIcon           *mocka_menu_app_get_icon     (MockaMenuApp *self);
GDesktopAppInfo *mocka_menu_app_get_app_info (MockaMenuApp *self);

#define MOCKA_TYPE_MENU_CATEGORY (mocka_menu_category_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuCategory, mocka_menu_category, MOCKA, MENU_CATEGORY,
                      GObject)

const gchar *mocka_menu_category_get_id   (MockaMenuCategory *self);
const gchar *mocka_menu_category_get_name (MockaMenuCategory *self);
GIcon       *mocka_menu_category_get_icon (MockaMenuCategory *self);
/* MockaMenuApp*, sorted by displayed name in the user's locale. */
GPtrArray   *mocka_menu_category_get_apps (MockaMenuCategory *self);

#define MOCKA_TYPE_MENU_DATA (mocka_menu_data_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuData, mocka_menu_data, MOCKA, MENU_DATA, GObject)

/* The menus the desktop ships: applications, then settings. */
MockaMenuData *mocka_menu_data_new            (void);
/* For the unit tests, which load a tree of their own. */
MockaMenuData *mocka_menu_data_new_for_menus  (const gchar * const *basenames);

gboolean   mocka_menu_data_load           (MockaMenuData *self,
                                           GError       **error);
/* MockaMenuCategory*, in menu order. */
GPtrArray *mocka_menu_data_get_categories (MockaMenuData *self);
/* Every app once, sorted by displayed name: the "All" category. */
GPtrArray *mocka_menu_data_get_all_apps   (MockaMenuData *self);

G_END_DECLS
