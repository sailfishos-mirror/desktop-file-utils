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

typedef enum
{
  NODE_FOLDER,
  NODE_APPLICATION
} NodeType;
  
typedef struct
{
  NodeType type;
  const char *basename;
  GnomeDesktopFile *df;
  Vfolder *folder;
} NodeData;

static GNode* node_from_vfolder     (DesktopFileTree  *tree,
                                     Vfolder          *folder);
static GNode* node_from_application (const char       *basename,
                                     GnomeDesktopFile *df);

static void distribute_unallocated (DesktopFileTree *tree,
                                    GNode           *node);

struct _DesktopFileTree
{
  Vfolder *folder;

  GNode *node;

  GHashTable *apps;
  GHashTable *dirs;

  GHashTable *allocated_apps;

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

  tree->allocated_apps = g_hash_table_new (g_str_hash, g_str_equal);
  
  return tree;
}

static gboolean
free_node_func (GNode *node,
                void  *data)
{
  g_free (node->data);
  
  return TRUE;
}

void
desktop_file_tree_free (DesktopFileTree *tree)
{
  if (tree->node)
    g_node_traverse (tree->node,
                     G_IN_ORDER, G_TRAVERSE_ALL,
                     -1, free_node_func, NULL);
  
  /* we don't own tree->folder which is bad practice but what the hell */

  g_hash_table_destroy (tree->apps);
  g_hash_table_destroy (tree->dirs);

  g_hash_table_destroy (tree->allocated_apps);
  
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
  NodeData *nd;
  int i;
  char *fields[MAX_FIELDS] = { NULL, NULL, NULL };
  char *s;
  
  pd = data;
  
  nd = node->data;
  
  i = g_node_depth (node);
  while (i > 0)
    {
      fputc (' ', stdout);
      --i;
    }

  i = 0;
  if (pd->flags & DESKTOP_FILE_TREE_PRINT_NAME)
    {
      if (!gnome_desktop_file_get_locale_string (nd->df,
                                                 NULL,
                                                 "Name",
                                                 &s))
        s = g_strdup (_("<missing Name>"));
      
      fields[i] = s;
      ++i;
    }
  
  if (pd->flags & DESKTOP_FILE_TREE_PRINT_GENERIC_NAME)
    {
      if (!gnome_desktop_file_get_locale_string (nd->df,
                                                 NULL,
                                                 "GenericName",
                                                 &s))
        s = g_strdup (_("<missing GenericName>"));
      
      fields[i] = s;
      ++i;
    }

  if (pd->flags & DESKTOP_FILE_TREE_PRINT_COMMENT)
    {
      if (!gnome_desktop_file_get_locale_string (nd->df,
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
add_or_free_desktop_file (GHashTable       *dirs_hash,
                          GHashTable       *apps_hash,
                          const char       *full_path,
                          const char       *basename,
                          GnomeDesktopFile *df)
{
  gboolean is_dir;
  char *type;
  GHashTable *hash;
  
  if (!desktop_file_fixup (df, full_path) ||
      !desktop_file_validate (df, full_path))
    {
      g_printerr (_("Warning: ignoring invalid desktop file %s\n"),
                  full_path);
      gnome_desktop_file_free (df);

      return;
    }

  type = NULL;
  if (!gnome_desktop_file_get_string (df, NULL,
                                      "Type", &type))
    {
      g_warning ("Desktop file validated but it has no Type field!\n");
      return;
    }

  g_assert (type != NULL);

  is_dir = strcmp (type, "Directory") == 0;

  if (!is_dir)
    {
      if (strcmp (type, "Application") != 0)
        {
          g_printerr (_("Warning: ignoring desktop file with type \"%s\" instead of \"Application\"\n"),
                      type);

          g_free (type);
          return;
        }
    }

  if (is_dir)
    hash = dirs_hash;
  else
    hash = apps_hash;
  
  if (g_hash_table_lookup (hash, basename))
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

  g_free (type);
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
read_desktop_dir (GHashTable *dirs_hash,
                  GHashTable *apps_hash,
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
          add_or_free_desktop_file (dirs_hash, apps_hash,
                                    fullpath, dent->d_name,
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

  read_desktop_dir (tree->dirs, tree->apps, DATADIR"/applications");
  
  list = vfolder_get_desktop_dirs (tree->folder);
  tmp = list;
  while (tmp != NULL)
    {
      read_desktop_dir (tree->dirs, tree->apps, tmp->data);
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

  distribute_unallocated (tree, tree->node);
}

static gboolean is_verbose = TRUE;
static void
query_verbose (int depth, const char *format, ...)
{
  va_list args;
  gchar *str;

  g_return_if_fail (format != NULL);

  if (!is_verbose)
    return;
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  {
    int i;
    i = depth;
    while (i > 0)
      {
        fputc (' ', stdout);
        fputc (' ', stdout);
        --i;
      }
  }
  
  fputs (str, stdout);

  g_free (str);
}

static gboolean
query_matches_desktop_file (VfolderQuery     *query,
                            const char       *basename,
                            GnomeDesktopFile *df,
                            int               depth_debug)
{
  gboolean retval;
  
  retval = FALSE;

  if (vfolder_query_get_negated (query))
    {
      query_verbose (depth_debug, "NOT\n");
      depth_debug += 1;
    }
  
  switch (vfolder_query_get_type (query))
    {
    case VFOLDER_QUERY_ROOT:
      g_assert_not_reached ();
      break;

    case VFOLDER_QUERY_AND:
      {
        GSList *tmp;

        query_verbose (depth_debug, "AND\n");
        
        retval = TRUE;
        tmp = vfolder_query_get_subqueries (query);
        while (tmp != NULL)
          {
            if (!query_matches_desktop_file (tmp->data,
                                             basename, df,
                                             depth_debug + 1))
              {
                retval = FALSE;
                break;
              }

            tmp = tmp->next;
          }
      }
      break;
      
    case VFOLDER_QUERY_OR:
      {
        GSList *tmp;

        query_verbose (depth_debug, "OR\n");
        
        retval = FALSE;
        tmp = vfolder_query_get_subqueries (query);
        while (tmp != NULL)
          {
            if (query_matches_desktop_file (tmp->data,
                                            basename, df,
                                            depth_debug + 1))
              {
                retval = TRUE;
                break;
              }

            tmp = tmp->next;
          }
      }
      break;

    case VFOLDER_QUERY_CATEGORY:
      {
        char **categories;
        int n_categories;
        
        categories = NULL;
        n_categories = 0;

        if (gnome_desktop_file_get_strings (df, NULL, "Categories", NULL,
                                            &categories, &n_categories))
          {
            int i;
            const char *search;

            search = vfolder_query_get_category (query);

            i = 0;
            while (i < n_categories)
              {
                if (strcmp (categories[i], search) == 0)                            
                  {
                    query_verbose (depth_debug, "%s IS in category %s\n",
                             basename, search);
                    retval = TRUE;
                    break;
                  }
                
                ++i;
              }

            g_strfreev (categories);

            if (!retval)
              query_verbose (depth_debug, "%s is NOT in category %s\n", basename, search);
          }
        else
          {
            query_verbose (depth_debug, "No Categories field in desktop file\n");
          }
      }
      break;
    }

  query_verbose (depth_debug, "%s\n",
                 retval ? "INCLUDED" : "EXCLUDED");
  
  if (vfolder_query_get_negated (query))
    {
      retval = !retval;
      query_verbose (depth_debug - 1, "%s\n",
                     retval ? "INCLUDED" : "EXCLUDED");
    }
  
  return retval;      
}

typedef struct
{
  GHashTable *allocated_apps;

  GNode *parent;

  gboolean only_unallocated;
  
  VfolderQuery *query;

  GSList *excludes;
} QueryData;

static gboolean
string_in_list (GSList     *strings,
                const char *str)
{
  GSList *tmp;

  tmp = strings;
  while (tmp != NULL)
    {
      if (strcmp (tmp->data, str) == 0)
        return TRUE;
      tmp = tmp->next;
    }

  return FALSE;
}

static void
query_foreach (void *key, void *value, void *data)
{
  QueryData *qd;
  gboolean include;
  
  qd = data;

  query_verbose (0, "Considering \"%s\"\n", key);
  
  include = query_matches_desktop_file (qd->query, key, value, 1);

  if (include && string_in_list (qd->excludes, key))
    {
      include = FALSE;
      query_verbose (1, "EXCLUDED due to exclude list\n");
    }
  
  if (include && qd->only_unallocated &&
      g_hash_table_lookup (qd->allocated_apps, key))
    {
      include = FALSE;
      query_verbose (1, "EXCLUDED because it was already allocated\n");
    }
  
  if (include)
    {
      GNode *child;
      
      child = node_from_application (key, value);

      g_node_append (qd->parent, child);

      if (!qd->only_unallocated)
        {
          /* Mark it allocated */
          g_hash_table_insert (qd->allocated_apps, key, value);
        }
    }
}

static void
fill_folder_with_apps (DesktopFileTree *tree,
                       GNode           *node,
                       Vfolder         *folder)
{
  QueryData qd;
  GSList *list;
  GSList *tmp;
  
  qd.query = vfolder_get_query (folder);
  if (qd.query)
    {
      qd.allocated_apps = tree->allocated_apps;
      qd.only_unallocated = vfolder_get_only_unallocated (folder);
      qd.parent = node;
      qd.excludes = vfolder_get_excludes (folder);
      
      g_hash_table_foreach (tree->apps, (GHFunc) query_foreach,
                            &qd);
    }

  /* Include the <Include> for first-pass folders */
  list = vfolder_get_includes (folder);
  tmp = list;
  while (tmp != NULL)
    {
      GnomeDesktopFile *df;
      
      df = g_hash_table_lookup (tree->apps,
                                tmp->data);
      
      if (df)
        {
          GNode *child;
          
          child = node_from_application (tmp->data, df);
          
          g_node_append (node, child);
          
          /* Mark it allocated */
          g_hash_table_insert (tree->allocated_apps, tmp->data, df);
        }
      
      tmp = tmp->next;
    }
}

static GNode*
node_from_vfolder (DesktopFileTree *tree,
                   Vfolder         *folder)
{
  GnomeDesktopFile *df;
  const char *df_basename;
  GNode *node;
  NodeData *nd;
  GSList *list;
  GSList *tmp;
  
  query_verbose (0, "FOLDER: %s\n", vfolder_get_name (folder));
  
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

  nd = g_new0 (NodeData, 1);
  nd->type = NODE_FOLDER;
  nd->basename = df_basename;
  nd->df = df;
  nd->folder = folder;
  
  node = g_node_new (nd);

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

  /* Only folders that are not "OnlyUnallocated" go in the first pass */
  if (!vfolder_get_only_unallocated (folder))
    fill_folder_with_apps (tree, node, folder);
  
  return node;
}

static GNode*
node_from_application (const char       *basename,
                       GnomeDesktopFile *df)
{
  GNode *node;
  NodeData *nd;
  
  nd = g_new0 (NodeData, 1);
  nd->type = NODE_APPLICATION;
  nd->basename = basename;
  nd->df = df;
  nd->folder = NULL;
  
  node = g_node_new (nd);

  return node;
}

static void
distribute_unallocated (DesktopFileTree *tree,
                        GNode           *node)
{
  NodeData *nd;
  GNode *child;

  child = node->children;
  while (child != NULL)
    {
      distribute_unallocated (tree, child);
      
      child = child->next;
    }

  nd = node->data;  
  
  /* Only folders that are "OnlyUnallocated" go in the first pass */
  if (nd->type == NODE_FOLDER &&
      vfolder_get_only_unallocated (nd->folder))
    fill_folder_with_apps (tree, node, nd->folder);
}
