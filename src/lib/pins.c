/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#include "pins.h"

#define DOCK_SCHEMA "org.mocka_desktop.Dock"
#define DOCK_KEY "pinned-apps"

#define COPY_KEY "X-Mocka-Menu-Copy-Of"

// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) two flags combined is not one named value
static const GKeyFileFlags COPY_FLAGS = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

static gchar *desktop_dir_override;

/* Asking GSettings for a schema that is not installed aborts, so look it up
 * first. The answer is kept: writes must reach the object the dock watches. */
static GSettings *
dock_settings (void)
{
  static GSettings *settings;
  static gboolean looked_up;

  if (!looked_up)
    {
      GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
      GSettingsSchema *schema = source != NULL ? g_settings_schema_source_lookup (source, DOCK_SCHEMA, TRUE) : NULL;

      if (schema != NULL)
        {
          if (g_settings_schema_has_key (schema, DOCK_KEY))
            {
              settings = g_settings_new_full (schema, NULL, NULL);
            }

          g_settings_schema_unref (schema);
        }

      looked_up = TRUE;
    }

  return settings;
}

gboolean
mocka_pins_dock_available (void)
{
  return dock_settings () != NULL;
}

gboolean
mocka_pins_dock_contains (const gchar *id)
{
  GSettings *settings = dock_settings ();
  gchar **pinned;
  gboolean found;

  if (settings == NULL || id == NULL)
    {
      return FALSE;
    }

  pinned = g_settings_get_strv (settings, DOCK_KEY);
  found = g_strv_contains ((const gchar *const *)pinned, id);
  g_strfreev (pinned);

  return found;
}

void
mocka_pins_dock_add (const gchar *id)
{
  GSettings *settings = dock_settings ();
  gchar **pinned;
  GPtrArray *kept;

  if (settings == NULL || id == NULL || mocka_pins_dock_contains (id))
    {
      return;
    }

  pinned = g_settings_get_strv (settings, DOCK_KEY);
  kept = g_ptr_array_new ();
  for (gchar **at = pinned; *at != NULL; at++)
    {
      g_ptr_array_add (kept, *at);
    }
  g_ptr_array_add (kept, (gpointer)id);
  g_ptr_array_add (kept, NULL);

  g_settings_set_strv (settings, DOCK_KEY, (const gchar *const *)kept->pdata);

  g_ptr_array_free (kept, TRUE);
  g_strfreev (pinned);
}

void
mocka_pins_dock_remove (const gchar *id)
{
  GSettings *settings = dock_settings ();
  gchar **pinned;
  GPtrArray *kept;

  if (settings == NULL || id == NULL)
    {
      return;
    }

  pinned = g_settings_get_strv (settings, DOCK_KEY);
  kept = g_ptr_array_new ();
  for (gchar **at = pinned; *at != NULL; at++)
    {
      if (!g_str_equal (*at, id))
        {
          g_ptr_array_add (kept, *at);
        }
    }
  g_ptr_array_add (kept, NULL);

  g_settings_set_strv (settings, DOCK_KEY, (const gchar *const *)kept->pdata);

  g_ptr_array_free (kept, TRUE);
  g_strfreev (pinned);
}

void
mocka_pins_set_desktop_dir (const gchar *path)
{
  g_free (desktop_dir_override);
  desktop_dir_override = g_strdup (path);
}

static const gchar *
desktop_dir (void)
{
  if (desktop_dir_override != NULL)
    {
      return desktop_dir_override;
    }

  return g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
}

static gchar *
copy_path (GDesktopAppInfo *info)
{
  const gchar *dir = desktop_dir ();
  const gchar *id = g_app_info_get_id (G_APP_INFO (info));

  if (dir == NULL || id == NULL)
    {
      return NULL;
    }

  return g_build_filename (dir, id, NULL);
}

gboolean
mocka_pins_desktop_has_copy (GDesktopAppInfo *info)
{
  gchar *path;
  GKeyFile *file;
  gchar *marker = NULL;
  gboolean ours;

  g_return_val_if_fail (G_IS_DESKTOP_APP_INFO (info), FALSE);

  path = copy_path (info);
  if (path == NULL)
    {
      return FALSE;
    }

  file = g_key_file_new ();
  if (g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, NULL))
    {
      marker = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, COPY_KEY, NULL);
    }

  ours = marker != NULL && g_strcmp0 (marker, g_app_info_get_id (G_APP_INFO (info))) == 0;

  g_free (marker);
  g_key_file_free (file);
  g_free (path);

  return ours;
}

gboolean
mocka_pins_desktop_add (GDesktopAppInfo *info, GError **error)
{
  const gchar *source = g_desktop_app_info_get_filename (info);
  gchar *path;
  GKeyFile *file;
  gboolean written;

  g_return_val_if_fail (G_IS_DESKTOP_APP_INFO (info), FALSE);

  path = copy_path (info);
  if (source == NULL || path == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no desktop entry file to copy");
      g_free (path);
      return FALSE;
    }

  file = g_key_file_new ();
  written = g_key_file_load_from_file (file, source, COPY_FLAGS, error);
  if (written)
    {
      g_key_file_set_string (file, G_KEY_FILE_DESKTOP_GROUP, COPY_KEY, g_app_info_get_id (G_APP_INFO (info)));
      written = g_key_file_save_to_file (file, path, error);
    }

  /* Executable, so the file manager runs it as a launcher (SPEC section 11.1). */
  if (written && g_chmod (path, 0755) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "could not make %s executable", path);
      written = FALSE;
    }

  g_key_file_free (file);
  g_free (path);

  return written;
}

gboolean
mocka_pins_desktop_remove (GDesktopAppInfo *info, GError **error)
{
  gchar *path;
  gboolean removed;

  g_return_val_if_fail (G_IS_DESKTOP_APP_INFO (info), FALSE);

  if (!mocka_pins_desktop_has_copy (info))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no copy of ours on the desktop");
      return FALSE;
    }

  path = copy_path (info);
  removed = g_remove (path) == 0;
  if (!removed)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "could not remove %s", path);
    }

  g_free (path);

  return removed;
}