/* update-desktop-database.c - maintains mimetype<->desktop mapping
 * cache
 *
 * Copyright 2004  Red Hat, Inc. 
 * 
 * Program written by Ray Strode <rstrode@redhat.com>
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <popt.h>
#include <glib.h>

#include "eggdesktopentries.h"
#include "eggdirfuncs.h"
#include "eggintl.h"

#define NAME "update-desktop-database"
#define CACHE_FILENAME "mimeinfo.cache"
#define TEMP_CACHE_FILENAME_PREFIX ".mimeinfo.cache."
#define TEMP_CACHE_FILENAME_MAX_LENGTH 64

#define udd_debug(...) if (verbose) g_printerr (__VA_ARGS__)

static int open_temp_cache_file (const char  *dir,
                                 char       **filename,
                                 GError     **error);
static void add_mime_type (const char *mime_type, GList *desktop_files, int fd);
static void sync_database (const char *dir, GError **error);
static void cache_desktop_file (const char  *desktop_file, 
                                const char  *mime_type,
                                GError     **error);
static gboolean is_valid_mime_type (const char *mime_type);
static void process_desktop_file (const char  *desktop_file, 
                                  const char  *name,
                                  GError     **error);
static void process_desktop_files (const char *desktop_dir,
                                   const char *prefix,
                                   GError **error);
static void update_database (const char *desktop_dir, GError **error);
static const char ** get_default_search_path (void);
static void print_desktop_dirs (const char **dirs);

static GHashTable *mime_types_map = NULL;
static gboolean verbose = FALSE, quiet = FALSE;

static void
cache_desktop_file (const char  *desktop_file,
                    const char  *mime_type,
                    GError     **error)
{
  GList *desktop_files;

  desktop_files = (GList *) g_hash_table_lookup (mime_types_map, mime_type);

  desktop_files = g_list_prepend (desktop_files, g_strdup (desktop_file));
  g_hash_table_insert (mime_types_map, g_strdup (mime_type), desktop_files);
}

static gboolean
is_valid_mime_type (const char *mime_type)
{
  char *index;
  index = strchr (mime_type, '/');

  return index != NULL && index != mime_type && index[1] != '\0';
}

static void
process_desktop_file (const char  *desktop_file, 
                      const char  *name,
                      GError     **error)
{
  GError *load_error; EggDesktopEntries *entries;
  char **mime_types; 
  int i;

  load_error = NULL;
  entries = 
    egg_desktop_entries_new_from_file (EGG_DESKTOP_ENTRIES_DISCARD_COMMENTS |
                                       EGG_DESKTOP_ENTRIES_DISCARD_TRANSLATIONS,
                                       desktop_file, &load_error);

  if (load_error != NULL) {
    g_propagate_error (error, load_error);
    return;
  }

  mime_types = egg_desktop_entries_get_string_list (entries, 
                                              egg_desktop_entries_get_default_group (entries),
                                              "MimeType", NULL, &load_error);

  if (load_error != NULL) 
    {
      g_propagate_error (error, load_error);
      egg_desktop_entries_free (entries);
      return;
    }

  for (i = 0; mime_types[i] != NULL; i++)
    {
      if (!is_valid_mime_type (mime_types[i]))
        continue;

      cache_desktop_file (name, mime_types[i], &load_error);

      if (load_error != NULL)
        {
          g_propagate_error (error, load_error);
          return;
        }
    }

  egg_desktop_entries_free (entries);
}

static void
process_desktop_files (const char  *desktop_dir,
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

          process_desktop_files (full_path, sub_prefix, &process_error);
          g_free (sub_prefix);

          if (process_error != NULL)
            {
              udd_debug ("Could not process directory '%s':\n"
                         "\t%s\n", full_path, process_error->message);
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
      process_desktop_file (full_path, name, &process_error);
      g_free (name);

      if (process_error != NULL)
        {
          if (!g_error_matches (process_error, 
                                EGG_DESKTOP_ENTRIES_ERROR,
                                EGG_DESKTOP_ENTRIES_ERROR_KEY_NOT_FOUND))
            udd_debug ("Could not parse file '%s': %s\n", full_path,
                        process_error->message);

          g_error_free (process_error);
          process_error = NULL;
        }

      g_free (full_path);
    }

  g_dir_close (dir);
}

static int
open_temp_cache_file (const char *dir, char **filename, GError **error)
{
  GRand *rand;
  GString *candidate_filename;
  int fd;

  enum 
    { 
      CHOICE_UPPER_CASE = 0, 
      CHOICE_LOWER_CASE, 
      CHOICE_DIGIT, 
      NUM_CHOICES
    } choice;

  candidate_filename = g_string_new (TEMP_CACHE_FILENAME_PREFIX);

  /* Generate a unique candidate_filename and open it for writing
   */
  rand = g_rand_new ();
  fd = -1;
  while (fd < 0)
    {
      char *full_path;

      if (candidate_filename->len > TEMP_CACHE_FILENAME_MAX_LENGTH) 
        g_string_assign (candidate_filename, TEMP_CACHE_FILENAME_PREFIX);

      choice = g_rand_int_range (rand, 0, NUM_CHOICES);

      switch (choice)
        {
        case CHOICE_UPPER_CASE:
          g_string_append_c (candidate_filename,
                             g_rand_int_range (rand, 'A' , 'Z' + 1));
          break;
        case CHOICE_LOWER_CASE:
          g_string_append_c (candidate_filename,
                             g_rand_int_range (rand, 'a' , 'z' + 1));
          break;
        case CHOICE_DIGIT:
          g_string_append_c (candidate_filename,
                             g_rand_int_range (rand, '0' , '9' + 1));
          break;
        default:
          g_assert_not_reached ();
          break;
        }

      full_path = g_build_filename (dir, candidate_filename->str, NULL);
      fd = open (full_path, O_CREAT | O_WRONLY | O_EXCL, 0644); 

      if (fd < 0) 
        {
          if (errno != EEXIST)
            {
              g_set_error (error, G_FILE_ERROR,
                           g_file_error_from_errno (errno),
                           "%s", g_strerror (errno));
              break;
            }
        }
      else if (fd >= 0 && filename != NULL)
        {
          *filename = full_path;
          break;
        }

      g_free (full_path);
    }

  g_rand_free (rand);

  g_string_free (candidate_filename, TRUE);

  return fd;
}

