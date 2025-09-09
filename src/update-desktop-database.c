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
#define MIMEINFO_CACHE_FILENAME "mimeinfo.cache"

#define udd_print(...) if (!opt_quiet) g_printerr (__VA_ARGS__)
#define udd_verbose_print(...) if (!opt_quiet && opt_verbose) g_printerr (__VA_ARGS__)

static gboolean opt_verbose = FALSE, opt_quiet = FALSE;

static gboolean
mime_type_map_add_desktop_file (GHashTable  *mime_types_map,
                                const char  *mime_type,
                                const char  *desktop_file,
                                GError     **error)
{
  GList *owned_desktop_files;
  g_autofree char *owned_mime_type = NULL;

  if (!g_hash_table_steal_extended (mime_types_map,
                                    mime_type,
                                    (gpointer*) &owned_mime_type,
                                    (gpointer*) &owned_desktop_files))
    {
      owned_mime_type = g_strdup (mime_type);
    }

  /* do not add twice a desktop file mentioning the mime type more than once
   * (no need to use g_list_find() because we cache all mime types registered
   * by a desktop file before moving to another desktop file) */
  if (!owned_desktop_files ||
      strcmp (desktop_file, (const char *) owned_desktop_files->data) != 0)
    {
      owned_desktop_files = g_list_prepend (owned_desktop_files,
                                            g_strdup (desktop_file));
    }

  g_hash_table_insert (mime_types_map,
                       g_steal_pointer (&owned_mime_type),
                       g_steal_pointer (&owned_desktop_files));

  return TRUE;
}

