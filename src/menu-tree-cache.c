/* Cache of DesktopEntryTree */
/*
 * Copyright (C) 2003 Red Hat, Inc.
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

#include "menu-tree-cache.h"
#include "menu-process.h"
#include "canonicalize.h"
#include <glib.h>
#include <string.h>
#include <errno.h>

#include <libintl.h>
#define _(x) gettext ((x))
#define N_(x) x

typedef struct XdgPathInfo XdgPathInfo;

struct XdgPathInfo
{
        char  *data_home;
        char  *config_home;
        char **data_dirs;
        char **config_dirs;
};

static char**
parse_search_path_and_prepend (const char *path,
                               const char *prepend_this)
{
        char **retval;
        int i;
        int len;

        if (path != NULL) {
                retval = g_strsplit (path, ":", -1);

                for (len = 0; retval[len] != NULL; len++)
                        ;

        
                i = 0;
                while (i < len) {
                        /* delete empty strings */
                        if (*(retval[i]) == '\0') {
                                g_free (retval[i]);
                                g_memmove (&retval[i],
                                           &retval[i + 1],
                                           (len - i) * sizeof (retval[0]));
                                --len;
                        } else {
                                ++i;
                        }
                }

                if (prepend_this != NULL) {
                        retval = g_realloc (retval, sizeof (retval[0]) * (len + 2));
                        g_memmove (&retval[0],
                                   &retval[1],
                                   (len + 1) * sizeof (retval[0]));
                        retval[0] = g_strdup (prepend_this);
                }
        } else {
                retval = g_new0 (char*, 2);
                if (prepend_this)
                        retval[0] = g_strdup (prepend_this);
        }
        
        return retval;
}
                        

static void
init_xdg_paths (XdgPathInfo *info_p)
{
        static XdgPathInfo info = { NULL, NULL, NULL, NULL };

        if (info.data_home == NULL) {
                const char *p;

                p = g_getenv ("XDG_DATA_HOME");
                if (p != NULL)
                        info.data_home = g_strdup (p);
                else
                        info.data_home = g_build_path (g_get_home_dir (),
                                                       ".local", "share",
                                                       NULL);
                
                p = g_getenv ("XDG_CONFIG_HOME");
                if (p != NULL)
                        info.config_home = g_strdup (p);
                else
                        info.config_home = g_build_path (g_get_home_dir (),
                                                         ".config", NULL);
                        
                p = g_getenv ("XDG_DATA_DIRS");
                info.data_dirs = parse_search_path_and_prepend (p, info.data_home);
                
                p = g_getenv ("XDG_CONFIG_DIRS");
                info.config_dirs = parse_search_path_and_prepend (p, info.config_home);
        }

        *info_p = info;
}

/* We need to keep track of what absolute path we're using for each
 * menu basename, and then cache the loaded tree for each absolute
 * path.
 */
typedef struct
{
  char             *canonical_path;
  DesktopEntryTree *tree;
  GError           *load_failure_reason;
} CacheEntry;

struct DesktopEntryTreeCache
{
  int refcount;

  GHashTable *entries;
  GHashTable *basename_to_canonical;
};

static void
free_cache_entry (void *data)
{
  CacheEntry *entry = data;

  g_free (entry->canonical_path);
  if (entry->tree)
    desktop_entry_tree_unref (entry->tree);
  if (entry->load_failure_reason)
    g_error_free (entry->load_failure_reason);

  g_free (entry);
}

DesktopEntryTreeCache*
desktop_entry_tree_cache_new (void)
{
  DesktopEntryTreeCache *cache;

  cache = g_new0 (DesktopEntryTreeCache, 1);

  cache->refcount = 1;

  cache->entries = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          NULL,
                                          free_cache_entry);
  cache->basename_to_canonical =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           g_free, g_free);
  
  return cache;
}

void
desktop_entry_tree_cache_ref (DesktopEntryTreeCache *cache)
{
  g_return_if_fail (cache != NULL);
  
  cache->refcount += 1;
}

