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

#include "dfi-string-list.h"

#include "dfi-string-table.h"

#include <string.h>
#include <stdlib.h>

struct _DfiStringList
{
  GHashTable *table;
  gchar **strings;
};

DfiStringList *
dfi_string_list_new (void)
{
  DfiStringList *string_list;

  string_list = g_slice_new0 (DfiStringList);
  string_list->table = g_hash_table_new (g_str_hash, g_str_equal);

  return string_list;
}

void
dfi_string_list_free (DfiStringList *string_list)
{
  /* This will call g_free() on each key */
  g_hash_table_foreach (string_list->table, (GHFunc) g_free, NULL);
  g_free (string_list->strings);

  g_slice_free (DfiStringList, string_list);
}

void
dfi_string_list_ensure (DfiStringList *string_list,
                        const gchar   *string)
{
  /* Ensure we're not already converted */
  g_assert (string_list->strings == NULL);

  if (!g_hash_table_contains (string_list->table, string))
    g_hash_table_add (string_list->table, g_strdup (string));
}

static int
indirect_strcmp (gconstpointer a,
                 gconstpointer b)
{
  return strcmp (*(gchar **) a, *(gchar **) b);
}

void
dfi_string_list_convert (DfiStringList *string_list)
{
  GHashTableIter iter;
  gpointer key;
  guint n, i;

  /* Ensure we're not already converted */
  g_assert (string_list->strings == NULL);

  n = g_hash_table_size (string_list->table);
  string_list->strings = g_new (gchar *, n + 1);
  i = 0;

  g_hash_table_iter_init (&iter, string_list->table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    string_list->strings[i++] = key;
  g_assert_cmpint (i, ==, n);
  string_list->strings[n] = NULL;

  qsort (string_list->strings, n, sizeof (char *), indirect_strcmp);

  /* Store the id of each string back into the table for fast lookup.
   *
   * Note: no free func on the hash table, so we can just reuse the same
   * string as the key without worrying that it will be freed.
   */
  for (i = 0; i < n; i++)
    g_hash_table_insert (string_list->table, (gchar *) string_list->strings[i], GUINT_TO_POINTER (i));
}

void
dfi_string_list_populate_strings (DfiStringList *string_list,
                                  GHashTable    *string_table)
{
  GHashTableIter iter;
  gpointer string;

  /* Ensure that we've been converted */
  g_assert (string_list->strings);

  g_hash_table_iter_init (&iter, string_list->table);
  while (g_hash_table_iter_next (&iter, &string, NULL))
    dfi_string_table_add_string (string_table, string);
}

guint
dfi_string_list_get_id (DfiStringList *string_list,
                        const gchar   *string)
{
  gpointer value;

  /* Ensure that we've been converted */
  g_assert (string_list->strings);

  value = g_hash_table_lookup (string_list->table, string);

  return GPOINTER_TO_UINT (value);
}

const gchar * const *
dfi_string_list_get_strings (DfiStringList *string_list,
                             guint         *n_strings)
{
  /* Ensure that we've been converted */
  g_assert (string_list->strings);

  if (n_strings)
    *n_strings = g_hash_table_size (string_list->table);

  return (const gchar **) string_list->strings;
}
