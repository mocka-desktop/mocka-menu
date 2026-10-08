/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* The desktop copy and its recognition (SPEC section 11.1), in a temporary
 * folder, never the real desktop. */

#include <glib/gstdio.h>

#include "pins.h"

#define ENTRY "alpha.desktop"

typedef struct
{
  gchar *desktop;
  GDesktopAppInfo *info;
} Fixture;

static void
fixture_set_up (Fixture *fixture, gconstpointer data)
{
  gchar *source = g_test_build_filename (G_TEST_DIST, "menus", "share", "applications", ENTRY, NULL);

  fixture->desktop = g_dir_make_tmp ("mocka-menu-desktop-XXXXXX", NULL);
  g_assert_nonnull (fixture->desktop);
  mocka_pins_set_desktop_dir (fixture->desktop);

  fixture->info = g_desktop_app_info_new_from_filename (source);
  g_assert_nonnull (fixture->info);
  g_assert_cmpstr (g_app_info_get_id (G_APP_INFO (fixture->info)), ==, ENTRY);

  g_free (source);
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer data)
{
  gchar *copy = g_build_filename (fixture->desktop, ENTRY, NULL);

  (void)g_remove (copy);
  (void)g_rmdir (fixture->desktop);
  mocka_pins_set_desktop_dir (NULL);

  g_object_unref (fixture->info);
  g_free (fixture->desktop);
  g_free (copy);
}

static gchar *
copy_path (Fixture *fixture)
{
  return g_build_filename (fixture->desktop, ENTRY, NULL);
}

static void
test_no_copy_at_first (Fixture *fixture, gconstpointer data)
{
  g_assert_false (mocka_pins_desktop_has_copy (fixture->info));
}

static void
test_add_copies_and_marks (Fixture *fixture, gconstpointer data)
{
  GError *error = NULL;
  gchar *path = copy_path (fixture);
  GKeyFile *file = g_key_file_new ();
  gchar *marker;
  gchar *exec;

  g_assert_true (mocka_pins_desktop_add (fixture->info, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));

  /* Executable, so the file manager runs it as a launcher. */
  g_assert_true (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE));

  g_assert_true (g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &error));
  g_assert_no_error (error);

  marker = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "X-Mocka-Menu-Copy-Of", NULL);
  g_assert_cmpstr (marker, ==, ENTRY);

  /* The rest of the entry comes along. */
  exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
  g_assert_cmpstr (exec, ==, "cat");

  g_assert_true (mocka_pins_desktop_has_copy (fixture->info));

  g_free (exec);
  g_free (marker);
  g_key_file_free (file);
  g_free (path);
}

static void
test_remove_deletes_our_copy (Fixture *fixture, gconstpointer data)
{
  GError *error = NULL;
  gchar *path = copy_path (fixture);

  g_assert_true (mocka_pins_desktop_add (fixture->info, &error));
  g_assert_no_error (error);

  g_assert_true (mocka_pins_desktop_remove (fixture->info, &error));
  g_assert_no_error (error);
  g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
  g_assert_false (mocka_pins_desktop_has_copy (fixture->info));

  g_free (path);
}

/* A launcher we did not write is not ours, and is never deleted. */
static void
test_unmarked_launcher_is_left_alone (Fixture *fixture, gconstpointer data)
{
  GError *error = NULL;
  gchar *path = copy_path (fixture);
  const gchar *hand_written = "[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=Alpha Browser\n"
                              "Exec=cat\n";

  g_assert_true (g_file_set_contents (path, hand_written, -1, &error));
  g_assert_no_error (error);

  g_assert_false (mocka_pins_desktop_has_copy (fixture->info));
  g_assert_false (mocka_pins_desktop_remove (fixture->info, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

  g_error_free (error);
  g_free (path);
}

/* A marker naming another app does not make the file ours either. */
static void
test_marker_for_another_app_is_not_ours (Fixture *fixture, gconstpointer data)
{
  GError *error = NULL;
  gchar *path = copy_path (fixture);
  const gchar *other = "[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=Alpha Browser\n"
                       "Exec=cat\n"
                       "X-Mocka-Menu-Copy-Of=beta.desktop\n";

  g_assert_true (g_file_set_contents (path, other, -1, &error));
  g_assert_no_error (error);

  g_assert_false (mocka_pins_desktop_has_copy (fixture->info));

  g_free (path);
}

/* Add over a file of ours is a plain overwrite, not a second launcher. */
static void
test_add_twice_keeps_one_copy (Fixture *fixture, gconstpointer data)
{
  GError *error = NULL;
  GDir *dir;
  guint count = 0;

  g_assert_true (mocka_pins_desktop_add (fixture->info, &error));
  g_assert_true (mocka_pins_desktop_add (fixture->info, &error));
  g_assert_no_error (error);

  dir = g_dir_open (fixture->desktop, 0, &error);
  g_assert_no_error (error);
  while (g_dir_read_name (dir) != NULL)
    {
      count++;
    }
  g_assert_cmpuint (count, ==, 1);

  g_dir_close (dir);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

#define ADD(path, func) g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)
  ADD ("/pins/desktop/no-copy-at-first", test_no_copy_at_first);
  ADD ("/pins/desktop/add-copies-and-marks", test_add_copies_and_marks);
  ADD ("/pins/desktop/remove-deletes-our-copy", test_remove_deletes_our_copy);
  ADD ("/pins/desktop/unmarked-launcher-is-left-alone", test_unmarked_launcher_is_left_alone);
  ADD ("/pins/desktop/marker-for-another-app-is-not-ours", test_marker_for_another_app_is_not_ours);
  ADD ("/pins/desktop/add-twice-keeps-one-copy", test_add_twice_keeps_one_copy);
#undef ADD

  return g_test_run ();
}