void
desktop_entry_tree_cache_unref (DesktopEntryTreeCache *cache)
{
  g_return_if_fail (cache != NULL);
  cache->refcount -= 1;
  if (cache->refcount == 0)
    {
      g_hash_table_destroy (cache->entries);
      g_hash_table_destroy (cache->basename_to_canonical);
      
      g_free (cache);
    }
}

static DesktopEntryTree*
lookup_canonical (DesktopEntryTreeCache *cache,
                  const char            *canonical,
                  GError               **error)
{
  CacheEntry *entry;
  
  entry = g_hash_table_lookup (cache->entries, canonical);
  if (entry == NULL)
    {
      char *basename;
      
      entry = g_new0 (CacheEntry, 1);
      entry->canonical_path = g_strdup (canonical);
      entry->tree = desktop_entry_tree_load (canonical,
                                             NULL, /* FIXME only show in desktop */
                                             &entry->load_failure_reason);
      g_hash_table_replace (cache->entries, entry->canonical_path,
                            entry);
      basename = g_path_get_basename (entry->canonical_path);
      g_hash_table_replace (cache->basename_to_canonical,
                            basename, g_strdup (entry->canonical_path));
    }

  g_assert (entry != NULL);
  
  if (entry->tree == NULL)
    {
      if (error)
        *error = g_error_copy (entry->load_failure_reason);
      return NULL;
    }
  else
    {
      desktop_entry_tree_ref (entry->tree);
      return entry->tree;
    }
}

static DesktopEntryTree*
lookup_absolute (DesktopEntryTreeCache *cache,
                 const char            *absolute,
                 GError               **error)
{
  DesktopEntryTree *tree;
  char *canonical;

  /* First just guess the absolute is already canonical and see if
   * it's in the cache, to avoid the canonicalization. Don't
   * want to cache the failed lookup so check the hash table
   * directly instead of doing lookup_canonical() first.
   */
  if (g_hash_table_lookup (cache->entries, absolute) != NULL)
    {
      tree = lookup_canonical (cache, absolute, error);
      if (tree != NULL)
        return tree;
    }

  /* Now really canonicalize it and try again */
  canonical = g_canonicalize_file_name (absolute);
  if (canonical == NULL)
    {
      g_set_error (error, G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   _("Could not find or canonicalize the file \"%s\"\n"),
                   absolute);
      return NULL;
    }
  
  tree = lookup_canonical (cache, canonical, error);
  g_free (canonical);
  return tree;
}

/* FIXME this cache has the usual cache problem - when to expire it?
 * Right now I think "never" is probably a good enough answer, but at
 * some point we might want to re-evaluate.
 */
DesktopEntryTree*
desktop_entry_tree_cache_lookup (DesktopEntryTreeCache *cache,
                                 const char            *menu_file,
                                 GError               **error)
{
  /* menu_file may be absolute, or if relative should be searched
   * for in the xdg dirs.
   */
  if (g_path_is_absolute (menu_file))
    {
      return lookup_absolute (cache, menu_file, error);
    }
  else
    {
      XdgPathInfo info;
      DesktopEntryTree *tree;
      int i;
      const char *canonical;

      /* Try to avoid the path search */
      canonical = g_hash_table_lookup (cache->basename_to_canonical,
                                       menu_file);
      if (canonical != NULL)
        return lookup_canonical (cache, canonical, error);

      /* Now look for the file in the path */
      init_xdg_paths (&info);

      i = 0;
      while (info.config_dirs[i] != NULL)
        {
          char *absolute;

          absolute = g_build_filename (info.config_dirs[i],
                                       menu_file, NULL);

          tree = lookup_absolute (cache, absolute, error);
          g_free (absolute);
          if (tree != NULL)
            {
              /* in case an earlier lookup piled up */
              g_clear_error (error);
              return tree;
            }
          
          ++i;
        }

      /* We didn't find anything; the error from the last lookup
       * should still be set.
       */

      return NULL;
    }
}

