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

#include "dfi-builder.h"

#include "dfi-string-table.h"
#include "dfi-keyfile.h"
#include "dfi-text-index.h"
#include "dfi-string-list.h"
#include "dfi-id-list.h"

#include <string.h>
#include <unistd.h>
#include <locale.h>

typedef struct
{
  GHashTable *locale_string_tables;  /* string tables */

  DfiStringList *app_names;
  DfiStringList *key_names;
  DfiStringList *locale_names;
  DfiStringList *group_names;

  DfiTextIndex  *c_text_index;
  DfiTextIndex  *mime_types;

  GHashTable *locale_text_indexes;   /* str -> DfiTextIndex */
  GHashTable *implementations;       /* str -> DfiIdList */
  GHashTable *desktop_files;         /* str -> DfiKeyfile */

  GString    *string;                /* file contents */
} DfiBuilder;

static GHashTable *
dfi_builder_get_string_table (DfiBuilder  *builder,
                              const gchar *locale)
{
  return dfi_string_tables_get_table (builder->locale_string_tables, locale);
}

static guint
dfi_builder_get_offset (DfiBuilder *builder)
{
  return builder->string->len;
}

static void
dfi_builder_align (DfiBuilder *builder,
                   guint       size)
{
  while (builder->string->len & (size - 1))
    g_string_append_c (builder->string, '\0');
}

static guint
dfi_builder_get_aligned (DfiBuilder *builder,
                         guint       size)
{
  dfi_builder_align (builder, size);

  return dfi_builder_get_offset (builder);
}

static void
dfi_builder_check_alignment (DfiBuilder *builder,
                             guint       size)
{
  g_assert (~builder->string->len & (size - 1));
}

static guint
dfi_builder_write_uint16 (DfiBuilder *builder,
                          guint16     value)
{
  guint offset = dfi_builder_get_offset (builder);

  dfi_builder_check_alignment (builder, sizeof (guint16));

  value = GUINT16_TO_LE (value);

  g_string_append_len (builder->string, (gpointer) &value, sizeof value);

  return offset;
}

static guint
dfi_builder_write_uint32 (DfiBuilder *builder,
                          guint32     value)
{
  guint offset = dfi_builder_get_offset (builder);

  dfi_builder_check_alignment (builder, sizeof (guint32));

  value = GUINT32_TO_LE (value);

  g_string_append_len (builder->string, (gpointer) &value, sizeof value);

  return offset;
}

#if 0
static guint
dfi_builder_write_raw_string (DfiBuilder *builder,
                                             const gchar             *string)
{
  guint offset = dfi_builder_get_offset (builder);

  g_string_append (builder->string, string);
  g_string_append_c (builder->string, '\0');

  return offset;
}
XXX
#endif

static guint
dfi_builder_write_string (DfiBuilder  *builder,
                          const gchar *from_locale,
                          const gchar *string)
{
  guint offset;

  offset = dfi_string_tables_get_offset (builder->locale_string_tables, from_locale, string);

  return dfi_builder_write_uint32 (builder, offset);
}

static guint
dfi_builder_write_string_list (DfiBuilder    *builder,
                               DfiStringList *string_list)
{
  guint offset = dfi_builder_get_aligned (builder, sizeof (guint32));
  const gchar * const *strings;
  guint n, i;

  strings = dfi_string_list_get_strings (string_list, &n);

  dfi_builder_write_uint16 (builder, n);
  dfi_builder_write_uint16 (builder, 0xffff); /* padding */

  for (i = 0; i < n; i++)
    dfi_builder_write_string (builder, "", strings[i]);

  return offset;
}

static guint
dfi_builder_write_id (DfiBuilder    *builder,
                      DfiStringList *string_list,
                      const gchar   *string)
{
  guint value;

  if (string == NULL)
    return dfi_builder_write_uint16 (builder, G_MAXUINT16);

  value = dfi_string_list_get_id (string_list, string);

  return dfi_builder_write_uint16 (builder, (gsize) value);
}

