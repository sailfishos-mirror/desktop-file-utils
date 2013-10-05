/*
 * Copyright (C) 2004-2006  Red Hat, Inc.
 * Copyright (C) 2006, 2008  Vincent Untz
 * Copyright © 2013 Canonical Limited
 *
 * Program written by Ray Strode <rstrode@redhat.com>
 *                    Vincent Untz <vuntz@gnome.org>
 *                    Ryan Lortie <desrt@desrt.ca>
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

#include <glib.h>
#include <glib/gi18n.h>

#include <sys/types.h>
#include <sys/time.h>

#include "mime-cache.h"
#include "dfi-builder.h"

#define CACHE_FILENAME "mimeinfo.cache"
#define INDEX_FILENAME "desktop-file-index"

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

static gboolean
update_database (const char  *desktop_dir,
                 GError     **error)
{
  gboolean success = FALSE;
  gchar *cache_file;
  gchar *index_file;
  GBytes *cache;
  GBytes *dfi;

  cache_file = g_build_filename (desktop_dir, CACHE_FILENAME, NULL);
  index_file = g_build_filename (desktop_dir, INDEX_FILENAME, NULL);
  dfi = NULL;

  cache = mime_cache_build (desktop_dir, error);
  if (!cache)
    goto out;

  dfi = dfi_builder_build (desktop_dir, error);
  if (!dfi)
    goto out;

  if (!g_file_set_contents (cache_file,
                            g_bytes_get_data (cache, NULL),
                            g_bytes_get_size (cache),
                            error))
    goto out;

  if (!g_file_set_contents (index_file,
                            g_bytes_get_data (dfi, NULL),
                            g_bytes_get_size (dfi),
                            error))
    goto out;

  /* Touch the timestamp after we have written both files in order to
   * ensure that each file has a timestamp newer than the directory
   * itself.
   */
  utimes (cache_file, NULL);
  utimes (index_file, NULL);

  success = TRUE;

out:
  g_bytes_unref (cache);
  g_bytes_unref (dfi);
  g_free (cache_file);
  g_free (index_file);

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
