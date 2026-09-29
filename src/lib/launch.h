/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Starting an application the way SPEC section 17 asks: from the user's home
 * folder, with startup notification so the dock and the window manager show
 * the launch, and with a message when it fails instead of nothing happening.
 *
 * context_widget only supplies the display and the timestamp, and may be NULL.
 */
void mocka_launch_app    (GDesktopAppInfo *info,
                          GtkWidget       *context_widget);

/* One of the actions in the app's desktop entry (SPEC section 11.1). */
void mocka_launch_action (GDesktopAppInfo *info,
                          const gchar     *action,
                          GtkWidget       *context_widget);

G_END_DECLS
