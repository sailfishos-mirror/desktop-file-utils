/* Tree of desktop entries */

/*
 * Copyright (C) 2002, 2003 Red Hat, Inc.
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

#include "menu-process.h"
#include "menu-layout.h"
#include "menu-entries.h"
#include "canonicalize.h"
#include "desktop_file.h"
#include "menu-util.h"
#include <stdio.h>
#include <string.h>

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x

static void menu_node_resolve_files (MenuCache  *menu_cache,
                                     EntryCache *entry_cache,
                                     MenuNode   *node);

static MenuNode*
find_menu_child (MenuNode *node)
{
  MenuNode *child;

  child = menu_node_get_children (node);
  while (child && menu_node_get_type (child) != MENU_NODE_MENU)
    child = menu_node_get_next (child);

  return child;
}

static void
merge_resolved_copy_of_children (MenuCache  *menu_cache,
                                 EntryCache *entry_cache,
                                 MenuNode   *where,
                                 MenuNode   *from)
{
  MenuNode *from_child;
  MenuNode *insert_after;
  MenuNode *from_copy;
  MenuNode *menu_child;
  
  /* Copy and file-resolve the node */
  from_copy = menu_node_deep_copy (from);
  menu_node_resolve_files (menu_cache, entry_cache, from_copy);
  
  insert_after = where;
  g_assert (menu_node_get_type (insert_after) != MENU_NODE_ROOT);
  g_assert (menu_node_get_parent (insert_after) != NULL);

  /* skip root node */
  menu_child = find_menu_child (from_copy);
  g_assert (menu_child != NULL);
  g_assert (menu_node_get_type (menu_child) == MENU_NODE_MENU);

  /* merge children of toplevel <Menu> */
  from_child = menu_node_get_children (menu_child);
  while (from_child != NULL)
    {
      MenuNode *next;

      next = menu_node_get_next (from_child);
      g_assert (next != from_child);

      menu_verbose ("Merging %p after %p\n", from_child, insert_after);
      menu_node_debug_print (from_child);
      menu_node_debug_print (insert_after);
      
      switch (menu_node_get_type (from_child))
        {
        case MENU_NODE_NAME:
          menu_node_unlink (from_child); /* delete this */
          break;
          
        default:
          {
            menu_node_steal (from_child);
            menu_node_insert_after (insert_after, from_child);
            insert_after = from_child;
            menu_node_unref (from_child);
          }
          break;
        }

      g_assert (menu_node_get_type (insert_after) != MENU_NODE_ROOT);
      g_assert (menu_node_get_parent (insert_after) != NULL);
      
      from_child = next;
    }

  /* Now "from_copy" should be a single root node plus a single <Menu>
   * node below it, possibly with some PASSTHROUGH nodes mixed in.
   */
  g_assert (menu_node_get_type (from_copy) == MENU_NODE_ROOT);
  g_assert (menu_node_get_children (from_copy) != NULL);
  
  menu_node_unref (from_copy);
}

static void
menu_node_resolve_files_recursive (MenuCache  *menu_cache,
                                   EntryCache *entry_cache,
                                   MenuNode   *node)
{
  MenuNode *child;

  menu_verbose ("Resolving files in node %p\n", node);
      
  switch (menu_node_get_type (node))
    {
    case MENU_NODE_MERGE_FILE:
      {
        MenuNode *to_merge;
        char *filename;

        filename = menu_node_get_content_as_path (node);
        if (filename == NULL)
          {
            menu_verbose ("No filename in MergeFile\n");
            goto done;
          }

        menu_verbose ("Merging file \"%s\"\n", filename);
            
        to_merge = menu_cache_get_menu_for_file (menu_cache,
                                                 filename,
                                                 NULL,
                                                 NULL); /* missing files ignored */
        if (to_merge == NULL)
          {
            menu_verbose ("No menu for file \"%s\" found when merging\n",
                          filename);
            goto done;
          }
            
        merge_resolved_copy_of_children (menu_cache, entry_cache,
                                         node, to_merge);

        menu_node_unref (to_merge);

      done:
        g_free (filename);
        menu_node_unlink (node); /* delete this child, replaced
                                   * by the merged content
                                   */
      }
      break;
    case MENU_NODE_MERGE_DIR:
      {
        /* FIXME don't just delete it ;-) */
        
        menu_node_unlink (node);
      }
      break;
    case MENU_NODE_LEGACY_DIR:
      {
        /* FIXME don't just delete it ;-) */
            
        menu_node_unlink (node);
      }
      break;

#if 0
      /* FIXME may as well expand these here */
    case MENU_NODE_DEFAULT_APP_DIRS:
      break;
    case MENU_NODE_DEFAULT_DIRECTORY_DIRS:
      break;
#endif

    case MENU_NODE_PASSTHROUGH:
      /* Just get rid of this, we don't need the memory usage */
      menu_node_unlink (node);
      break;
          
    default:
      /* Recurse */
      child = menu_node_get_children (node);
      
      while (child != NULL)
        {
          MenuNode *next;
          
          menu_verbose ("  (recursing to node %p)\n", child);
          
          /* get next first, because we may delete this child (and place new
           * file contents between "child" and "next")
           */
          next = menu_node_get_next (child);

          menu_node_resolve_files_recursive (menu_cache, entry_cache,
                                             child);
          
          child = next;
        }
      break;
    }
}

