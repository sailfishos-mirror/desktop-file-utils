/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* menu-method.c - Menu method for gnome-vfs
 *
 * Copyright (C) 2003 Red Hat Inc.
 * Developed by Havoc Pennington
 * Some bits from file-method.c in gnome-vfs
 * Copyright (C) 1999 Free Software Foundation
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
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

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
typedef struct FileHandle FileHandle;

static MenuMethod*       method_checkout         (void);
static void              method_return           (MenuMethod               *method);
static DesktopEntryTree* menu_method_get_tree    (MenuMethod               *method,
						  const char               *scheme,
						  GError                  **error);
static gboolean          menu_method_resolve_uri (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  DesktopEntryTree        **tree_p,
						  DesktopEntryTreeNode    **node_p,
						  char                    **real_path_p,
						  GError                  **error);
static GnomeVFSResult    menu_method_get_info    (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  GnomeVFSFileInfo         *file_info,
						  GnomeVFSFileInfoOptions   options);


static GnomeVFSResult dir_handle_new            (MenuMethod               *method,
                                                 GnomeVFSURI              *uri,
                                                 GnomeVFSFileInfoOptions   options,
                                                 DirHandle               **handle);
static GnomeVFSResult dir_handle_next_file_info (DirHandle                *handle,
                                                 GnomeVFSFileInfo         *info);
static void           dir_handle_ref            (DirHandle                *handle);
static void           dir_handle_unref          (DirHandle                *handle);


static GnomeVFSResult file_handle_open     (MenuMethod               *method,
					    GnomeVFSURI              *uri,
					    GnomeVFSOpenMode          mode,
					    FileHandle              **handle);
static GnomeVFSResult file_handle_create   (MenuMethod               *method,
					    GnomeVFSURI              *uri,
					    GnomeVFSOpenMode          mode,
					    FileHandle              **handle,
					    gboolean                  exclusive,
					    unsigned int              perms);
static void           file_handle_unref    (FileHandle               *handle);
static GnomeVFSResult file_handle_read     (FileHandle               *handle,
					    gpointer                  buffer,
					    GnomeVFSFileSize          num_bytes,
					    GnomeVFSFileSize         *bytes_read,
					    GnomeVFSContext          *context);
static GnomeVFSResult file_handle_get_info (FileHandle               *handle,
					    GnomeVFSFileInfo         *file_info,
					    GnomeVFSFileInfoOptions   options);     

static GnomeVFSResult
do_open (GnomeVFSMethod        *vtable,
         GnomeVFSMethodHandle **method_handle,
         GnomeVFSURI           *uri,
         GnomeVFSOpenMode       mode,
         GnomeVFSContext       *context)
{
        MenuMethod *method;
        GnomeVFSResult result;
        FileHandle *handle;
        
        method = method_checkout ();

        handle = NULL;
        result = file_handle_open (method, uri, mode, &handle);
        *method_handle = (GnomeVFSMethodHandle*) handle;
                
        method_return (method);
        
        return result;
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
#if 0
        MenuMethod *method;
        GnomeVFSResult result;
        FileHandle *handle;
        
        method = method_checkout ();

        handle = NULL;
        result = file_handle_create (method, uri, mode, exclusive,
				     perms, &handle);
        *method_handle = (GnomeVFSMethodHandle*) handle;
                
        method_return (method);
        
        return result;
#endif
}

static GnomeVFSResult
do_close (GnomeVFSMethod       *vtable,
          GnomeVFSMethodHandle *method_handle,
          GnomeVFSContext      *context)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;
         
        handle = (FileHandle*) method_handle;

        file_handle_unref (handle);
        
        return GNOME_VFS_OK;
}

