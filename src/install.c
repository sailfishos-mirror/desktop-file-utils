
#include <config.h>

#include <glib.h>
#include <popt.h>

#include "desktop_file.h"
#include "validate.h"

#include <libintl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define _(x) gettext ((x))
#define N_(x) x

static gboolean delete_original = FALSE;
static gboolean copy_generic_name_to_name = FALSE;
static gboolean copy_name_to_generic_name = FALSE;
static char *vendor_name = NULL;
static char *target_dir = NULL;
static GSList *added_categories = NULL;
static GSList *removed_categories = NULL;
static GSList *added_only_show_in = NULL;
static GSList *removed_only_show_in = NULL;
static mode_t permissions = 0644;

static gboolean
str_has_prefix (const char *str,
                const char *prefix)
{
  int str_len;
  int prefix_len;
  
  g_return_val_if_fail (str != NULL, FALSE);
  g_return_val_if_fail (prefix != NULL, FALSE);

  str_len = strlen (str);
  prefix_len = strlen (prefix);

  if (str_len < prefix_len)
    return FALSE;
  
  return strncmp (str, prefix, prefix_len) == 0;
}

static gboolean
files_are_the_same (const char *first,
                    const char *second)
{
  /* This check gets confused by hard links.
   * but it's too annoying to check if two
   * paths are the same (though I'm sure there's a
   * "path canonicalizer" I should be using...)
   */

  struct stat first_sb;
  struct stat second_sb;

  if (stat (first, &first_sb) < 0)
    {
      g_printerr (_("Could not stat \"%s\": %s\n"), first, g_strerror (errno));
      exit (1);
    }

  if (stat (second, &second_sb) < 0)
    {
      g_printerr (_("Could not stat \"%s\": %s\n"), first, g_strerror (errno));
      exit (1);
    }
  
  return ((first_sb.st_dev == second_sb.st_dev) &&
          (first_sb.st_ino == second_sb.st_ino) &&
          /* Broken paranoia in case an OS doesn't use inodes or something */
          (first_sb.st_size == second_sb.st_size) &&
          (first_sb.st_mtime == second_sb.st_mtime));
}

static void
process_one_file (const char *filename,
                  GError    **err)
{
  char *new_filename;
  char *dirname;
  char *basename;
  GnomeDesktopFile *df = NULL;
  GSList *tmp;
  
  g_assert (vendor_name);

  dirname = g_path_get_dirname (filename);
  basename = g_path_get_basename (filename);
  
  if (!str_has_prefix (basename, vendor_name))
    {
      char *new_base;
      new_base = g_strconcat (vendor_name, "-", basename, NULL);
      new_filename = g_build_filename (target_dir, new_base, NULL);
      g_free (new_base);
    }
  else
    {
      new_filename = g_build_filename (target_dir, basename, NULL);
    }

  g_free (dirname);
  g_free (basename);

  df = gnome_desktop_file_load (filename, err);
  if (df == NULL)
    goto cleanup;

  if (!desktop_file_fixup (df, filename))
    exit (1);

  if (copy_name_to_generic_name)
    gnome_desktop_file_copy_key (df, NULL, "Name", "GenericName");

  if (copy_generic_name_to_name)
    gnome_desktop_file_copy_key (df, NULL, "GenericName", "Name");
  
  /* Mark file as having been processed by us, so automated
   * tools can check that desktop files went through our
   * munging
   */
  gnome_desktop_file_set_raw (df, NULL, "X-Desktop-File-Install-Version", NULL, VERSION);

  /* Add categories */
  tmp = added_categories;
  while (tmp != NULL)
    {
      gnome_desktop_file_merge_string_into_list (df, NULL, "Categories",
                                                 NULL, tmp->data);

      tmp = tmp->next;
    }

  /* Remove categories */
  tmp = removed_categories;
  while (tmp != NULL)
    {
      gnome_desktop_file_remove_string_from_list (df, NULL, "Categories",
                                                  NULL, tmp->data);

      tmp = tmp->next;
    }

  /* Add onlyshowin */
  tmp = added_only_show_in;
  while (tmp != NULL)
    {
      gnome_desktop_file_merge_string_into_list (df, NULL, "OnlyShowIn",
                                                 NULL, tmp->data);

      tmp = tmp->next;
    }

  /* Remove onlyshowin */
  tmp = removed_only_show_in;
  while (tmp != NULL)
    {
      gnome_desktop_file_remove_string_from_list (df, NULL, "OnlyShowIn",
                                                  NULL, tmp->data);

      tmp = tmp->next;
    }
  
  if (!gnome_desktop_file_save (df, new_filename,
                                permissions, err))
    goto cleanup;
  
  if (delete_original &&
      !files_are_the_same (filename, new_filename))
    {
      if (unlink (filename) < 0)
        g_printerr (_("Error removing original file \"%s\": %s\n"),
                    filename, g_strerror (errno));
    }

  gnome_desktop_file_free (df);

  /* Load and validate the file we just wrote */
  df = gnome_desktop_file_load (new_filename, err);
  if (df == NULL)
    goto cleanup;
  
  if (!desktop_file_validate (df, new_filename))
    {
      g_printerr (_("desktop-file-install created an invalid desktop file!\n"));
      exit (1);
    }
  
 cleanup:
  g_free (new_filename);
  
  if (df)
    gnome_desktop_file_free (df);
}