static guint
dfi_builder_write_keyfile (DfiBuilder  *builder,
                           const gchar *app,
                           gpointer     data)
{
  guint offset = dfi_builder_get_aligned (builder, sizeof (guint16));
  DfiKeyfile *keyfile = data;
  gint n_groups, n_items;
  gint i;

  n_groups = dfi_keyfile_get_n_groups (keyfile);
  n_items = dfi_keyfile_get_n_items (keyfile);

  dfi_builder_write_uint16 (builder, n_groups);
  dfi_builder_write_uint16 (builder, n_items);

  for (i = 0; i < n_groups; i++)
    {
      const gchar *group_name;
      guint start;

      group_name = dfi_keyfile_get_group_name (keyfile, i);
      dfi_keyfile_get_group_range (keyfile, i, &start, NULL);

      dfi_builder_write_id (builder, builder->group_names, group_name);
      dfi_builder_write_uint16 (builder, start);
    }

  for (i = 0; i < n_items; i++)
    {
      const gchar *key, *locale, *value;

      dfi_keyfile_get_item (keyfile, i, &key, &locale, &value);

      dfi_builder_write_id (builder, builder->key_names, key);
      dfi_builder_write_id (builder, builder->locale_names, locale);
      dfi_builder_write_string (builder, locale, value);
    }

  return offset;
}

typedef guint (* DfiBuilderFunc) (DfiBuilder  *builder,
                                  const gchar *key,
                                  gpointer     data);

static guint
dfi_builder_write_pointer_array (DfiBuilder     *builder,
                                 DfiStringList  *key_list,
                                 guint           key_list_offset,
                                 GHashTable     *data_table,
                                 DfiBuilderFunc  func)
{
  const gchar * const *strings;
  guint *offsets;
  guint offset;
  guint n, i;

  strings = dfi_string_list_get_strings (key_list, &n);
  offsets = g_new0 (guint, n);

  for (i = 0; i < n; i++)
    {
      gpointer data;

      data = g_hash_table_lookup (data_table, strings[i]);
      offsets[i] = (* func) (builder, strings[i], data);
    }

  offset = dfi_builder_get_aligned (builder, sizeof (guint32));
  dfi_builder_write_uint32 (builder, key_list_offset);

  for (i = 0; i < n; i++)
    dfi_builder_write_uint32 (builder, offsets[i]);

  g_free (offsets);

  return offset;
}

static guint
dfi_builder_write_id_list (DfiBuilder  *builder,
                           const gchar *key,
                           gpointer     data)
{
  GArray *id_list = data;
  const guint16 *ids;
  guint offset;
  guint n_ids;
  guint i;

  if (data == NULL)
    return 0;

  ids = dfi_id_list_get_ids (id_list, &n_ids);

  offset = dfi_builder_write_uint16 (builder, n_ids);

  for (i = 0; i < n_ids; i++)
    dfi_builder_write_uint16 (builder, ids[i]);

  return offset;
}

static guint
dfi_builder_write_text_index (DfiBuilder  *builder,
                              const gchar *key,
                              gpointer     data)
{
  DfiTextIndex *text_index = data;
  const gchar *locale = key;
  GHashTable *string_table;
  const gchar * const *tokens;
  guint *id_lists;
  guint offset;
  guint n_items;
  guint i;

  string_table = dfi_builder_get_string_table (builder, locale);
  if (!dfi_string_table_is_written (string_table))
    {
      GHashTable *c_string_table;

      c_string_table = dfi_string_tables_get_table (builder->locale_string_tables, "");
      dfi_string_table_write (string_table, c_string_table, builder->string);
    }

  tokens = dfi_text_index_get_tokens (text_index, &n_items);
  id_lists = g_new (guint, n_items);

  dfi_builder_align (builder, sizeof (guint16));

  for (i = 0; i < n_items; i++)
    {
      DfiIdList *id_list;

      id_list = dfi_text_index_get_id_list_for_token (text_index, tokens[i]);
      id_lists[i] = dfi_builder_write_id_list (builder, NULL, id_list);
    }

  dfi_builder_align (builder, sizeof (guint32));

  offset = dfi_builder_get_offset (builder);

  dfi_builder_write_uint32 (builder, n_items);

  for (i = 0; i < n_items; i++)
    {
      dfi_builder_write_string (builder, locale, tokens[i]);
      dfi_builder_write_uint32 (builder, id_lists[i]);
    }

  g_free (id_lists);

  return offset;
}

