/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* menu-method.c - Menu method for gnome-vfs
 *
 * Copyright (C) 2003 Red Hat Inc.
 * Developed by Havoc Pennington
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include <config.h>

#include <libgnomevfs/gnome-vfs-method.h>
#include <libgnomevfs/gnome-vfs-module-shared.h>
#include <libgnomevfs/gnome-vfs-module.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "menu-tree-cache.h"

/* FIXME - we have a gettext domain problem while not included in libgnomevfs */
#define _(x) x

typedef struct MenuMethod MenuMethod;
typedef struct DirHandle  DirHandle;

static MenuMethod*       method_checkout         (void);
static void              method_return           (MenuMethod            *method);
static DesktopEntryTree* menu_method_get_tree    (MenuMethod            *method,
                                                  const char            *scheme,
                                                  GError               **error);
static gboolean          menu_method_resolve_uri (MenuMethod            *method,
                                                  GnomeVFSURI           *uri,
                                                  DesktopEntryTree     **tree_p,
                                                  DesktopEntryTreeNode **node_p,
                                                  char                 **real_path_p,
                                                  GError               **error);

static GnomeVFSResult dir_handle_new            (MenuMethod               *method,
                                                 GnomeVFSURI              *uri,
                                                 GnomeVFSFileInfoOptions   options,
                                                 DirHandle               **handle);
static GnomeVFSResult dir_handle_next_file_info (DirHandle                *handle,
                                                 GnomeVFSFileInfo         *info);
static void           dir_handle_ref            (DirHandle                *handle);
static void           dir_handle_unref          (DirHandle                *handle);