static GnomeVFSResult
do_read (GnomeVFSMethod       *vtable,
         GnomeVFSMethodHandle *method_handle,
         gpointer              buffer,
         GnomeVFSFileSize      num_bytes,
         GnomeVFSFileSize     *bytes_read,
         GnomeVFSContext      *context)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;
	
	handle = (FileHandle *) method_handle;

	return file_handle_read (handle, buffer, num_bytes,
				 bytes_read, context);
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
        MenuMethod *method;
        GnomeVFSResult result;

        method = method_checkout ();

	result = menu_method_get_info (method, uri, file_info, options);

	method_return (method);
        
        return result;	
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod         *vtable,
                              GnomeVFSMethodHandle   *method_handle,
                              GnomeVFSFileInfo       *file_info,
                              GnomeVFSFileInfoOptions options,
                              GnomeVFSContext        *context)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;
	
        handle = (FileHandle*) method_handle;

	return file_handle_get_info (handle, file_info, options);
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

        if (strcmp (scheme, "menu-test") == 0) {
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

/* Fill in dir info that's true for all dirs in the vfs */
static void
fill_in_generic_dir_info (GnomeVFSFileInfo         *info,
			  GnomeVFSFileInfoOptions   options)
{
	info->type = GNOME_VFS_FILE_TYPE_DIRECTORY;
		
	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
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

}

static void
fill_in_dir_info (DesktopEntryTree         *tree,
		  DesktopEntryTreeNode     *node,
		  GnomeVFSFileInfo         *file_info,
		  GnomeVFSFileInfoOptions   options)
{
	g_assert (tree != NULL);
	g_assert (node != NULL);
	g_assert (file_info != NULL);

	fill_in_generic_dir_info (file_info, options);
}

/* Fill in file info that's true for all .desktop files */
static void
fill_in_generic_file_info (GnomeVFSFileInfo         *info,
			   GnomeVFSFileInfoOptions   options)
{
	info->type = GNOME_VFS_FILE_TYPE_REGULAR;
		
	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
		info->mime_type = g_strdup ("application/x-gnome-app-info");
		info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}

	info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE;
}

static void
fill_in_file_info (DesktopEntryTree         *tree,
		   DesktopEntryTreeNode     *node,
		   const char               *path,
		   GnomeVFSFileInfo         *file_info,
		   GnomeVFSFileInfoOptions   options)
{
	g_assert (tree != NULL);
	g_assert (node != NULL);	
	g_assert (path != NULL);
	g_assert (file_info != NULL);

	fill_in_generic_file_info (file_info, options);
}

static GnomeVFSResult
menu_method_get_info (MenuMethod               *method,
		      GnomeVFSURI              *uri,
		      GnomeVFSFileInfo         *file_info,
		      GnomeVFSFileInfoOptions   options)
{
        GnomeVFSResult retval;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;
	char *path;

	path = NULL;
	node = NULL;
	tree = NULL;

	retval = GNOME_VFS_OK;
	
        if (!menu_method_resolve_uri (method, uri,
                                      &tree, &node,
                                      &path, NULL))
                return GNOME_VFS_ERROR_NOT_FOUND; /* FIXME propagate GError ? */
	
        g_assert (tree != NULL);
	
	if (path == NULL) {
		fill_in_dir_info (tree, node,
				  file_info, options);
	} else {
		fill_in_file_info (tree, node, path,
				   file_info, options);
	}
        
        return retval;
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
	int    n_entries_that_are_subdirs;
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
                                     &handle->n_entries,
				     &handle->n_entries_that_are_subdirs);

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

        if (handle->current <= handle->n_entries_that_are_subdirs) {
		fill_in_generic_dir_info (info, handle->options);
	} else {
		fill_in_generic_file_info (info, handle->options);
	}

        info->valid_fields &= ~ UNSUPPORTED_INFO_FIELDS;
        
        return GNOME_VFS_OK;
}

struct FileHandle
{
	int   refcount;
	int   fd;
	char *name;
};

static char *
get_base_from_uri (GnomeVFSURI const *uri)
{
	char *escaped_base, *base;
	
	escaped_base = gnome_vfs_uri_extract_short_path_name (uri);
	base = gnome_vfs_unescape_string (escaped_base, G_DIR_SEPARATOR_S);
	g_free (escaped_base);
	return base;
}


static gboolean
unix_mode_from_vfs_mode (GnomeVFSOpenMode mode,
			 mode_t          *unix_mode)
{
	*unix_mode = 0;

	if (mode & GNOME_VFS_OPEN_READ) {
		if (mode & GNOME_VFS_OPEN_WRITE)
			*unix_mode = O_RDWR;
		else
			*unix_mode = O_RDONLY;
	} else {
		if (mode & GNOME_VFS_OPEN_WRITE)
			*unix_mode = O_WRONLY;
		else
			return FALSE; /* invalid mode - no read or write */
	}

	/* truncate file if we open for writing without random access */
	if ((!(mode & GNOME_VFS_OPEN_RANDOM)) &&
	    (mode & GNOME_VFS_OPEN_WRITE))
		*unix_mode |= O_TRUNC;

	return TRUE;
}

