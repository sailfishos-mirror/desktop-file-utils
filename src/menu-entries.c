/* Desktop and directory entries */

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

#include <config.h>

#include "menu-entries.h"
#include "menu-layout.h"
#include "canonicalize.h"
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x


typedef struct _CachedDir CachedDir;

struct _Entry
{
  char *relative_path;
  char *absolute_path;
  
  char **categories;
  
  guint type : 4;
  guint refcount : 24;
};

struct _EntryDirectory
{
  char *absolute_path;
  CachedDir *root;
  guint flags : 4;
  guint refcount : 24;
};

static char *only_show_in_name = NULL;

static CachedDir*  cached_dir_load        (const char  *canonical_path,
                                           GError     **err);
static void        cached_dir_mark_used   (CachedDir   *dir);
static void        cached_dir_mark_unused (CachedDir   *dir);
static GSList*     cached_dir_get_subdirs (CachedDir   *dir);
static GSList*     cached_dir_get_entries (CachedDir   *dir);
static const char* cached_dir_get_name    (CachedDir   *dir);
static void        cache_clear_unused     (void);
static Entry*      cached_dir_find_entry  (CachedDir   *dir,
                                           const char  *name);

void
entry_set_only_show_in_name (const char *name)
{
  g_free (only_show_in_name);
  only_show_in_name = g_strdup (name);
}

static Entry*
entry_new (EntryType   type,
           const char *relative_path,
           const char *absolute_path)
{
  Entry *e;

  e = g_new (Entry, 1);
  
  e->categories = NULL;
  e->type = type;
  e->relative_path = g_strdup (relative_path);
  e->absolute_path = g_strdup (absolute_path);
  e->refcount = 1;
  
  return e;
}

void
entry_ref (Entry *entry)
{
  entry->refcount += 1;
}

void
entry_unref (Entry *entry)
{
  g_return_if_fail (entry != NULL);
  g_return_if_fail (entry->refcount > 0);

  entry->refcount -= 1;
  if (entry->refcount == 0)
    {
      if (entry->categories)
        g_strfreev (entry->categories);

      g_free (entry->relative_path);
      g_free (entry->absolute_path);
      
      g_free (entry);
    }
}

const char*
entry_get_absolute_path (Entry *entry)
{
  return entry->absolute_path;
}

const char*
entry_get_relative_path (Entry *entry)
{
  return entry->relative_path;
}

gboolean
entry_has_category (Entry      *entry,
                    const char *category)
{
  int i;

  if (entry->categories == NULL)
    return FALSE;
  
  i = 0;
  while (entry->categories[i] != NULL)
    {
      if (strcmp (category, entry->categories[i]) == 0)
        return TRUE;
      
      ++i;
    }

  return FALSE;
}

Entry*
entry_get_by_absolute_path (const char *path)
{
  CachedDir* dir;
  char *dirname;
  char *basename;
  char *canonical;
  Entry *retval;

  retval = NULL;
  dirname = g_path_get_basename (path);

  canonical = g_canonicalize_file_name (dirname);
  if (canonical == NULL)
    {
      menu_verbose ("Error %d getting entry \"%s\": %s\n", errno, path,
                    g_strerror (errno));
      g_free (dirname);
      return NULL;
    }

  basename = g_path_get_dirname (path);
  
  dir = cached_dir_load (dirname, NULL);

  if (dir != NULL)
    retval = cached_dir_find_entry (dir, basename);

  g_free (dirname);
  g_free (basename);

  if (retval)
    entry_ref (retval);
  return retval;
}

