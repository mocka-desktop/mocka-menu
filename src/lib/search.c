/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * SPEC section 9.2. Every field is normalized the same way as the search
 * text, so matching ignores case and accents in both directions: "editeur"
 * finds "Éditeur", and "ÉDITEUR" finds "editeur".
 *
 * Normalizing on every keystroke is more work than caching it would be, but
 * it is a handful of short strings per app and keeps the search independent
 * of how the menu stores them. Section 14 revisits it if it ever shows.
 */

#include "config.h"

#include <string.h>

#include "search.h"

gchar *
mocka_search_normalize (const gchar *text)
{
  gchar *decomposed;
  gchar *stripped;
  gchar *folded;
  const gchar *cursor;
  GString *out;

  if (text == NULL)
    {
      return NULL;
    }

  /* Decomposed first, so an accent becomes a letter and a separate mark. */
  decomposed = g_utf8_normalize (text, -1, G_NORMALIZE_ALL);
  if (decomposed == NULL)
    {
      return NULL;
    }

  out = g_string_sized_new (strlen (decomposed));
  for (cursor = decomposed; *cursor != '\0'; cursor = g_utf8_next_char (cursor))
    {
      gunichar character = g_utf8_get_char (cursor);

      /* The marks left behind by decomposing are what the accents became. */
      if (g_unichar_type (character) == G_UNICODE_NON_SPACING_MARK)
        {
          continue;
        }

      g_string_append_unichar (out, character);
    }

  stripped = g_string_free (out, FALSE);
  folded = g_utf8_casefold (stripped, -1);

  g_free (decomposed);
  g_free (stripped);

  return folded;
}

/* Where needle sits in haystack, or NULL. Both are already normalized. */
static const gchar *
find_in (const gchar *haystack, const gchar *needle)
{
  if (haystack == NULL || needle == NULL || *needle == '\0')
    {
      return NULL;
    }

  return strstr (haystack, needle);
}

/*
 * Which of the three name ranks this is: the start of the name, the start of
 * a word within it, or simply somewhere inside it.
 */
static MockaSearchRank
rank_in_name (const gchar *name, const gchar *text)
{
  const gchar *found = find_in (name, text);
  MockaSearchRank best = MOCKA_SEARCH_RANK_NONE;

  while (found != NULL)
    {
      if (found == name)
        {
          return MOCKA_SEARCH_RANK_NAME_PREFIX;
        }

      /* A word starts wherever the character before it is not part of one. */
      if (!g_unichar_isalnum (g_utf8_get_char (g_utf8_prev_char (found))))
        {
          best = MOCKA_SEARCH_RANK_NAME_WORD;
        }
      else if (best == MOCKA_SEARCH_RANK_NONE)
        {
          best = MOCKA_SEARCH_RANK_NAME_CONTAINS;
        }

      found = find_in (found + 1, text);
    }

  return best;
}

/* The file name of TryExec, or of the first word of Exec (SPEC section 9.2). */
static gchar *
program_name (GDesktopAppInfo *info)
{
  gchar *try_exec = g_desktop_app_info_get_string (info, "TryExec");
  const gchar *executable;
  gchar *base;

  if (try_exec != NULL && *try_exec != '\0')
    {
      base = g_path_get_basename (try_exec);
      g_free (try_exec);
      return base;
    }
  g_free (try_exec);

  executable = g_app_info_get_executable (G_APP_INFO (info));
  if (executable == NULL || *executable == '\0')
    {
      return NULL;
    }

  return g_path_get_basename (executable);
}

static gboolean
keywords_match (GDesktopAppInfo *info, const gchar *text)
{
  const gchar *const *keywords = g_desktop_app_info_get_keywords (info);

  for (gsize i = 0; keywords != NULL && keywords[i] != NULL; i++)
    {
      gchar *normalized = mocka_search_normalize (keywords[i]);
      gboolean found = find_in (normalized, text) != NULL;

      g_free (normalized);
      if (found)
        {
          return TRUE;
        }
    }

  return FALSE;
}