static GnomeVFSResult
unix_open (MenuMethod        *method,
	   GnomeVFSURI       *uri,
	   mode_t             unix_mode,
	   unsigned int       perms,
	   FileHandle       **handle_p)
{
	int fd;
	FileHandle *handle;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;
	char *path;
	GnomeVFSResult retval;
	
	*handle_p = NULL;

	path = NULL;
	node = NULL;
	tree = NULL;

	retval = GNOME_VFS_OK;
	
        if (!menu_method_resolve_uri (method, uri,
                                      &tree, &node,
                                      &path, NULL))
                return GNOME_VFS_ERROR_NOT_FOUND; /* FIXME propagate GError ? */
	
        g_assert (tree != NULL);
	
	if (path == NULL) {
		retval = GNOME_VFS_ERROR_IS_DIRECTORY;
		goto out;
	}

 again:
	fd = open (path, unix_mode, perms);
	if (fd < 0) {
		if (errno == EINTR)
			goto again;

		retval = gnome_vfs_result_from_errno ();
		goto out;
	}

	handle = g_new0 (FileHandle, 1);
	handle->refcount = 1;
	handle->fd = fd;
	handle->name = get_base_from_uri (uri);
	
	*handle_p = handle;
	
	retval = GNOME_VFS_OK;
	
 out:
	desktop_entry_tree_unref (tree);
	g_free (path);
	return retval;
}

static GnomeVFSResult
file_handle_open (MenuMethod        *method,
		  GnomeVFSURI       *uri,
		  GnomeVFSOpenMode   mode,		  
		  FileHandle       **handle_p)
{
	mode_t unix_mode;
	
	if (!unix_mode_from_vfs_mode (mode, &unix_mode))
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;

	return unix_open (method, uri, unix_mode, 0, handle_p);
}

static GnomeVFSResult
file_handle_create (MenuMethod        *method,
		    GnomeVFSURI       *uri,
		    GnomeVFSOpenMode   mode,
		    FileHandle       **handle,
		    gboolean           exclusive,
		    unsigned int       perms)
{
	mode_t unix_mode;
	
	unix_mode = O_CREAT | O_TRUNC;
	
	if (!(mode & GNOME_VFS_OPEN_WRITE))
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	
	if (mode & GNOME_VFS_OPEN_READ)
		unix_mode |= O_RDWR;
	else
		unix_mode |= O_WRONLY;

	if (exclusive)
		unix_mode |= O_EXCL;

	/* FIXME this can't possibly work since it doesn't
	 * create an item in the DesktopEntryTree
	 */
	return unix_open (method, uri, unix_mode, perms, handle);
}

static void
file_handle_unref (FileHandle *handle)
{
	g_return_if_fail (handle->refcount > 0);
	
	handle->refcount -= 1;
	if (handle->refcount == 0) {
		close (handle->fd);
		g_free (handle->name);
		g_free (handle);
	}
}

static GnomeVFSResult
file_handle_read (FileHandle        *handle,
		  gpointer           buffer,
		  GnomeVFSFileSize   num_bytes,
		  GnomeVFSFileSize  *bytes_read,
		  GnomeVFSContext   *context)
{
	int read_val;

	do {
		read_val = read (handle->fd, buffer, num_bytes);
	} while (read_val == -1
	         && errno == EINTR
	         && ! gnome_vfs_context_check_cancellation (context));

	if (read_val == -1) {
		*bytes_read = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_read = read_val;

		/* Getting 0 from read() means EOF! */
		if (read_val == 0) {
			return GNOME_VFS_ERROR_EOF;
		}
	}
	return GNOME_VFS_OK;
}

static GnomeVFSResult
file_handle_get_info (FileHandle               *handle,
		      GnomeVFSFileInfo         *file_info,
		      GnomeVFSFileInfoOptions   options)
{
	struct stat statbuf;

	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_NONE;
	
	if (fstat (handle->fd, &statbuf) != 0) {
		return gnome_vfs_result_from_errno ();
	}
	
	gnome_vfs_stat_to_file_info (file_info, &statbuf);
	GNOME_VFS_FILE_INFO_SET_LOCAL (file_info, TRUE);
	
	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_TYPE;
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->name = g_strdup (handle->name);

	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
		file_info->mime_type = g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}
	
	return GNOME_VFS_OK;
}

