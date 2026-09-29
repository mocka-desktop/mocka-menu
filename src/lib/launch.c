/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <unistd.h>

#include <glib/gi18n-lib.h>

#include "launch.h"

/*
 * Runs in the child, between fork and exec, so applications start in the
 * user's home folder rather than wherever the panel happens to be
 * (SPEC section 17). Only async-signal-safe calls belong here, which is why
 * the directory is worked out by the caller and passed in.
 */
static void
start_in_directory (gpointer user_data)
{
  const gchar *directory = user_data;

  if (directory != NULL)
    (void) chdir (directory);
}

/* Carries the startup notification the dock and the window manager watch. */
static GdkAppLaunchContext *
launch_context_for (GDesktopAppInfo *info, GtkWidget *context_widget)
{
  GdkDisplay *display = context_widget != NULL
    ? gtk_widget_get_display (context_widget)
    : gdk_display_get_default ();
  GdkAppLaunchContext *context;
  GIcon *icon;

  if (display == NULL)
    return NULL;

  context = gdk_display_get_app_launch_context (display);
  gdk_app_launch_context_set_timestamp (context, gtk_get_current_event_time ());

  icon = g_app_info_get_icon (G_APP_INFO (info));
  if (icon != NULL)
    gdk_app_launch_context_set_icon (context, icon);

  return context;
}

/*
 * A launch we are still watching, so the startup feedback can be ended when
 * the process turns out not to be showing a window after all.
 */
typedef struct
{
  GAppLaunchContext *context;     /* NULL once we have stopped watching */
  gchar             *startup_id;
  GPid               pid;
  guint              timer_id;
} Sequence;

/* Long enough that a slow application is up, short enough to let go. */
#define SEQUENCE_WATCH_SECONDS 20

static void
sequence_free (gpointer data)
{
  Sequence *sequence = data;

  if (sequence->timer_id != 0)
    g_source_remove (sequence->timer_id);

  g_clear_object (&sequence->context);
  g_free (sequence->startup_id);
  g_free (sequence);
}

/*
 * The process is gone. If it never completed its startup sequence, it never
 * will, so end it here rather than leaving the pointer and the dock showing
 * a launch that finished long ago. This is what happens when an already
 * running application is asked to open something: the new process hands the
 * request over and exits without ever mapping a window.
 */
static void
on_child_exited (GPid pid, gint status, gpointer data)
{
  Sequence *sequence = data;

  if (sequence->context != NULL)
    g_app_launch_context_launch_failed (sequence->context, sequence->startup_id);

  g_spawn_close_pid (pid);
}

/* Still running after all this time, so it is up: stop watching it. */
static gboolean
on_sequence_settled (gpointer data)
{
  Sequence *sequence = data;

  sequence->timer_id = 0;
  g_clear_object (&sequence->context);
  g_clear_pointer (&sequence->startup_id, g_free);

  return G_SOURCE_REMOVE;
}

/*
 * The startup id and the process id both arrive here, once the application
 * has been spawned.
 */
static void
on_launched (GAppLaunchContext *context,
             GAppInfo          *info,
             GVariant          *platform_data,
             gpointer           user_data)
{
  Sequence *sequence;
  const gchar *startup_id = NULL;
  gint32 pid = 0;

  if (!g_variant_lookup (platform_data, "startup-notification-id", "&s", &startup_id))
    return;
  if (!g_variant_lookup (platform_data, "pid", "i", &pid))
    return;

  sequence = g_new0 (Sequence, 1);
  sequence->context = g_object_ref (context);
  sequence->startup_id = g_strdup (startup_id);
  sequence->pid = (GPid) pid;
  sequence->timer_id = g_timeout_add_seconds (SEQUENCE_WATCH_SECONDS,
                                              on_sequence_settled, sequence);

  /* The watch also reaps the child, so it is kept until it fires. */
  g_child_watch_add_full (G_PRIORITY_DEFAULT, sequence->pid,
                          on_child_exited, sequence, sequence_free);
}

/* Says so plainly and goes away on its own, so nothing is left waiting. */
static void
report_failure (GDesktopAppInfo *info,
                GtkWidget       *context_widget,
                const GError    *error)
{
  GtkWidget *dialog;
  const gchar *name = g_app_info_get_display_name (G_APP_INFO (info));

  g_warning ("mocka-menu: could not start %s: %s", name,
             error != NULL ? error->message : "unknown error");

  dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE,
                                   _("Could not start %s"), name);
  if (error != NULL)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                              "%s", error->message);

  gtk_window_set_title (GTK_WINDOW (dialog), _("Mocka Menu"));
  g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
  gtk_widget_show_all (dialog);
}

void
mocka_launch_app (GDesktopAppInfo *info, GtkWidget *context_widget)
{
  GdkAppLaunchContext *context;
  GError *error = NULL;
  gchar *path;

  g_return_if_fail (G_IS_DESKTOP_APP_INFO (info));

  context = launch_context_for (info, context_widget);

  /*
   * An entry that names its own working directory means it, so only entries
   * without one are started from the home folder.
   */
  path = g_desktop_app_info_get_string (info, G_KEY_FILE_DESKTOP_KEY_PATH);

  if (context != NULL)
    g_signal_connect (context, "launched", G_CALLBACK (on_launched), NULL);

  /*
   * launch_uris_as_manager, rather than the plain launch, because it is the
   * one that lets the child be set up before it runs. DO_NOT_REAP_CHILD so
   * the process can be watched; the child watch reaps it.
   */
  if (!g_desktop_app_info_launch_uris_as_manager (info, NULL,
                                                  G_APP_LAUNCH_CONTEXT (context),
                                                  G_SPAWN_SEARCH_PATH
                                                  | G_SPAWN_DO_NOT_REAP_CHILD,
                                                  path != NULL ? NULL : start_in_directory,
                                                  (gpointer) g_get_home_dir (),
                                                  NULL, NULL, &error))
    {
      report_failure (info, context_widget, error);
      g_clear_error (&error);
    }

  g_free (path);
  g_clear_object (&context);
}

void
mocka_launch_action (GDesktopAppInfo *info,
                     const gchar     *action,
                     GtkWidget       *context_widget)
{
  GdkAppLaunchContext *context;

  g_return_if_fail (G_IS_DESKTOP_APP_INFO (info));
  g_return_if_fail (action != NULL);

  context = launch_context_for (info, context_widget);

  /* The action call reports no error of its own, so there is none to show. */
  g_desktop_app_info_launch_action (info, action,
                                    G_APP_LAUNCH_CONTEXT (context));

  g_clear_object (&context);
}
