/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include "menu-data.h"

G_BEGIN_DECLS

/*
 * What to change in a showing list so it matches a new one, as the fewest
 * removals and insertions, so everything else keeps its place and its widget
 * (SPEC section 14).
 */

typedef struct
{
  gboolean insert;   /* otherwise a removal */
  guint position;    /* in the list as it stands when this step is applied */
  MockaMenuApp *app; /* what to insert, borrowed from the new list */
} MockaAppStep;

/* MockaAppStep, in the order they must be applied. Free with g_array_unref. */
GArray *mocka_app_model_diff (GPtrArray *from, GPtrArray *to);

G_END_DECLS