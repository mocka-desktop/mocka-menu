/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "session.h"

typedef struct
{
  const gchar *name; /* the bus name to call and to watch */
  const gchar *path; /* NULL takes the seat path from the environment */
  const gchar *interface;
  const gchar *method;
  const gchar *label; /* untranslated, through _() when asked for */
  const gchar *icon;
  gboolean on_system_bus;
} Service;

static const Service services[MOCKA_SESSION_N_ACTIONS] = {
  [MOCKA_SESSION_LOCK] = { "org.mate.ScreenSaver", "/org/mate/ScreenSaver", "org.mate.ScreenSaver", "Lock", N_ ("Lock"),
                           "system-lock-screen", FALSE },
  [MOCKA_SESSION_SWITCH_USER] = { "org.freedesktop.DisplayManager", NULL, "org.freedesktop.DisplayManager.Seat",
                                  "SwitchToGreeter", N_ ("Switch User"), "system-users", TRUE },
  [MOCKA_SESSION_LOG_OUT] = { "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager",
                              "Logout", N_ ("Log Out"), "system-log-out", FALSE },
  [MOCKA_SESSION_SHUT_DOWN] = { "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager",
                                "Shutdown", N_ ("Shut Down"), "system-shutdown", FALSE },
};

struct _MockaSession
{
  GObject parent_instance;

  GDBusConnection *session_bus;
  GDBusConnection *system_bus;
  GCancellable *cancellable;

  gchar *seat_path; /* XDG_SEAT_PATH, NULL without a display manager seat */

  guint watch[MOCKA_SESSION_N_ACTIONS];
  gboolean owned[MOCKA_SESSION_N_ACTIONS];       /* a process has the name now */
  gboolean activatable[MOCKA_SESSION_N_ACTIONS]; /* the bus can start it */
};

G_DEFINE_TYPE (MockaSession, mocka_session, G_TYPE_OBJECT)

enum
{
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static GDBusConnection *
bus_for (MockaSession *self, MockaSessionAction action)
{
  return services[action].on_system_bus ? self->system_bus : self->session_bus;
}

static const gchar *
path_for (MockaSession *self, MockaSessionAction action)
{
  return services[action].path != NULL ? services[action].path : self->seat_path;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) GBusNameAppearedCallback fixes this signature
static void
on_name_appeared (GDBusConnection *connection, const gchar *name, const gchar *owner, gpointer user_data)
{
  MockaSession *self = user_data;

  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      if (g_str_equal (services[i].name, name))
        {
          self->owned[i] = TRUE;
        }
    }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static void
on_name_vanished (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
  MockaSession *self = user_data;

  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      if (g_str_equal (services[i].name, name))
        {
          self->owned[i] = FALSE;
        }
    }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* A service the bus can start counts as available even while it is not
 * running, which is how mate-screensaver and mate-session-manager appear
 * before anything has asked them for something. */
static void
on_activatable_names (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GDBusConnection *connection = G_DBUS_CONNECTION (source);
  GVariant *reply = g_dbus_connection_call_finish (connection, result, NULL);
  MockaSession *self = user_data;
  gchar **names = NULL;

  if (reply == NULL)
    {
      g_object_unref (self);
      return;
    }

  g_variant_get (reply, "(^as)", &names);

  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      if (bus_for (self, i) == connection && g_strv_contains ((const gchar *const *)names, services[i].name))
        {
          self->activatable[i] = TRUE;
        }
    }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);

  g_strfreev (names);
  g_variant_unref (reply);
  g_object_unref (self);
}

/*
 * Each asynchronous call holds a reference. Cancelling in dispose does not
 * help on its own: a call that already succeeded still delivers its result,
 * and the callback would then be left with a freed session.
 */
static void
ask_activatable_names (MockaSession *self, GDBusConnection *connection)
{
  g_dbus_connection_call (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "ListActivatableNames", NULL, G_VARIANT_TYPE ("(as)"), G_DBUS_CALL_FLAGS_NONE, -1,
                          self->cancellable, on_activatable_names, g_object_ref (self));
}

static void
watch_names (MockaSession *self, gboolean system_bus)
{
  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      if (services[i].on_system_bus != system_bus || self->watch[i] != 0)
        {
          continue;
        }

      self->watch[i]
          = g_bus_watch_name_on_connection (bus_for (self, i), services[i].name, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                            on_name_appeared, on_name_vanished, self, NULL);
    }
}