static void
menu_node_resolve_files (MenuCache  *menu_cache,
                         EntryCache *entry_cache,
                         MenuNode   *node)
{
  menu_verbose ("Resolving files in root node %p\n", node);
  
  /* FIXME
   * if someone does <MergeFile>A.menu</MergeFile> inside
   * A.menu, or a more elaborate loop involving multiple
   * files, we'll just get really hosed and eat all the RAM
   * we can find
   */
  menu_node_root_set_entry_cache (node, entry_cache);

  menu_node_resolve_files_recursive (menu_cache, entry_cache,
                                     node);
}

static int
null_safe_strcmp (const char *a,
                  const char *b)
{
  if (a == NULL && b == NULL)
    return 0;
  else if (a == NULL)
    return -1;
  else if (b == NULL)
    return 1;
  else
    return strcmp (a, b);
}

static int
node_compare_func (const void *a,
                   const void *b)
{
  MenuNode *node_a = (MenuNode*) a;
  MenuNode *node_b = (MenuNode*) b;
  MenuNodeType t_a = menu_node_get_type (node_a);
  MenuNodeType t_b = menu_node_get_type (node_b);

  if (t_a < t_b)
    return -1;
  else if (t_a > t_b)
    return 1;
  else
    {
      const char *c_a = menu_node_get_content (node_a);
      const char *c_b = menu_node_get_content (node_b);
 
      return null_safe_strcmp (c_a, c_b);
    }
}

static int
node_menu_compare_func (const void *a,
                        const void *b)
{
  MenuNode *node_a = (MenuNode*) a;
  MenuNode *node_b = (MenuNode*) b;

  return null_safe_strcmp (menu_node_menu_get_name (node_a),
                           menu_node_menu_get_name (node_b));
}

static void
move_children (MenuNode *from,
               MenuNode *to)
{
  MenuNode *from_child;
  MenuNode *insert_before;

  insert_before = menu_node_get_children (to);
  from_child = menu_node_get_children (from);

  while (from_child != NULL)
    {
      MenuNode *next;

      next = menu_node_get_next (from_child);

      menu_node_steal (from_child);
      menu_node_insert_before (insert_before, from_child);
      
      from_child = next;
    }
}

static void
menu_node_strip_duplicate_children (MenuNode *node)
{
  GSList *simple_nodes;
  GSList *menu_nodes;
  GSList *move_nodes;
  GSList *prev;
  GSList *tmp;
  MenuNode *child;

  /* to strip dups, we find all the child nodes where
   * we want to kill dups, sort them,
   * then nuke the adjacent nodes that are equal
   */
  
  simple_nodes = NULL;
  menu_nodes = NULL;
  move_nodes = NULL;
  child = menu_node_get_children (node);
  while (child != NULL)
    {
      switch (menu_node_get_type (child))
        {
          /* These are dups if their content is the same */
        case MENU_NODE_APP_DIR:
        case MENU_NODE_DIRECTORY_DIR:
        case MENU_NODE_DIRECTORY:
          simple_nodes = g_slist_prepend (simple_nodes, child);
          break;

          /* These have to be merged in a more complicated way,
           * and then recursed
           */
        case MENU_NODE_MENU:
          menu_nodes = g_slist_prepend (menu_nodes, child);
          break;

          /* These have to be merged in a different more complicated way */
        case MENU_NODE_MOVE:
          move_nodes = g_slist_prepend (move_nodes, child);
          break;

        default:
          break;
        }

      child = menu_node_get_next (child);
    }

  /* Note that the lists are all backward. So we want to keep
   * the items that are earlier in the list, because they were
   * later in the file
   */

  /* stable sort the simple nodes */
  simple_nodes = g_slist_sort (simple_nodes,
                               node_compare_func);

  prev = NULL;
  tmp = simple_nodes;
  while (tmp != NULL)
    {
      if (prev)
        {
          MenuNode *p = prev->data;
          MenuNode *n = tmp->data;

          if (node_compare_func (p, n) == 0)
            {
              /* nuke it! */
              menu_node_unlink (n);
            }
        }
      
      prev = tmp;
      tmp = tmp->next;
    }

  g_slist_free (simple_nodes);
  simple_nodes = NULL;

  /* stable sort the menu nodes */
  menu_nodes = g_slist_sort (menu_nodes,
                             node_menu_compare_func);

  prev = NULL;
  tmp = menu_nodes;
  while (tmp != NULL)
    {
      if (prev)
        {
          MenuNode *p = prev->data;
          MenuNode *n = tmp->data;

          if (node_compare_func (p, n) == 0)
            {
              /* Move children of first menu to the start of second
               * menu and nuke the first menu
               */
              move_children (n, p);
              menu_node_unlink (n);
            }
        }
      
      prev = tmp;
      tmp = tmp->next;
    }

  g_slist_free (menu_nodes);
  menu_nodes = NULL;

  /* move nodes are pretty much annoying as hell */

  /* FIXME */
  
  g_slist_free (move_nodes);
  move_nodes = NULL;
  
  /* Finally, recursively clean up our children */
  child = menu_node_get_children (node);
  while (child != NULL)
    {
      if (menu_node_get_type (child) == MENU_NODE_MENU)
        menu_node_strip_duplicate_children (child);

      child = menu_node_get_next (child);
    }
}