static void
add_mime_type (const char *mime_type, GList *desktop_files, int fd)
{
  GString *list;
  GList *desktop_file;

  list = g_string_new("");
  for (desktop_file = desktop_files;
       desktop_file != NULL; 
       desktop_file = desktop_file->next)
    {
      g_string_append (list, (const char *) desktop_file->data);

      if (desktop_files->next != NULL)
        g_string_append_c (list, ';');
    }

  write (fd, mime_type, strlen(mime_type));
  write (fd, "=", 1);
  write (fd, list->str, list->len);
  write (fd, "\n", 1);

  g_string_free (list, TRUE);
}

static void
sync_database (const char *dir, GError **error)
{
  GError *sync_error;
  char *temp_cache_file, *cache_file;
  int fd;

  sync_error = NULL;
  fd = open_temp_cache_file (dir, &temp_cache_file, &sync_error);

  if (sync_error != NULL)
    {
      g_propagate_error (error, sync_error);
      return;
    }

  write (fd, "[MIME Cache]\n", sizeof ("[MIME Cache\n]") - 1);
  g_hash_table_foreach (mime_types_map, (GHFunc) add_mime_type,
                        GINT_TO_POINTER (fd));

  close (fd);

  cache_file = g_build_filename (dir, CACHE_FILENAME, NULL);
  if (rename (temp_cache_file, cache_file) < 0)
    {
      g_set_error (error, G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   _("Cache file '%s' could not be written: %s"),
                   cache_file, g_strerror (errno));

      unlink (temp_cache_file);
    }
  g_free (cache_file);
}

static void
update_database (const char  *desktop_dir, 
                 GError     **error)
{
  GError *update_error;

  mime_types_map = g_hash_table_new (g_str_hash, g_str_equal);

  update_error = NULL;
  process_desktop_files (desktop_dir, "", &update_error);

  if (update_error != NULL)
    g_propagate_error (error, update_error);
  else
    {
      sync_database (desktop_dir, &update_error);
      if (update_error != NULL)
        g_propagate_error (error, update_error);
    }

  g_hash_table_destroy (mime_types_map);
}

static const char **
get_default_search_path (void)
{
  static char **args = NULL;
  char **data_dirs;
  int i;

  if (args != NULL)
    return (const char **) args;

  data_dirs = egg_get_secondary_data_dirs ();

  for (i = 0; data_dirs[i] != NULL; i++);

  args = g_new (char *, i + 1);

  for (i = 0; data_dirs[i] != NULL; i++)
    args[i] = g_build_filename (data_dirs[i], "applications", NULL);

  g_strfreev (data_dirs);

  return (const char **) args;
}

void
print_desktop_dirs (const char **dirs)
{
  int i;
  const char *delimiter;

  udd_debug(_("Search path is now: ["));
  delimiter = "";
  for (i = 0; dirs[i] != NULL; i++)
    {
      udd_debug (delimiter);
      delimiter = ", ";
      udd_debug (dirs[i]);
    }
  udd_debug ("]\n");
}

int
main (int    argc,
      char **argv)
{
  GError *error;
  poptContext popt_context;
  const char **desktop_dirs;
  int i;
  gboolean found_processable_dir;

  struct poptOption options[] =
   {
     { "verbose", 'v', POPT_ARG_NONE, &verbose, 0, 
       N_("Display more information about processing and updating progress")},

     { "quiet", 'q', POPT_ARG_NONE, &quiet, 0, 
       N_("Don't display any information about about processing and "
          "updating progress")},

     POPT_AUTOHELP

     { NULL, 0, 0, NULL, 0 }
   };

  popt_context = poptGetContext (NAME, argc, (const char **) argv,
                                 options, 0);
  while ((i = poptGetNextOpt (popt_context)) != -1)
    {
      if (i < -1)
        {
          poptPrintHelp (popt_context, stderr, 0);
          return 1;
        }
    }

  desktop_dirs = poptGetArgs (popt_context);

  if (desktop_dirs == NULL || desktop_dirs[0] == NULL)
    desktop_dirs = get_default_search_path ();

  if (verbose)
    print_desktop_dirs (desktop_dirs);

  found_processable_dir = FALSE;
  for (i = 0; desktop_dirs[i] != NULL; i++)
    {
      error = NULL;
      update_database (desktop_dirs[i], &error);

      if (error != NULL)
        {
          udd_debug (_("Could not create cache file in directory '%s':\n"
                       "\t%s\n"),
                     desktop_dirs[i], error->message);
          g_error_free (error);
          error = NULL;
        }
      else
        found_processable_dir = TRUE;
    }
  poptFreeContext (popt_context);

  if (!found_processable_dir)
    {
      if (!quiet)
        g_printerr (_("No directories in update-desktop-database search path "
                      "could processed and updated.\n"));
      return 1;
    }

  return 0;
}
