/* Overrides for .desktop files in a menu */

/*
 * Copyright (C) 2003 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "menu-overrides.h"
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x

gboolean
g_create_dir (const char    *dir,
              unsigned int   mode,
              GError       **err)
{
  char *parent;
  
  menu_verbose ("Creating directory \"%s\" mode %o\n", dir, mode);
  
  parent = g_path_get_dirname (dir);

  menu_verbose ("Parent dir is \"%s\"\n", parent);

  if (!g_file_test (parent, G_FILE_TEST_IS_DIR))
    {
      if (!g_create_dir (parent, mode, err))
        {
          menu_verbose ("Failed to create parent dir\n");
          g_free (parent);
          return FALSE;
        }
    }

  g_free (parent);
  
  if (mkdir (dir, mode) < 0)
    {
      if (errno != EEXIST)
        {
          g_set_error (err, G_FILE_ERROR_FAILED,
                       g_file_error_from_errno (errno),
                       _("Could not make directory \"%s\": %s"),
                       dir, g_strerror (errno));
          menu_verbose ("Error: \"%s\"\n", err ? (*err)->message : "(no err requested)");
          return FALSE;
        }
    }
  
  return TRUE;
}

struct MenuOverrideDir
{
  int refcount;

  char *root_dir;
};

MenuOverrideDir*
menu_override_dir_create (const char       *path,
                          GError          **error)
{
  MenuOverrideDir *override;

  if (!g_create_dir (path, 0755, error))
    return NULL;

  override = g_new0 (MenuOverrideDir, 1);
  override->refcount = 1;
  override->root_dir = g_strdup (path);
  
  return override;
}

void
menu_override_dir_ref (MenuOverrideDir  *override)
{
  g_return_if_fail (override != NULL);
  g_return_if_fail (override->refcount > 0);
  
  override->refcount += 1;
}

void
menu_override_dir_unref (MenuOverrideDir  *override)
{
  g_return_if_fail (override != NULL);
  g_return_if_fail (override->refcount > 0);

  if (override->refcount == 0)
    {
      g_free (override->root_dir);
      g_free (override);
    }
}

gboolean
menu_override_dir_add (MenuOverrideDir  *override,
                       const char       *menu_path,
                       const char       *name_to_override,
                       const char       *based_on_fs_path,
                       GError          **error)
{
  char *fs_dir_path;
  char *fs_file_path;
  gboolean retval;

  retval = FALSE;
  
  fs_dir_path =
    menu_override_dir_get_fs_path (override, menu_path, NULL);

  if (!g_create_dir (fs_dir_path, 0755, error))
    {
      g_free (fs_dir_path);
      return FALSE;
    }

  g_free (fs_dir_path);
  
  fs_file_path =
    menu_override_dir_get_fs_path (override, menu_path, name_to_override);

  if (based_on_fs_path)
    {
      char *contents;
      int len;

      if (!g_file_get_contents (based_on_fs_path, &contents, &len,
                                error))
        {
          menu_verbose ("Failed to get contents of \"%s\"\n",
                        based_on_fs_path);
          goto out;
        }
      
      if (!g_file_save_atomically (fs_file_path,
                                   contents, len,
                                   error))
        {
          menu_verbose ("Failed to save \"%s\"\n",
                        fs_file_path);
          g_free (contents);
          goto out;
        }

      g_free (contents);
    }
  
  retval = TRUE;
  
 out:
  g_free (fs_file_path);
  return retval;
}

gboolean
menu_override_dir_remove (MenuOverrideDir  *override,
                          const char       *menu_path,
                          const char       *name_to_unoverride,
                          GError          **error)
{
  char *fs_dir_path;
  char *fs_file_path;  
  
  fs_file_path =
    menu_override_dir_get_fs_path (override, menu_path, name_to_unoverride);
  if (unlink (fs_file_path) < 0)
    {
      g_set_error (error, G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   _("Failed to remove file \"%s\": %s\n"),
                   fs_file_path, g_strerror (errno));
      g_free (fs_file_path);
      return FALSE;
    }

  /* always try removing the directory, it will fail if the dir isn't
   * empty and succeed if the directory has nothing worthwhile in it.
   */
  fs_dir_path =
    menu_override_dir_get_fs_path (override, menu_path, NULL);
  rmdir (fs_dir_path);
  g_free (fs_dir_path);

  return TRUE;
}

char*
menu_override_dir_get_fs_path (MenuOverrideDir  *override,
                               const char       *menu_path,
                               const char       *name_to_unoverride)
{
  char *path;

  g_return_val_if_fail (override != NULL, NULL);
  g_return_val_if_fail (override->refcount > 0, NULL);
  g_return_val_if_fail (menu_path != NULL, NULL);
  
  /* name_to_unoverride is allowed to be NULL */
  
  path = g_build_filename (override->root_dir,
                           menu_path,
                           name_to_unoverride,
                           NULL);

  return path;
}