EntryDirectory*
entry_directory_load  (const char     *path,
                       EntryLoadFlags  flags,
                       GError        **err)
{
  char *canonical;
  CachedDir *cd;
  EntryDirectory *ed;
  
  canonical = g_canonicalize_file_name (path);
  if (canonical == NULL)
    {
      g_set_error (err, ENTRY_ERROR,
                   ENTRY_ERROR_BAD_PATH,
                   _("Filename \"%s\" could not be canonicalized: %s\n"),
                   path, g_strerror (errno));
      menu_verbose ("Error %d loading cached dir \"%s\": %s\n", errno, path,
                    g_strerror (errno));
      return NULL;
    }

  cd = cached_dir_load (canonical, err);
  if (cd == NULL)
    {
      g_free (canonical);
      return NULL;
    }

  ed = g_new (EntryDirectory, 1);
  ed->absolute_path = canonical;
  ed->root = cd;
  ed->flags = flags;
  ed->refcount = 1;

  cached_dir_mark_used (ed->root);
  
  return ed;
}

void
entry_directory_ref (EntryDirectory *ed)
{
  ed->refcount += 1;
}

void
entry_directory_unref (EntryDirectory *ed)
{
  g_return_if_fail (ed != NULL);
  g_return_if_fail (ed->refcount > 0);

  ed->refcount -= 1;
  if (ed->refcount == 0)
    {
      cached_dir_mark_unused (ed->root);
      ed->root = NULL;
      g_free (ed->absolute_path);

      g_free (ed);
    }
}

static Entry*
entry_from_cached_entry (EntryDirectory *ed,
                         Entry          *src,
                         const char     *relative_path)
{
  Entry *e;
  int i;

  if (src->type != ENTRY_DESKTOP)
    return NULL;

  /* try to avoid a copy (no need to change path
   * or add "Legacy" keyword). This should
   * hold for the usual /usr/share/applications/foo.desktop
   * case, so is worthwhile.
   */
  if ((ed->flags & ENTRY_LOAD_LEGACY) == 0 &&
      strcmp (src->relative_path, relative_path) == 0)
    {
      entry_ref (src);
      return src;
    }
  
  e = entry_new (src->type, relative_path, src->absolute_path);
  
  i = 0;
  if (src->categories)
    {
      while (src->categories[i] != NULL)
        ++i;
    }
  
  if (ed->flags & ENTRY_LOAD_LEGACY)
    ++i;
  
  if (i > 0)
    {
      e->categories = g_new (char*, i + 1);
      
      i = 0;
      if (src->categories)
        {
          while (src->categories[i] != NULL)
            {
              e->categories[i] = g_strdup (src->categories[i]);
              ++i;
            }
        }

      if (ed->flags & ENTRY_LOAD_LEGACY)
        {
          e->categories[i] = g_strdup ("Legacy");
          ++i;
        }

      e->categories[i] = NULL;
    }

  return e;
}

Entry*
entry_directory_get_desktop (EntryDirectory *ed,
                             const char     *relative_path)
{
  Entry *src;
  
  if ((ed->flags & ENTRY_LOAD_DESKTOPS) == 0)
    return NULL;
  
  src = cached_dir_find_entry (ed->root, relative_path);
  if (src == NULL)
    return NULL;
  else
    return entry_from_cached_entry (ed, src, relative_path);
}

Entry*
entry_directory_get_directory (EntryDirectory *ed,
                               const char     *relative_path)
{
  Entry *src;
  Entry *e;
  
  if ((ed->flags & ENTRY_LOAD_DIRECTORIES) == 0)
    return NULL;
  
  src = cached_dir_find_entry (ed->root, relative_path);
  if (src == NULL)
    return NULL;

  if (src->type != ENTRY_DIRECTORY)
    return NULL;
  
  e = entry_new (src->type, relative_path, src->absolute_path);
  
  return e;
}

typedef gboolean (* EntryDirectoryForeachFunc) (EntryDirectory *ed,
                                                Entry          *src,
                                                const char     *relative_path,
                                                void           *data1,
                                                void           *data2);
