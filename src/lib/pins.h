/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

/* Pinning an app outside the menu: Mocka Dock (SPEC section 13) and the user's
 * desktop folder (SPEC section 11.1). */

gboolean mocka_pins_dock_available (void);
gboolean mocka_pins_dock_contains (const gchar *id);
void mocka_pins_dock_add (const gchar *id);
void mocka_pins_dock_remove (const gchar *id);

/*
 * A file named after the app's entry is the app's launcher when it carries
 * X-Mocka-Menu-Copy-Of for this app, or, carrying no such key, when its Exec
 * is the app's own. Only such a file is removed, and the add refuses to
 * replace anything else.
 */
gboolean mocka_pins_desktop_has_copy (GDesktopAppInfo *info);
gboolean mocka_pins_desktop_add (GDesktopAppInfo *info, GError **error);
gboolean mocka_pins_desktop_remove (GDesktopAppInfo *info, GError **error);

/* For the tests, which must not write to the real desktop. NULL restores it. */
void mocka_pins_set_desktop_dir (const gchar *path);

G_END_DECLS