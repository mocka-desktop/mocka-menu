/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* The session controls (SPEC section 10), as a GDBus client only. Session
 * management goes through mate-session-manager, never ConsoleKit2 or logind. */

typedef enum
{
  MOCKA_SESSION_LOCK,
  MOCKA_SESSION_SWITCH_USER,
  MOCKA_SESSION_LOG_OUT,
  MOCKA_SESSION_SHUT_DOWN,
  MOCKA_SESSION_N_ACTIONS
} MockaSessionAction;

#define MOCKA_TYPE_SESSION (mocka_session_get_type ())
G_DECLARE_FINAL_TYPE (MockaSession, mocka_session, MOCKA, SESSION, GObject)

MockaSession *mocka_session_new (void);

const gchar *mocka_session_action_name (MockaSessionAction action);
const gchar *mocka_session_action_icon (MockaSessionAction action);

/* FALSE while the service behind the action is missing, so its button hides.
 * The "changed" signal fires whenever any of these answers changes. */
gboolean mocka_session_can (MockaSession *self, MockaSessionAction action);

/* Asynchronous and unwaited: the window closes without the call finishing. */
void mocka_session_run (MockaSession *self, MockaSessionAction action);

G_END_DECLS