static gboolean
entry_directory_foreach_recursive (EntryDirectory           *ed,
                                   CachedDir                *cd,
                                   char                     *parent_path,
                                   int                       parent_path_len,
                                   EntryDirectoryForeachFunc func,
                                   void                     *data1,
                                   void                     *data2)
{
  GSList *tmp;
  char *child_path_start;

  if (parent_path_len > 0)
    {
      parent_path[parent_path_len] = '/';
      child_path_start = parent_path + parent_path_len + 1;
    }
  else
    {
      child_path_start = parent_path + parent_path_len;
    }
  
  tmp = cached_dir_get_entries (cd);
  while (tmp != NULL)
    {
      Entry *src;

      src = tmp->data;

      if (src->type == ENTRY_DESKTOP &&
          (ed->flags & ENTRY_LOAD_DESKTOPS) == 0)
        goto next;

      if (src->type == ENTRY_DIRECTORY &&
          (ed->flags & ENTRY_LOAD_DIRECTORIES) == 0)
        goto next;
      
      strcpy (child_path_start,
              src->relative_path);

      if (!(* func) (ed, src, parent_path,
                     data1, data2))
        return FALSE;

    next:
      tmp = tmp->next;
    }

  tmp = cached_dir_get_subdirs (cd);
  while (tmp != NULL)
    {
      CachedDir *sub;
      int path_len;
      const char *name;

      sub = tmp->data;
      name = cached_dir_get_name (sub);

      strcpy (child_path_start, name);

      path_len = (child_path_start - parent_path) + strlen (name);   
      
      if (!entry_directory_foreach_recursive (ed, sub,
                                              parent_path,
                                              path_len,
                                              func, data1, data2))
        return FALSE;
      
      tmp = tmp->next;
    }
  
  return TRUE;
}

static void
entry_directory_foreach (EntryDirectory           *ed,
                         EntryDirectoryForeachFunc func,
                         void                     *data1,
                         void                     *data2)
{
  char *parent_path;

  parent_path = g_new (char, PATH_MAX + 2);
  *parent_path = '\0';

  entry_directory_foreach_recursive (ed, ed->root,
                                     parent_path,
                                     0,
                                     func, data1, data2);

  g_free (parent_path);
}

static gboolean
list_all_func (EntryDirectory *ed,
               Entry          *src,
               const char     *relative_path,
               void           *data1,
               void           *data2)
{
  GSList **listp = data1;

  *listp = g_slist_prepend (*listp,
                            entry_from_cached_entry (ed, src, relative_path));

  return TRUE;
}

GSList*
entry_directory_get_all_desktops (EntryDirectory *ed)
{
  GSList *list;

  list = NULL;
  entry_directory_foreach (ed, list_all_func, &list, NULL);

  return list;
}

static gboolean
list_in_category_func (EntryDirectory *ed,
                       Entry          *src,
                       const char     *relative_path,
                       void           *data1,
                       void           *data2)
{
  GSList **listp = data1;
  const char *category = data2;

  if (entry_has_category (src, category) ||
      ((ed->flags & ENTRY_LOAD_LEGACY) &&
       strcmp (category, "Legacy") == 0))
    *listp = g_slist_prepend (*listp,
                              entry_from_cached_entry (ed, src, relative_path));

  return TRUE;
}

GSList*
entry_directory_get_by_category (EntryDirectory *ed,
                                 const char     *category)
{
  GSList *list;

  list = NULL;
  entry_directory_foreach (ed, list_in_category_func, &list,
                           (char*) category);

  return list;
}

struct _EntryDirectoryList
{
  int refcount;
  GSList *dirs;
};

EntryDirectoryList*
entry_directory_list_new (void)
{
  EntryDirectoryList *list;

  list = g_new (EntryDirectoryList, 1);

  list->refcount = 1;
  list->dirs = NULL;

  return list;
}

void
entry_directory_list_ref (EntryDirectoryList *list)
{
  list->refcount += 1;
}

void
entry_directory_list_unref (EntryDirectoryList *list)
{
  g_return_if_fail (list != NULL);
  g_return_if_fail (list->refcount > 0);

  list->refcount -= 1;
  if (list->refcount == 0)
    {
      entry_directory_list_clear (list);
      g_free (list);
    }
}

void
entry_directory_list_clear (EntryDirectoryList *list)
{
  g_slist_foreach (list->dirs,
                   (GFunc) entry_directory_unref,
                   NULL);
  g_slist_free (list->dirs);
  list->dirs = NULL;
}

