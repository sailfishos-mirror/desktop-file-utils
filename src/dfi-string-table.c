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

#include "dfi-string-table.h"

#include <string.h>

static guint
str_hash0 (gconstpointer a)
{
  return a ? g_str_hash (a) : 0;
}

static gboolean
str_equal0 (gconstpointer a,
            gconstpointer b)
{
  return g_strcmp0 (a, b) == 0;
}

GHashTable *
dfi_string_tables_create (void)
{
  return g_hash_table_new_full (str_hash0, str_equal0, g_free, (GDestroyNotify) g_hash_table_unref);
}

static gchar *
get_locale_group (const gchar *for_locale)
{
  /* This function decides how to group the string tables of locales in
   * order to improve sharing of strings between similar locales while
   * preventing too much overlap between unrelated ones (thus improving
   * locality of access).
   *
   * This function doesn't need to be "correct" in any sense (beyond
   * being deterministic); this grouping is merely an optimisation.
   */

  /* Untranslated strings... */
  g_assert (for_locale);
  if (!for_locale)
    return NULL;

  /* English translations will share 99% of strings with the C locale,
   * so avoid duplicating them.  Note: careful to avoid en@shaw.
   */
  if (g_str_equal (for_locale, "en") || g_str_has_prefix (for_locale, "en_"))
    return g_strdup ("");

  /* Valencian is just a dialect of Catalan, so make sure they get
   * grouped together.
   */
  if (g_str_equal (for_locale, "ca@valencia"))
    return g_strdup ("ca");

  /* Other uses of '@' indicate different character sets.  Not much will
   * be gained by grouping them, so keep them separate.
   */
  if (for_locale[0] && for_locale[1] && for_locale[2] == '@')
    return g_strdup (for_locale);

  /* Otherwise, we have cases like pt_BR and fr_CH.  Group these by
   * language code for hope that they will be similar.
   */
  if (for_locale[0] && for_locale[1] && for_locale[2] == '_')
    return g_strndup (for_locale, 2);

  /* Otherwise, it's something else.  Return it, I guess... */
  return g_strdup (for_locale);
}

GHashTable *
dfi_string_tables_get_table (GHashTable  *string_tables,
                             const gchar *locale)
{
  GHashTable *string_table;

  string_table = g_hash_table_lookup (string_tables, locale);

  if (!string_table)
    {
      gchar *locale_group = get_locale_group (locale);

      string_table = g_hash_table_lookup (string_tables, locale_group);

      if (!string_table)
        {
          string_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
          g_hash_table_insert (string_tables, g_strdup (locale_group), string_table);
        }

      g_free (locale_group);

      g_hash_table_insert (string_tables, g_strdup (locale), g_hash_table_ref (string_table));
    }

  return string_table;
}

void
dfi_string_tables_add_string (GHashTable  *string_tables,
                              const gchar *locale,
                              const gchar *string)
{
  GHashTable *string_table;

  string_table = dfi_string_tables_get_table (string_tables, locale);

  dfi_string_table_add_string (string_table, string);
}

void
dfi_string_table_add_string (GHashTable  *string_table,
                             const gchar *string)
{
  g_hash_table_insert (string_table, g_strdup (string), NULL);
}

guint
dfi_string_tables_get_offset (GHashTable  *string_tables,
                              const gchar *locale,
                              const gchar *string)
{
  GHashTable *string_table;

  string_table = dfi_string_tables_get_table (string_tables, locale);

  return dfi_string_table_get_offset (string_table, string);
}

guint
dfi_string_table_get_offset (GHashTable  *string_table,
                             const gchar *string)
{
  gpointer offset;

  offset = g_hash_table_lookup (string_table, string);
  g_assert (offset);

  return GPOINTER_TO_UINT (offset);
}

gboolean
dfi_string_table_is_written (GHashTable *string_table)
{
  GHashTableIter iter;
  gpointer val;

  g_hash_table_iter_init (&iter, string_table);
  if (!g_hash_table_iter_next (&iter, NULL, &val))
    g_error ("mysterious empty string table...");

  return val != NULL;
}

void
dfi_string_table_write (GHashTable *string_table,
                        GHashTable *shared_table,
                        GString    *file)
{
  GHashTableIter iter;
  gpointer key, val;

  g_hash_table_iter_init (&iter, string_table);
  while (g_hash_table_iter_next (&iter, &key, &val))
    {
      g_assert (val == NULL);

      if (shared_table)
        val = g_hash_table_lookup (shared_table, key);

      if (val == NULL)
        {
          g_hash_table_iter_replace (&iter, GUINT_TO_POINTER (file->len));
          g_string_append_len (file, key, strlen (key) + 1);
        }
      else
        g_hash_table_iter_replace (&iter, val);
    }
}
