/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * The library holds everything but the MATE panel applet glue, so Mocka Dock's
 * menu button can open the same menu (SPEC section 13). The API is internal to
 * the Mocka project and not stable.
 */

void mocka_menu_init (void);
const gchar *mocka_menu_get_version (void);
GSettings *mocka_menu_get_settings (void);

G_END_DECLS