void
entry_directory_list_prepend (EntryDirectoryList *list,
                              EntryDirectory     *dir)
{
  entry_directory_ref (dir);
  list->dirs = g_slist_prepend (list->dirs, dir);
}

void
entry_directory_list_append  (EntryDirectoryList *list,
                              EntryDirectory     *dir)
{
  entry_directory_ref (dir);
  list->dirs = g_slist_append (list->dirs, dir);
}

Entry*
entry_directory_list_get_desktop (EntryDirectoryList *list,
                                  const char         *relative_path)
{
  GSList *tmp;

  tmp = list->dirs;
  while (tmp != NULL)
    {
      Entry *e;

      e = entry_directory_get_desktop (tmp->data, relative_path);
      if (e && e->type == ENTRY_DESKTOP)
        return e;
      else if (e)
        entry_unref (e);
      
      tmp = tmp->next;
    }

  return NULL;
}

Entry*
entry_directory_list_get_directory (EntryDirectoryList *list,
                                    const char         *relative_path)
{
  GSList *tmp;

  tmp = list->dirs;
  while (tmp != NULL)
    {
      Entry *e;

      e = entry_directory_get_directory (tmp->data, relative_path);
      if (e && e->type == ENTRY_DIRECTORY)
        return e;
      else if (e)
        entry_unref (e);
      
      tmp = tmp->next;
    }

  return NULL;
}

static void
entry_hash_listify_foreach (void *key, void *value, void *data)
{
  GSList **list = data;
  Entry *e = value;
  
  *list = g_slist_prepend (*list, e);
  entry_ref (e);
}

static GSList*
entry_directory_list_get (EntryDirectoryList       *list,
                          EntryDirectoryForeachFunc func,
                          void                     *data2)
{
  /* The only tricky thing here is that desktop files later
   * in the search list with the same relative path
   * are "hidden" by desktop files earlier in the path,
   * so we have to do the hash table thing.
   */
  GHashTable *hash;
  GSList *tmp;
  GSList *reversed;
  
  hash = g_hash_table_new_full (g_str_hash,
                                g_str_equal,
                                NULL,
                                (GDestroyNotify) entry_unref);

  /* We go from the end of the list so we can just
   * g_hash_table_replace and not have to do two
   * hash lookups (check for existing entry, then insert new
   * entry)
   */
  reversed = g_slist_copy (list->dirs);
  reversed = g_slist_reverse (list->dirs);

  tmp = reversed;
  while (tmp != NULL)
    {
      entry_directory_foreach (tmp->data, func, hash, data2);
      
      tmp = tmp->next;
    }

  g_slist_free (reversed);

  /* listify the hash */
  tmp = NULL;
  g_hash_table_foreach (hash, entry_hash_listify_foreach, &tmp);
  
  g_hash_table_destroy (hash);

  return tmp;
}

static gboolean
hash_all_func (EntryDirectory *ed,
               Entry          *src,
               const char     *relative_path,
               void           *data1,
               void           *data2)
{
  GHashTable *hash = data1;
  Entry *e;

  e = entry_from_cached_entry (ed, src, relative_path);

  /* pass ownership of reference to the hash table */
  g_hash_table_replace (hash, e->relative_path,
                        e);
}

GSList*
entry_directory_list_get_all_desktops (EntryDirectoryList *list)
{
  return entry_directory_list_get (list, hash_all_func,
                                   NULL);
}

static gboolean
hash_by_category_func (EntryDirectory *ed,
                       Entry          *src,
                       const char     *relative_path,
                       void           *data1,
                       void           *data2)
{
  GHashTable *hash = data1;
  const char *category = data2;
  Entry *e;
  
  if (entry_has_category (src, category) ||
      ((ed->flags & ENTRY_LOAD_LEGACY) &&
       strcmp (category, "Legacy") == 0))
    {
      e = entry_from_cached_entry (ed, src, relative_path);
      
      /* pass ownership of reference to the hash table */
      g_hash_table_replace (hash, e->relative_path,
                            e);
    }

  return TRUE;
}

