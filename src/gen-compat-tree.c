
#include <config.h>

#include <glib.h>
#include <popt.h>

#include "desktop_file.h"
#include "validate.h"
#include "vfolder-parser.h"
#include "vfolder-query.h"

#include <libintl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define _(x) gettext ((x))
#define N_(x) x

static char *target_dir = NULL;
static gboolean do_print = FALSE;
static gboolean do_verbose = FALSE;
static char *only_show_in_desktop = NULL;
static gboolean print_available = FALSE;

static void parse_options_callback (poptContext              ctx,
                                    enum poptCallbackReason  reason,
                                    const struct poptOption *opt,
                                    const char              *arg,
                                    void                    *data);

enum {
  OPTION_DIR,
  OPTION_PRINT,
  OPTION_VERBOSE,
  OPTION_DESKTOP,
  OPTION_PRINT_AVAILABLE,
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
    "dir",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_DIR,
    N_("Specify the directory where the compat tree should be generated."),
    NULL
  },
  {
    "print",
    '\0',
    POPT_ARG_NONE,
    NULL,
    OPTION_PRINT,
    N_("Print a human-readable representation of the menu to standard output."),
    NULL
  },
  {
    "desktop",
    '\0',
    POPT_ARG_STRING,
    NULL,
    OPTION_DESKTOP,
    N_("Specify the current desktop, for purposes of OnlyShowIn."),
    NULL
  },
  {
    "print-available",
    '\0',
    POPT_ARG_NONE,
    NULL,
    OPTION_PRINT_AVAILABLE,
    N_("Print the set of desktop files used for a given menu file."),
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
    case OPTION_DIR:
      if (target_dir)
        {
          g_printerr (_("Can only specify %s once\n"), "--dir");

          exit (1);
        }

      str = poptGetOptArg (ctx);
      target_dir = g_strdup (str);
      break;

    case OPTION_PRINT:
      if (do_print)
        {
          g_printerr (_("Can only specify %s once\n"), "--print");

          exit (1);
        }
      do_print = TRUE;
      break;

    case OPTION_VERBOSE:
      if (do_verbose)
        {
          g_printerr (_("Can only specify %s once\n"), "--verbose");

          exit (1);
        }

      do_verbose = TRUE;
      set_verbose_queries (TRUE);
      break;

    case OPTION_DESKTOP:
      if (only_show_in_desktop)
        {
          g_printerr (_("Can only specify %s once\n"), "--desktop");

          exit (1);
        }

      only_show_in_desktop = g_strdup (poptGetOptArg (ctx));
      break;
    case OPTION_PRINT_AVAILABLE:
      if (print_available)
        {
          g_printerr (_("Can only specify %s once\n"), "--print-available");

          exit (1);
        }
      print_available = TRUE;
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
  
  ctx = poptGetContext ("desktop-menu-tool", argc, (const char **) argv, options, 0);

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

  if (target_dir == NULL && !do_print && !print_available)
    {
      g_printerr (_("Must specify --dir option for target directory or --print option to print the menu or --print-available to print available desktop files.\n"));
      return 1;
    }

  if (only_show_in_desktop)
    set_only_show_in_desktop (only_show_in_desktop);
  
  args = poptGetArgs (ctx);

  i = 0;
  while (args && args[i])
    {
      Vfolder *folder;

      err = NULL;
      folder = vfolder_load (args[i], &err);
      if (err)
        {
          g_printerr (_("Failed to load %s: %s\n"),
                      args[i], err->message);
          g_error_free (err);

          return 1;
        }

      if (folder)
        {
          DesktopFileTree *tree;

          tree = desktop_file_tree_new (folder);

          if (print_available)
            desktop_file_tree_dump_desktop_list (tree);
          
          if (do_print)
            desktop_file_tree_print (tree,
                                     DESKTOP_FILE_TREE_PRINT_NAME |
                                     DESKTOP_FILE_TREE_PRINT_GENERIC_NAME); 

          if (target_dir)
            desktop_file_tree_write_symlink_dir (tree, target_dir);
          
          desktop_file_tree_free (tree);
          
          vfolder_free (folder);
        }
      
      ++i;
    }

  if (i == 0)
    {
      g_printerr (_("Must specify one menu file to parse\n"));

      return 1;
    }
  
  poptFreeContext (ctx);
        
  return 0;
}
