/* Menu layout in-memory data structure (a custom "DOM tree") */

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

#include <config.h>
#include <string.h>
#include <stdio.h>
#include "menu-layout.h"
#include "canonicalize.h"
#include "menu-entries.h"
#include "menu-parser.h"

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x

typedef struct _MenuFile MenuFile;
typedef struct _MenuNodeMenu MenuNodeMenu;
typedef struct _MenuNodeLegacyDir MenuNodeLegacyDir;

struct _MenuFile
{
  char *filename;
  MenuNode *root;
};

struct _MenuNode
{
  /* Node lists are circular, for length-one lists
   * prev/next point back to the node itself.
   */
  MenuNode *prev;
  MenuNode *next;
  MenuNode *parent;
  MenuNode *children;  

  char *content;

  guint is_file_root : 1;
  guint refcount : 20;
  guint type : 7;
};

struct _MenuNodeMenu
{
  MenuNode node;
  MenuNode *name_node; /* cache of the <Name> node */
  EntryDirectoryList *app_dirs;
  EntryDirectoryList *dir_dirs;
};

struct _MenuNodeLegacyDir
{
  MenuNode node;
  char *prefix;
};

/* root nodes (no parent) never have siblings */
#define NODE_NEXT(node)                                 \
((node)->parent == NULL) ?                              \
  NULL : (((node)->next == (node)->parent->children) ?  \
          NULL : (node)->next)

static MenuFile* find_file_by_name (const char *filename);
static MenuFile* find_file_by_node (MenuNode *node);
static void      drop_menu_file    (MenuFile *file);

void
menu_node_ref (MenuNode *node)
{
  g_return_if_fail (node != NULL);
  node->refcount += 1;
}

void
menu_node_unref (MenuNode *node)
{
  g_return_if_fail (node != NULL);
  g_return_if_fail (node->refcount > 0);
  
  node->refcount -= 1;
  if (node->refcount == 0)
    {
      MenuNode *iter;
      MenuNode *next;

      if (node->is_file_root)
        {
          MenuFile *f;

          f = find_file_by_node (node);
          if (f != NULL)
            drop_menu_file (f);
        }
      
      /* unref children */
      iter = node->children;
      while (iter != NULL)
        {
          next = NODE_NEXT (iter);
          menu_node_unref (iter);
          iter = next;
        }

      if (node->type == MENU_NODE_MENU)
        {
          MenuNodeMenu *nm = (MenuNodeMenu*) node;

          if (nm->name_node)
            menu_node_unref (nm->name_node);

          if (nm->app_dirs)
            entry_directory_list_unref (nm->app_dirs);

          if (nm->dir_dirs)
            entry_directory_list_unref (nm->dir_dirs);
        }
      else if (node->type == MENU_NODE_LEGACY_DIR)
        {
          MenuNodeLegacyDir *nld = (MenuNodeLegacyDir*) node;

          g_free (nld->prefix);
        }
      
      /* free ourselves */
      g_free (node->content);
      g_free (node);
    }
}

MenuNode*
menu_node_new (MenuNodeType type)
{
  MenuNode *node;

  if (type == MENU_NODE_MENU)
    node = (MenuNode*) g_new0 (MenuNodeMenu, 1);
  else if (type == MENU_NODE_LEGACY_DIR)
    node = (MenuNode*) g_new0 (MenuNodeLegacyDir, 1);
  else
    node = g_new0 (MenuNode, 1);

  node->type = type;
  
  node->refcount = 1;
  
  /* we're in a list of one node */
  node->next = node;
  node->prev = node;
  
  return node;
}

MenuNode*
menu_node_deep_copy (MenuNode *node)
{
  MenuNode *copy;
  MenuNode *iter;
  MenuNode *next;
  
  copy = menu_node_copy_one (node);

  /* Copy children */
  iter = node->children;
  while (iter != NULL)
    {
      MenuNode *child;
      
      next = NODE_NEXT (iter);

      child = menu_node_deep_copy (iter);
      menu_node_append_child (copy, child);
      menu_node_unref (child);
      
      iter = next;
    }

  return copy;
}

MenuNode*
menu_node_copy_one (MenuNode *node)
{
  MenuNode *copy;

  copy = menu_node_new (node->type);

  copy->content = g_strdup (node->content);

  /* don't copy is_file_root */
  
  return copy;
}

