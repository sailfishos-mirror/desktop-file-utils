/* Vfolder query */

/*
 * Copyright (C) 2002 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "vfolder-query.h"
#include "validate.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x

static void load_tree (DesktopFileTree *tree);
static GNode* node_from_vfolder (DesktopFileTree *tree,
                                 Vfolder         *folder);

struct _DesktopFileTree
{
  Vfolder *folder;

  GNode *node;

  GHashTable *apps;
  GHashTable *dirs;
  
  guint loaded : 1;
};

DesktopFileTree*
desktop_file_tree_new (Vfolder *folder)
{
  DesktopFileTree *tree;

  tree = g_new0 (DesktopFileTree, 1);

  tree->folder = folder;

  tree->apps = g_hash_table_new_full (g_str_hash, g_str_equal,
                                      g_free,
                                      (GDestroyNotify) gnome_desktop_file_free);

  tree->dirs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                      g_free,
                                      (GDestroyNotify) gnome_desktop_file_free);  
  
  return tree;
}

static gboolean
free_node_func (GNode *node,
                void  *data)
{
  gnome_desktop_file_free (node->data);
  
  return TRUE;
}

void
desktop_file_tree_free (DesktopFileTree *tree)
{
#if 0
  /* Don't do this, the nodes just point into the copies
   * of the desktop files in the hash
   */
  if (tree->node)
    g_node_traverse (tree->node,
                     G_IN_ORDER, G_TRAVERSE_ALL,
                     -1, free_node_func, NULL);
#endif
  
  /* we don't own tree->folder which is bad practice but what the hell */

  g_hash_table_destroy (tree->apps);
  g_hash_table_destroy (tree->dirs);
  
  g_free (tree);
}

typedef struct
{
  DesktopFileTreePrintFlags flags;
} PrintData;

static gboolean
print_node_func (GNode *node,
                 void  *data)
{
#define MAX_FIELDS 3
  PrintData *pd;
  GnomeDesktopFile *df;
  int i;
  char *fields[MAX_FIELDS] = { NULL, NULL, NULL };
  char *s;
  
  pd = data;
  
  df = node->data;
  
  i = g_node_depth (node);
  while (i > 0)
    {
      fputc (' ', stdout);
      --i;
    }

  i = 0;
  if (pd->flags & DESKTOP_FILE_TREE_PRINT_NAME)
    {
      if (!gnome_desktop_file_get_locale_string (df,
                                                 NULL,
                                                 "Name",
                                                 &s))
        s = g_strdup (_("<missing Name>"));
      
      fields[i] = s;
      ++i;
    }
  
  if (pd->flags & DESKTOP_FILE_TREE_PRINT_GENERIC_NAME)
    {
      if (!gnome_desktop_file_get_locale_string (df,
                                                 NULL,
                                                 "GenericName",
                                                 &s))
        s = g_strdup (_("<missing GenericName>"));
      
      fields[i] = s;
      ++i;
    }

  if (pd->flags & DESKTOP_FILE_TREE_PRINT_COMMENT)
    {
      if (!gnome_desktop_file_get_locale_string (df,
                                                 NULL,
                                                 "Comment",
                                                 &s))
        s = g_strdup (_("<missing Comment>"));
      
      fields[i] = s;
      ++i;
    }

  switch (i)
    {
    case 3:
      g_print ("%s : %s : %s\n",
               fields[0], fields[1], fields[2]);
      break;
    case 2:
      g_print ("%s : %s\n",
               fields[0], fields[1]);
      break;
    case 1:
      g_print ("%s\n",
               fields[0]);
      break;
    }

  --i;
  while (i >= 0)
    {
      g_free (fields[i]);
      --i;
    }
  
  return FALSE;
}

void
desktop_file_tree_print (DesktopFileTree          *tree,
                         DesktopFileTreePrintFlags flags)
{
  PrintData pd;

  load_tree (tree);
  
  pd.flags = flags;
  
  if (tree->node)
    g_node_traverse (tree->node,
                     G_PRE_ORDER, G_TRAVERSE_ALL,
                     -1, print_node_func, &pd);
}

static void
add_or_free_desktop_file (GHashTable       *hash,
                          const char       *full_path,
                          const char       *basename,
                          GnomeDesktopFile *df)
{
  if (!desktop_file_fixup (df, full_path) ||
      !desktop_file_validate (df, full_path))
    {
      g_printerr (_("Warning: ignoring invalid desktop file %s\n"),
                  full_path);
      gnome_desktop_file_free (df);
    }
  else if (g_hash_table_lookup (hash, basename))
    {
      g_printerr (_("Warning: %s is a duplicate desktop file, ignoring\n"),
                  full_path);
      gnome_desktop_file_free (df);
    }
  else
    {
      /*       g_print ("Adding desktop file %s\n", full_path);*/
      g_hash_table_replace (hash, g_strdup (basename),
                            df);
    }
}