struct DesktopEntryTreeNode
{
  char   *name;
  Entry  *dir_entry; /* may be NULL, name should be used as user-visible dirname */
  GSList *entries;
  GSList *subdirs;
};

/* make a shorter name */
typedef struct DesktopEntryTreeNode TreeNode;

struct DesktopEntryTree
{
  int refcount;
  char *menu_file;
  char *menu_file_dir;
  EntryCache *entry_cache;
  MenuCache  *menu_cache;
  MenuNode   *orig_node;
  MenuNode   *resolved_node;
  TreeNode   *root;
};

static void build_tree     (DesktopEntryTree *tree);
static void tree_node_free (TreeNode *node);

DesktopEntryTree*
desktop_entry_tree_load (const char  *filename,
                         const char  *only_show_in_desktop,
                         const char  *create_chaining_to,
                         GError     **error)
{
  DesktopEntryTree *tree;
  MenuNode *orig_node;
  MenuNode *resolved_node;
  char *canonical;
  EntryCache *entry_cache;
  MenuCache *menu_cache;

  menu_verbose ("Loading desktop entry tree at \"%s\"\n", filename);
  
  canonical = g_canonicalize_file_name (filename, create_chaining_to != NULL);
  if (canonical == NULL)
    {
      g_set_error (error, G_FILE_ERROR,
                   G_FILE_ERROR_FAILED,
                   _("Could not canonicalize filename \"%s\"\n"),
                   filename);
      return NULL;
    }

  menu_verbose ("Canonicalized \"%s\" -> \"%s\"\n", filename, canonical);
  
  menu_cache = menu_cache_new ();
  
  orig_node = menu_cache_get_menu_for_canonical_file (menu_cache,
                                                      canonical,
                                                      create_chaining_to,
                                                      error);
  if (orig_node == NULL)    
    {
      menu_cache_unref (menu_cache);
      g_free (canonical);
      return NULL;
    }

  entry_cache = entry_cache_new ();
  if (only_show_in_desktop)
    entry_cache_set_only_show_in_name (entry_cache,
                                       only_show_in_desktop);

  /* menu_node_debug_print (orig_node); */
  
  resolved_node = menu_node_deep_copy (orig_node);
  menu_node_resolve_files (menu_cache, entry_cache, resolved_node);

  menu_node_strip_duplicate_children (resolved_node);

  tree = g_new0 (DesktopEntryTree, 1);
  tree->refcount = 1;
  tree->menu_cache = menu_cache;
  tree->entry_cache = entry_cache;
  tree->menu_file = canonical;
  tree->menu_file_dir = g_path_get_dirname (canonical);
  tree->orig_node = orig_node;
  tree->resolved_node = resolved_node;
  tree->root = NULL;
  
  return tree;
}

void
desktop_entry_tree_ref (DesktopEntryTree *tree)
{
  g_return_if_fail (tree != NULL);
  g_return_if_fail (tree->refcount > 0);

  tree->refcount += 1;
}

void
desktop_entry_tree_unref (DesktopEntryTree *tree)
{
  g_return_if_fail (tree != NULL);
  g_return_if_fail (tree->refcount > 0);

  tree->refcount -= 1;

  if (tree->refcount == 0)
    {
      g_free (tree->menu_file);
      g_free (tree->menu_file_dir);
      menu_node_unref (tree->orig_node);
      menu_node_unref (tree->resolved_node);
      if (tree->root)
        tree_node_free (tree->root);
      entry_cache_unref (tree->entry_cache);
      menu_cache_unref (tree->menu_cache);
      g_free (tree);
    }
}

void
desktop_entry_tree_invalidate (DesktopEntryTree *tree,
                               const char       *dirname)
{
  menu_cache_invalidate (tree->menu_cache, dirname);
  entry_cache_invalidate (tree->entry_cache, dirname);
}

static TreeNode*
find_subdir (TreeNode    *parent,
             const char  *subdir)
{
  GSList *tmp;

  tmp = parent->subdirs;
  while (tmp != NULL)
    {
      TreeNode *sub = tmp->data;

      if (strcmp (sub->name, subdir) == 0)
        return sub;

      tmp = tmp->next;
    }

  return NULL;
}