MenuNode*
menu_node_get_next (MenuNode *node)
{
  return NODE_NEXT (node);
}

MenuNode*
menu_node_get_parent (MenuNode *node)
{
  return node->parent;
}

MenuNode*
menu_node_get_children (MenuNode *node)
{
  return node->children;
}

MenuNode*
menu_node_get_root (MenuNode *node)
{
  MenuNode *parent;

  parent = node;
  while (parent->parent != NULL)
    parent = parent->parent;

  return parent;
}

#define RETURN_IF_NO_PARENT(node) \
do                                                              \
{                                                               \
  if ((node)->parent == NULL)                                   \
    {                                                           \
      g_warning ("To add siblings to a menu node, "             \
                 "it must not be the root node, "               \
                 "and must be linked in below some root node"); \
      return;                                                   \
    }                                                           \
}                                                               \
while (0)

#define RETURN_IF_HAS_ENTRY_DIRS(node)                          \
do                                                              \
{                                                               \
  if ((node)->type == MENU_NODE_MENU &&                         \
      (((MenuNodeMenu*)(node))->app_dirs != NULL ||             \
       ((MenuNodeMenu*)(node))->dir_dirs != NULL))              \
    {                                                           \
      g_warning ("node acquired ->app_dirs or ->dir_dirs "      \
                 "while not rooted in a tree\n");               \
      return;                                                   \
    }                                                           \
}                                                               \
while (0)

void
menu_node_insert_before (MenuNode *node,
                         MenuNode *new_sibling)
{
  RETURN_IF_NO_PARENT (node);
  RETURN_IF_HAS_ENTRY_DIRS (new_sibling);
  g_return_if_fail (new_sibling != NULL);
  g_return_if_fail (new_sibling->parent == NULL);

  new_sibling->next = node;
  new_sibling->prev = node->prev;
  node->prev = new_sibling;
  new_sibling->prev->next = new_sibling;

  new_sibling->parent = node->parent;
  
  if (node == node->parent->children)
    node->parent->children = new_sibling;

  menu_node_ref (new_sibling);
}

void
menu_node_insert_after (MenuNode *node,
                        MenuNode *new_sibling)
{
  RETURN_IF_NO_PARENT (node);
  RETURN_IF_HAS_ENTRY_DIRS (new_sibling);
  g_return_if_fail (new_sibling != NULL);
  g_return_if_fail (new_sibling->parent == NULL);

  new_sibling->prev = node;
  new_sibling->next = node->next;
  node->next = new_sibling;
  new_sibling->next->prev = new_sibling;
  
  new_sibling->parent = node->parent;
  
  menu_node_ref (new_sibling);
}

void
menu_node_prepend_child (MenuNode *parent,
                         MenuNode *new_child)
{
  RETURN_IF_HAS_ENTRY_DIRS (new_child);
  
  if (parent->children)
    menu_node_insert_before (parent->children, new_child);
  else
    {
      parent->children = new_child;
      new_child->parent = parent;
      menu_node_ref (new_child);
    }
}

void
menu_node_append_child (MenuNode *parent,
                        MenuNode *new_child)
{  
  RETURN_IF_HAS_ENTRY_DIRS (new_child);
  
  if (parent->children)
    menu_node_insert_before (parent->children->prev, new_child);
  else
    {
      parent->children = new_child;
      new_child->parent = parent;
      menu_node_ref (new_child);
    }
}

void
menu_node_unlink (MenuNode *node)
{
  g_return_if_fail (node != NULL);
  g_return_if_fail (node->parent != NULL);

  menu_node_steal (node);
  menu_node_unref (node);
}

static void
recursive_clean_entry_directory_lists (MenuNode *node,
                                       gboolean  apps)
{
  MenuNodeMenu *nm;
  MenuNode *iter;
  
  if (node->type != MENU_NODE_MENU)
    return;

  nm = (MenuNodeMenu*) node;

  if (apps)
    {
      if (nm->app_dirs == NULL ||
          entry_directory_list_get_length (nm->app_dirs) == 0)
        return; /* child menus continue to have valid lists */
      
      entry_directory_list_unref (nm->app_dirs);
      nm->app_dirs = NULL;
    }
  else
    {
      if (nm->dir_dirs == NULL ||
          entry_directory_list_get_length (nm->dir_dirs) == 0)
        return; /* child menus continue to have valid lists */
      
      entry_directory_list_unref (nm->dir_dirs);
      nm->dir_dirs = NULL;
    }
  
  iter = node->children;
  while (iter != NULL)
    {
      MenuNode *next;
      
      next = NODE_NEXT (iter);
      if (iter->type == MENU_NODE_MENU)
        recursive_clean_entry_directory_lists (iter, apps);
      iter = next;
    }
}