static void parse_options_callback (poptContext              ctx,
                                    enum poptCallbackReason  reason,
                                    const struct poptOption *opt,
                                    const char              *arg,
                                    void                    *data);

enum {
  OPTION_VENDOR = 1,
  OPTION_DIR,
  OPTION_ADD_CATEGORY,
  OPTION_REMOVE_CATEGORY,
  OPTION_ADD_ONLY_SHOW_IN,
  OPTION_REMOVE_ONLY_SHOW_IN,
  OPTION_DELETE_ORIGINAL,
  OPTION_MODE,
  OPTION_COPY_NAME_TO_GENERIC_NAME,
  OPTION_COPY_GENERIC_NAME_TO_NAME,
  OPTION_LAST
};

struct poptOption options[] = {
  {
    NULL, 
    '\0', 
    POPT_ARG_CALLBACK,
    parse_options_callback, 
    0, 
    NULL, 
    NULL
  },
  { 
    NULL, 
    '\0', 
    POPT_ARG_INCLUDE_TABLE, 
    poptHelpOptions,
    0, 
    N_("Help options"), 
    NULL 
  },
  {
    "vendor",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_VENDOR,
    N_("Specify the vendor prefix to be applied to the desktop file. If the file already has this prefix, nothing happens."),
    NULL
  },
  {
    "dir",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_DIR,
    N_("Specify the directory where files should be installed."),
    NULL
  },
  {
    "add-category",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_ADD_CATEGORY,
    N_("Specify a category to be added to the Categories field."),
    NULL
  },
  {
    "remove-category",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_REMOVE_CATEGORY,
    N_("Specify a category to be removed from the Categories field."),
    NULL
  },
  {
    "add-only-show-in",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_ADD_ONLY_SHOW_IN,
    N_("Specify a product name to be added to the OnlyShowIn field."),
    NULL
  },
  {
    "remove-only-show-in",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_REMOVE_ONLY_SHOW_IN,
    N_("Specify a product name to be removed from the OnlyShowIn field."),
    NULL
  },
  {
    "delete-original",
    '\0',
    POPT_ARG_NONE,
    &delete_original,
    OPTION_DELETE_ORIGINAL,
    N_("Delete the source desktop file, leaving only the target file. Effectively \"renames\" a desktop file."),
    NULL
  },
  {
    "mode",
    'm',
    POPT_ARG_STRING,
    NULL,
    OPTION_MODE,
    N_("Set the given permissions on the destination file."),
    NULL
  },
  {
    "copy-name-to-generic-name",
    '\0',
    POPT_ARG_NONE,
    &copy_name_to_generic_name,
    OPTION_COPY_NAME_TO_GENERIC_NAME,
    N_("Copy the contents of the \"Name\" field to the \"GenericName\" field."),
    NULL
  },
  {
    "copy-generic-name-to-name",
    '\0',
    POPT_ARG_NONE,
    &copy_generic_name_to_name,
    OPTION_COPY_GENERIC_NAME_TO_NAME,
    N_("Copy the contents of the \"GenericName\" field to the \"Name\" field."),
    NULL
  },
  {
    NULL,
    '\0',
    0,
    NULL,
    0,
    NULL,
    NULL
  }
};

