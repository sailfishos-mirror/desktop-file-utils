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

typedef enum
{
  ELEMENT_NONE,
  ELEMENT_BUSCONFIG,
  ELEMENT_INCLUDE,
  ELEMENT_USER,
  ELEMENT_LISTEN,
  ELEMENT_AUTH,
  ELEMENT_POLICY,
  ELEMENT_LIMIT,
  ELEMENT_ALLOW,
  ELEMENT_DENY,
  ELEMENT_FORK,
  ELEMENT_PIDFILE,
  ELEMENT_SERVICEDIR,
  ELEMENT_INCLUDEDIR,
  ELEMENT_TYPE
} ElementType;

typedef struct
{
  ElementType type;

  unsigned int had_content : 1;

  union
  {
    struct
    {
      unsigned int ignore_missing : 1;
    } include;
    
  } d;

} Element;


struct MenuParser
{
  GSList *states;  
};


static const char*
element_type_to_name (ElementType type)
{
  switch (type)
    {
    case ELEMENT_NONE:
      return NULL;
    case ELEMENT_BUSCONFIG:
      return "busconfig";
    case ELEMENT_INCLUDE:
      return "include";
    case ELEMENT_USER:
      return "user";
    case ELEMENT_LISTEN:
      return "listen";
    case ELEMENT_AUTH:
      return "auth";
    case ELEMENT_POLICY:
      return "policy";
    case ELEMENT_LIMIT:
      return "limit";
    case ELEMENT_ALLOW:
      return "allow";
    case ELEMENT_DENY:
      return "deny";
    case ELEMENT_FORK:
      return "fork";
    case ELEMENT_PIDFILE:
      return "pidfile";
    case ELEMENT_SERVICEDIR:
      return "servicedir";
    case ELEMENT_INCLUDEDIR:
      return "includedir";
    case ELEMENT_TYPE:
      return "type";
    }

  _dbus_assert_not_reached ("bad element type");

  return NULL;
}

static Element*
push_element (BusConfigParser *parser,
              ElementType      type)
{
  Element *e;

  _dbus_assert (type != ELEMENT_NONE);
  
  e = dbus_new0 (Element, 1);
  if (e == NULL)
    return NULL;

  if (!_dbus_list_append (&parser->stack, e))
    {
      dbus_free (e);
      return NULL;
    }
  
  e->type = type;

  return e;
}

static void
element_free (Element *e)
{
  if (e->type == ELEMENT_LIMIT)
    dbus_free (e->d.limit.name);
  
  dbus_free (e);
}

static void
pop_element (BusConfigParser *parser)
{
  Element *e;

  e = _dbus_list_pop_last (&parser->stack);
  
  element_free (e);
}

static Element*
peek_element (BusConfigParser *parser)
{
  Element *e;

  e = _dbus_list_get_last (&parser->stack);

  return e;
}

static ElementType
top_element_type (BusConfigParser *parser)
{
  Element *e;

  e = _dbus_list_get_last (&parser->stack);

  if (e)
    return e->type;
  else
    return ELEMENT_NONE;
}

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

static GMarkupParser vfolder_parser = {
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
start_element_handler (GMarkupParseContext *context,
                       const gchar         *element_name,
                       const gchar        **attribute_names,
                       const gchar        **attribute_values,
                       gpointer             user_data,
                       GError             **error)
{

}

static void
end_element_handler (GMarkupParseContext *context,
                     const gchar         *element_name,
                     gpointer             user_data,
                     GError             **error)
{

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
  if (all_whitespace (text, text_len))
    return;
  
  /* FIXME http://bugzilla.gnome.org/show_bug.cgi?id=70448 would
   * allow a nice cleanup here.
   */

}