/* Get node if it's a dir and return TRUE if it's an entry */
static gboolean
tree_node_find_subdir_or_entry (TreeNode   *node,
                                const char *name,
                                TreeNode  **subdir_p,
                                char      **real_fs_absolute_path_p)
{
  char **split;
  int i;
  TreeNode *iter;
  TreeNode *prev;
  Entry *entry;

  if (subdir_p)
    *subdir_p = NULL;
  if (real_fs_absolute_path_p)
    *real_fs_absolute_path_p = NULL;
  
  entry = NULL;

  /* Skip leading '/' (we're already at the root) */
  i = 0;
  while (name[i] &&
         name[i] == '/')
    ++i;
  
  menu_verbose (" (splitting \"%s\")\n", name + i);
  
  split = g_strsplit (name + i, "/", -1);

  prev = NULL;
  iter = node;
  i = 0;
  while (split[i] != NULL && *(split[i]) != '\0')
    {      
      prev = iter;
      iter = find_subdir (iter, split[i]);

      menu_verbose ("Node %p found for path component \"%s\"\n",
                    iter, split[i]);

      if (iter == NULL)
        {
          /* We ran out of nodes before running out of path
           * components
           */
          menu_verbose ("Remaining path component \"%s\" doesn't point to a directory node\n",
                        split[i]);
          break;
        }
      
      ++i;
    }

  if (iter == NULL && prev != NULL && split[i] != NULL)
    {
      /* The last element was not a dir; see if it's one
       * of the entries.
       */
      const char *entry_name;
      GSList *tmp;

      menu_verbose ("Scanning for entry named \"%s\"\n",
                    split[i]);
      
      entry_name = split[i];
      
      tmp = prev->entries;
      while (tmp != NULL)
        {
          Entry *e = tmp->data;
          if (strcmp (entry_get_name (e), entry_name) == 0)
            {
              entry = e;
              iter = prev; /* set found node to the previous */
              break;
            }

          menu_verbose ("   (\"%s\" doesn't match)\n",
                        entry_get_name (e));
          
          tmp = tmp->next;
        }
    }

  g_strfreev (split);

  menu_verbose (" Found node %p and entry path \"%s\"\n",
                iter, entry ? entry_get_absolute_path (entry) : "(none)");
  
  if (real_fs_absolute_path_p && entry)
    *real_fs_absolute_path_p = g_strdup (entry_get_absolute_path (entry));
  
  if (subdir_p)
    {
      *subdir_p = iter;
      return TRUE;
    }
  else if (entry != NULL)
    return TRUE;
  else
    return FALSE;
}


static TreeNode*
tree_node_find_subdir (TreeNode   *node,
                       const char *name)
{
  TreeNode *ret;
  ret = NULL;
  tree_node_find_subdir_or_entry (node, name, &ret, NULL);
  return ret;
}

gboolean
desktop_entry_tree_get_node (DesktopEntryTree      *tree,
                             const char            *path,
                             DesktopEntryTreeNode **node)
{
  *node = NULL;
  
  build_tree (tree);
  if (tree->root == NULL)
    return FALSE;

  *node = tree_node_find_subdir (tree->root, path);
  return *node != NULL;
}

gboolean
desktop_entry_tree_resolve_path (DesktopEntryTree       *tree,
                                 const char             *path,
                                 DesktopEntryTreeNode  **node_p,
                                 char                  **real_fs_absolute_path_p)
{  
  build_tree (tree);
  if (tree->root == NULL)
    return FALSE;
  
  return tree_node_find_subdir_or_entry (tree->root,
                                         path, node_p,
                                         real_fs_absolute_path_p);
}

void
desktop_entry_tree_list_subdirs (DesktopEntryTree       *tree,
                                 DesktopEntryTreeNode   *parent_node,
                                 DesktopEntryTreeNode ***subdirs,
                                 int                    *n_subdirs)
{
  int len;
  GSList *tmp;
  int i;

  g_return_if_fail (parent_node != NULL);
  g_return_if_fail (subdirs != NULL);
  
  *subdirs = NULL;
  if (n_subdirs)
    *n_subdirs = 0;
  
  build_tree (tree);
  if (tree->root == NULL)
    return;
  
  len = g_slist_length (parent_node->subdirs);
  *subdirs = g_new0 (DesktopEntryTreeNode*, len + 1);

  i = 0;
  tmp = parent_node->subdirs;
  while (tmp != NULL)
    {
      TreeNode *sub = tmp->data;

      (*subdirs)[i] = sub;

      ++i;
      tmp = tmp->next;
    }

  if (n_subdirs)
    *n_subdirs = len;
}

/* This lists absolute paths in the real filesystem */
void
desktop_entry_tree_list_entries (DesktopEntryTree     *tree,
                                 DesktopEntryTreeNode *parent_node,
                                 char               ***entries,
                                 int                  *n_entries)
{
  int len;
  int i;
  GSList *tmp;
  
  *entries = NULL;
  if (n_entries)
    *n_entries = 0;

  build_tree (tree);
  if (tree->root == NULL)
    return;
  
  len = g_slist_length (parent_node->entries);
  *entries = g_new0 (char*, len + 1);

  i = 0;
  tmp = parent_node->entries;
  while (tmp != NULL)
    {
      Entry *e = tmp->data;

      (*entries)[i] = g_strdup (entry_get_absolute_path (e));

      ++i;
      tmp = tmp->next;
    }

  if (n_entries)
    *n_entries = len;
}

/* Lists entries, subdirs, *and* the ".directory" file if any,
 * as relative paths in the VFS
 */
