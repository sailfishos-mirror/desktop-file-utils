/* Test program for menu system */

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

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static char*
find_word (const char *str,
           const char *word)
{
  char *p;
  
  p = strstr (str, word);
  if (p == NULL)
    {
      g_printerr ("%s not found\n", word);
      exit (1);
    }

  return p;
}

static char*
find_eol (const char *str)
{
  char *p;
  
  p = strchr (str, '\n');
  if (p == NULL)
    {
      g_printerr ("newline not found\n");
      exit (1);
    }

  return (char*) p;
}

static char*
skip_whitespace (const char *str,
                 int        *spaces_after_newline)
{
  const char *p;

  if (spaces_after_newline)
    *spaces_after_newline = 0;
  
  p = str;
  while (*p && (*p == ' ' || *p == '\n'))
    {
      if (spaces_after_newline)
        {
          if (*p == ' ')
            *spaces_after_newline += 1;
          else
            *spaces_after_newline = 0;
        }
      ++p;
    }

  return (char*) p;
}

static char*
skip_spaces (const char *str,
             int        *n_skipped)
{
  const char *p;

  if (n_skipped)
    *n_skipped = 0;
  
  p = str;
  while (*p && *p == ' ')
    {
      if (n_skipped)
        *n_skipped += 1;
      ++p;
    }

  return (char*) p;
}

static char*
dup_quoted_string (const char *str, 
                   char      **end)
{
  const char *p = str;
  GString *gstr = g_string_new (NULL);
  gboolean in_quotes = FALSE;

  while (*p)
    {
      if (in_quotes)
        {
          if (*p == '\'')
            in_quotes = FALSE;
          else
            g_string_append_c (gstr, *p);
        }
      else
        {
          if (*p == '\'')
            in_quotes = TRUE;
          else if (*p == ' ' || *p == '\n' || *p == '\t')
            break; /* end on whitespace if not quoted */
          else
            g_string_append_c (gstr, *p);
        }
      
      ++p;
    }

  if (end)
    *end = (char*) p;

  return g_string_free (gstr, FALSE);
}

typedef enum
{
  NODE_DIRECTORY,
  NODE_ENTRY
} NodeType;

typedef struct
{
  NodeType type;
  char *filename;
  int depth;
  char *name; /* directories only */
} Node;

static Node*
node_new (NodeType    type,
          const char *filename,
          int         depth)
{
  Node *n;

  n = g_new0 (Node, 1);
  n->type = type;
  if (filename)
    n->filename = dup_quoted_string (filename, NULL);
  else
    n->filename = NULL;
  n->depth = depth;
  n->name = NULL;
  
  return n;
}

#define DIRECTORY_LEN 9
#define ENTRY_LEN 5
#define MENU_LEN 4

static char*
create_node (char       *line,
             const char *pwd,
             Node      **node)
{
  char *p;
  int depth;

  *node = NULL;
  
  depth = 0;
  p = skip_whitespace (line, &depth);
  if (*p != '\0')
    {
      if (strncmp (p, "DIRECTORY", DIRECTORY_LEN) == 0)
        {
          char *eol;
          
          p += DIRECTORY_LEN;
          p = skip_spaces (p, NULL);
          eol = find_eol (p);
          if (*p && eol)
            {
              char *name;

              /* DIRECTORY Applications /usr/share/blah/Applications.directory */
              
              *eol = '\0';
              
              name = dup_quoted_string (p, &p); 
              p = skip_spaces (p, NULL);
              
              *node = node_new (NODE_DIRECTORY,
                                *p != '\0' ? p : NULL,
                                depth);
              (*node)->name = name;
              
              p = eol + 1;
            }
          else
            {
              g_printerr ("no name for DIRECTORY or no newline\n");
              exit (1);
            }
        }
      else if (strncmp (p, "ENTRY", ENTRY_LEN) == 0)
        {
          char *eol;
          
          p += ENTRY_LEN;
          p = skip_spaces (p, NULL);
          eol = find_eol (p);
          if (*p && eol)
            {
              *eol = '\0';
              *node = node_new (NODE_ENTRY,
                                p, depth);
            }
          else
            {
              g_printerr ("no name for ENTRY or no newline\n");
              exit (1);
            }

          p = eol + 1;
        }
      else
        {
          char *eol;

          eol = find_eol (p);
          if (eol)
            *eol = '\0';

          g_printerr ("Don't know what to do with line \"%s\"\n",
                      p);
          exit (1);
        }
    }

  if (*node && (*node)->filename)
    {
      char *f = g_build_filename (pwd, (*node)->filename, NULL);
      g_free ((*node)->filename);
      (*node)->filename = f;
    }
  
  return p;
}

