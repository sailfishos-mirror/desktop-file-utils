/* Menu file parsing */

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
#include "menu-parser.h"
#include <string.h>
#include <stdlib.h>

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x

typedef struct MenuParser MenuParser;

struct MenuParser
{
  MenuNode *root;
  MenuNode *stack_top;
};

static void set_error (GError             **err,
                       GMarkupParseContext *context,
                       int                  error_domain,
                       int                  error_code,
                       const char          *format,
                       ...) G_GNUC_PRINTF (5, 6);

static void add_context_to_error (GError             **err,
                                  GMarkupParseContext *context);

static void start_element_handler (GMarkupParseContext  *context,
                                   const gchar          *element_name,
                                   const gchar         **attribute_names,
                                   const gchar         **attribute_values,
                                   gpointer              user_data,
                                   GError              **error);
static void end_element_handler   (GMarkupParseContext  *context,
                                   const gchar          *element_name,
                                   gpointer              user_data,
                                   GError              **error);
static void text_handler          (GMarkupParseContext  *context,
                                   const gchar          *text,
                                   gsize                 text_len,
                                   gpointer              user_data,
                                   GError              **error);

static GMarkupParser menu_funcs = {
  start_element_handler,
  end_element_handler,
  text_handler,
  NULL,
  NULL
};

static void
set_error (GError             **err,
           GMarkupParseContext *context,
           int                  error_domain,
           int                  error_code,
           const char          *format,
           ...)
{
  int line, ch;
  va_list args;
  char *str;
  
  g_markup_parse_context_get_position (context, &line, &ch);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  g_set_error (err, error_domain, error_code,
               _("Line %d character %d: %s"),
               line, ch, str);

  g_free (str);
}

static void
add_context_to_error (GError             **err,
                      GMarkupParseContext *context)
{
  int line, ch;
  char *str;

  if (err == NULL || *err == NULL)
    return;

  g_markup_parse_context_get_position (context, &line, &ch);

  str = g_strdup_printf (_("Line %d character %d: %s"),
                         line, ch, (*err)->message);
  g_free ((*err)->message);
  (*err)->message = str;
}

#define ELEMENT_IS(name) (strcmp (element_name, (name)) == 0)

typedef struct
{
  const char  *name;
  const char **retloc;
} LocateAttr;

static gboolean
locate_attributes (GMarkupParseContext *context,
                   const char  *element_name,
                   const char **attribute_names,
                   const char **attribute_values,
                   GError     **error,
                   const char  *first_attribute_name,
                   const char **first_attribute_retloc,
                   ...)
{
  va_list args;
  const char *name;
  const char **retloc;
  int n_attrs;
#define MAX_ATTRS 24
  LocateAttr attrs[MAX_ATTRS];
  gboolean retval;
  int i;

  g_return_val_if_fail (first_attribute_name != NULL, FALSE);
  g_return_val_if_fail (first_attribute_retloc != NULL, FALSE);

  retval = TRUE;

  n_attrs = 1;
  attrs[0].name = first_attribute_name;
  attrs[0].retloc = first_attribute_retloc;
  *first_attribute_retloc = NULL;
  
  va_start (args, first_attribute_retloc);

  name = va_arg (args, const char*);
  retloc = va_arg (args, const char**);

  while (name != NULL)
    {
      g_return_val_if_fail (retloc != NULL, FALSE);

      g_assert (n_attrs < MAX_ATTRS);
      
      attrs[n_attrs].name = name;
      attrs[n_attrs].retloc = retloc;
      n_attrs += 1;
      *retloc = NULL;      

      name = va_arg (args, const char*);
      retloc = va_arg (args, const char**);
    }

  va_end (args);

  if (!retval)
    return retval;

  i = 0;
  while (attribute_names[i])
    {
      int j;
      gboolean found;

      found = FALSE;
      j = 0;
      while (j < n_attrs)
        {
          if (strcmp (attrs[j].name, attribute_names[i]) == 0)
            {
              retloc = attrs[j].retloc;

              if (*retloc != NULL)
                {
                  set_error (error, context,
                             G_MARKUP_ERROR,
                             G_MARKUP_ERROR_PARSE,
                             _("Attribute \"%s\" repeated twice on the same <%s> element"),
                             attrs[j].name, element_name);
                  retval = FALSE;
                  goto out;
                }

              *retloc = attribute_values[i];
              found = TRUE;
            }

          ++j;
        }

      if (!found)
        {
          set_error (error, context,
                     G_MARKUP_ERROR,
                     G_MARKUP_ERROR_PARSE,
                     _("Attribute \"%s\" is invalid on <%s> element in this context"),
                     attribute_names[i], element_name);
          retval = FALSE;
          goto out;
        }

      ++i;
    }

 out:
  return retval;
}

