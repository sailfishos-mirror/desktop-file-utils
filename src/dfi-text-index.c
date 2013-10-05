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

typedef struct
{
  /* Our GSequence compare function treats DesktopFileIndexTextIndexItem
   * as a subclass of 'string' for purposes of comparison.
   *
   * The string, therefore, must come first.
   */
  gchar *token;

  GArray *id_list;
} DesktopFileIndexTextIndexItem;

static gint
dfi_text_index_string_compare (gconstpointer a,
                               gconstpointer b,
                               gpointer      user_data)
{
  /* As mentioned above: the pointers can equivalently be pointers to a
   * 'DesktopFileIndexTextIndexItem' or to a 'gchar *'.
   */
  const gchar * const *str_a = a;
  const gchar * const *str_b = b;

  return strcmp (*str_a, *str_b);
}

static DesktopFileIndexTextIndexItem *
dfi_text_index_item_new (const gchar *token)
{
  DesktopFileIndexTextIndexItem *item;

  item = g_slice_new (DesktopFileIndexTextIndexItem);
  item->token = g_strdup (token);
  item->id_list = dfi_id_list_new ();

  return item;
}

static void
dfi_text_index_item_free (gpointer data)
{
  DesktopFileIndexTextIndexItem *item = data;

  dfi_id_list_free (item->id_list);
  g_free (item->token);

  g_slice_free (DesktopFileIndexTextIndexItem, item);
}

GSequence *
dfi_text_index_new (void)
{
  return g_sequence_new (dfi_text_index_item_free);
}

void
dfi_text_index_free (gpointer data)
{
  g_sequence_free (data);
}

void
dfi_text_index_add_ids (GSequence     *text_index,
                        const gchar   *token,
                        const guint16 *ids,
                        gint           n_ids)
{
  DesktopFileIndexTextIndexItem *item;
  GSequenceIter *iter;

  iter = g_sequence_lookup (text_index, &token, dfi_text_index_string_compare, NULL);
  if (iter)
    {
      item = g_sequence_get (iter);
    }
  else
    {
      item = dfi_text_index_item_new (token);
      g_sequence_insert_sorted (text_index, item, dfi_text_index_string_compare, NULL);
    }

  dfi_id_list_add_ids (item->id_list, ids, n_ids);
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
dfi_text_index_add_ids_tokenised (GSequence     *text_index,
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
dfi_text_index_get_item (GSequenceIter  *iter,
                         const gchar   **token,
                         GArray        **id_list)
{
  DesktopFileIndexTextIndexItem *item;

  item = g_sequence_get (iter);

  *token = item->token;
  *id_list = item->id_list;
}

void
dfi_text_index_populate_strings (GSequence      *text_index,
                                 DfiStringTable *string_table)
{
  GSequenceIter *iter;

  iter = g_sequence_get_begin_iter (text_index);

  while (!g_sequence_iter_is_end (iter))
    {
      DesktopFileIndexTextIndexItem *item = g_sequence_get (iter);

      dfi_string_table_add_string (string_table, item->token);

      iter = g_sequence_iter_next (iter);
    }
}
