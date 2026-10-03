/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * A key that opens the menu when pressed and released on its own
 * (SPEC section 12.1).
 *
 * The key is never grabbed. XInput 2 raw events are selected on the root
 * window, and since XI 2.1 those reach every client that asks for them, even
 * while another holds a grab. So every other shortcut using the same key keeps
 * working, and so does this one.
 */

#define MOCKA_TYPE_HOTKEY (mocka_hotkey_get_type ())
G_DECLARE_FINAL_TYPE (MockaHotkey, mocka_hotkey, MOCKA, HOTKEY, GObject)

/* NULL when the display cannot provide the raw events. */
MockaHotkey *mocka_hotkey_new (void);

/* A keysym name such as "Super_L". NULL or empty turns the key off. */
void mocka_hotkey_set_key (MockaHotkey *self, const gchar *keysym_name);

G_END_DECLS