GSList*
entry_directory_list_get_by_category (EntryDirectoryList *list,
                                      const char         *category)
{
  return entry_directory_list_get (list, hash_by_category_func,
                                   (char*) category);
}
  
/*
 * Big global cache of desktop entries
 */

struct _CachedDir
{
  CachedDir *parent;
  char *name;
  GSList *entries;
  GSList *subdirs;
  guint have_read_entries : 1;
  guint use_count : 27;
};

static CachedDir *root_dir = NULL;

static CachedDir* cached_dir_ensure         (const char  *canonical);
static void       cached_dir_scan_recursive (CachedDir   *dir,
                                             const char  *path);
static void       cached_dir_free           (CachedDir *dir);


static CachedDir*
cached_dir_new (const char *name)
{
  CachedDir *dir;

  dir = g_new0 (CachedDir, 1);

  dir->name = g_strdup (name);

  menu_verbose ("New cached dir \"%s\"\n", name);
  
  return dir;
}

static void
cached_dir_clear_all_children (CachedDir *dir)
{
  GSList *tmp;

  tmp = dir->entries;
  while (tmp != NULL)
    {
      entry_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (dir->entries);
  dir->entries = NULL;

  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      cached_dir_free (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (dir->subdirs);
  dir->subdirs = NULL;
}

static void
cached_dir_free (CachedDir *dir)
{
  cached_dir_clear_all_children (dir);

  if (dir->use_count > 0)
    {
      CachedDir *iter;
      iter = dir;
      while (iter != NULL)
        {
          iter->use_count -= dir->use_count;
          iter = iter->parent;
        }
    }

  g_assert (dir->use_count == 0);
  
  g_free (dir->name);
  g_free (dir);
}

static void
ensure_root_dir (void)
{
  if (root_dir == NULL)
    {
      root_dir = cached_dir_new ("/");
    }
}

static CachedDir*
find_subdir (CachedDir   *dir,
             const char  *subdir)
{
  GSList *tmp;

  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      CachedDir *sub = tmp->data;

      if (strcmp (sub->name, subdir) == 0)
        return sub;

      tmp = tmp->next;
    }

  return NULL;
}

static Entry*
find_entry (CachedDir   *dir,
            const char  *name)
{
  GSList *tmp;

  tmp = dir->entries;
  while (tmp != NULL)
    {
      Entry *e = tmp->data;

      if (strcmp (e->relative_path, name) == 0)
        return e;

      tmp = tmp->next;
    }

  return NULL;
}

static Entry*
cached_dir_find_entry (CachedDir   *dir,
                       const char  *name)
{
  char **split;
  int i;
  CachedDir *iter;
  
  split = g_strsplit (name, "/", -1);

  iter = dir;
  i = 0;
  while (iter != NULL && split[i] != NULL && *(split[i]) != '\0')
    {
      if (split[i+1] == NULL)
        {
          /* this is the last element, should be an entry */
          Entry *e;
          e = find_entry (iter, split[i]);
          g_strfreev (split);
          return e;
        }
      else
        {
          iter = find_subdir (dir, split[i]);
        }

      ++i;
    }

  g_strfreev (split);
  return NULL;
}

static CachedDir*
cached_dir_ensure (const char *canonical)
{
  char **split;
  int i;
  CachedDir *dir;

  menu_verbose ("Ensuring cached dir \"%s\"\n", canonical);
  
  /* canonicalize_file_name doesn't allow empty strings */
  g_assert (*canonical != '\0');  
  
  split = g_strsplit (canonical + 1, "/", -1);

  ensure_root_dir ();
  
  dir = root_dir;

  /* empty strings probably mean a trailing '/' */
  i = 0;
  while (split[i] != NULL && *(split[i]) != '\0')
    {
      CachedDir *cd;

      cd = find_subdir (dir, split[i]);

      if (cd == NULL)
        {
          cd = cached_dir_new (split[i]);
          dir->subdirs = g_slist_prepend (dir->subdirs, cd);
          cd->parent = dir;
        }

      dir = cd;
      
      ++i;
    }
  
  g_strfreev (split);

  return dir;
}

static CachedDir*
cached_dir_load (const char *canonical_path,
                 GError    **err)
{
  CachedDir *retval;

  menu_verbose ("Loading cached dir \"%s\"\n", canonical_path);  

  retval = NULL;

  retval = cached_dir_ensure (canonical_path);
  g_assert (retval != NULL);

  cached_dir_scan_recursive (retval, canonical_path);
  
  return retval;
}

static char*
cached_dir_get_full_path (CachedDir *dir)
{
  GString *str;
  GSList *parents;
  GSList *tmp;
  CachedDir *iter;
  
  str = g_string_new (NULL);

  parents = NULL;
  iter = dir;
  while (iter != NULL)
    {
      parents = g_slist_prepend (parents, iter->name);
      iter = iter->parent;
    }
  
  tmp = parents;
  while (tmp != NULL)
    {
      g_string_append (str, tmp->data);
      
      tmp = tmp->next;
    }

  g_slist_free (parents);

  return g_string_free (str, FALSE);
}

static char *
unescape_string (const char *str, int len)
{
  char *res;
  const char *p;
  char *q;
  const char *end;

  /* len + 1 is enough, because unescaping never makes the
   * string longer */
  res = g_new (gchar, len + 1);
  p = str;
  q = res;
  end = str + len;

  while (p < end)
    {
      if (*p == 0)
	{
	  /* Found an embedded null */
	  g_free (res);
	  return NULL;
	}
      if (*p == '\\')
	{
	  p++;
	  if (p >= end)
	    {
	      /* Escape at end of string */
	      g_free (res);
	      return NULL;
	    }
	  
	  switch (*p)
	    {
	    case 's':
              *q++ = ' ';
              break;
           case 't':
              *q++ = '\t';
              break;
           case 'n':
              *q++ = '\n';
              break;
           case 'r':
              *q++ = '\r';
              break;
           case '\\':
              *q++ = '\\';
              break;
           default:
	     /* Invalid escape code */
	     g_free (res);
	     return NULL;
	    }
	  p++;
	}
      else
	*q++ = *p++;
    }
  *q = 0;

  return res;
}

static char*
find_value (const char *str,
            const char *key)
{
  const char *p;
  const char *q;
  int key_len;

  key_len = -1;
  
  /* As a heuristic, scan quickly for the key name */
  p = str;

 scan_again:
  while (*p && *p != *key) /* find first byte */
    ++p;

  if (*p == '\0')
    return NULL;

  key_len = strlen (key);
  if (strncmp (p, key, key_len) != 0)
    {
      ++p;
      goto scan_again;
    }

  /* If we get here, then the strncmp passed.
   * verify that we have '^ *key *='
   */
  q = p;
  while (q != str)
    {      
      if (!(*q == ' ' || *q == '\t'))
        break;

      --q;
    }
  
  if (!(q == str || *q == '\n' || *q == '\r'))
    return NULL;

  q = p + key_len;
  while (*q)
    {
      if (!(*q == ' ' || *q == '\t'))
        break;
      ++q;
    }

  if (!(*q == '='))
    return NULL;

  /* skip '=' */
  ++q;  
  
  while (*q && (*q == ' ' || *q == '\t'))
    ++q;

  /* point p at start of value, then run q forward to the end of it */
  p = q;
  while (*q && (*q != '\n' && *q != '\r'))
    ++q;
  
  return unescape_string (p, q - p);
}

static char**
string_list_from_desktop_value (const char *raw)
{
  char **retval;
  int i;
  
  retval = g_strsplit (raw, ";", G_MAXINT);

  i = 0;
  while (retval[i])
    ++i;

  /* Drop the empty string g_strsplit leaves in the vector since
   * our list of strings ends in ";"
   */
  --i;
  g_free (retval[i]);
  retval[i] = NULL;

  return retval;
}

static Entry*
entry_new_desktop_from_file (const char *filename,
                             const char *basename)
{
  char *str;
  int len;
  GError *err;
  char *categories;
  Entry *e;
  
  str = NULL;
  len = 0;
  err = NULL;
  if (!g_file_get_contents (filename, &str, &len, NULL))
    {
      menu_verbose ("Could not get contents of \"%s\": %s\n",
                    filename, err->message);
      g_error_free (err);
      return NULL;
    }

  if (only_show_in_name)
    {
      char *onlyshowin;
      gboolean show;

      show = TRUE;
      
      onlyshowin = find_value (str, "OnlyShowIn");
  
      if (onlyshowin != NULL)
        {
          char **split;
          int i;

          show = FALSE;
          
          split = string_list_from_desktop_value (onlyshowin);
          i = 0;
          while (split[i] != NULL)
            {
              if (strcmp (split[i], only_show_in_name) == 0)
                {
                  show = TRUE;
                  break;
                }
                  
              ++i;
            }

          if (!show)
            menu_verbose ("Not showing \"%s\" due to OnlyShowIn=%s\n",
                          filename, onlyshowin);
          
          g_strfreev (split);
          g_free (onlyshowin);          
        }

      if (!show)
        {
          g_free (str);
          return NULL;
        }
    }

  e = entry_new (ENTRY_DESKTOP, basename, filename);
  
  categories = find_value (str, "Categories");
  if (categories != NULL)
    {
      e->categories = string_list_from_desktop_value (categories);
      g_free (categories);
    }

  g_free (str);
  
  return e;
}

static Entry*
entry_new_directory_from_file (const char *filename,
                               const char *basename)
{
  return entry_new (ENTRY_DIRECTORY, basename, filename);
}

static void
load_entries_recursive (CachedDir  *dir,
                        CachedDir  *parent,
                        const char *dirname,
                        const char *basename)
{
  DIR* dp;
  struct dirent* dent;
  char* fullpath;
  char* fullpath_end;
  guint len;
  guint subdir_len;

  if (dir && dir->have_read_entries)
    return;

  menu_verbose ("Reading entries for %s (full path %s)\n",
                dir ? dir->name : "(not created yet)", dirname);
  
  dp = opendir (dirname);
  if (dp == NULL)
    {
      menu_verbose (_("Ignoring file \"%s\"\n"),
                    dirname);
      return;
    }

  if (dir == NULL)
    {
      dir = cached_dir_new (basename);
      dir->parent = parent;
      parent->subdirs = g_slist_prepend (parent->subdirs,
                                         dir);
    }
  
  /* Blow away all current entries */  
  cached_dir_clear_all_children (dir);
  
  len = strlen (dirname);
  subdir_len = PATH_MAX - len;
  
  fullpath = g_new0 (char, subdir_len + len + 2); /* ensure null termination */
  strcpy (fullpath, dirname);
  
  fullpath_end = fullpath + len;
  if (*(fullpath_end - 1) != '/')
    {
      *fullpath_end = '/';
      ++fullpath_end;
    }
  
  while ((dent = readdir (dp)) != NULL)
    {      
      /* ignore . and .. */
      if (dent->d_name[0] == '.' &&
          (dent->d_name[1] == '\0' ||
           (dent->d_name[1] == '.' &&
            dent->d_name[2] == '\0')))
        continue;
      
      len = strlen (dent->d_name);

      if (len < subdir_len)
        {
          strcpy (fullpath_end, dent->d_name);
        }
      else
        continue; /* Shouldn't ever happen since PATH_MAX is available */

      if (g_str_has_suffix (dent->d_name, ".desktop"))
        {
          Entry *e;

          e = entry_new_desktop_from_file (fullpath, dent->d_name);

          if (e != NULL)
            dir->entries = g_slist_prepend (dir->entries, e);
        }
      else if (g_str_has_suffix (dent->d_name, ".directory"))
        {
          Entry *e;

          e = entry_new_directory_from_file (fullpath, dent->d_name);

          if (e != NULL)
            dir->entries = g_slist_prepend (dir->entries, e);
        }
      else
        {
          /* Try recursing */
          load_entries_recursive (NULL,
                                  dir,
                                  fullpath,
                                  dent->d_name);
        }
    }

  /* if this fails, we really can't do a thing about it
   * and it's not a meaningful error
   */
  closedir (dp);

  g_free (fullpath);

  dir->have_read_entries = TRUE;
}

static void
cached_dir_scan_recursive (CachedDir   *dir,
                           const char  *path)
{
  char *canonical;
  
  /* "path" corresponds to the canonical path to dir,
   * if path is non-NULL
   */
  canonical = NULL;
  if (path == NULL)
    {
      canonical = cached_dir_get_full_path (dir);
      path = canonical;
    }

  load_entries_recursive (dir, NULL, path, dir->name);

  g_free (canonical);
}

static int
mark_used_recursive (CachedDir *dir)
{
  int n_uses_added;
  GSList *tmp;

  /* Mark all children used, adding
   * the number of child uses to our own
   * use count
   */
  
  n_uses_added = 0;
  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      n_uses_added += mark_used_recursive (tmp->data);
      
      tmp = tmp->next;
    }

  dir->use_count += n_uses_added + 1;

  /* return number of uses we added */
  return n_uses_added + 1;
}

