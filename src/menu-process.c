/* Tree of desktop entries */

/*
 * Copyright (C) 2002 Red Hat, Inc.
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

static void menu_node_resolve_files (const char *node_dirname,
                                     const char *node_filename,
                                     MenuNode   *node);

static void
merge_resolved_copy_of_children (MenuNode   *where,
                                 MenuNode   *from,
                                 const char *from_filename)
{
  MenuNode *from_child;
  MenuNode *insert_after;
  MenuNode *from_copy;
  char *dirname;

  /* Copy and file-resolve the node */
  from_copy = menu_node_deep_copy (from);
  dirname = g_path_get_dirname (from_filename);
  menu_node_resolve_files (dirname, from_filename, from_copy);
  g_free (dirname);
  
  insert_after = where;
  from_child = menu_node_get_children (from_copy);
  while (from_child != NULL)
    {
      MenuNode *next;

      next = menu_node_get_next (from_child);
      
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
      
      from_child = next;
    }

  /* Now "from_copy" should be a single root node */
  menu_node_unref (from_copy);
}

static char*
node_get_filename (const char *dirname,
                   MenuNode   *node)
{
  const char *content;
  char *filename;
  
  content = menu_node_get_content (node);
  
  if (content == NULL)
    {
      /* really the parser should have caught this */
      menu_verbose ("MergeFile/MergeDir/LegacyDir element has no content!\n");
      return NULL;
    }

  if (*content != '/')
    {
      filename = g_build_filename (dirname, content, NULL);
      menu_verbose ("Node contains file %s made relative to %s\n",
                    content, dirname);
    }
  else
    {
      filename = g_strdup (content);
      menu_verbose ("Node contains absolute path %s\n",
                    content);
    }

  return filename;
}

static void
menu_node_resolve_files (const char *node_dirname,
                         const char *node_filename,
                         MenuNode   *node)
{
  MenuNode *child;

  /* FIXME
   * if someone does <MergeFile>A.menu</MergeFile> inside
   * A.menu, or a more elaborate loop involving multiple
   * files, we'll just get really hosed and eat all the RAM
   * we can find
   */
  
  child = menu_node_get_children (node);

  while (child != NULL)
    {
      MenuNode *next;

      /* get next first, because we delete this child (and place new
       * file contents between "child" and "next")
       */
      next = menu_node_get_next (child);
      
      switch (menu_node_get_type (child))
        {
        case MENU_NODE_MERGE_FILE:
          {
            MenuNode *to_merge;
            char *filename;

            filename = node_get_filename (node_dirname, child);
            if (filename == NULL)
              goto done;
            
            to_merge = menu_node_get_for_file (filename);
            if (to_merge == NULL)
              goto done;
            
            merge_resolved_copy_of_children (child, to_merge,
                                             filename);

            menu_node_unref (to_merge);

          done:
            g_free (filename);
            menu_node_unlink (child); /* delete this child, replaced
                                       * by the merged content
                                       */
          }
          break;
        case MENU_NODE_MERGE_DIR:
          {
            /* FIXME don't just delete it ;-) */
            
            menu_node_unlink (child);
          }
          break;
        case MENU_NODE_LEGACY_DIR:
          {
            /* FIXME don't just delete it ;-) */
            
            menu_node_unlink (child);
          }
          break;

#if 0
          /* FIXME may as well expand these here */
        case MENU_NODE_DEFAULT_APP_DIRS:
          break;
        case MENU_NODE_DEFAULT_DIRECTORY_DIRS:
          break;
#endif
          
        default:
          break;
        }

      child = next;
    }
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

  recurse_nodes = NULL;
  prev = NULL;
  tmp = menu_nodes;
  while (tmp != NULL)
    {
      if (prev)
        {
          MenuNode *p = prev->data;
          MenuNode *n = tmp->data;

          if (menu_node_compare_func (p, n) == 0)
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

typedef struct
{
  Entry  *dir_entry;
  GSList *entries;
  GSList *children;
} TreeNode;

struct _DesktopEntryTree
{
  char *menu_file;
  char *menu_file_dir;
  MenuNode *orig_node;
  MenuNode *resolved_node;
  TreeNode *root;
};

DesktopEntryTree*
desktop_entry_tree_load (const char *filename)
{
  DesktopEntryTree *tree;
  MenuNode *orig_node;
  MenuNode *resolved_node;
  char *dirname;
  char *canonical;

  canonical = g_canonicalize_file_name (filename, NULL);
  if (canonical == NULL)
    return NULL;
  
  orig_node = menu_node_get_for_canonical_file (canonical);
  if (orig_node == NULL)
    {
      g_free (canonical);
      return NULL;
    }

  dirname = g_path_get_dirname (canonical);

  resolved_node = menu_node_deep_copy (orig_node);
  menu_node_resolve_files (dirname, canonical, resolved_node);

  menu_node_strip_duplicate_children (resolved_node);

  tree = g_new0 (DesktopEntryTree, 1);
  tree->menu_file = canonical;
  tree->menu_file_dir = dirname;
  tree->orig_node = orig_node;
  tree->resolved_node = resolved_node;
  tree->root = NULL;
  
  return tree;
}