static void
parse_options_callback (poptContext              ctx,
                        enum poptCallbackReason  reason,
                        const struct poptOption *opt,
                        const char              *arg,
                        void                    *data)
{
  const char *str;
  
  if (reason != POPT_CALLBACK_REASON_OPTION)
    return;

  switch (opt->val & POPT_ARG_MASK)
    {
    case OPTION_VENDOR:
      if (vendor_name)
        {
          g_printerr (_("Can only specify --vendor once\n"));
          exit (1);
        }
      
      str = poptGetOptArg (ctx);
      vendor_name = g_strdup (str);
      break;

    case OPTION_DIR:
      if (target_dir)
        {
          g_printerr (_("Can only specify --dir once\n"));
          exit (1);
        }

      str = poptGetOptArg (ctx);
      target_dir = g_strdup (str);
      break;

    case OPTION_ADD_CATEGORY:
      str = poptGetOptArg (ctx);
      added_categories = g_slist_prepend (added_categories,
                                          g_strdup (str));
      break;

    case OPTION_REMOVE_CATEGORY:
      str = poptGetOptArg (ctx);
      removed_categories = g_slist_prepend (removed_categories,
                                            g_strdup (str));
      break;

    case OPTION_ADD_ONLY_SHOW_IN:
      str = poptGetOptArg (ctx);
      added_only_show_in = g_slist_prepend (added_only_show_in,
                                            g_strdup (str));
      break;

    case OPTION_REMOVE_ONLY_SHOW_IN:
      str = poptGetOptArg (ctx);
      removed_only_show_in = g_slist_prepend (removed_only_show_in,
                                              g_strdup (str));
      break;

    case OPTION_MODE:
      {
        unsigned long ul;
        char *end;
        
        str = poptGetOptArg (ctx);

        end = NULL;
        ul = strtoul (str, &end, 8);
        if (*str && end && *end == '\0')
          permissions = ul;
        else
          {
            g_printerr (_("Could not parse mode string \"%s\"\n"),
                        str);
            
            exit (1);
          }
      }
      break;
      
    default:
      break;
    }
}

int
main (int argc, char **argv)
{
  poptContext ctx;
  int nextopt;
  GError* err = NULL;
  const char** args;
  int i;
  
  setlocale (LC_ALL, "");
  
  ctx = poptGetContext ("desktop-file-install", argc, (const char **) argv, options, 0);

  poptReadDefaultConfig (ctx, TRUE);

  while ((nextopt = poptGetNextOpt (ctx)) > 0)
    /*nothing*/;

  if (nextopt != -1)
    {
      g_printerr (_("Error on option %s: %s.\nRun '%s --help' to see a full list of available command line options.\n"),
                  poptBadOption (ctx, 0),
                  poptStrerror (nextopt),
                  argv[0]);
      return 1;
    }

  if (vendor_name == NULL)
    vendor_name = g_strdup (g_getenv ("DESKTOP_FILE_VENDOR"));
  
  if (vendor_name == NULL)
    {
      g_printerr (_("Must specify the vendor namespace for these files with --vendor\n"));
      return 1;
    }

  if (copy_generic_name_to_name && copy_name_to_generic_name)
    {
      g_printerr (_("Specifying both --copy-name-to-generic-name and --copy-generic-name-to-name at once doesn't make much sense.\n"));
      return 1;
    }
  
  if (target_dir == NULL)
    target_dir = g_strdup (g_getenv ("DESKTOP_FILE_INSTALL_DIR"));

  if (target_dir == NULL)
    target_dir = g_build_filename (DATADIR, "applications", NULL);
  
  args = poptGetArgs (ctx);

  i = 0;
  while (args && args[i])
    {
      err = NULL;
      process_one_file (args[i], &err);
      if (err != NULL)
        {
          g_printerr (_("Error on file \"%s\": %s\n"),
                      args[i], err->message);

          g_error_free (err);
          
          return 1;
        }
      
      ++i;
    }

  if (i == 0)
    {
      g_printerr (_("Must specify one or more desktop files to install\n"));

      return 1;
    }
  
  poptFreeContext (ctx);
        
  return 0;
}