MockaSearchRank
mocka_search_rank (MockaMenuApp *app, const gchar *normalized_text)
{
  GDesktopAppInfo *info;
  MockaSearchRank rank;
  gchar *field;

  g_return_val_if_fail (MOCKA_IS_MENU_APP (app), MOCKA_SEARCH_RANK_NONE);

  if (normalized_text == NULL || *normalized_text == '\0')
    {
      return MOCKA_SEARCH_RANK_NONE;
    }

  info = mocka_menu_app_get_app_info (app);

  field = mocka_search_normalize (mocka_menu_app_get_name (app));
  rank = rank_in_name (field, normalized_text);
  g_free (field);
  if (rank != MOCKA_SEARCH_RANK_NONE)
    {
      return rank;
    }

  /* Starting with or containing it are the same rank here. */
  field = mocka_search_normalize (g_desktop_app_info_get_generic_name (info));
  if (find_in (field, normalized_text) != NULL)
    {
      g_free (field);
      return MOCKA_SEARCH_RANK_GENERIC_NAME;
    }
  g_free (field);

  if (keywords_match (info, normalized_text))
    {
      return MOCKA_SEARCH_RANK_KEYWORDS;
    }

  field = program_name (info);
  if (field != NULL)
    {
      gchar *normalized = mocka_search_normalize (field);
      gboolean found = find_in (normalized, normalized_text) != NULL;

      g_free (normalized);
      g_free (field);
      if (found)
        {
          return MOCKA_SEARCH_RANK_PROGRAM;
        }
    }

  field = mocka_search_normalize (mocka_menu_app_get_comment (app));
  if (find_in (field, normalized_text) != NULL)
    {
      g_free (field);
      return MOCKA_SEARCH_RANK_COMMENT;
    }
  g_free (field);

  return MOCKA_SEARCH_RANK_NONE;
}

/* One result while it is being ranked and sorted. */
typedef struct
{
  MockaMenuApp *app;
  MockaSearchRank rank;
  gboolean favourite;
  gchar *collate_key;
} Result;

// NOLINTBEGIN(bugprone-easily-swappable-parameters) GCompareFunc fixes this signature
static gint
compare_results (gconstpointer left, gconstpointer right)
{
  const Result *first = left;
  const Result *second = right;

  if (first->rank != second->rank)
    {
      return (gint)first->rank - (gint)second->rank;
    }

  /* Favourites first within a rank, then by name (SPEC section 9.2). */
  if (first->favourite != second->favourite)
    {
      return first->favourite ? -1 : 1;
    }

  return g_strcmp0 (first->collate_key, second->collate_key);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

GPtrArray *
mocka_search_run (GPtrArray *apps, const gchar *text, GHashTable *favourite_ids)
{
  GPtrArray *results = g_ptr_array_new_with_free_func (g_object_unref);
  GArray *ranked;
  gchar *normalized;

  g_return_val_if_fail (apps != NULL, results);

  normalized = mocka_search_normalize (text);
  if (normalized == NULL || *normalized == '\0')
    {
      g_free (normalized);
      return results;
    }

  ranked = g_array_new (FALSE, FALSE, sizeof (Result));

  for (guint i = 0; i < apps->len; i++)
    {
      MockaMenuApp *app = g_ptr_array_index (apps, i);
      MockaSearchRank rank = mocka_search_rank (app, normalized);
      Result result;

      if (rank == MOCKA_SEARCH_RANK_NONE)
        {
          continue;
        }

      result.app = app;
      result.rank = rank;
      result.favourite = favourite_ids != NULL && g_hash_table_contains (favourite_ids, mocka_menu_app_get_id (app));
      result.collate_key = g_utf8_collate_key_for_filename (mocka_menu_app_get_name (app), -1);

      g_array_append_val (ranked, result);
    }

  g_array_sort (ranked, compare_results);

  for (guint i = 0; i < ranked->len; i++)
    {
      Result *result = &g_array_index (ranked, Result, i);

      g_ptr_array_add (results, g_object_ref (result->app));
      g_free (result->collate_key);
    }

  g_array_free (ranked, TRUE);
  g_free (normalized);

  return results;
}