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
#include <stdio.h>
#include <string.h>

static void load_tree (DesktopFileTree *tree);

struct _DesktopFileTree
{
  Vfolder *folder;

  GNode *node;

  guint loaded : 1;
};

DesktopFileTree*
desktop_file_tree_new (Vfolder *folder)
{
  DesktopFileTree *tree;

  tree = g_new0 (DesktopFileTree, 1);

  tree->folder = folder;

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
  if (tree->node)
    g_node_traverse (tree->node,
                     G_IN_ORDER, G_TRAVERSE_ALL,
                     -1, free_node_func, NULL);

  /* we don't own tree->folder which is bad practice but what the hell */
  
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
    g_free (fields[i]);
  
  return TRUE;
}

void
desktop_file_tree_print (DesktopFileTree          *tree,
                         DesktopFileTreePrintFlags flags)
{
  PrintData pd;

  pd.depth = 0;
  pd.flags = flags;
  
  if (tree->node)
    g_node_traverse (tree->node,
                     G_IN_ORDER, G_TRAVERSE_ALL,
                     -1, print_node_func, &pd);
}

static void
load_tree (DesktopFileTree *tree)
{
  if (tree->loaded)
    return;

  tree->loaded = TRUE;
  
  
}
