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
#include "menu-layout.h"
#include "menu-overrides.h"
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
  const char *first_system_data;
  const char *first_system_config;
};

static char**
parse_search_path_and_prepend (const char *path,
                               const char *prepend_this)
{
  char **retval;
  int i;
  int len;

  if (path != NULL)
    {
      menu_verbose ("Parsing path \"%s\"\n", path);
      
      retval = g_strsplit (path, ":", -1);

      for (len = 0; retval[len] != NULL; len++)
        ;

      menu_verbose ("%d elements after split\n", len);
        
      i = 0;
      while (i < len) {
        /* delete empty strings */
        if (*(retval[i]) == '\0')
          {
            menu_verbose ("Deleting element %d\n", i);
            
            g_free (retval[i]);
            g_memmove (&retval[i],
                       &retval[i + 1],
                       (len - i) * sizeof (retval[0]));
            --len;
          }
        else
          {
            menu_verbose ("Keeping element %d\n", i);
            ++i;
          }
      }

      if (prepend_this != NULL)
        {
          menu_verbose ("Prepending \"%s\"\n", prepend_this);
          
          retval = g_renew (char*, retval, len + 2);
          g_memmove (&retval[1],
                     &retval[0],
                     (len + 1) * sizeof (retval[0]));
          retval[0] = g_strdup (prepend_this);
        }
    }
  else
    {
      menu_verbose ("Using \"%s\" as only path element\n", prepend_this);
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

  if (info.data_home == NULL)
    {
      const char *p;

      p = g_getenv ("XDG_DATA_HOME");
      if (p != NULL && *p != '\0')
        info.data_home = g_strdup (p);
      else
        info.data_home = g_build_filename (g_get_home_dir (),
                                           ".local", "share",
                                           NULL);
                
      p = g_getenv ("XDG_CONFIG_HOME");
      if (p != NULL && *p != '\0')
        info.config_home = g_strdup (p);
      else
        info.config_home = g_build_filename (g_get_home_dir (),
                                             ".config", NULL);
                        
      p = g_getenv ("XDG_DATA_DIRS");
      if (p == NULL || *p == '\0')
        p = PREFIX"/local/share:"DATADIR;
      info.data_dirs = parse_search_path_and_prepend (p, info.data_home);
      info.first_system_data = info.data_dirs[1];
      
      p = g_getenv ("XDG_CONFIG_DIRS");
      if (p == NULL || *p == '\0')
        p = SYSCONFDIR"/xdg";
      info.config_dirs = parse_search_path_and_prepend (p, info.config_home);
      info.first_system_config = info.config_dirs[1];
      
#ifndef DFU_MENU_DISABLE_VERBOSE
      {
        int q;
        q = 0;
        while (info.data_dirs[q])
          {
            menu_verbose ("Data Path Entry: %s\n", info.data_dirs[q]);
            ++q;
          }
        q = 0;
        while (info.config_dirs[q])
          {
            menu_verbose ("Conf Path Entry: %s\n", info.config_dirs[q]);
            ++q;
          }
      }
#endif /* Verbose */
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
  MenuOverrideDir  *overrides;
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

static CacheEntry*
lookup_canonical_entry (DesktopEntryTreeCache *cache,
                        const char            *canonical,
                        const char            *create_chaining_to,
                        GError               **error)
{
  CacheEntry *entry;

  menu_verbose ("Looking up canonical filename in tree cache: \"%s\"\n",
                canonical);
  
  entry = g_hash_table_lookup (cache->entries, canonical);
  if (entry == NULL)
    {
      char *basename;
      
      entry = g_new0 (CacheEntry, 1);
      entry->canonical_path = g_strdup (canonical);
      entry->tree = desktop_entry_tree_load (canonical,
                                             NULL, /* FIXME only show in desktop */
                                             create_chaining_to,
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
      g_assert (entry->load_failure_reason != NULL);
      
      menu_verbose ("File load failure cached, reason for failure: %s\n",
                    entry->load_failure_reason->message);
      
      if (error)
        *error = g_error_copy (entry->load_failure_reason);
      return NULL;
    }
  else
    {
      menu_verbose ("Returning cached entry\n");
      
      return entry;
    }
}

static CacheEntry*
lookup_absolute_entry (DesktopEntryTreeCache *cache,
                       const char            *absolute,
                       const char            *create_chaining_to,
                       GError               **error)
{
  CacheEntry *entry;
  char *canonical;

  menu_verbose ("Looking up absolute filename in tree cache: \"%s\"\n",
                absolute);
  
  /* First just guess the absolute is already canonical and see if
   * it's in the cache, to avoid the canonicalization. Don't
   * want to cache the failed lookup so check the hash table
   * directly instead of doing lookup_canonical() first.
   */
  if (g_hash_table_lookup (cache->entries, absolute) != NULL)
    {
      entry = lookup_canonical_entry (cache, absolute, create_chaining_to, error);
      if (entry != NULL)
        return entry;
    }

  /* Now really canonicalize it and try again */
  canonical = g_canonicalize_file_name (absolute, TRUE);
  if (canonical == NULL)
    {
      g_set_error (error, G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   _("Could not find or canonicalize the file \"%s\"\n"),
                   absolute);
      menu_verbose ("Failed to canonicalize: \"%s\": %s\n",
                    absolute, g_strerror (errno));
      return NULL;
    }
  
  entry = lookup_canonical_entry (cache, canonical, create_chaining_to, error);
  g_free (canonical);
  return entry;
}

/* FIXME this cache has the usual cache problem - when to expire it?
 * Right now I think "never" is probably a good enough answer, but at
 * some point we might want to re-evaluate.
 */
static CacheEntry*
cache_lookup (DesktopEntryTreeCache *cache,
              const char            *menu_file,
              gboolean               create_user_file,
              GError               **error)
{
  CacheEntry *retval;

  retval = NULL;
  
  /* menu_file may be absolute, or if relative should be searched
   * for in the xdg dirs.
   */
  if (g_path_is_absolute (menu_file))
    {
      retval = lookup_absolute_entry (cache, menu_file, NULL, error);
    }
  else
    {
      XdgPathInfo info;
      CacheEntry *entry;
      int i;
      const char *canonical;

      /* Try to avoid the path search */
      canonical = g_hash_table_lookup (cache->basename_to_canonical,
                                       menu_file);
      if (canonical != NULL)
        {
          retval = lookup_canonical_entry (cache, canonical, NULL, error);
          goto out;
        }

      /* Now look for the file in the path */
      init_xdg_paths (&info);
      
      i = 0;
      while (info.config_dirs[i] != NULL)
        {
          char *absolute;
          char *chain_to;

          absolute = g_build_filename (info.config_dirs[i],
                                       "menus", menu_file, NULL);

          if (i == 0 && create_user_file)
            {
              char *dirname;
              
              chain_to = g_build_filename (info.first_system_config,
                                           "menus", menu_file, NULL);
              
              /* Create directory for the user menu file */
              dirname = g_build_filename (info.config_dirs[i], "menus",
                                          NULL);

              menu_verbose ("Will chain to \"%s\" from user file \"%s\" in directory \"%s\"\n",
                            chain_to, absolute, dirname);
              
              /* ignore errors, if it's a problem stuff will fail
               * later.
               */
              g_create_dir (dirname, 0755, NULL);
              g_free (dirname);
            }
          else
            chain_to = NULL;
          
          entry = lookup_absolute_entry (cache, absolute, chain_to, error);
          g_free (absolute);
          g_free (chain_to);
          
          if (entry != NULL)
            {
              /* in case an earlier lookup piled up */
              menu_verbose ("Successfully got entry %p\n", entry);
              g_clear_error (error);
              retval = entry;
              goto out;
            }
          
          ++i;
        }

      /* We didn't find anything; the error from the last lookup
       * should still be set.
       */
    }

 out:
  
  return retval;
}

DesktopEntryTree*
desktop_entry_tree_cache_lookup (DesktopEntryTreeCache *cache,
                                 const char            *menu_file,
                                 gboolean               create_user_file,
                                 GError               **error)
{
  CacheEntry *entry;

  entry = cache_lookup (cache, menu_file, create_user_file,
                        error);

  if (entry)
    {
      desktop_entry_tree_ref (entry->tree);
      return entry->tree;
    }
  else
    return NULL;
}

/* For a menu_file like "applications.menu" override a menu_path
 * entry like "Applications/Games/foo.desktop"
 */
gboolean
desktop_entry_tree_cache_override (DesktopEntryTreeCache *cache,
                                   const char            *menu_file,
                                   const char            *menu_path,
                                   GError               **error)
{
  CacheEntry *entry;

  entry = cache_lookup (cache, menu_file, TRUE, error);

  if (entry == NULL)
    return FALSE;

  if (entry->overrides == NULL)
    {
      char *d;
      GString *menu_type;
      XdgPathInfo info;

      init_xdg_paths (&info);

      menu_type = g_string_new (menu_path);
      g_string_truncate (menu_type, menu_type->len - strlen (".menu"));
      g_string_append (menu_type, "-edits");
      
      d = g_build_filename (info.config_home,
                            menu_type->str, NULL);
      
      entry->overrides = menu_override_dir_create (d, error);

      g_string_free (menu_type, TRUE);
      g_free (d);
    }

  if (entry->overrides == NULL)
    return FALSE;

  /* FIXME */
  
  return TRUE;
}
