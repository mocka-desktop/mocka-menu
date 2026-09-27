/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "mocka-menu.h"

#define MOCKA_MENU_SCHEMA_ID "org.mocka_desktop.Menu"

/*
 * Binds the translation domain. Every entry point into the library calls this
 * first, since the library is used both by our applet and by Mocka Dock, and
 * neither can be relied on to have bound it.
 */
void
mocka_menu_init (void)
{
  static gsize done = 0;

  if (g_once_init_enter (&done))
    {
      bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
      bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
      g_once_init_leave (&done, 1);
    }
}

const gchar *
mocka_menu_get_version (void)
{
  return PACKAGE_VERSION;
}

/*
 * The settings every applet shares, on one fixed path (SPEC section 15).
 * Returns the same object each time; the caller does not own it.
 */
GSettings *
mocka_menu_get_settings (void)
{
  static GSettings *settings = NULL;

  if (g_once_init_enter (&settings))
    {
      GSettings *created = g_settings_new (MOCKA_MENU_SCHEMA_ID);

      g_once_init_leave (&settings, created);
    }

  return settings;
}