void
desktop_entry_tree_list_all (DesktopEntryTree       *tree,
                             DesktopEntryTreeNode   *parent_node,
                             char                 ***names,
                             int                    *n_names,
                             int                    *n_subdirs)
{
  int len;
  GSList *tmp;
  int i;

  g_return_if_fail (parent_node != NULL);
  g_return_if_fail (names != NULL);
  
  *names = NULL;
  if (n_names)
    *n_names = 0;
  if (n_subdirs)
    *n_subdirs = 0;
  
  build_tree (tree);
  if (tree->root == NULL)
    return;
  
  len = g_slist_length (parent_node->subdirs);
  len += g_slist_length (parent_node->entries);
  *names = g_new0 (char*, len + 2); /* 1 extra for .directory */

  i = 0;
  tmp = parent_node->subdirs;
  while (tmp != NULL)
    {
      TreeNode *sub = tmp->data;

      (*names)[i] = g_strdup (sub->name);

      ++i;
      tmp = tmp->next;
    }

  if (n_subdirs)
    *n_subdirs = i;
  
  tmp = parent_node->entries;
  while (tmp != NULL)
    {
      Entry *e = tmp->data;
      
      (*names)[i] = g_strdup (entry_get_name (e));

      ++i;
      tmp = tmp->next;
    }

  g_assert (i == len);
  
  if (parent_node->dir_entry)
    {
      (*names)[i] = g_strdup (".directory");
      len += 1;
      ++i;      
    }

  g_assert (i == len);
  
  if (n_names)
    *n_names = len;
}

char*
desktop_entry_tree_node_get_directory (DesktopEntryTreeNode *node)
{
  g_return_val_if_fail (node != NULL, NULL);

  if (node->dir_entry)
    return g_strdup (entry_get_absolute_path (node->dir_entry));
  else
    return NULL;
}

const char*
desktop_entry_tree_node_get_name (DesktopEntryTreeNode *node)
{
  g_return_val_if_fail (node != NULL, NULL);
  
  return node->name;
}

static gboolean
foreach_dir (DesktopEntryTree            *tree,
             TreeNode                    *dir,
             int                          depth,
             DesktopEntryTreeForeachFunc  func,
             void                        *user_data)
{
  GSList *tmp;

  if (! (* func) (tree,
                  TRUE,
                  depth,
                  dir->name,
                  NULL, /* FIXME */
                  dir->dir_entry ? entry_get_absolute_path (dir->dir_entry) : NULL,
                  user_data))
    return FALSE;
  
  tmp = dir->entries;
  while (tmp != NULL)
    {
      Entry *e = tmp->data;

      if (! (* func) (tree,
                      FALSE,
                      depth + 1,
                      entry_get_name (e),
                      NULL, /* FIXME */
                      entry_get_absolute_path (e),
                      user_data))
        return FALSE;

      tmp = tmp->next;
    }

  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      TreeNode *d = tmp->data;

      if (!foreach_dir (tree, d, depth + 1, func, user_data))
        return FALSE;

      tmp = tmp->next;
    }

  return TRUE;
}

void
desktop_entry_tree_foreach (DesktopEntryTree            *tree,
                            const char                  *parent_dir,
                            DesktopEntryTreeForeachFunc  func,
                            void                        *user_data)
{
  TreeNode *dir;
  
  build_tree (tree);
  if (tree->root == NULL)
    return;
  
  dir = tree_node_find_subdir (tree->root, parent_dir);
  if (dir == NULL)
    return;

  foreach_dir (tree, dir, 0, func, user_data);
}

static TreeNode*
tree_node_new (void)
{
  TreeNode *node;

  node = g_new0 (TreeNode, 1);
  node->name = NULL;
  node->dir_entry = NULL;
  node->entries = NULL;
  node->subdirs = NULL;

  return node;
}

