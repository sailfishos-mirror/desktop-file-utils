/* Menu layout in-memory data structure (a custom "DOM tree") */

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

#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

#include <glib.h>

typedef struct _MenuNode MenuNode;
typedef struct _Entry Entry;
typedef struct _EntryDirectory EntryDirectory;
typedef struct _EntryDirectoryList EntryDirectoryList;
typedef struct _EntrySet EntrySet;

typedef enum
{
  MENU_NODE_PASSTHROUGH,
  MENU_NODE_MENU,
  MENU_NODE_APP_DIR,
  MENU_NODE_DEFAULT_APP_DIRS,
  MENU_NODE_DIRECTORY_DIR,
  MENU_NODE_DEFAULT_DIRECTORY_DIRS,
  MENU_NODE_NAME,
  MENU_NODE_DIRECTORY,
  MENU_NODE_ONLY_UNALLOCATED,
  MENU_NODE_NOT_ONLY_UNALLOCATED,
  MENU_NODE_INCLUDE,
  MENU_NODE_EXCLUDE,
  MENU_NODE_FILENAME,
  MENU_NODE_CATEGORY,
  MENU_NODE_ALL,
  MENU_NODE_AND,
  MENU_NODE_OR,
  MENU_NODE_NOT,
  MENU_NODE_MERGE_FILE,
  MENU_NODE_MERGE_DIR,
  MENU_NODE_LEGACY_DIR,
  MENU_NODE_MOVE,
  MENU_NODE_OLD,
  MENU_NODE_NEW,
  MENU_NODE_DELETED,
  MENU_NODE_NOT_DELETED
} MenuNodeType;

void         menu_node_ref          (MenuNode *node);
void         menu_node_unref        (MenuNode *node);
MenuNode*    menu_node_deep_copy    (MenuNode *node);
MenuNode*    menu_node_copy_one     (MenuNode *node);
MenuNode*    menu_node_get_next     (MenuNode *node);
MenuNode*    menu_node_get_parent   (MenuNode *node);
MenuNode*    menu_node_get_children (MenuNode *node);
MenuNode*    menu_node_get_root     (MenuNode *node);

void menu_node_insert_before (MenuNode *node,
                              MenuNode *new_sibling);
void menu_node_insert_after  (MenuNode *node,
                              MenuNode *new_sibling);
void menu_node_prepend_child (MenuNode *parent,
                              MenuNode *new_child);
void menu_node_append_child  (MenuNode *parent,
                              MenuNode *new_child);

void menu_node_unlink (MenuNode *node);
void menu_node_steal  (MenuNode *node);

MenuNodeType menu_node_get_type      (MenuNode *node);
const char*  menu_node_get_content   (MenuNode *node);
const char*  menu_node_get_filename  (MenuNode *node);
const char*  menu_node_menu_get_name (MenuNode *node);

EntryDirectoryList* menu_node_menu_get_app_entries (MenuNode *node);
EntryDirectoryList* menu_node_menu_get_directory_entries (MenuNode *node);

/* Return the pristine menu node for the file.
 * Changing this node will change what gets written
 * back to disk, if you do menu_node_sync_for_file.
 * get_for_file returns a new reference count.
 * the menu node for each file is cached while references
 * are outstanding.
 */
MenuNode* menu_node_get_for_file            (const char *filename);
MenuNode* menu_node_get_for_canonical_file  (const char *filename);
void      menu_node_sync_for_file           (const char *filename);

/* random utility function */
void menu_verbose    (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);

#endif /* MENU_LAYOUT_H */