static void
on_session_bus (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GDBusConnection *connection = g_bus_get_finish (result, NULL);
  MockaSession *self = user_data;

  if (connection != NULL)
    {
      self->session_bus = connection;
      watch_names (self, FALSE);
      ask_activatable_names (self, connection);
    }

  g_object_unref (self);
}

static void
on_system_bus (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GDBusConnection *connection = g_bus_get_finish (result, NULL);
  MockaSession *self = user_data;

  if (connection != NULL)
    {
      self->system_bus = connection;
      watch_names (self, TRUE);
      ask_activatable_names (self, connection);
    }

  g_object_unref (self);
}

static void
mocka_session_init (MockaSession *self)
{
  const gchar *seat = g_getenv ("XDG_SEAT_PATH");

  self->cancellable = g_cancellable_new ();
  self->seat_path = g_strdup (seat);

  g_bus_get (G_BUS_TYPE_SESSION, self->cancellable, on_session_bus, g_object_ref (self));

  /* Only the display manager's seat lives on the system bus, and without a
   * seat path there is nothing to call there. */
  if (self->seat_path != NULL)
    {
      g_bus_get (G_BUS_TYPE_SYSTEM, self->cancellable, on_system_bus, g_object_ref (self));
    }
}

static void
mocka_session_dispose (GObject *object)
{
  MockaSession *self = MOCKA_SESSION (object);

  g_cancellable_cancel (self->cancellable);

  for (guint i = 0; i < MOCKA_SESSION_N_ACTIONS; i++)
    {
      if (self->watch[i] != 0)
        {
          g_bus_unwatch_name (self->watch[i]);
          self->watch[i] = 0;
        }
    }

  g_clear_object (&self->cancellable);
  g_clear_object (&self->session_bus);
  g_clear_object (&self->system_bus);
  g_clear_pointer (&self->seat_path, g_free);

  G_OBJECT_CLASS (mocka_session_parent_class)->dispose (object);
}

static void
mocka_session_class_init (MockaSessionClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = mocka_session_dispose;

  signals[SIGNAL_CHANGED]
      = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

MockaSession *
mocka_session_new (void)
{
  return g_object_new (MOCKA_TYPE_SESSION, NULL);
}

const gchar *
mocka_session_action_name (MockaSessionAction action)
{
  g_return_val_if_fail (action < MOCKA_SESSION_N_ACTIONS, NULL);

  return _ (services[action].label);
}

const gchar *
mocka_session_action_icon (MockaSessionAction action)
{
  g_return_val_if_fail (action < MOCKA_SESSION_N_ACTIONS, NULL);

  return services[action].icon;
}

gboolean
mocka_session_can (MockaSession *self, MockaSessionAction action)
{
  g_return_val_if_fail (MOCKA_IS_SESSION (self), FALSE);
  g_return_val_if_fail (action < MOCKA_SESSION_N_ACTIONS, FALSE);

  if (bus_for (self, action) == NULL || path_for (self, action) == NULL)
    {
      return FALSE;
    }

  return self->owned[action] || self->activatable[action];
}

/*
 * The window is already closed, so there is nowhere to show a failure. It is
 * logged rather than dropped, which is what tells a maintainer that a session
 * service refused or went away.
 */
static void
on_call_done (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (reply != NULL)
    {
      g_variant_unref (reply);
      return;
    }

  if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_warning ("mocka-menu: %s failed: %s", (const gchar *)user_data, error->message);
    }

  g_error_free (error);
}

void
mocka_session_run (MockaSession *self, MockaSessionAction action)
{
  GVariant *args = NULL;

  g_return_if_fail (MOCKA_IS_SESSION (self));
  g_return_if_fail (action < MOCKA_SESSION_N_ACTIONS);

  if (!mocka_session_can (self, action))
    {
      return;
    }

  /* Normal mode, so the session manager shows its own confirmation. */
  if (action == MOCKA_SESSION_LOG_OUT)
    {
      args = g_variant_new ("(u)", 0);
    }

  g_dbus_connection_call (bus_for (self, action), services[action].name, path_for (self, action),
                          services[action].interface, services[action].method, args, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
                          self->cancellable, on_call_done, (gpointer)services[action].method);
}