static GnomeVFSResult
do_open (GnomeVFSMethod        *vtable,
         GnomeVFSMethodHandle **method_handle,
         GnomeVFSURI           *uri,
         GnomeVFSOpenMode       mode,
         GnomeVFSContext       *context)
{
        
        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_create (GnomeVFSMethod        *vtable,
           GnomeVFSMethodHandle **method_handle,
           GnomeVFSURI           *uri,
           GnomeVFSOpenMode       mode,
           gboolean               exclusive,
           guint                  perm,
           GnomeVFSContext       *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_close (GnomeVFSMethod       *vtable,
          GnomeVFSMethodHandle *method_handle,
          GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_read (GnomeVFSMethod       *vtable,
         GnomeVFSMethodHandle *method_handle,
         gpointer              buffer,
         GnomeVFSFileSize      num_bytes,
         GnomeVFSFileSize     *bytes_read,
         GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_write (GnomeVFSMethod       *vtable,
          GnomeVFSMethodHandle *method_handle,
          gconstpointer         buffer,
          GnomeVFSFileSize      num_bytes,
          GnomeVFSFileSize     *bytes_written,
          GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_seek (GnomeVFSMethod       *vtable,
         GnomeVFSMethodHandle *method_handle,
         GnomeVFSSeekPosition  whence,
         GnomeVFSFileOffset    offset,
         GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_tell (GnomeVFSMethod       *vtable,
         GnomeVFSMethodHandle *method_handle,
         GnomeVFSFileOffset   *offset_return)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}


static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod       *vtable,
                    GnomeVFSMethodHandle *method_handle,
                    GnomeVFSFileSize      where,
                    GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod   *vtable,
             GnomeVFSURI      *uri,
             GnomeVFSFileSize  where,
             GnomeVFSContext  *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_open_directory (GnomeVFSMethod           *vtable,
                   GnomeVFSMethodHandle    **method_handle,
                   GnomeVFSURI              *uri,
                   GnomeVFSFileInfoOptions   options,
                   GnomeVFSContext          *context)
{
        MenuMethod *method;
        GnomeVFSResult result;
        DirHandle *handle;
        
        method = method_checkout ();

        handle = NULL;
        result = dir_handle_new (method, uri, options, &handle);
        *method_handle = (GnomeVFSMethodHandle*) handle;
                
        method_return (method);
        
        return result;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod       *vtable,
                    GnomeVFSMethodHandle *method_handle,
                    GnomeVFSContext      *context)
{
        MenuMethod *method;
        DirHandle *handle;

        method = method_checkout ();
        
        handle = (DirHandle*) method_handle;

        dir_handle_unref (handle);

        method_return (method);
        
        return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod       *vtable,
                   GnomeVFSMethodHandle *method_handle,
                   GnomeVFSFileInfo     *file_info,
                   GnomeVFSContext      *context)
{
        MenuMethod *method;
        DirHandle *handle;
        GnomeVFSResult result;
        
        method = method_checkout ();
        
        handle = (DirHandle*) method_handle;

        result = dir_handle_next_file_info (handle, file_info);

        method_return (method);
        
        return result;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod         *vtable,
                  GnomeVFSURI            *uri,
                  GnomeVFSFileInfo       *file_info,
                  GnomeVFSFileInfoOptions options,
                  GnomeVFSContext        *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod         *vtable,
                              GnomeVFSMethodHandle   *method_handle,
                              GnomeVFSFileInfo       *file_info,
                              GnomeVFSFileInfoOptions options,
                              GnomeVFSContext        *context)
{
        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static gboolean
do_is_local (GnomeVFSMethod    *vtable,
             const GnomeVFSURI *uri)
{

        return TRUE;
}


static GnomeVFSResult
do_make_directory (GnomeVFSMethod  *vtable,
                   GnomeVFSURI     *uri,
                   guint            perm,
                   GnomeVFSContext *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod  *vtable,
                     GnomeVFSURI     *uri,
                     GnomeVFSContext *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_find_directory (GnomeVFSMethod           *vtable,
                   GnomeVFSURI              *near_uri,
                   GnomeVFSFindDirectoryKind kind,
                   GnomeVFSURI             **result_uri,
                   gboolean                  create_if_needed,
                   gboolean                  find_if_needed,
                   guint                     permissions,
                   GnomeVFSContext          *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_move (GnomeVFSMethod  *vtable,
         GnomeVFSURI     *old_uri,
         GnomeVFSURI     *new_uri,
         gboolean         force_replace,
         GnomeVFSContext *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod  *vtable,
           GnomeVFSURI     *uri,
           GnomeVFSContext *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod  *vtable,
                         GnomeVFSURI     *uri,
                         const char      *target_reference,
                         GnomeVFSContext *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod  *vtable,
                  GnomeVFSURI     *source_uri,
                  GnomeVFSURI     *target_uri,
                  gboolean        *same_fs_return,
                  GnomeVFSContext *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod          *vtable,
                  GnomeVFSURI             *uri,
                  const GnomeVFSFileInfo  *info,
                  GnomeVFSSetFileInfoMask  mask,
                  GnomeVFSContext         *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_monitor_add (GnomeVFSMethod        *vtable,
                GnomeVFSMethodHandle **method_handle_return,
                GnomeVFSURI           *uri,
                GnomeVFSMonitorType    monitor_type)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_monitor_cancel (GnomeVFSMethod       *vtable,
                   GnomeVFSMethodHandle *method_handle)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_file_control (GnomeVFSMethod       *vtable,
                 GnomeVFSMethodHandle *method_handle,
                 const char           *operation,
                 gpointer              operation_data,
                 GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSMethod vtable = {
        sizeof (GnomeVFSMethod),
        do_open,
        do_create,
        do_close,
        do_read,
        do_write,
        do_seek,
        do_tell,
        do_truncate_handle,
        do_open_directory,
        do_close_directory,
        do_read_directory,
        do_get_file_info,
        do_get_file_info_from_handle,
        do_is_local,
        do_make_directory,
        do_remove_directory,
        do_move,
        do_unlink,
        do_check_same_fs,
        do_set_file_info,
        do_truncate,
        do_find_directory,
        do_create_symbolic_link,
        do_monitor_add,
        do_monitor_cancel,
        do_file_control
};

GnomeVFSMethod *
vfs_module_init (const char *method_name, const char *args)
{
        return &vtable;
}

void
vfs_module_shutdown (GnomeVFSMethod *vtable)
{
}


/*
 * Method object
 */

struct MenuMethod
{
        int refcount;
        DesktopEntryTreeCache *cache;

};

static MenuMethod*
menu_method_new (void)
{
        MenuMethod *method;

        method = g_new0 (MenuMethod, 1);
        method->refcount = 1;
        method->cache = desktop_entry_tree_cache_new ();

        return method;
}

static void
menu_method_ref (MenuMethod *method)
{
        method->refcount += 1;
}

static void
menu_method_unref (MenuMethod *method)
{
        g_assert (method->refcount > 0);
        
        method->refcount -= 1;

        if (method->refcount == 0) {
                desktop_entry_tree_cache_unref (method->cache);
                g_free (method);
        }
}

static DesktopEntryTree*
menu_method_get_tree (MenuMethod  *method,
                      const char  *scheme,
                      GError     **error)
{
        DesktopEntryTree *tree;

        if (strcmp (scheme, "applications") == 0) {
                tree = desktop_entry_tree_cache_lookup (method->cache,
                                                        "applications.menu",
                                                        error);
        } else {
                g_set_error (error, G_FILE_ERROR,
                             G_FILE_ERROR_FAILED,
                             _("Unknown protocol \"%s\"\n"),
                             scheme);
                tree = NULL;
        }

        return tree;    
}

static gboolean
menu_method_resolve_uri (MenuMethod            *method,
                         GnomeVFSURI           *uri,
                         DesktopEntryTree     **tree_p,
                         DesktopEntryTreeNode **node_p,
                         char                 **real_path_p, /* path to .desktop file */
                         GError               **error)
{
        const char *scheme;
        char *unescaped;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;
        
        if (tree_p)
                *tree_p = NULL;
        if (node_p)
                *node_p = NULL;
        if (real_path_p)
                *real_path_p = NULL;
        
        scheme = gnome_vfs_uri_get_scheme (uri);
        g_assert (scheme != NULL);
        
        tree = menu_method_get_tree (method, scheme, error);
        if (tree == NULL)
                return FALSE;
        
        unescaped = gnome_vfs_unescape_string (uri->text, "");

        /* May not find a node, perhaps because it's a .desktop not a
         * directory, which is very possible.
         */
        if (!desktop_entry_tree_resolve_path (tree, unescaped, &node,
                                              real_path_p)) {
                desktop_entry_tree_unref (tree);
                g_set_error (error, G_FILE_ERROR,
                             G_FILE_ERROR_EXIST,
                             _("No such file or directory \"%s\"\n"),
                             unescaped);
                g_free (unescaped);
                return FALSE;
        }

        if (tree_p)
                *tree_p = tree;
        else
                desktop_entry_tree_unref (tree);
        
        if (node_p)
                *node_p = node; /* remember, node may be NULL */

        g_free (unescaped);

        return TRUE;
}

G_LOCK_DEFINE_STATIC (global_method);
static MenuMethod* global_method = NULL;

static MenuMethod*
method_checkout (void)
{
        G_LOCK (global_method);
        if (global_method == NULL) {
                global_method = menu_method_new ();
        }

        return global_method;
}

static void
method_return (MenuMethod *method)
{
        g_assert (method == global_method);
        G_UNLOCK (global_method);
}

/*
 * Directory handle object
 */
struct DirHandle
{
        /* Currently we don't need to thread-lock this object, since
         * it contains no reference to the global entry tree
         * data. You'd need to change the various VFS calls to do
         * locking if you changed this.
         */
        int    refcount;
        char **entries;
        int    n_entries;
        int    current;
        int    validity_stamp;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;
        GnomeVFSFileInfoOptions options;
};

static GnomeVFSResult
dir_handle_new (MenuMethod               *method,
                GnomeVFSURI              *uri,
                GnomeVFSFileInfoOptions   options,
                DirHandle               **handle_p)
{
        DirHandle *handle;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;

        if (!menu_method_resolve_uri (method, uri,
                                      &tree, &node,
                                      NULL, NULL))
                return GNOME_VFS_ERROR_NOT_FOUND; /* FIXME propagate GError ? */

        g_assert (tree != NULL);
        
        if (node == NULL) {
                desktop_entry_tree_unref (tree);
                return GNOME_VFS_ERROR_NOT_A_DIRECTORY;
        }
        
        handle = g_new0 (DirHandle, 1);

        handle->refcount = 1;
        handle->tree = tree; /* adopts refcount */
        handle->node = node;
        handle->options = options;
        
        handle->current = 0;
        
        desktop_entry_tree_list_all (handle->tree,
                                     handle->node,
                                     &handle->entries,
                                     &handle->n_entries);

        *handle_p = handle;
	return GNOME_VFS_OK;
}

static void
dir_handle_ref (DirHandle *handle)
{
        g_assert (handle->refcount > 0);
        handle->refcount += 1;
}

static void
dir_handle_unref (DirHandle *handle)
{
        g_assert (handle->refcount > 0);
        handle->refcount -= 1;
        if (handle->refcount == 0) {
                int i;
                
                desktop_entry_tree_unref (handle->tree);
                
                i = 0;
                while (i < handle->n_entries) {
                        /* remember that we NULL these as we
                         * return them to the app
                         */
                        g_free (handle->entries[i]);
                        ++i;
                }
                g_free (handle->entries);
                
                g_free (handle);
        }
}

#define UNSUPPORTED_INFO_FIELDS (GNOME_VFS_FILE_INFO_FIELDS_DEVICE      |       \
                                 GNOME_VFS_FILE_INFO_FIELDS_INODE       |       \
                                 GNOME_VFS_FILE_INFO_FIELDS_LINK_COUNT  |       \
                                 GNOME_VFS_FILE_INFO_FIELDS_ATIME)

static GnomeVFSResult
dir_handle_next_file_info (DirHandle         *handle,
                           GnomeVFSFileInfo  *info)
{
        if (handle->current >= handle->n_entries)
                return GNOME_VFS_ERROR_EOF;

        /* Pass on ownership, since dir iterators are a one-time thing anyhow */
        info->name = handle->entries[handle->current];
        handle->entries[handle->current] = NULL;
        handle->current += 1;
        
        info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
        
        if (handle->options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
                info->mime_type = g_strdup ("x-directory/normal");
                info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
        }

        info->permissions =
                GNOME_VFS_PERM_USER_ALL |
                GNOME_VFS_PERM_GROUP_ALL |
                GNOME_VFS_PERM_OTHER_ALL;

        info->valid_fields |=
                GNOME_VFS_FILE_INFO_FIELDS_TYPE |
                GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS;

        info->valid_fields &= ~ UNSUPPORTED_INFO_FIELDS;
        
        return GNOME_VFS_OK;
}