enum
{
  DFI_ITEM_APP_NAMES,
  DFI_ITEM_KEY_NAMES,
  DFI_ITEM_LOCALE_NAMES,
  DFI_ITEM_GROUP_NAMES,
  DFI_ITEM_KEYFILE_CONTENTS,
  DFI_ITEM_MIME_INDEX,
  DFI_ITEM_IMPLEMENTS_INDEX,
  DFI_ITEM_TEXT_INDEX,
  DFI_N_ITEMS
};

static void
dfi_builder_serialise (DfiBuilder *builder)
{
  guint32 items[DFI_N_ITEMS] = { 0, };

  builder->string = g_string_new ("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
                                  "desktopfileindex"
                                  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
                                  "\0\0\0\0\0\0\0\0\0\0\0\0");

  /* Write the number of supported items.  This effectively serves as a
   * version field.
   */
  dfi_builder_write_uint32 (builder, DFI_N_ITEMS);

  /* Make space for the item offsets */
  g_string_set_size (builder->string, builder->string->len + sizeof items);

  /* Write out the C string table, filling in the offsets
   *
   * We have to do this first because all of the string lists (apps,
   * keys, locales, groups) are stored as strings in the C locale.
   */
  {
    GHashTable *c_table;

    c_table = dfi_builder_get_string_table (builder, "");
    dfi_string_table_write (c_table, NULL, builder->string);
  }

  /* Write out the string lists.  This will work because they only
   * refer to strings in the C locale.
   */
  {
    items[DFI_ITEM_APP_NAMES] = dfi_builder_write_string_list (builder, builder->app_names);
    items[DFI_ITEM_KEY_NAMES] = dfi_builder_write_string_list (builder, builder->key_names);
    items[DFI_ITEM_LOCALE_NAMES] = dfi_builder_write_string_list (builder, builder->locale_names);
    items[DFI_ITEM_GROUP_NAMES] = dfi_builder_write_string_list (builder, builder->group_names);
  }

  /* Write out the group implementors */
  {
    items[DFI_ITEM_IMPLEMENTS_INDEX] = dfi_builder_write_pointer_array (builder,
                                                                        builder->group_names,
                                                                        items[DFI_ITEM_GROUP_NAMES],
                                                                        builder->implementations,
                                                                        dfi_builder_write_id_list);
  }

  /* Write out the text indexes for the actual locales.
   *
   * Note: we do this by visiting each item in the locale string list,
   * which doesn't include the C locale, so we won't end up emitting the
   * C locale again here.
   *
   * Note: this function will write out the locale-specific string
   * tables alongside the table for each locale in order to improve
   * locality.
   */
  {
    items[DFI_ITEM_TEXT_INDEX] = dfi_builder_write_pointer_array (builder,
                                                                  builder->locale_names,
                                                                  items[DFI_ITEM_LOCALE_NAMES],
                                                                  builder->locale_text_indexes,
                                                                  dfi_builder_write_text_index);
  }

  /* Write out the desktop file contents.
   *
   * We have to do this last because the desktop files refer to strings
   * from all the locales and those are only actually written in the
   * last step.
   */
  {
    items[DFI_ITEM_KEYFILE_CONTENTS] = dfi_builder_write_pointer_array (builder,
                                                                        builder->app_names,
                                                                        items[DFI_ITEM_APP_NAMES],
                                                                        builder->desktop_files,
                                                                        dfi_builder_write_keyfile);
  }

  /* Write out the mime types index */
  {
    items[DFI_ITEM_MIME_INDEX] = dfi_builder_write_text_index (builder, NULL, builder->mime_types);
  }

  /* Replace the header */
  {
    guint32 *file = (guint32 *) builder->string->str;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (items); i++)
      file[i] = GUINT32_TO_LE (items[i]);
  }
}