void
menu_node_steal (MenuNode *node)
{
  g_return_if_fail (node != NULL);
  g_return_if_fail (node->parent != NULL);

  switch (node->type)
    {
    case MENU_NODE_NAME:
      {
        MenuNodeMenu *nm = (MenuNodeMenu*) node->parent;
        if (nm->name_node == node)
          {
            menu_node_unref (nm->name_node);
            nm->name_node = NULL;
          }
      }
      break;
    case MENU_NODE_APP_DIR:
      recursive_clean_entry_directory_lists (node, TRUE);
      break;
    case MENU_NODE_DIRECTORY_DIR:
      recursive_clean_entry_directory_lists (node, FALSE);
      break;

    default:
      break;
    }
  
  /* these are no-ops for length-one node lists */
  node->prev->next = node->next;
  node->next->prev = node->prev;
  
  node->parent = NULL;
  
  /* point to ourselves, now we're length one */
  node->next = node;
  node->prev = node;
}

MenuNodeType
menu_node_get_type (MenuNode *node)
{
  return node->type;
}

const char*
menu_node_get_content (MenuNode *node)
{
  return node->content;
}

void
menu_node_set_content   (MenuNode   *node,
                         const char *content)
{
  if (node->content == content)
    return;
  
  g_free (node->content);
  node->content = g_strdup (content);
}

const char*
menu_node_get_filename (MenuNode *node)
{
  MenuFile *file;

  file = find_file_by_node (node);
  if (file)
    return file->filename;
  else
    return NULL;
}

const char*
menu_node_menu_get_name (MenuNode *node)
{
  MenuNodeMenu *nm;
  
  g_return_val_if_fail (node->type == MENU_NODE_MENU, NULL);

  nm = (MenuNodeMenu*) node;

  if (nm->name_node == NULL)
    {
      MenuNode *iter;
      
      iter = node->children;
      while (iter != NULL)
        {
          MenuNode *next;
          
          next = NODE_NEXT (iter);

          if (iter->type == MENU_NODE_NAME)
            {
              nm->name_node = iter;
              menu_node_ref (nm->name_node);
              break;
            }
          
          iter = next;
        }
    }
  
  if (nm->name_node)
    return menu_node_get_content (nm->name_node);
  else
    return NULL;
}

const char*
menu_node_legacy_dir_get_prefix (MenuNode   *node)
{
  MenuNodeLegacyDir *nld;
  
  g_return_val_if_fail (node->type == MENU_NODE_LEGACY_DIR, NULL);

  nld = (MenuNodeLegacyDir*) node;
  
  return nld->prefix;
}

void
menu_node_legacy_dir_set_prefix (MenuNode   *node,
                                 const char *prefix)
{
  MenuNodeLegacyDir *nld;
  
  g_return_if_fail (node->type == MENU_NODE_LEGACY_DIR);

  nld = (MenuNodeLegacyDir*) node;

  if (nld->prefix == prefix)
    return;
  
  g_free (nld->prefix);
  nld->prefix = g_strdup (prefix);
}

