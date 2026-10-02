/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#pragma once

#include "menu-data.h"

G_BEGIN_DECLS

/*
 * Searching the menu (SPEC section 9.2). An app matches when the text is
 * found in one of its fields, and the first field that matches decides its
 * rank. Within a rank, favourites come first and the rest go by name.
 */

typedef enum
{
  MOCKA_SEARCH_RANK_NONE = 0,      /* no match */
  MOCKA_SEARCH_RANK_NAME_PREFIX,   /* name starts with the text */
  MOCKA_SEARCH_RANK_NAME_WORD,     /* a word of the name starts with it */
  MOCKA_SEARCH_RANK_NAME_CONTAINS, /* the name contains it */
  MOCKA_SEARCH_RANK_GENERIC_NAME,  /* the generic name starts with or contains it */
  MOCKA_SEARCH_RANK_KEYWORDS,      /* one of the entry's keywords */
  MOCKA_SEARCH_RANK_PROGRAM,       /* the program the app runs */
  MOCKA_SEARCH_RANK_COMMENT,       /* the comment contains it */
} MockaSearchRank;

/*
 * Case folded with accents removed, so searching ignores both. Returns NULL
 * for NULL. The caller owns the result.
 */
gchar *mocka_search_normalize (const gchar *text);

/* The rank of one app against already normalized text. */
MockaSearchRank mocka_search_rank (MockaMenuApp *app, const gchar *normalized_text);

/*
 * The apps that match, best first. favourite_ids may be NULL; when given, it
 * is a set of desktop entry IDs that come first within their rank.
 *
 * Returns a new array holding references.
 */
GPtrArray *mocka_search_run (GPtrArray *apps, const gchar *text, GHashTable *favourite_ids);

G_END_DECLS