static void
dfi_builder_add_strings_for_keyfile (DfiBuilder *builder,
                                     DfiKeyfile *keyfile)
{
  gint n_groups, n_items;
  gint i;

  n_groups = dfi_keyfile_get_n_groups (keyfile);
  n_items = dfi_keyfile_get_n_items (keyfile);

  for (i = 0; i < n_groups; i++)
    {
      const gchar *group_name;

      group_name = dfi_keyfile_get_group_name (keyfile, i);
      dfi_string_list_ensure (builder->group_names, group_name);
    }

  for (i = 0; i < n_items; i++)
    {
      const gchar *key, *locale, *value;

      dfi_keyfile_get_item (keyfile, i, &key, &locale, &value);

      dfi_string_list_ensure (builder->key_names, key);
      dfi_string_list_ensure (builder->locale_names, locale); /* may be "" */
      dfi_string_tables_add_string (builder->locale_string_tables, locale, value);
    }
}

static void
dfi_builder_add_strings (DfiBuilder *builder)
{
  GHashTableIter keyfile_iter;
  gpointer key, value;

  builder->locale_string_tables = dfi_string_tables_create ();
  builder->app_names = dfi_string_list_new ();
  builder->key_names = dfi_string_list_new ();
  builder->locale_names = dfi_string_list_new ();
  builder->group_names = dfi_string_list_new ();

  g_hash_table_iter_init (&keyfile_iter, builder->desktop_files);
  while (g_hash_table_iter_next (&keyfile_iter, &key, &value))
    {
      DfiKeyfile *keyfile = value;
      const gchar *app = key;

      dfi_string_list_ensure (builder->app_names, app);
      dfi_builder_add_strings_for_keyfile (builder, keyfile);
    }

  dfi_string_list_convert (builder->app_names);
  dfi_string_list_convert (builder->group_names);
  dfi_string_list_convert (builder->key_names);
  dfi_string_list_convert (builder->locale_names);

  {
    GHashTable *c_string_table;

    c_string_table = dfi_string_tables_get_table (builder->locale_string_tables, "");

    dfi_string_list_populate_strings (builder->app_names, c_string_table);
    dfi_string_list_populate_strings (builder->group_names, c_string_table);
    dfi_string_list_populate_strings (builder->key_names, c_string_table);
    dfi_string_list_populate_strings (builder->locale_names, c_string_table);
  }
}

static DfiTextIndex *
dfi_builder_index_one_locale (DfiBuilder  *builder,
                              const gchar *locale)
{
  const gchar *fields[] = { "Name", "GenericName", "X-GNOME-FullName", "Comment", "Keywords" };
  gchar **locale_variants;
  GHashTableIter keyfile_iter;
  gpointer key, val;
  DfiTextIndex *text_index;

  if (locale)
    locale_variants = g_get_locale_variants (locale);
  else
    locale_variants = g_new0 (gchar *, 0 + 1);

  text_index = dfi_text_index_new ();

  g_hash_table_iter_init (&keyfile_iter, builder->desktop_files);
  while (g_hash_table_iter_next (&keyfile_iter, &key, &val))
    {
      DfiKeyfile *kf = val;
      const gchar *app = key;
      guint i;

      for (i = 0; i < G_N_ELEMENTS (fields); i++)
        {
          const gchar *value;

          value = dfi_keyfile_get_value (kf, (const gchar **) locale_variants, "Desktop Entry", fields[i]);

          if (value)
            {
              guint16 ids[3];

              ids[0] = dfi_string_list_get_id (builder->app_names, app);
              ids[1] = dfi_string_list_get_id (builder->group_names, "Desktop Entry");
              ids[2] = dfi_string_list_get_id (builder->key_names, fields[i]);

              dfi_text_index_add_ids_tokenised (text_index, value, ids, 3);
            }
        }
    }

  g_free (locale_variants);

  dfi_text_index_convert (text_index);

  return text_index;
}