static gboolean
parse_test (const char *test_file,
            const char *pwd,
            char      **menu_filename,
            GNode     **correct_results)
{
  /* The test results are of the form:
   * MENU foo.menu
   * 
   * DIRECTORY /foo/bar/baz.directory
   *   ENTRY /bar/foo/baz.desktop
   *   ENTRY /bar/foo/bar.desktop
   *   DIRECTORY /whatever/foo.directory
   *     ENTRY /whatever/bar.desktop
   *      
   */
  
  char *contents;
  gsize len;
  GError *err;
  char *p;
  char *eol;
  GSList *nodes;
  GSList *tmp;
  GSList *stack; /* stack of GNode, root node is last in list */
  
  err = NULL;
  if (!g_file_get_contents (test_file, &contents, &len, &err))
    {
      g_printerr ("failed to open test file: %s\n", err->message);
      exit (1);
    }
  
  p = find_word (contents, "MENU");
  p += MENU_LEN;
  eol = find_eol (p);

  p = skip_spaces (p, NULL);

  *eol = '\0';
  *menu_filename = g_build_filename (pwd, p, NULL);

  /* g_print ("Menu file: %s\n", *menu_filename); */
  
  p = eol + 1;

  nodes = NULL;
  while (p && *p)
    {
      Node *new_node = NULL;
      
      p = create_node (p, pwd, &new_node);
      
      if (new_node != NULL)
        {
          nodes = g_slist_prepend (nodes, new_node);
#if 0
          g_print ("found node depth %d filename \"%s\"\n",
                   new_node->depth, new_node->filename);
#endif
        }
    }

  g_free (contents);
  
  if (nodes == NULL)
    {
      *correct_results = NULL;
      return TRUE;
    }

  nodes = g_slist_reverse (nodes); /* put back in order */
  
  if (((Node*)nodes->data)->type != NODE_DIRECTORY)
    {
      g_printerr ("root has to be a DIRECTORY\n");
      exit (1);
    }
  
  if (((Node*)nodes->data)->depth != 0)
    {
      g_printerr ("root DIRECTORY has to be in first column\n");
      exit (1);
    }
  
  stack = g_slist_prepend (NULL, g_node_new (nodes->data));
  tmp = nodes->next;
  while (tmp != NULL)
    {
      Node *n = tmp->data;
      Node *top = ((GNode*)stack->data)->data;
      GNode *child;
      
      while (n->depth <= top->depth)
        {
          /* sibling of top or ancestor of top, need to pop top */
          stack = g_slist_remove (stack, stack->data);
          if (stack == NULL)
            {
              g_printerr ("multiple root nodes or other stack underflow\n");
              exit (1);
            }
          /* new top */
          top = ((GNode*)stack->data)->data;
        }

      /* Append the child */
      
      child = g_node_new (n);
      
      g_node_append (stack->data, child);

#if 0
      g_print ("adding %s as child of %s\n",
               n->filename, top->filename);
#endif
      
      /* push on stack if needed */
      if (n->type == NODE_DIRECTORY)
        stack = g_slist_prepend (stack, child);
      
      tmp = tmp->next;
    }

  g_slist_free (nodes);

  tmp = g_slist_last (stack);

  g_assert (tmp != NULL);
  *correct_results = tmp->data;

  g_slist_free (stack);

  return TRUE;
}