static gboolean
process_desktop_file_mime (GKeyFile    *keyfile,
                           const char  *desktop_file,
                           GHashTable  *mime_types_map,
                           const char  *name,
                           GError     **error)
{
  g_auto(GStrv) mime_types = NULL;
  g_autoptr(GError) local_error = NULL;

  mime_types = g_key_file_get_string_list (keyfile,
                                           GROUP_DESKTOP_ENTRY,
                                           "MimeType",
                                           NULL,
                                           &local_error);
  if (!mime_types)
    {
      if (g_error_matches (local_error,
                           G_KEY_FILE_ERROR,
                           G_KEY_FILE_ERROR_KEY_NOT_FOUND))
        {
          udd_verbose_print (_("File \"%s\" lacks MimeType key\n"),
                             desktop_file);
          return TRUE;
        }
      else
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }

  for (size_t i = 0; mime_types[i] != NULL; i++)
    {
      char *mime_type;
      MimeUtilsValidity valid;
      g_autofree char *valid_error = NULL;

      mime_type = g_strchomp (mime_types[i]);
      valid = mu_mime_type_is_valid (mime_types[i], &valid_error);
      switch (valid)
        {
        case MU_VALID:
          break;
        case MU_DISCOURAGED:
          udd_print (_("Warning in file \"%s\": usage of MIME type \"%s\" is "
                       "discouraged (%s)\n"),
                     desktop_file, mime_types[i], valid_error);
          break;
        case MU_INVALID:
          udd_print (_("Error in file \"%s\": \"%s\" is an invalid MIME type "
                       "(%s)\n"),
                     desktop_file, mime_types[i], valid_error);
          /* not a break: we continue to the next mime type */
          continue;
        default:
          g_assert_not_reached ();
        }

      if (!mime_type_map_add_desktop_file (mime_types_map,
                                           mime_type,
                                           name,
                                           error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
process_desktop_file (const char  *desktop_file,
                      GHashTable  *mime_types_map,
                      const char  *name,
                      GError     **error)
{
  g_autoptr(GKeyFile) keyfile = NULL;

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_file (keyfile,
                                  desktop_file,
                                  G_KEY_FILE_NONE,
                                  error))
    return FALSE;

  /* Hidden=true means that the .desktop file should be completely ignored */
  if (g_key_file_get_boolean (keyfile, GROUP_DESKTOP_ENTRY, "Hidden", NULL))
    return TRUE;

  if (!process_desktop_file_mime (keyfile,
                                  desktop_file,
                                  mime_types_map,
                                  name,
                                  error))
    return FALSE;

  return TRUE;
}

static gboolean
process_desktop_files (const char  *desktop_dir,
                       GHashTable  *mime_types_map,
                       const char  *prefix,
                       GError     **error)
{
  g_autoptr(GDir) dir = NULL;
  const char *filename;

  dir = g_dir_open (desktop_dir, 0, error);
  if (!dir)
    return FALSE;

  while ((filename = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *full_path = NULL;
      g_autofree char *name = NULL;
      g_autoptr(GError) process_error = NULL;

      full_path = g_build_filename (desktop_dir, filename, NULL);

      if (g_file_test (full_path, G_FILE_TEST_IS_DIR))
        {
          g_autofree char *sub_prefix = NULL;

          sub_prefix = g_strdup_printf ("%s%s-", prefix, filename);
          if (!process_desktop_files (full_path,
                                      mime_types_map,
                                      sub_prefix,
                                      &process_error))
            {
              udd_verbose_print (_("Could not process directory \"%s\": %s\n"),
                                 full_path, process_error->message);
            }
          continue;
        }
      else if (!g_str_has_suffix (filename, ".desktop"))
        {
          continue;
        }

      name = g_strdup_printf ("%s%s", prefix, filename);
      if (!process_desktop_file (full_path,
                                 mime_types_map,
                                 name,
                                 &process_error))
        {
          udd_print (_("Could not parse file \"%s\": %s\n"), full_path,
                     process_error->message);
        }
    }

  return TRUE;
}

static void
serialize_mime_cache_for_type (GString    *contents,
                               const char *mime_type,
                               GList      *desktop_files)
{
  GList *sorted_desktop_files;

  g_string_append (contents, mime_type);
  g_string_append_c (contents, '=');

  sorted_desktop_files = g_list_sort (desktop_files,
                                      (GCompareFunc) g_strcmp0);

  for (GList *l = sorted_desktop_files; l != NULL; l = l->next)
    {
      g_string_append (contents, (const char *) l->data);
      g_string_append_c (contents, ';');
    }

  g_string_append_c (contents, '\n');
}

static char *
serialize_mime_cache (const char  *dir,
                      GHashTable  *mime_types_map,
                      GError     **error)
{
  g_autoptr(GList) keys = NULL;
  g_autoptr(GString) contents = g_string_new ("[MIME Cache]\n");

  keys = g_hash_table_get_keys (mime_types_map);
  keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);

  for (GList *l = keys; l != NULL; l = l->next)
    {
      const char *mime_type = l->data;
      GList *desktop_files = g_hash_table_lookup (mime_types_map, mime_type);

      serialize_mime_cache_for_type (contents, mime_type, desktop_files);
    }

  return g_string_free_and_steal (g_steal_pointer (&contents));
}

static gboolean
update_mime_cache_database (const char  *dir,
                            GHashTable  *mime_types_map,
                            GError     **error)
{
  g_autofree char *mime_cache_file = NULL;
  g_autofree char *mime_cache_contents = NULL;

  mime_cache_contents = serialize_mime_cache (dir, mime_types_map, error);
  if (!mime_cache_contents)
    return FALSE;

  mime_cache_file = g_build_filename (dir, MIMEINFO_CACHE_FILENAME, NULL);
  if (!g_file_set_contents_full (mime_cache_file,
                                 mime_cache_contents,
                                 -1,
                                 G_FILE_SET_CONTENTS_CONSISTENT,
                                 0666,
                                 error))
      return FALSE;

  return TRUE;
}

static void
list_free_deep (GList *l)
{
  g_list_free_full (l, g_free);
}

static gboolean
update_databases (const char  *desktop_dir,
                  GError     **error)
{
  g_autoptr(GHashTable) mime_types_map = NULL;

  mime_types_map = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          (GDestroyNotify) g_free,
                                          (GDestroyNotify) list_free_deep);

  if (!process_desktop_files (desktop_dir,
                              mime_types_map,
                              "",
                              error))
    return FALSE;

  if (!update_mime_cache_database (desktop_dir, mime_types_map, error))
    return FALSE;

  return TRUE;
}

static GStrv
get_default_search_path (const char **desktop_dirs)
{
  g_autoptr(GStrvBuilder) builder = NULL;
  const char * const *data_dirs;

  if (desktop_dirs)
    return g_strdupv ((GStrv) desktop_dirs);

  builder = g_strv_builder_new ();
  data_dirs = g_get_system_data_dirs ();

  for (size_t i = 0; data_dirs[i] != NULL; i++)
    {
      g_autofree char *path = NULL;

      path = g_build_filename (data_dirs[i], "applications", NULL);
      g_strv_builder_add (builder, path);
    }

  return g_strv_builder_end (builder);
}

static void
print_desktop_dirs (GStrv dirs)
{
  g_autofree char *directories = NULL;

  directories = g_strjoinv (", ", (char **) dirs);
  udd_verbose_print (_("Search path is now: [%s]\n"), directories);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr(GOptionContext) context = NULL;
  g_auto(GStrv) desktop_dirs = NULL;
  gboolean found_processable_dir;
  g_autoptr(GError) error = NULL;

  gboolean opt_print_version = FALSE;
  const char **opt_desktop_dirs = NULL;

  const GOptionEntry options[] =
   {
     { "quiet", 'q', 0, G_OPTION_ARG_NONE, &opt_quiet,
       N_("Do not display any information about processing and "
          "updating progress"), NULL},

     { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose,
       N_("Display more information about processing and updating progress"),
       NULL},

     { "version", 0, 0, G_OPTION_ARG_NONE, &opt_print_version,
       N_("Show the program version"),
       NULL},

     { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_desktop_dirs,
       NULL, N_("[DIRECTORY...]") },
     { NULL }
   };

#ifdef HAVE_PLEDGE
  if (pledge ("stdio rpath wpath cpath fattr", NULL) == -1) {
    g_printerr ("pledge\n");
    return 1;
  }
#endif

  context = g_option_context_new ("");
  g_option_context_set_summary (context, _("Build cache database of MIME types handled by desktop files."));
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      g_printerr (_("Run \"%s --help\" to see a full list of available command line options.\n"), argv[0]);
      return 1;
    }

  if (opt_print_version)
    {
      g_print("update-desktop-database %s\n", VERSION);
      return 0;
    }

  desktop_dirs = get_default_search_path (opt_desktop_dirs);

  print_desktop_dirs (desktop_dirs);

  found_processable_dir = FALSE;
  for (size_t i = 0; desktop_dirs[i] != NULL; i++)
    {
      g_autoptr(GError) update_error = NULL;

      if (!update_databases (desktop_dirs[i], &update_error))
        {
          udd_verbose_print (_("Could not create cache file in \"%s\": %s\n"),
                             desktop_dirs[i], update_error->message);
          continue;
        }

      found_processable_dir = TRUE;
    }

  if (!found_processable_dir)
    {
      g_autofree char *directories = NULL;

      directories = g_strjoinv (", ", (char **) desktop_dirs);
      udd_print (_("The databases in [%s] could not be updated.\n"),
                 directories);

      return 1;
    }

  return 0;
}
