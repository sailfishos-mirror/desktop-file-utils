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

struct DesktopEntryTreeCache
{
  int refcount;


};

DesktopEntryTreeCache*
desktop_entry_tree_cache_new (void)
{
  DesktopEntryTreeCache *cache;

  cache = g_new0 (DesktopEntryTreeCache, 1);

  cache->refcount = 1;

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
      
      g_free (cache);
    }
}

DesktopEntryTree*
desktop_entry_tree_cache_lookup (DesktopEntryTreeCache *cache,
                                 const char            *menu_file)
{
  /* menu_file may be absolute, or if relative should be searched
   * for in the xdg dirs.
   */

  /* FIXME */
  
  return NULL;
}

