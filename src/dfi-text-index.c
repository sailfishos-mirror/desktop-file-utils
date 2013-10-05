/*
 * Copyright © 2013 Canonical Limited
 *
 * update-desktop-database is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * update-desktop-database is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with update-desktop-database; see the file COPYING.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite
 * 330, Boston, MA 02111-1307, USA.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "dfi-text-index.h"

#include "dfi-string-table.h"
#include "dfi-id-list.h"

#include <string.h>
#include <stdlib.h>

struct _DfiTextIndex
{
  GHashTable *table;
  gchar **tokens;
};

DfiTextIndex *
dfi_text_index_new (void)
{
  DfiTextIndex *text_index;

  text_index = g_slice_new0 (DfiTextIndex);
  text_index->table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, dfi_id_list_free);

  return text_index;
}

void
dfi_text_index_free (gpointer data)
{
  DfiTextIndex *text_index = data;

  g_hash_table_unref (text_index->table);
  g_free (text_index->tokens);

  g_slice_free (DfiTextIndex, text_index);
}

void
dfi_text_index_add_ids (DfiTextIndex  *text_index,
                        const gchar   *token,
                        const guint16 *ids,
                        gint           n_ids)
{
  DfiIdList *id_list;

  /* Ensure we're not already converted */
  g_assert (text_index->tokens == NULL);

  id_list = g_hash_table_lookup (text_index->table, token);

  if (id_list == NULL)
    {
      id_list = dfi_id_list_new ();
      g_hash_table_insert (text_index->table, g_strdup (token), id_list);
    }

  dfi_id_list_add_ids (id_list, ids, n_ids);
}

static void
dfi_text_index_add_folded (GPtrArray   *array,
                           const gchar *start,
                           const gchar *end)
{
  gchar *normal;

  normal = g_utf8_normalize (start, end - start, G_NORMALIZE_ALL_COMPOSE);

  /* TODO: Invent time machine.  Converse with Mustafa Ataturk... */
  if (strstr (normal, "ı") || strstr (normal, "İ"))
    {
      gchar *s = normal;
      GString *tmp;

      tmp = g_string_new (NULL);

      while (*s)
        {
          gchar *i, *I, *e;

          i = strstr (s, "ı");
          I = strstr (s, "İ");

          if (!i && !I)
            break;
          else if (i && !I)
            e = i;
          else if (I && !i)
            e = I;
          else if (i < I)
            e = i;
          else
            e = I;

          g_string_append_len (tmp, s, e - s);
          g_string_append_c (tmp, 'i');
          s = g_utf8_next_char (e);
        }

      g_string_append (tmp, s);
      g_free (normal);
      normal = g_string_free (tmp, FALSE);
    }

  g_ptr_array_add (array, g_utf8_casefold (normal, -1));
  g_free (normal);
}

static gchar **
dfi_text_index_split_words (const gchar *value)
{
  const gchar *start = NULL;
  GPtrArray *result;
  const gchar *s;

  result = g_ptr_array_new ();

  for (s = value; *s; s = g_utf8_next_char (s))
    {
      gunichar c = g_utf8_get_char (s);

      if (start == NULL)
        {
          if (g_unichar_isalnum (c))
            start = s;
        }
      else
        {
          if (!g_unichar_isalnum (c))
            {
              dfi_text_index_add_folded (result, start, s);
              start = NULL;
            }
        }
    }

  if (start)
    dfi_text_index_add_folded (result, start, s);

  g_ptr_array_add (result, NULL);

  return (gchar **) g_ptr_array_free (result, FALSE);
}

void
dfi_text_index_add_ids_tokenised (DfiTextIndex  *text_index,
                                  const gchar   *string_to_tokenise,
                                  const guint16 *ids,
                                  gint           n_ids)
{
  gchar **tokens;
  gint i;

  tokens = dfi_text_index_split_words (string_to_tokenise);
  for (i = 0; tokens[i]; i++)
    {
      gint j;

      for (j = 0; j < i; j++)
        if (g_str_equal (tokens[i], tokens[j]))
          break;

      if (j < i)
        continue;

      dfi_text_index_add_ids (text_index, tokens[i], ids, n_ids);
    }
}

void
dfi_text_index_convert (DfiTextIndex *text_index)
{
  GHashTableIter iter;
  gpointer key;
  guint n, i;

  /* Ensure we're not already converted */
  g_assert (text_index->tokens == NULL);

  n = g_hash_table_size (text_index->table);
  text_index->tokens = g_new (gchar *, n + 1);
  i = 0;

  g_hash_table_iter_init (&iter, text_index->table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    text_index->tokens[i++] = key;
  g_assert_cmpint (i, ==, n);
  text_index->tokens[n] = NULL;

  qsort (text_index->tokens, n, sizeof (char *), (GCompareFunc) strcmp);
}

const gchar * const *
dfi_text_index_get_tokens (DfiTextIndex  *text_index,
                           guint         *n_tokens)
{
  /* Ensure that we've been converted */
  g_assert (text_index->tokens);

  if (n_tokens)
    *n_tokens = g_hash_table_size (text_index->table);

  return (const gchar **) text_index->tokens;
}

DfiIdList *
dfi_text_index_get_id_list_for_token (DfiTextIndex *text_index,
                                      const gchar  *token)
{
  DfiIdList *id_list;

  /* Ensure that we've been converted */
  g_assert (text_index->tokens);

  id_list = g_hash_table_lookup (text_index->table, token);
  g_assert (id_list != NULL);

  return id_list;
}

void
dfi_text_index_populate_strings (DfiTextIndex   *text_index,
                                 DfiStringTable *string_table)
{
  GHashTableIter iter;
  gpointer string;

  /* Ensure that we've been converted */
  g_assert (text_index->tokens);

  g_hash_table_iter_init (&iter, text_index->table);
  while (g_hash_table_iter_next (&iter, &string, NULL))
    dfi_string_table_add_string (string_table, string);
}
