/* Vfolder parsing */

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

#include <config.h>
#include "vfolder-parser.h"
#include <string.h>
#include <stdlib.h>

struct _Vfolder
{
  char *name;
  char *desktop_file;
  
  GSList *subfolders;

  VfolderQuery *query;

  guint only_unallocated : 1;
  guint show_if_empty : 1;
};

struct _VfolderQuery
{
  VfolderQueryType type;
  gboolean negated;
};

typedef struct
{
  VfolderQuery query;
  GSList *sub_queries;
} VfolderLogicalQuery;

typedef struct
{
  VfolderQuery query;
  char *category;
} VfolderCategoryQuery;

typedef struct
{
  VfolderQuery query;
  char *filename;
} VfolderFilenameQuery;

#define VFOLDER_LOGICAL_QUERY(q)  ((VfolderLogicalQuery*)q)
#define VFOLDER_CATEGORY_QUERY(q) ((VfolderCategoryQuery*)q)
#define VFOLDER_FILENAME_QUERY(q) ((VfolderFilenameQuery*)q)

typedef enum
{
  STATE_START,
  STATE_VFOLDER,
  STATE_MERGE_DIR
} ParseState;

typedef struct
{
  GSList *states;

  Vfolder *vfolder;
} ParseInfo;

static void set_error (GError             **err,
                       GMarkupParseContext *context,
                       int                  error_domain,
                       int                  error_code,
                       const char          *format,
                       ...) G_GNUC_PRINTF (5, 6);

static void add_context_to_error (GError             **err,
                                  GMarkupParseContext *context);

static void       parse_info_init (ParseInfo *info);
static void       parse_info_free (ParseInfo *info);

static void       push_state (ParseInfo  *info,
                              ParseState  state);
static void       pop_state  (ParseInfo  *info);
static ParseState peek_state (ParseInfo  *info);


static void parse_toplevel_element  (GMarkupParseContext  *context,
                                     const gchar          *element_name,
                                     const gchar         **attribute_names,
                                     const gchar         **attribute_values,
                                     ParseInfo            *info,
                                     GError              **error);

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

static void
parse_info_init (ParseInfo *info)
{
  info->states = g_slist_prepend (NULL, GINT_TO_POINTER (STATE_START));
}

static void
parse_info_free (ParseInfo *info)
{
  g_slist_free (info->states);
}

static void
push_state (ParseInfo  *info,
            ParseState  state)
{
  info->states = g_slist_prepend (info->states, GINT_TO_POINTER (state));
}

static void
pop_state (ParseInfo *info)
{
  g_return_if_fail (info->states != NULL);
  
  info->states = g_slist_remove (info->states, info->states->data);
}

static ParseState
peek_state (ParseInfo *info)
{
  g_return_val_if_fail (info->states != NULL, STATE_START);

  return GPOINTER_TO_INT (info->states->data);
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
parse_toplevel_element (GMarkupParseContext  *context,
                        const gchar          *element_name,
                        const gchar         **attribute_names,
                        const gchar         **attribute_values,
                        ParseInfo            *info,
                        GError              **error)
{
  g_return_if_fail (peek_state (info) == STATE_VFOLDER);

  if (ELEMENT_IS ("MergeDir"))
    {
      if (!check_no_attributes (context, element_name,
                                attribute_names, attribute_values,
                                error))
        return;

      push_state (info, STATE_MERGE_DIR);
    }
  else
    {
      set_error (error, context,
                 G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _("Element <%s> is not allowed below <%s>"),
                 element_name, "VFolderInfo");
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
  ParseInfo *info = user_data;

  switch (peek_state (info))
    {
    case STATE_START:
      if (strcmp (element_name, "VFolderInfo") == 0)
        {
          
          push_state (info, STATE_VFOLDER);
        }
      else
        set_error (error, context, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   _("Outermost element in theme must be <VFolderInfo> not <%s>"),
                   element_name);
      break;

    case STATE_MERGE_DIR:
#if 0
      parse_toplevel_element (context, element_name,
                              attribute_names, attribute_values,
                              info, error);
#endif
      break;

    }
}

static void
end_element_handler (GMarkupParseContext *context,
                     const gchar         *element_name,
                     gpointer             user_data,
                     GError             **error)
{
  ParseInfo *info = user_data;

  switch (peek_state (info))
    {
    case STATE_START:
      break;
    case STATE_VFOLDER:
      
      pop_state (info);
      g_assert (peek_state (info) == STATE_START);
      break;
    }
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
  ParseInfo *info = user_data;

  if (all_whitespace (text, text_len))
    return;
  
  /* FIXME http://bugzilla.gnome.org/show_bug.cgi?id=70448 would
   * allow a nice cleanup here.
   */

  switch (peek_state (info))
    {
    case STATE_START:
      g_assert_not_reached (); /* gmarkup shouldn't do this */
      break;
    case STATE_VFOLDER:
      NO_TEXT ("VFolderInfo");
      break;
    }
}

Vfolder*
vfolder_load (const char *filename,
              GError    **err)
{
  GMarkupParseContext *context;
  GError *error;
  ParseInfo info;
  char *text;
  int length;
  Vfolder *retval;

  text = NULL;
  length = 0;
  retval = NULL;
  
  if (!g_file_get_contents (filename,
                            &text,
                            &length,
                            err))
    return NULL;

  g_assert (text);

  parse_info_init (&info);
  
  context = g_markup_parse_context_new (&vfolder_parser,
                                        0, &info, NULL);

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
  else if (info.vfolder)
    {
      retval = info.vfolder;
      info.vfolder = NULL;
    }
  else
    {
      g_set_error (err, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                   _("Menu file %s did not contain a root <VFolderInfo> element"),
                   filename);
    }

  parse_info_free (&info);
  
  return retval;
}

GSList*
vfolder_get_subfolders (Vfolder *folder)
{
  return folder->subfolders;
}

const char*
vfolder_get_name (Vfolder *folder)
{
  return folder->name;
}

const char*
vfolder_get_desktop_file (Vfolder *folder)
{
  return folder->desktop_file;
}

gboolean
vfolder_get_show_if_empty (Vfolder *folder)
{
  return folder->show_if_empty;
}

gboolean
vfolder_get_only_unallocated (Vfolder *folder)
{
  return folder->only_unallocated;
}

VfolderQuery*
vfolder_get_query (Vfolder *folder)
{
  return folder->query;
}

VfolderQueryType
vfolder_query_get_type (VfolderQuery *query)
{
  return query->type;
}

GSList*
vfolder_query_get_subqueries (VfolderQuery *query)
{
  g_return_val_if_fail (query->type == VFOLDER_QUERY_OR ||
                        query->type == VFOLDER_QUERY_AND,
                        NULL);

  return VFOLDER_LOGICAL_QUERY (query)->subqueries;
}

const char*
vfolder_query_get_category (VfolderQuery *query)
{
  g_return_val_if_fail (query->type == VFOLDER_QUERY_CATEGORY, NULL);
  
  return VFOLDER_CATEGORY_QUERY (query)->category;
}

const char*
vfolder_query_get_filename (VfolderQuery *query)
{
  g_return_val_if_fail (query->type == VFOLDER_QUERY_FILENAME, NULL);
  
  return VFOLDER_FILENAME_QUERY (query)->filename;
}

