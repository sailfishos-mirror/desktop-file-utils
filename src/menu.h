/* "public" API for dealing with menus */

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

#ifndef MENU_H
#define MENU_H

typedef struct MenuTree     MenuTree;
typedef struct MenuTreeNode MenuTreeNode;

typedef enum
{
  MENU_TREE_NODE_DIRECTORY,
  MENU_TREE_NODE_ENTRY
} MenuTreeNodeType;

MenuTree* menu_tree_load_and_lock    (const char *filename);
void      menu_tree_ref              (MenuTree   *tree);
void      menu_tree_unref_and_unlock (MenuTree   *tree);

void menu_tree_lock   (MenuTree *tree);
void menu_tree_unlock (MenuTree *tree);

/* The lock has to be held to call any of the below functions */

/* returns a new ref */
MenuTreeNode* menu_tree_get_root (MenuTree *tree);

void menu_tree_node_ref    (MenuTreeNode *node);
void menu_tree_node_unref  (MenuTreeNode *node);

MenuTreeNodeType menu_tree_node_get_type     (MenuTreeNode *node);
MenuTreeNode*    menu_tree_node_get_children (MenuTreeNode *node);
MenuTreeNode*    menu_tree_node_get_next     (MenuTreeNode *node);

/* returns a copy of .directory file absolute path */
char*       menu_tree_node_get_directory_file (MenuTreeNode *node);
char*       menu_tree_node_get_entry_file     (MenuTreeNode *node);
const char* menu_tree_node_get_directory_name (MenuTreeNode *node);

/* return the path within the tree, relative to root */
char*       menu_tree_node_get_path           (MenuTreeNode *node);


void        menu_tree_node_move               (MenuTreeNode *node,
                                               const char   *tree_path,
                                               GError      **error);

void        menu_tree_node_delete             (MenuTreeNode *node,
                                               const char   *tree_path,
                                               GError      **error);

void        menu_tree_node_add                (MenuTreeNode *node,
                                               const char   *filesystem_path,
                                               GError      **error);

void        menu_tree_node_copy                (MenuTreeNode *node,
                                                const char   *tree_path,
                                                GError      **error);

gboolean    menu_tree_node_is_writable         (MenuTreeNode *node); 

#endif /* MENU_H */
