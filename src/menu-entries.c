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

#include "menu-entries.h"
#include "canonicalize.h"

struct _Entry
{
  char *relative_path;

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

static CachedDir* cached_dir_load        (const char  *canonical_path,
                                          GError     **err);
static void       cached_dir_mark_used   (CachedDir   *dir);
static void       cached_dir_mark_unused (CachedDir   *dir);
static GSList*    cached_dir_get_subdirs (CachedDir   *dir);
static GSList*    cached_dir_get_entries (CachedDir   *dir);
static void       cache_clear_unused     (void);
static Entry*     cached_dir_find_entry  (CachedDir   *dir,
                                          const char  *name);

void
entry_set_only_show_in_name (const char *name)
{
  g_free (only_show_in_name);
  only_show_in_name = g_strdup (name);
}

static Entry*
entry_new (EntryType   type,
           const char *relative_path)
{
  Entry *e;

  e = g_new (Entry, 1);
  
  e->categories = NULL;
  e->type = type;
  e->relative_path = g_strdup (relative_path);
  e->refcount = 1;
  
  return e;
}

void entry_ref   (Entry *entry);
void entry_unref (Entry *entry);

const char* entry_get_relative_path (Entry      *entry);
gboolean    entry_has_category      (Entry      *entry,
                                     const char *category);


Entry*
entry_get_by_absolute_path (const char *path)
{
  CachedDir* dir;
  char *dirname;
  char *basename;
  Entry *retval;

  retval = NULL;
  dirname = g_path_get_basename (path);
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
                   g_strerror (errno));
      menu_verbose ("Error %d loading cached dir \"%s\": %s\n", errno, path);
      goto out;
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

Entry*
entry_directory_get_desktop (EntryDirectory *ed,
                             const char     *relative_path)
{
  Entry *src;
  Entry *e;
  int i;
  
  if ((ed->flags & ENTRY_LOAD_DESKTOPS) == 0)
    return NULL;
  
  src = cached_dir_find_entry (ed->root, relative_path);
  if (src == NULL)
    return NULL;

  if (src->type != ENTRY_DESKTOP)
    return NULL;
  
  e = entry_new (src->type, relative_path);

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
entry_directory_get_directory (EntryDirectory *ed,
                               const char     *relative_path)
{
  Entry *src;
  Entry *e;
  int i;
  
  if ((ed->flags & ENTRY_LOAD_DIRECTORIES) == 0)
    return NULL;
  
  src = cached_dir_find_entry (ed->root, relative_path);
  if (src == NULL)
    return NULL;

  if (src->type != ENTRY_DIRECTORY)
    return NULL;
  
  e = entry_new (src->type, relative_path);
  
  return e;
}

EntryDirectoryList* entry_directory_list_new (void);

void entry_directory_list_ref     (EntryDirectoryList *list);
void entry_directory_list_unref   (EntryDirectoryList *list);
void entry_directory_list_clear   (EntryDirectoryList *list);
/* prepended dirs are searched first */
void entry_directory_list_prepend (EntryDirectoryList *list,
                                   EntryDirectory     *dir);
void entry_directory_list_append  (EntryDirectoryList *list,
                                   EntryDirectory     *dir);

/* return a new ref */
Entry* entry_directory_list_get_desktop   (EntryDirectoryList *list,
                                           const char         *relative_path);
Entry* entry_directory_list_get_directory (EntryDirectoryList *list,
                                           const char         *relative_path);


/*
 * Big global cache of desktop entries
 */

typedef struct
{
  CachedDir *parent;
  char *name;
  GSList *entries;
  GSList *subdirs;
  guint have_read_entries : 1;
  guint use_count : 27;
} CachedDir;

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

static CachedDir*
find_entry (CachedDir   *dir,
            const char  *name)
{
  GSList *tmp;

  tmp = dir->entries;
  while (tmp != NULL)
    {
      Entry *e = tmp->data;

      if (strcmp (e->name, name) == 0)
        return e;

      tmp = tmp->next;
    }

  return NULL;
}

static Entry*
cached_dir_find_entry (CachedDir   *dir,
                       const char  *name)
{
  GSList *tmp;
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

      parent = dir;
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

  menu_verbose ("Loading cached dir \"%s\"\n", path);  

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
unescape_string (gchar *str, gint len)
{
  gchar *res;
  gchar *p, *q;
  gchar *end;

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

static const char*
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
      if (!(q == ' ' || q == '\t'))
        break;

      --q;
    }
  
  if (!(q == str || q == '\n' || q == '\r'))
    return NULL;

  q = p + key_len;
  while (*q)
    {
      if (!(q == ' ' || q == '\t'))
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

  e = entry_new (ENTRY_DESKTOP, basename);
  
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
  return entry_new (ENTRY_DIRECTORY, basename);
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
  int i;

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
  GSList *tmp;
  
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