static void
merge_compat_dir (GHashTable *dirs_hash,
                  GHashTable *apps_hash,
                  const char *dirname)
{
  DIR* dp;
  struct dirent* dent;
  struct stat statbuf;
  char* fullpath;
  char* fullpath_end;
  guint len;
  guint subdir_len;

  /* FIXME */
  return;
  
  len = strlen (dirname);
  subdir_len = PATH_MAX - len;
  
  fullpath = g_new0 (char, subdir_len + len + 2); /* ensure null termination */
  strcpy (fullpath, dirname);
  
  fullpath_end = fullpath + len;
  if (*(fullpath_end - 1) != '/')
    {
      *fullpath_end = '/';
      ++fullpath_end;
    }
  
  while ((dent = readdir (dp)) != NULL)
    {
      /* Load directory file for this dir */
      if (strcmp (".directory", dent->d_name) == 0)
        {
          
        }
      /* ignore ., .., and all dot-files */
      else if (dent->d_name[0] == '.')
        continue;

      len = strlen (dent->d_name);

      if (len < subdir_len)
        {
          strcpy (fullpath_end, dent->d_name);
          strncpy (fullpath_end + len, ".directory", subdir_len - len);
        }
      else
        continue; /* Shouldn't ever happen since PATH_MAX is available */
      
      if (stat (fullpath, &statbuf) < 0)
        {
          /* Not a directory name */
          continue;
        }
      
      
    }

  /* if this fails, we really can't do a thing about it
   * and it's not a meaningful error
   */
  closedir (dp);

  g_free (fullpath);
}

static gboolean
g_str_has_suffix (const gchar  *str,
		  const gchar  *suffix)
{
  int str_len;
  int suffix_len;
  
  g_return_val_if_fail (str != NULL, FALSE);
  g_return_val_if_fail (suffix != NULL, FALSE);

  str_len = strlen (str);
  suffix_len = strlen (suffix);

  if (str_len < suffix_len)
    return FALSE;

  return strcmp (str + str_len - suffix_len, suffix) == 0;
}

static void
read_desktop_dir (GHashTable *hash,
                  const char *dirname)
{
  DIR* dp;
  struct dirent* dent;
  char* fullpath;
  char* fullpath_end;
  guint len;
  guint subdir_len;

  dp = opendir (dirname);
  if (dp == NULL)
    {
      g_printerr (_("Warning: Could not open directory %s: %s\n"),
                  dirname, g_strerror (errno));
      return;
    }
  
  len = strlen (dirname);
  subdir_len = PATH_MAX - len;
  
  fullpath = g_new0 (char, subdir_len + len + 2); /* ensure null termination */
  strcpy (fullpath, dirname);
  
  fullpath_end = fullpath + len;
  if (*(fullpath_end - 1) != '/')
    {
      *fullpath_end = '/';
      ++fullpath_end;
    }
  
  while ((dent = readdir (dp)) != NULL)
    {
      GnomeDesktopFile *df;
      GError *err;
      
      /* ignore ., .., and all dot-files */
      if (dent->d_name[0] == '.')
        continue;
      else if (!(g_str_has_suffix (dent->d_name, ".desktop") ||
                 g_str_has_suffix (dent->d_name, ".directory")))
        {
          g_printerr (_("Warning: ignoring file \"%s\" that doesn't end in .desktop or .directory\n"),
                      dent->d_name);
          continue;
        }
      
      len = strlen (dent->d_name);

      if (len < subdir_len)
        {
          strcpy (fullpath_end, dent->d_name);
        }
      else
        continue; /* Shouldn't ever happen since PATH_MAX is available */

      err = NULL;
      df = gnome_desktop_file_load (fullpath, &err);
      if (err)
        {
          g_printerr (_("Warning: failed to load %s: %s\n"),
                      fullpath, err->message);
          g_error_free (err);
        }

      if (df)
        {
          add_or_free_desktop_file (hash, fullpath, dent->d_name,
                                    df);
        }
    }

  /* if this fails, we really can't do a thing about it
   * and it's not a meaningful error
   */
  closedir (dp);

  g_free (fullpath);
}

static void
load_tree (DesktopFileTree *tree)
{
  GSList *tmp;
  GSList *list;
  
  if (tree->loaded)
    return;

  tree->loaded = TRUE;

  read_desktop_dir (tree->apps, DATADIR"/applications");
  
  list = vfolder_get_desktop_dirs (tree->folder);
  tmp = list;
  while (tmp != NULL)
    {
      read_desktop_dir (tree->dirs, tmp->data);
      tmp = tmp->next;
    }
  
  list = vfolder_get_merge_dirs (tree->folder);
  tmp = list;
  while (tmp != NULL)
    {
      merge_compat_dir (tree->dirs, tree->apps, tmp->data);
      tmp = tmp->next;
    }

  tree->node = node_from_vfolder (tree, tree->folder);
}

static GNode*
node_from_vfolder (DesktopFileTree *tree,
                   Vfolder         *folder)
{
  GnomeDesktopFile *df;
  const char *df_basename;
  GNode *node;
  GSList *list;
  GSList *tmp;
  
  df_basename = vfolder_get_desktop_file (folder);
  if (df_basename == NULL)
    {
      g_warning ("Folder has no desktop file, should have triggered a parse error on the menu file\n");
      return NULL;
    }

  df = g_hash_table_lookup (tree->dirs, df_basename);

  if (df == NULL)
    {
      g_printerr (_("Desktop file %s not found; ignoring directory %s\n"),
                  df_basename, vfolder_get_name (folder));
      return NULL;
    }

  node = g_node_new (df);

  list = vfolder_get_subfolders (folder);
  tmp = list;
  while (tmp != NULL)
    {
      GNode *child;

      child = node_from_vfolder (tree, tmp->data);

      if (child)
        g_node_append (node, child);
      
      tmp = tmp->next;
    }
  
  /* FIXME run the query to add child application nodes */
  
  return node;
}