static void
menu_node_menu_ensure_entry_lists (MenuNode *node)
{
  MenuNodeMenu *nm;
  MenuNode *parent;
  MenuNode *iter;
  GSList *app_dirs;
  GSList *dir_dirs;
  GSList *tmp;
  
  g_return_if_fail (node->type == MENU_NODE_MENU);

  nm = (MenuNodeMenu*) node;  

  if (nm->app_dirs && nm->dir_dirs)
    return;
  
  app_dirs = NULL;
  dir_dirs = NULL;
      
  iter = node->children;
  while (iter != NULL)
    {
      MenuNode *next;
      
      next = NODE_NEXT (iter);
      
      if ((nm->app_dirs == NULL && iter->type == MENU_NODE_APP_DIR) ||
          (nm->dir_dirs == NULL && iter->type == MENU_NODE_DIRECTORY_DIR) ||
          iter->type == MENU_NODE_LEGACY_DIR)
        {
          EntryDirectory *ed;

          if (iter->type == MENU_NODE_APP_DIR)
            {
              ed = entry_directory_load (iter->content,
                                         ENTRY_LOAD_DESKTOPS, NULL);
              if (ed != NULL)
                app_dirs = g_slist_prepend (app_dirs, ed);
            }
          else if (iter->type == MENU_NODE_DIRECTORY_DIR)
            {
              ed = entry_directory_load (iter->content,
                                         ENTRY_LOAD_DIRECTORIES, NULL);
              if (ed != NULL)
                dir_dirs = g_slist_prepend (dir_dirs, ed);
            }
          else if (iter->type == MENU_NODE_LEGACY_DIR)
            {
              if (nm->app_dirs == NULL)
                {
                  ed = entry_directory_load (iter->content,
                                             ENTRY_LOAD_DESKTOPS | ENTRY_LOAD_LEGACY,
                                             NULL);
                  if (ed != NULL)
                    app_dirs = g_slist_prepend (app_dirs, ed);
                }
              
              if (nm->dir_dirs == NULL)
                {
                  ed = entry_directory_load (iter->content,
                                             ENTRY_LOAD_DIRECTORIES | ENTRY_LOAD_LEGACY,
                                             NULL);
                  if (ed != NULL)
                    dir_dirs = g_slist_prepend (dir_dirs, ed);
                }
            }
        }
          
      iter = next;
    }

  if (nm->app_dirs == NULL)
    {
      if (app_dirs == NULL)
        {
          /* If we don't have anything new to add, just find parent
           * EntryDirectoryList, or create an empty one if we're a
           * root node
           */
          if (node->parent == NULL)
            nm->app_dirs = entry_directory_list_new ();
          else
            {
              nm->app_dirs = menu_node_menu_get_app_entries (node->parent);
              entry_directory_list_ref (nm->app_dirs);
            }
        }
      else
        {
          /* The app_dirs are currently in the same order they
           * should be for the directory list
           */
          tmp = app_dirs;
          while (tmp != NULL)
            {
              entry_directory_list_append (nm->app_dirs, tmp->data);
              entry_directory_unref (tmp->data);
              tmp = tmp->next;
            }

          g_slist_free (tmp->data);
          
          /* Get all the EntryDirectory from parent nodes */
          parent = node->parent;
          while (parent != NULL)
            {
              EntryDirectoryList *plist;
              
              plist = menu_node_menu_get_app_entries (parent);
              
              if (plist != NULL)
                entry_directory_list_append_list (nm->app_dirs,
                                                  plist);
              
              parent = parent->parent;
            }
        }
    }
  else
    {
      g_assert (app_dirs == NULL);
    }

  if (nm->dir_dirs == NULL)
    {
      if (dir_dirs == NULL)
        {
          /* If we don't have anything new to add, just find parent
           * EntryDirectoryList, or create an empty one if we're a
           * root node
           */
          if (node->parent == NULL)
            nm->dir_dirs = entry_directory_list_new ();
          else
            {
              nm->dir_dirs = menu_node_menu_get_directory_entries (node->parent);
              entry_directory_list_ref (nm->dir_dirs);
            }
        }
      else
        {
          /* The dir_dirs are currently in the same order they
           * should be for the directory list
           */
          tmp = dir_dirs;
          while (tmp != NULL)
            {
              entry_directory_list_append (nm->dir_dirs, tmp->data);
              entry_directory_unref (tmp->data);
              tmp = tmp->next;
            }

          g_slist_free (tmp->data);
          
          /* Get all the EntryDirectory from parent nodes */
          parent = node->parent;
          while (parent != NULL)
            {
              EntryDirectoryList *plist;
              
              plist = menu_node_menu_get_directory_entries (parent);
              
              if (plist != NULL)
                entry_directory_list_append_list (nm->dir_dirs,
                                                  plist);
              
              parent = parent->parent;
            }
        }
    }
  else
    {
      g_assert (dir_dirs == NULL);
    }  
}
     
EntryDirectoryList*
menu_node_menu_get_app_entries (MenuNode *node)
{
  MenuNodeMenu *nm;
  
  g_return_val_if_fail (node->type == MENU_NODE_MENU, NULL);

  nm = (MenuNodeMenu*) node;
  
  menu_node_menu_ensure_entry_lists (node);
  
  return nm->app_dirs;
}