static void
tree_node_free (TreeNode *node)
{
  GSList *tmp;

  g_free (node->name);
  
  tmp = node->subdirs;
  while (tmp != NULL)
    {
      tree_node_free (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (node->subdirs);

  tmp = node->entries;
  while (tmp != NULL)
    {
      entry_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (node->entries);

  if (node->dir_entry)
    entry_unref (node->dir_entry);

  g_free (node);
}

static gboolean
tree_node_free_if_broken (TreeNode *node)
{
  if (node->name == NULL)
    {
      menu_verbose ("Broken node is missing <Name>\n");
      tree_node_free (node);
      return TRUE;
    }
  else
    return FALSE;
}

static EntrySet*
menu_node_to_entry_set (EntryDirectoryList *list,
                        MenuNode           *node)
{
  EntrySet *set;

  set = NULL;
  
  switch (menu_node_get_type (node))
    {
    case MENU_NODE_AND:
      {
        MenuNode *child;
        
        child = menu_node_get_children (node);
        while (child != NULL)
          {
            EntrySet *child_set;
            child_set = menu_node_to_entry_set (list, child);

            if (set == NULL)
              set = child_set;
            else
              {
                entry_set_intersection (set, child_set);
                entry_set_unref (child_set);
              }

            /* as soon as we get empty results, we can bail,
             * because it's an AND
             */
            if (entry_set_get_count (set) == 0)
              break;
            
            child = menu_node_get_next (child);
          }
      }
      break;
      
    case MENU_NODE_OR:
      {
        MenuNode *child;
        
        child = menu_node_get_children (node);
        while (child != NULL)
          {
            EntrySet *child_set;
            child_set = menu_node_to_entry_set (list, child);
            
            if (set == NULL)
              set = child_set;
            else
              {
                entry_set_union (set, child_set);
                entry_set_unref (child_set);
              }
            
            child = menu_node_get_next (child);
          }
      }
      break;
      
    case MENU_NODE_NOT:
      {
        /* First get the OR of all the rules */
        MenuNode *child;
        
        child = menu_node_get_children (node);
        while (child != NULL)
          {
            EntrySet *child_set;
            child_set = menu_node_to_entry_set (list, child);
            
            if (set == NULL)
              set = child_set;
            else
              {
                entry_set_union (set, child_set);
                entry_set_unref (child_set);
              }
            
            child = menu_node_get_next (child);
          }

        if (set != NULL)
          {
            /* Now invert the result */
            entry_directory_list_invert_set (list, set);
          }
      }
      break;
      
    case MENU_NODE_ALL:
      set = entry_set_new ();
      entry_directory_list_get_all_desktops (list, set);
      break;

    case MENU_NODE_FILENAME:
      {
        Entry *e;
        e = entry_directory_list_get_desktop (list,
                                              menu_node_get_content (node));
        if (e != NULL)
          {
            set = entry_set_new ();
            entry_set_add_entry (set, e);
            entry_unref (e);
          }
      }
      break;
      
    case MENU_NODE_CATEGORY:
      set = entry_set_new ();
      entry_directory_list_get_by_category (list,
                                            menu_node_get_content (node),
                                            set);
      break;

    default:
      break;
    }

  if (set == NULL)
    set = entry_set_new (); /* create an empty set */
  
  return set;
}

static int
compare_entries_func (const void *a,
                      const void *b)
{
  Entry *ae = (Entry*) a;
  Entry *be = (Entry*) b;

  return strcmp (entry_get_relative_path (ae),
                 entry_get_relative_path (be));
}

static void
fill_tree_node_from_menu_node (TreeNode *tree_node,
                               MenuNode *menu_node)
{
  MenuNode *child;
  EntryDirectoryList *app_dirs;
  EntryDirectoryList *dir_dirs;
  EntrySet *entries;
  
  g_return_if_fail (menu_node_get_type (menu_node) == MENU_NODE_MENU);
  
  app_dirs = menu_node_menu_get_app_entries (menu_node);
  dir_dirs = menu_node_menu_get_directory_entries (menu_node);

  entries = entry_set_new ();
  
  child = menu_node_get_children (menu_node);
  while (child != NULL)
    {
      switch (menu_node_get_type (child))
        {
        case MENU_NODE_MENU:
          /* recurse */
          {
            TreeNode *child_tree;
            child_tree = tree_node_new ();
            fill_tree_node_from_menu_node (child_tree, child);
            if (!tree_node_free_if_broken (child_tree))
              tree_node->subdirs = g_slist_prepend (tree_node->subdirs,
                                                    child_tree);
          }
          break;

        case MENU_NODE_NAME:
          {
            if (tree_node->name)
              g_free (tree_node->name); /* should not happen */
            tree_node->name = g_strdup (menu_node_get_content (child));

            menu_verbose ("Processed <Name> new name = %p (%s)\n",
                          tree_node->name, tree_node->name ? tree_node->name : "none");
          }
          break;
          
        case MENU_NODE_INCLUDE:
          {
            /* The match rule children of the <Include> are
             * independent (logical OR) so we can process each one by
             * itself
             */
            MenuNode *rule;
            
            rule = menu_node_get_children (child);
            while (rule != NULL)
              {
                EntrySet *rule_set;
                rule_set = menu_node_to_entry_set (app_dirs, rule);

                if (rule_set != NULL)
                  {
                    entry_set_union (entries, rule_set);
                    entry_set_unref (rule_set);
                  }
                
                rule = menu_node_get_next (rule);
              }
          }
          break;

        case MENU_NODE_EXCLUDE:
          {
            /* The match rule children of the <Exclude> are
             * independent (logical OR) so we can process each one by
             * itself
             */
            MenuNode *rule;
            
            rule = menu_node_get_children (child);
            while (rule != NULL)
              {
                EntrySet *rule_set;
                rule_set = menu_node_to_entry_set (app_dirs, rule);
                
                if (rule_set != NULL)
                  {
                    entry_set_subtract (entries, rule_set);
                    entry_set_unref (rule_set);
                  }
                
                rule = menu_node_get_next (rule);
              }
          }
          break;

        case MENU_NODE_DIRECTORY:
          {
            Entry *e;

            /* The last <Directory> to exist wins, so we always try overwriting */
            e = entry_directory_list_get_directory (dir_dirs,
                                                    menu_node_get_content (child));

            if (e != NULL)
              {
                if (tree_node->dir_entry)
                  entry_unref (tree_node->dir_entry);
                tree_node->dir_entry = e; /* pass ref ownership */
              }

            menu_verbose ("Processed <Directory> new dir_entry = %p\n", tree_node->dir_entry);
          }
          break;
        default:
          break;
        }

      child = menu_node_get_next (child);
    }

  tree_node->entries = entry_set_list_entries (entries);
  entry_set_unref (entries);

  /* FIXME this is really only needed for the test suite, not
   * for real menus
   */
  tree_node->entries = g_slist_sort (tree_node->entries,
                                     compare_entries_func);
}

static void
build_tree (DesktopEntryTree *tree)
{
  if (tree->root != NULL)
    return;

  tree->root = tree_node_new ();
  fill_tree_node_from_menu_node (tree->root,
                                 find_menu_child (tree->resolved_node));
  if (tree_node_free_if_broken (tree->root))
    {
      tree->root = NULL;
      menu_verbose ("Broken root node!\n");
    }
}

typedef struct
{
  unsigned int flags;

} PrintData;

static gboolean
foreach_print (DesktopEntryTree *tree,
               gboolean          is_dir,
               int               depth,
               const char       *menu_basename,
               const char       *menu_fullpath,
               const char       *filesystem_path_to_entry,
               void             *data)
{
#define MAX_FIELDS 3
  PrintData *pd = data;
  int i;
  GnomeDesktopFile *df;
  GError *error;
  char *fields[MAX_FIELDS] = { NULL, NULL, NULL };
  char *name;
  char *generic_name;
  char *comment;

  g_assert (menu_basename != NULL);
  
  pd = data;

  name = NULL;
  generic_name = NULL;
  comment = NULL;
  
  df = NULL;
  error = NULL;
  if (filesystem_path_to_entry != NULL)
    {
      df = gnome_desktop_file_load (filesystem_path_to_entry, &error);
      if (df == NULL)
        {
          g_printerr ("Warning: failed to load desktop file \"%s\": %s\n",
                      filesystem_path_to_entry, error->message);
          g_error_free (error);
        }
      g_assert (error == NULL);
    }

  if (df != NULL)
    {
      gnome_desktop_file_get_locale_string (df,
                                            NULL,
                                            "Name",
                                            &name);
      gnome_desktop_file_get_locale_string (df,
                                            NULL,
                                            "GenericName",
                                            &generic_name);
      gnome_desktop_file_get_locale_string (df,
                                            NULL,
                                            "Comment",
                                            &comment);
      gnome_desktop_file_free (df);
      df = NULL;
    }

  if (name == NULL)
    name = g_strdup (menu_basename);

  if (generic_name == NULL)
    generic_name = g_strdup (_("<missing GenericName>"));    

  if (comment == NULL)
    comment = g_strdup (_("<missing Comment>"));
  
  i = depth;
  while (i > 0)
    {
      fputc (' ', stdout);
      --i;
    }

  i = 0;
  if (pd->flags & DESKTOP_ENTRY_TREE_PRINT_NAME)
    {
      fields[i] = name;  
      ++i;
    }
  
  if (pd->flags & DESKTOP_ENTRY_TREE_PRINT_GENERIC_NAME)
    {      
      fields[i] = generic_name;
      ++i;
    }

  if (pd->flags & DESKTOP_ENTRY_TREE_PRINT_COMMENT)
    {      
      fields[i] = comment;
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
    case 0:
      break;
    }

  g_free (name);
  g_free (generic_name);
  g_free (comment);

  if (pd->flags & DESKTOP_ENTRY_TREE_PRINT_TEST_RESULTS)
    {
      if (is_dir)
        g_print ("DIRECTORY %s\n", menu_basename);
      else
        g_print ("ENTRY %s\n", filesystem_path_to_entry);
    }
  
  return TRUE;
}

void
desktop_entry_tree_print (DesktopEntryTree           *tree,
                          DesktopEntryTreePrintFlags  flags)
{
  PrintData pd;

  pd.flags = flags;

  if (flags & DESKTOP_ENTRY_TREE_PRINT_TEST_RESULTS)
    {
      char *basename;

      basename = g_path_get_basename (tree->menu_file);
      g_print ("MENU \"%s\"\n", basename);
      g_free (basename);
    }
  
  desktop_entry_tree_foreach (tree, "/", foreach_print, &pd);
}

void
desktop_entry_tree_write_symlink_dir (DesktopEntryTree *tree,
                                      const char       *dirname)
{


}

void
desktop_entry_tree_dump_desktop_list (DesktopEntryTree *tree)
{


}

void
menu_set_verbose_queries (gboolean setting)
{
  /* FIXME */
}

static MenuNode*
menu_node_find_immediate_submenu (MenuNode    *parent,
                                  const char  *subdir)
{
  MenuNode *tmp;

  tmp = menu_node_get_children (parent);
  while (tmp != NULL)
    {
      if (menu_node_get_type (tmp) == MENU_NODE_MENU)
        {
          const char *name;
          name = menu_node_menu_get_name (tmp);
          if (name && strcmp (name, subdir) == 0)
            return tmp;
        }

      tmp = menu_node_get_next (tmp);
    }

  return NULL;
}

/* Get node if it's a dir and return TRUE if it's an entry */
static MenuNode*
menu_node_find_submenu (MenuNode   *node,
                        const char *name,
                        gboolean    create_if_not_found)
{
  char **split;
  int i;
  MenuNode *iter;
  MenuNode *prev;
  
  /* Skip leading '/' (we're already at the root) */
  i = 0;
  while (name[i] &&
         name[i] == '/')
    ++i;
  
  menu_verbose (" (splitting \"%s\")\n", name + i);
  
  split = g_strsplit (name + i, "/", -1);

  prev = NULL;
  iter = node;
  i = 0;
  while (split[i] != NULL && *(split[i]) != '\0' && iter != NULL)
    {
      prev = iter;
      iter = menu_node_find_immediate_submenu (iter, split[i]);

      menu_verbose ("MenuNode %p found for path component \"%s\"\n",
                    iter, split[i]);

      if (iter == NULL)
        {
          /* We ran out of nodes before running out of path
           * components
           */
          menu_verbose ("Remaining path component \"%s\" doesn't point to a current menu node\n",
                        split[i]);

          if (create_if_not_found)
            {
              MenuNode *name_node;

              menu_verbose ("Creating submenu \"%s\"\n", split[i]);
              
              iter = menu_node_new (MENU_NODE_MENU);
              name_node = menu_node_new (MENU_NODE_NAME);
              menu_node_set_content (name_node, split[i]);

              menu_node_append_child (iter, name_node);

              menu_node_append_child (prev, iter);

              g_assert (menu_node_menu_get_name (iter) != NULL);
              g_assert (strcmp (menu_node_menu_get_name (iter), split[i]) == 0);
              
              menu_node_unref (name_node);
              menu_node_unref (iter);
            }
        }

      ++i;
    }

  g_assert (iter == NULL || menu_node_get_parent (iter) == prev);
  
  g_strfreev (split);

  menu_verbose (" Found menu node %p parent is %p\n", iter, prev);

  return iter;
}

static void
menu_node_ensure_child (MenuNode    *parent,
                        MenuNodeType child_type,
                        const char  *child_content,
                        gboolean     content_as_path)
{
  MenuNode *tmp;
  gboolean already_there;

  menu_verbose ("Checking whether we already have a subnode with type %d and content \"%s\"\n", child_type, child_content);

  already_there = FALSE;
  tmp = menu_node_get_children (parent);
  while (tmp != NULL && !already_there)
    {
      if (menu_node_get_type (tmp) == child_type)
        {

          if (content_as_path)
            {
              char *name;
              name = menu_node_get_content_as_path (tmp);
              if (name && strcmp (name, child_content) == 0)
                {
                  menu_verbose ("Already have it!\n");
                  already_there = TRUE;
                }

              g_free (name);
            }
          else
            {
              const char *name;
              name = menu_node_get_content (tmp);
              if (name && strcmp (name, child_content) == 0)
                {
                  menu_verbose ("Already have it!\n");
                  already_there = TRUE;
                }
            }
        }

      tmp = menu_node_get_next (tmp);
    }

  if (!already_there)
    {
      MenuNode *node;
      
      menu_verbose ("Node not found, adding it\n");
      
      node = menu_node_new (child_type);
      menu_node_set_content (node, child_content);

      menu_node_append_child (parent, node);

      menu_node_unref (node);
    }
}

gboolean
desktop_entry_tree_include (DesktopEntryTree *tree,
                            const char       *menu_path_dirname,
                            const char       *menu_path_basename,
                            const char       *override_fs_dirname,
                            GError          **error)
{
  gboolean retval;
  MenuNode *node;
  MenuNode *submenu;
  
  retval = FALSE;

  /* Get node to modify and save */
  node = menu_cache_get_menu_for_canonical_file (tree->menu_cache,
                                                 tree->menu_file,
                                                 NULL, error);
  if (node == NULL)
    goto out;


  /* Create submenu */
  submenu = menu_node_find_submenu (node, menu_path_dirname, TRUE);

  g_assert (submenu != NULL);

  /* Add the given stuff to it */

  menu_node_ensure_child (submenu, MENU_NODE_APP_DIR,
                          override_fs_dirname,
                          TRUE);
  menu_node_ensure_child (submenu, MENU_NODE_INCLUDE,
                          menu_path_basename,
                          FALSE);
  
  /* Write to disk */
  if (!menu_cache_sync_for_file (tree->menu_cache,
                                 tree->menu_file,
                                 error))
    goto out;
  
  /* We have to reload the menu file */
  menu_cache_invalidate (tree->menu_cache,
                         tree->menu_file);
  

  retval = TRUE;

 out:
  
  return retval;
}

gboolean
desktop_entry_tree_exclude (DesktopEntryTree *tree,
                            const char       *menu_path_dirname,
                            const char       *menu_path_basename,
                            GError          **error)
{

  return TRUE;
}

gboolean
desktop_entry_tree_move (DesktopEntryTree *tree,
                         const char       *menu_path_dirname_src,
                         const char       *menu_path_dirname_dest,
                         const char       *menu_path_basename,
                         const char       *override_fs_dirname_dest,
                         GError          **error)
{



  return TRUE;
}