static gboolean
check_no_attributes (GMarkupParseContext *context,
                     const char  *element_name,
                     const char **attribute_names,
                     const char **attribute_values,
                     GError     **error)
{
  if (attribute_names[0] != NULL)
    {
      set_error (error, context,
                 G_MARKUP_ERROR,
                 G_MARKUP_ERROR_PARSE,
                 _("Attribute \"%s\" is invalid on <%s> element in this context"),
                 attribute_names[0], element_name);
      return FALSE;
    }

  return TRUE;
}

static void
start_menu_element (MenuParser          *parser,
                    GMarkupParseContext *context,
                    const gchar         *element_name,
                    const gchar        **attribute_names,
                    const gchar        **attribute_values,
                    GError             **error)
{
  MenuNode *node;

  if (!check_no_attributes (context, element_name,
                            attribute_names, attribute_values,
                            error))
    return;
  
  node = menu_node_new (MENU_NODE_MENU);

  if (parser->stack_top == NULL)
    {
      g_assert (parser->root == NULL);
      
      parser->root = node;
      parser->stack_top = node;
    }
  else
    {
      if (menu_node_get_type (parser->stack_top != MENU_NODE_MENU))
        {
          set_error (error, context,
                     G_MARKUP_ERROR,
                     G_MARKUP_ERROR_PARSE,
                     _("<Menu> element can only appear below other <Menu> elements\n"));
          menu_node_unref (node);
          return;
        }

      menu_node_append_child (parser->stack_top, node);
      menu_node_unref (node);
    }
}

static void
start_element_handler (GMarkupParseContext *context,
                       const gchar         *element_name,
                       const gchar        **attribute_names,
                       const gchar        **attribute_values,
                       gpointer             user_data,
                       GError             **error)
{
  MenuParser *parser = user_data;

  if (ELEMENT_IS ("menu"))
    {
      if (parser->root != NULL &&
          parser->stack_top == NULL)
        {
          set_error (error, context, G_MARKUP_ERROR,
                     G_MARKUP_ERROR_PARSE,
                     _("Multiple root elements in menu file, only one toplevel <Menu> is allowed\n"));
          return;
        }
      
      start_menu_element (parser, element_name,
                          attribute_names, attribute_values,
                          error);
    }
  else if (parser->stack_top == NULL)
    {
      set_error (error, context, G_MARKUP_ERROR,
                 G_MARKUP_ERROR_PARSE,
                 _("Root element in a menu file must be <Menu>, not <%s>\n"),
                 element_name);
    }
  
}

static void
end_element_handler (GMarkupParseContext *context,
                     const gchar         *element_name,
                     gpointer             user_data,
                     GError             **error)
{
  MenuParser *parser = user_data;

  g_assert (parser->stack_top != NULL);

  parser->stack_top = menu_node_get_parent (parser->stack_top);
}

#define NO_TEXT(element_name) set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE, _("No text is allowed inside element <%s>"), element_name)

static gboolean
all_whitespace (const char *text,
                int         text_len)
{
  const char *p;
  const char *end;
  
  p = text;
  end = text + text_len;
  
  while (p != end)
    {
      if (!g_ascii_isspace (*p))
        return FALSE;

      p = g_utf8_next_char (p);
    }

  return TRUE;
}

static void
text_handler (GMarkupParseContext *context,
              const gchar         *text,
              gsize                text_len,
              gpointer             user_data,
              GError             **error)
{
  MenuParser *parser = user_data;
  
  if (all_whitespace (text, text_len))
    return;

  g_assert (parser->stack_top != NULL);  
}

static void
menu_parser_init (MenuParser *parser)
{
  parser->root = NULL;
  parser->stack_top = NULL;
}

static void
menu_parser_free (MenuParser *parser)
{
  if (parser->root)
    menu_node_unref (parser->root);
}

MenuNode*
menu_load (const char *filename,
           GError    **err)
{
  GMarkupParseContext *context;
  GError *error;
  MenuParser parser;
  char *text;
  int length;
  MenuNode *retval;

  text = NULL;
  length = 0;
  retval = NULL;
  
  if (!g_file_get_contents (filename,
                            &text,
                            &length,
                            err))
    return NULL;
  
  g_assert (text);

  menu_parser_init (&parser);
  
  context = g_markup_parse_context_new (&menu_funcs,
                                        0, &parser, NULL);

  error = NULL;
  if (!g_markup_parse_context_parse (context,
                                     text,
                                     length,
                                     &error))
    goto out;

  error = NULL;
  if (!g_markup_parse_context_end_parse (context, &error))
    goto out;

  g_markup_parse_context_free (context);

  goto out;

 out:

  g_free (text);
  
  if (error)
    {
      g_propagate_error (err, error);
    }
  else if (parser.root)
    {
      retval = parser.root;
      parser.root = NULL;
    }
  else
    {
      g_set_error (err, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   _("Menu file %s did not contain a root <VFolderInfo> element"),
                   filename);
    }

  menu_parser_free (&parser);
  
  return retval;
}