static void
cached_dir_mark_used (CachedDir *dir)
{
  CachedDir *iter;
  int n_uses_added;

  n_uses_added = mark_used_recursive (dir);
  
  iter = dir->parent;
  while (iter != NULL)
    {
      iter->use_count += n_uses_added;
      iter = iter->parent;
    }
}

static int
mark_unused_recursive (CachedDir *dir)
{
  int n_uses_removed;
  GSList *tmp;

  /* Mark all children used, adding
   * the number of child uses to our own
   * use count
   */
  
  n_uses_removed = 0;
  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      n_uses_removed += mark_unused_recursive (tmp->data);
      
      tmp = tmp->next;
    }

  dir->use_count -= n_uses_removed + 1;

  /* return number of uses we added */
  return n_uses_removed + 1;
}

static void
cached_dir_mark_unused (CachedDir *dir)
{
  CachedDir *iter;
  int n_uses_removed;

  g_return_if_fail (dir->use_count > 0);
  
  n_uses_removed = mark_unused_recursive (dir);
  
  iter = dir->parent;
  while (iter != NULL)
    {
      iter->use_count -= n_uses_removed;
      iter = iter->parent;
    }
}

static void
recursive_free_unused (CachedDir *dir)
{
  GSList *tmp;
  GSList *prev;

  prev = NULL;
  tmp = dir->subdirs;
  while (tmp != NULL)
    {
      CachedDir *child = tmp->data;
      GSList *next = tmp->next;
      
      if (child->use_count == 0)
        {
          cached_dir_free (child);

          if (prev == NULL)
            {
              g_slist_free (dir->subdirs);
              dir->subdirs = NULL;
            }
          else
            {
              g_assert (prev->next->data == tmp->data);
              g_assert (tmp->data == child);
              prev->next = g_slist_remove (prev->next, prev->next->data);
            }
        }
      else
        {
          prev = tmp;
          
          recursive_free_unused (child);
        }
      
      tmp = next;
    }
}

static void
cache_clear_unused (void)
{
  if (root_dir != NULL)
    {
      recursive_free_unused (root_dir);
      if (root_dir->use_count == 0)
        {
          cached_dir_free (root_dir);
          root_dir = NULL;
        }
    }
}


static const char*
cached_dir_get_name (CachedDir *dir)
{
  return dir->name;
}

static GSList*
cached_dir_get_subdirs (CachedDir *dir)
{
  if (!dir->have_read_entries)
    {
      cached_dir_scan_recursive (dir, NULL);
    }

  return dir->subdirs;
}

static GSList*
cached_dir_get_entries (CachedDir   *dir)
{
  if (!dir->have_read_entries)
    {
      cached_dir_scan_recursive (dir, NULL);
    }

  return dir->entries;
}

GQuark
entry_error_quark (void)
{
  static GQuark q = 0;
  if (q == 0)
    q = g_quark_from_static_string ("entry-error-quark");

  return q;
}
