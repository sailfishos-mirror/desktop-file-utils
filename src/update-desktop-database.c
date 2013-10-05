/* update-desktop-database.c - maintains mimetype<->desktop mapping cache
 * vim: set ts=2 sw=2 et: */

/*
 * Copyright (C) 2004-2006  Red Hat, Inc.
 * Copyright (C) 2006, 2008  Vincent Untz
 *
 * Program written by Ray Strode <rstrode@redhat.com>
 *                    Vincent Untz <vuntz@gnome.org>
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
 */

#include <config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "keyfileutils.h"
#include "mimeutils.h"

#define NAME "update-desktop-database"
#define CACHE_FILENAME "mimeinfo.cache"

static gboolean verbose = FALSE, quiet = FALSE;

static void
log_handler (const gchar    *log_domain,
             GLogLevelFlags  log_level,
             const gchar    *message,
             gpointer        user_data)
{
  if (g_str_equal (log_domain, G_LOG_DOMAIN))
    {
      if (verbose || ((log_level & G_LOG_LEVEL_WARNING) && !quiet))
        g_printerr ("%s\n", message);
    }
  else
    g_log_default_handler (log_domain, log_level, message, NULL);
}

static void
list_free_deep (gpointer key, GList *l, gpointer data)
{
  g_list_foreach (l, (GFunc)g_free, NULL);
  g_list_free (l);
}

static void
cache_desktop_file (GHashTable  *mime_types_map,
                    const char  *desktop_file,
                    const char  *mime_type,
                    GError     **error)
{
  GList *desktop_files;

  desktop_files = (GList *) g_hash_table_lookup (mime_types_map, mime_type);

  /* do not add twice a desktop file mentioning the mime type more than once
   * (no need to use g_list_find() because we cache all mime types registered
   * by a desktop file before moving to another desktop file) */
  if (desktop_files &&
      strcmp (desktop_file, (const char *) desktop_files->data) == 0)
    return;

  desktop_files = g_list_prepend (desktop_files, g_strdup (desktop_file));
  g_hash_table_insert (mime_types_map, g_strdup (mime_type), desktop_files);
}


static void
process_desktop_file (GHashTable  *mime_types_map,
                      const char  *desktop_file,
                      const char  *name,
                      GError     **error)
{
  GError *load_error;
  GKeyFile *keyfile;
  char **mime_types;
  int i;

  keyfile = g_key_file_new ();

  load_error = NULL;
  g_key_file_load_from_file (keyfile, desktop_file,
                             G_KEY_FILE_NONE, &load_error);

  if (load_error != NULL)
    {
      g_propagate_error (error, load_error);
      return;
    }

  /* Hidden=true means that the .desktop file should be completely ignored */
  if (g_key_file_get_boolean (keyfile, GROUP_DESKTOP_ENTRY, "Hidden", NULL))
    {
      g_key_file_free (keyfile);
      return;
    }

  mime_types = g_key_file_get_string_list (keyfile,
                                           GROUP_DESKTOP_ENTRY,
                                           "MimeType", NULL, &load_error);

  g_key_file_free (keyfile);

  if (load_error != NULL)
    {
      g_propagate_error (error, load_error);
      return;
    }

  for (i = 0; mime_types[i] != NULL; i++)
    {
      char *mime_type;
      MimeUtilsValidity valid;
      char *valid_error;

      mime_type = g_strchomp (mime_types[i]);
      valid = mu_mime_type_is_valid (mime_types[i], &valid_error);
      switch (valid)
      {
        case MU_VALID:
          break;
        case MU_DISCOURAGED:
          g_warning (_("Warning in file \"%s\": usage of MIME type \"%s\" is "
                       "discouraged (%s)\n"),
                     desktop_file, mime_types[i], valid_error);
          g_free (valid_error);
          break;
        case MU_INVALID:
          g_warning (_("Error in file \"%s\": \"%s\" is an invalid MIME type "
                       "(%s)\n"),
                     desktop_file, mime_types[i], valid_error);
          g_free (valid_error);
          /* not a break: we continue to the next mime type */
          continue;
        default:
          g_assert_not_reached ();
      }

      cache_desktop_file (mime_types_map, name, mime_type, &load_error);

      if (load_error != NULL)
        {
          g_propagate_error (error, load_error);
          g_strfreev (mime_types);
          return;
        }
    }
  g_strfreev (mime_types);
}

static void
process_desktop_files (GHashTable  *mime_types_map,
                       const char  *desktop_dir,
                       const char  *prefix,
                       GError     **error)
{
  GError *process_error;
  GDir *dir;
  const char *filename;

  process_error = NULL;
  dir = g_dir_open (desktop_dir, 0, &process_error);

  if (process_error != NULL)
    {
      g_propagate_error (error, process_error);
      return;
    }

  while ((filename = g_dir_read_name (dir)) != NULL)
    {
      char *full_path, *name;

      full_path = g_build_filename (desktop_dir, filename, NULL);

      if (g_file_test (full_path, G_FILE_TEST_IS_DIR))
        {
          char *sub_prefix;

          sub_prefix = g_strdup_printf ("%s%s-", prefix, filename);

          process_desktop_files (mime_types_map, full_path, sub_prefix, &process_error);
          g_free (sub_prefix);

          if (process_error != NULL)
            {
              g_warning (_("Could not process directory \"%s\": %s\n"), full_path, process_error->message);
              g_error_free (process_error);
              process_error = NULL;
            }
          g_free (full_path);
          continue;
        }
      else if (!g_str_has_suffix (filename, ".desktop"))
        {
          g_free (full_path);
          continue;
        }

      name = g_strdup_printf ("%s%s", prefix, filename);
      process_desktop_file (mime_types_map, full_path, name, &process_error);
      g_free (name);

      if (process_error != NULL)
        {
          if (!g_error_matches (process_error,
                                G_KEY_FILE_ERROR,
                                G_KEY_FILE_ERROR_KEY_NOT_FOUND))
            {
              g_warning (_("Could not parse file \"%s\": %s\n"), full_path, process_error->message);
            }
          else
            {
              g_debug (_("File \"%s\" lacks MimeType key\n"), full_path);
            }

          g_error_free (process_error);
          process_error = NULL;
        }

      g_free (full_path);
    }

  g_dir_close (dir);
}