static GNode *
g_node_sort_merge (GNode           *l1, 
		   GNode           *l2,
                   GCompareDataFunc compare_func,
		   gpointer         user_data)
{
  GNode list, *l, *lprev;
  gint cmp;

  l = &list; 
  lprev = NULL;

  while (l1 && l2)
    {
      cmp = ((GCompareDataFunc) compare_func) (l1->data, l2->data, user_data);

      if (cmp <= 0)
        {
	  l->next = l1;
	  l = l->next;
	  l->prev = lprev; 
	  lprev = l;
	  l1 = l1->next;
        } 
      else 
	{
	  l->next = l2;
	  l = l->next;
	  l->prev = lprev; 
	  lprev = l;
	  l2 = l2->next;
        }
    }
  l->next = l1 ? l1 : l2;
  l->next->prev = l;

  return list.next;
}

static GNode* 
g_node_sort_children (GNode            *children,
                      GCompareDataFunc  compare_func,
                      gpointer          user_data)
{
  GNode *l1, *l2;
  
  if (!children) 
    return NULL;
  if (!children->next) 
    return children;
  
  l1 = children; 
  l2 = children->next;

  while ((l2 = l2->next) != NULL)
    {
      if ((l2 = l2->next) == NULL) 
	break;
      l1 = l1->next;
    }
  l2 = l1->next; 
  l1->next = NULL; 

  return g_node_sort_merge (g_node_sort_children (children, compare_func, user_data),
			    g_node_sort_children (l2, compare_func, user_data),
			    compare_func,
			    user_data);
}

static int
node_cmp (const void* a, const void* b, void* data)
{
  const Node *node_a = a;
  const Node *node_b = b;

  if (node_a->type != node_b->type)
    {
      if (node_a->type == NODE_DIRECTORY)
        return -1;
      else
        return 1;
    }
  else if (node_a->type == NODE_DIRECTORY)
    {
      return strcmp (node_a->name, node_b->name);
    }
  else if (node_a->type == NODE_ENTRY)
    {
      return strcmp (node_a->filename, node_b->filename);
    }  
  else
    {
      g_assert_not_reached ();
      return 0;
    }
}

static gboolean
traverse_and_sort_func (GNode    *node,
                        gpointer  data)
{
  node->children = g_node_sort_children (node->children,
                                         node_cmp,
                                         NULL);

  return FALSE;
}

static gboolean
traverse_and_print_func (GNode    *node,
                         gpointer  data)
{
  int depth;
  Node *n = node->data;
  
  depth = g_node_depth (node);
  depth -= 1;
  while (depth > 0)
    {
      g_print ("  ");
      --depth;
    }

  if (n->type == NODE_DIRECTORY)
    {
      g_print ("DIRECTORY %s %s\n",
               n->name,
               n->filename ? n->filename : "");
    }
  else
    {
      g_print ("ENTRY %s\n",
               n->filename);
    }
  
  return FALSE;
}

int
main (int argc, char **argv)
{
  GNode *correct_results;
  char *menu_filename;
  char *pwd;
  
  if (argc != 2)
    {
      g_printerr ("must provide test description file as only argument\n");
      exit (1);
    }

  pwd = g_get_current_dir ();
  
  if (!parse_test (argv[1], pwd, &menu_filename, &correct_results))
    {
      g_printerr ("failed to parse test description file\n");
      exit (1);
    }
  
  g_node_traverse (correct_results,
                   G_PRE_ORDER,
                   G_TRAVERSE_ALL,
                   -1,
                   traverse_and_sort_func,
                   NULL);

  g_print ("Expected results\n====\nMENU %s\n",
           menu_filename);
  
  g_node_traverse (correct_results,
                   G_PRE_ORDER,
                   G_TRAVERSE_ALL,
                   -1,
                   traverse_and_print_func,
                   NULL);

  return 0;
}