static void
dfi_builder_collect_implementations (DfiBuilder *builder)
{
  GHashTableIter keyfile_iter;
  gpointer key, val;

  g_hash_table_iter_init (&keyfile_iter, builder->desktop_files);
  while (g_hash_table_iter_next (&keyfile_iter, &key, &val))
    {
      DfiKeyfile *kf = val;
      const gchar *app = key;
      const gchar *implements;
      gchar **ifaces;
      gint i;

      implements = dfi_keyfile_get_value (kf, NULL, "Desktop Entry", "Implements");
      if (!implements)
        continue;

      ifaces = g_strsplit (implements, ";", -1);
      if (!ifaces)
        continue;

      for (i = 0; ifaces[i]; i++)
        {
          const gchar *iface = ifaces[i];
          DfiIdList *id_list;
          guint16 id;

          if (!iface[0])
            continue;

          dfi_string_list_ensure (builder->group_names, iface);

          id_list = g_hash_table_lookup (builder->implementations, iface);
          if (!id_list)
            {
              id_list = dfi_id_list_new ();
              g_hash_table_insert (builder->implementations, g_strdup (iface), id_list);
            }

          id = dfi_string_list_get_id (builder->app_names, app);
          dfi_id_list_add_ids (id_list, &id, 1);
        }

      g_strfreev (ifaces);
    }
}

static void
dfi_builder_index_strings (DfiBuilder *builder)
{
  const gchar * const *locale_names;
  GHashTable *c_string_table;
  guint i;

  c_string_table = dfi_string_tables_get_table (builder->locale_string_tables, "");
  builder->c_text_index = dfi_builder_index_one_locale (builder, "");
  dfi_text_index_populate_strings (builder->c_text_index, c_string_table);

  locale_names = dfi_string_list_get_strings (builder->locale_names, NULL);

  builder->locale_text_indexes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, dfi_text_index_free);

  for (i = 0; locale_names[i]; i++)
    {
      const gchar *locale = locale_names[i];
      GHashTable *string_table;
      DfiTextIndex *text_index;

      text_index = dfi_builder_index_one_locale (builder, locale);
      g_hash_table_insert (builder->locale_text_indexes, g_strdup (locale), text_index);
      string_table = dfi_string_tables_get_table (builder->locale_string_tables, locale);
      dfi_text_index_populate_strings (text_index, string_table);
    }
}

static DfiBuilder *
dfi_builder_new (void)
{
  DfiBuilder *builder;

  builder = g_slice_new0 (DfiBuilder);
  builder->desktop_files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) dfi_keyfile_free);
  builder->string = NULL;

  return builder;
}

static gboolean
dfi_builder_add_desktop_file (DfiBuilder   *builder,
                              const gchar  *desktop_id,
                              const gchar  *filename,
                              GError      **error)
{
  DfiKeyfile *kf;

  kf = dfi_keyfile_new (filename, error);
  if (!kf)
    return FALSE;

  g_hash_table_insert (builder->desktop_files, g_strdup (desktop_id), kf);

  return TRUE;
}

GBytes *
dfi_builder_build (const gchar  *desktop_dir,
                   GError      **error)
{
  DfiBuilder *builder;
  const gchar *name;
  GDir *dir;

  builder = dfi_builder_new ();

  dir = g_dir_open (desktop_dir, 0, error);
  if (!dir)
    return NULL;

  while ((name = g_dir_read_name (dir)))
    {
      gboolean success;
      gchar *fullname;

      if (!g_str_has_suffix (name, ".desktop"))
        continue;

      fullname = g_build_filename (desktop_dir, name, NULL);
      success = dfi_builder_add_desktop_file (builder, name, fullname, error);
      g_free (fullname);

      if (!success)
        return NULL;
    }
  g_dir_close (dir);

  dfi_builder_add_strings (builder);

  dfi_builder_collect_implementations (builder);

  dfi_builder_index_strings (builder);

  dfi_builder_serialise (builder);

  return g_bytes_new (builder->string->str, builder->string->len);
}