static void
add_mime_type (const char *mime_type, GList *desktop_files, GString *str)
{
  GList *desktop_file;

  g_string_append (str, mime_type);
  g_string_append_c (str, '=');
  for (desktop_file = desktop_files;
       desktop_file != NULL;
       desktop_file = desktop_file->next)
    {
      g_string_append (str, (const char *) desktop_file->data);
      g_string_append_c (str, ';');
    }
  g_string_append_c (str, '\n');
}

static GBytes *
mime_cache_build (const char  *desktop_dir,
                  GError     **error)
{
  GHashTable *mime_types_map;
  GError *update_error;
  GList *keys, *key;
  GString *contents;
  GBytes *result;
  gsize size;

  result = NULL;

  mime_types_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  update_error = NULL;
  process_desktop_files (mime_types_map, desktop_dir, "", &update_error);

  if (update_error != NULL)
    {
      g_propagate_error (error, update_error);
      goto out;
    }

  contents = g_string_new ("[MIME Cache]\n");

  keys = g_hash_table_get_keys (mime_types_map);
  keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);

  for (key = keys; key != NULL; key = key->next)
    add_mime_type (key->data, g_hash_table_lookup (mime_types_map, key->data), contents);

  g_list_free (keys);

  size = contents->len;
  result = g_bytes_new_take (g_string_free (contents, FALSE), size);

out:
  g_hash_table_foreach (mime_types_map, (GHFunc) list_free_deep, NULL);
  g_hash_table_unref (mime_types_map);

  return result;
}

static gboolean
update_database (const char  *desktop_dir,
                 GError     **error)
{
  gboolean success;
  char *cache_file;
  GBytes *cache;

  cache = mime_cache_build (desktop_dir, error);
  if (!cache)
    return FALSE;

  cache_file = g_build_filename (desktop_dir, CACHE_FILENAME, NULL);
  success = g_file_set_contents (cache_file,
                                 g_bytes_get_data (cache, NULL),
                                 g_bytes_get_size (cache),
                                 error);
  g_bytes_unref (cache);
  g_free (cache_file);

  return success;
}

static const char **
get_default_search_path (void)
{
  static char **args = NULL;
  const char * const *data_dirs;
  int i;

  if (args != NULL)
    return (const char **) args;

  data_dirs = g_get_system_data_dirs ();

  for (i = 0; data_dirs[i] != NULL; i++);

  args = g_new (char *, i + 1);

  for (i = 0; data_dirs[i] != NULL; i++)
    args[i] = g_build_filename (data_dirs[i], "applications", NULL);

  args[i] = NULL;

  return (const char **) args;
}

static void
print_desktop_dirs (const char **dirs)
{
  char *directories;

  directories = g_strjoinv (", ", (char **) dirs);
  g_debug (_("Search path is now: [%s]\n"), directories);
  g_free (directories);
}

int
main (int    argc,
      char **argv)
{
  GError *error;
  GOptionContext *context;
  const char **desktop_dirs;
  int i;
  gboolean found_processable_dir;

  const GOptionEntry options[] =
   {
     { "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
       N_("Do not display any information about processing and "
          "updating progress"), NULL},

     { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
       N_("Display more information about processing and updating progress"),
       NULL},

     { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &desktop_dirs,
       NULL, N_("[DIRECTORY...]") },
     { NULL }
   };

  context = g_option_context_new ("");
  g_option_context_set_summary (context, _("Build cache database of MIME types handled by desktop files."));
  g_option_context_add_main_entries (context, options, NULL);

  desktop_dirs = NULL;
  error = NULL;
  g_option_context_parse (context, &argc, &argv, &error);

  if (error != NULL) {
    g_printerr ("%s\n", error->message);
    g_printerr (_("Run \"%s --help\" to see a full list of available command line options.\n"), argv[0]);
    g_error_free (error);
    return 1;
  }

  g_log_set_default_handler (log_handler, NULL);

  if (desktop_dirs == NULL || desktop_dirs[0] == NULL)
    desktop_dirs = get_default_search_path ();

  print_desktop_dirs (desktop_dirs);

  found_processable_dir = FALSE;
  for (i = 0; desktop_dirs[i] != NULL; i++)
    {
      error = NULL;
      update_database (desktop_dirs[i], &error);

      if (error != NULL)
        {
          g_warning (_("Could not create cache file in \"%s\": %s\n"), desktop_dirs[i], error->message);
          g_error_free (error);
          error = NULL;
        }
      else
        found_processable_dir = TRUE;
    }
  g_option_context_free (context);

  if (!found_processable_dir)
    {
      char *directories;

      directories = g_strjoinv (", ", (char **) desktop_dirs);
      g_warning (_("The databases in [%s] could not be updated.\n"), directories);

      g_free (directories);

      return 1;
    }

  return 0;
}