EntryDirectoryList*
menu_node_menu_get_directory_entries (MenuNode *node)
{
  MenuNodeMenu *nm;
  
  g_return_val_if_fail (node->type == MENU_NODE_MENU, NULL);

  nm = (MenuNodeMenu*) node;

  menu_node_menu_ensure_entry_lists (node);
  
  return nm->dir_dirs;
}

static GSList *menu_files = NULL;

static MenuFile*
find_file_by_name (const char *filename)
{
  GSList *tmp;

  tmp = menu_files;
  while (tmp != NULL)
    {
      MenuFile *f = tmp->data;
      if (strcmp (filename, f->filename) == 0)
        return f;
      tmp = tmp->next;
    }

  return NULL;
}

static MenuFile*
find_file_by_node (MenuNode *node)
{
  GSList *tmp;
  MenuNode *root;

  root = menu_node_get_root (node);
  g_assert (root->type == MENU_NODE_ROOT);
  
  tmp = menu_files;
  while (tmp != NULL)
    {
      MenuFile *f = tmp->data;
      if (root == f->root)
        return f;
      tmp = tmp->next;
    }

  return NULL;
}

static void
drop_menu_file (MenuFile *file)
{
  menu_files = g_slist_remove (menu_files, file);

  /* we should be called from the unref() of the root node */
  g_assert (file->root->refcount == 0);
  
  g_free (file->filename);
  g_free (file);
}

MenuNode*
menu_node_get_for_canonical_file  (const char *canonical,
                                   GError    **error)
{
  MenuFile *file;
  MenuNode *node;
  
  file = find_file_by_name (canonical);
  
  if (file)
    {
      g_assert (file->root->is_file_root);
      
      menu_node_ref (file->root);
      return file->root;
    }

  node = menu_load (canonical, error);
  if (node == NULL)
    return NULL; /* FIXME - cache failures? */
  
  file = g_new0 (MenuFile, 1);

  file->filename = g_strdup (canonical);
  file->root = node;

  menu_files = g_slist_prepend (menu_files, file);

  menu_node_ref (file->root);
  
  return file->root;
}

MenuNode*
menu_node_get_for_file  (const char *filename,
                         GError    **error)
{
  char *canonical;
  MenuNode *node;

  canonical = g_canonicalize_file_name (filename);
  if (canonical == NULL)
    {
      g_set_error (error, G_FILE_ERROR,
                   G_FILE_ERROR_FAILED,
                   _("Could not canonicalize filename \"%s\"\n"),
                   filename);
      return NULL;
    }
  
  node = menu_node_get_for_canonical_file (canonical,
                                           error);

  g_free (canonical);

  return node;
}

gboolean
menu_node_sync_for_file (const char  *filename,
                         GError     **error)
{
  MenuFile *file;
  char *canonical;

  canonical = g_canonicalize_file_name (filename);
  if (canonical == NULL)
    {
      g_set_error (error, G_FILE_ERROR,
                   G_FILE_ERROR_FAILED,
                   _("Could not canonicalize filename \"%s\"\n"),
                   filename);
      return FALSE;
    }
  
  file = find_file_by_name (canonical);

  g_free (canonical);
  
  if (file == NULL)
    {
      g_set_error (error, G_FILE_ERROR,
                   G_FILE_ERROR_FAILED,
                   _("No menu file loaded for filename \"%s\"\n"),
                   filename);
      return FALSE;
    }

  /* FIXME save file */

  return TRUE;
}

static int
utf8_fputs (const char *str,
            FILE       *f)
{
  char *l;
  
  l = g_locale_from_utf8 (str, -1, NULL, NULL, NULL);

  if (l == NULL)
    fputs (str, f); /* just print it anyway, better than nothing */
  else
    fputs (l, f);

  g_free (l);
}

void
menu_verbose (const char *format,
              ...)
{
  va_list args;
  char *str;
  static gboolean verbose = FALSE;
  static gboolean initted = FALSE;
  
  if (!initted)
    {
      verbose = g_getenv ("DFU_MENU_VERBOSE") != NULL;
      initted = TRUE;
    }

  if (!verbose)
    return;
    
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  utf8_fputs (str, stderr);
  fflush (stderr);
  
  g_free (str);
}
