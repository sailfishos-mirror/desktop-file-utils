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
#include "menu-util.h"

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
static GnomeVFSResult    menu_method_truncate    (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  GnomeVFSFileSize          where,
						  GnomeVFSContext          *context);
static GnomeVFSResult    menu_method_move        (MenuMethod               *method,
						  GnomeVFSURI              *old_uri,
						  GnomeVFSURI              *new_uri,
						  gboolean                  force_replace,
						  GnomeVFSContext          *context);
static GnomeVFSResult    menu_method_unlink      (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  GnomeVFSContext          *context);
static GnomeVFSResult    menu_method_mkdir       (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  guint                     perm,
						  GnomeVFSContext          *context);
static GnomeVFSResult    menu_method_rmdir       (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  GnomeVFSContext          *context);

static gboolean menu_method_resolve_uri_writable (MenuMethod               *method,
						  GnomeVFSURI              *uri,
						  gboolean                  create_if_not_found,
						  DesktopEntryTree        **tree_p,
						  DesktopEntryTreeNode    **node_p,
						  char                    **real_path_p,
						  GError                  **error);

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
static GnomeVFSResult file_handle_write    (FileHandle               *handle,
					    gconstpointer             buffer,
					    GnomeVFSFileSize          num_bytes,
					    GnomeVFSFileSize         *bytes_written,
					    GnomeVFSContext          *context);
static GnomeVFSResult file_handle_seek     (FileHandle               *handle,
					    GnomeVFSSeekPosition      whence,
					    GnomeVFSFileOffset        offset,
					    GnomeVFSContext          *context);
static GnomeVFSResult file_handle_tell     (FileHandle               *handle,
					    GnomeVFSFileOffset       *offset_return);
static GnomeVFSResult file_handle_truncate (FileHandle               *handle,
					    GnomeVFSFileSize          where,
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

	menu_verbose ("method: Opening %s\n", uri->text);
	
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
           guint                  perms,
           GnomeVFSContext       *context)
{
        MenuMethod *method;
        GnomeVFSResult result;
        FileHandle *handle;

	menu_verbose ("method: Creating %s\n", uri->text);
	
        method = method_checkout ();

        handle = NULL;
        result = file_handle_create (method, uri, mode, &handle,
				     exclusive, perms);
        *method_handle = (GnomeVFSMethodHandle*) handle;
                
        method_return (method);
        
        return result;
}

static GnomeVFSResult
do_close (GnomeVFSMethod       *vtable,
          GnomeVFSMethodHandle *method_handle,
          GnomeVFSContext      *context)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;

	menu_verbose ("method: Closing\n");
	
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

	menu_verbose ("method: Reading\n");
	
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
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;

	menu_verbose ("method: Writing\n");
	
	handle = (FileHandle *) method_handle;

	return file_handle_write (handle, buffer, num_bytes,
				  bytes_written, context);
}

static GnomeVFSResult
do_seek (GnomeVFSMethod       *vtable,
         GnomeVFSMethodHandle *method_handle,
         GnomeVFSSeekPosition  whence,
         GnomeVFSFileOffset    offset,
         GnomeVFSContext      *context)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;

	menu_verbose ("method: Seeking\n");
	
	handle = (FileHandle *) method_handle;

	return file_handle_seek (handle, whence, offset, context);
}

static GnomeVFSResult
do_tell (GnomeVFSMethod       *vtable,
         GnomeVFSMethodHandle *method_handle,
         GnomeVFSFileOffset   *offset_return)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;

	menu_verbose ("method: Telling\n");
	
	handle = (FileHandle *) method_handle;

	return file_handle_tell (handle, offset_return);
}


static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod       *vtable,
                    GnomeVFSMethodHandle *method_handle,
                    GnomeVFSFileSize      where,
                    GnomeVFSContext      *context)
{
	/* No thread locks since FileHandle is threadsafe */
	FileHandle *handle;

	menu_verbose ("method: Truncate handle\n");
	
	handle = (FileHandle *) method_handle;

	return file_handle_truncate (handle, where, context);
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod   *vtable,
             GnomeVFSURI      *uri,
             GnomeVFSFileSize  where,
             GnomeVFSContext  *context)
{
        MenuMethod *method;
        GnomeVFSResult result;

	menu_verbose ("method: Truncate %s\n", uri->text);
	
        method = method_checkout ();

	result = menu_method_truncate (method, uri, where, context);
	
        method_return (method);
        
        return result;
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

	menu_verbose ("method: Open directory %s\n", uri->text);
	
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

	menu_verbose ("method: Close directory\n");
	
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

	menu_verbose ("method: Read directory\n");
	
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

	menu_verbose ("method: Get file info on %s\n",
		      uri->text);
	
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

	menu_verbose ("method: Get file info from handle\n");
	
        handle = (FileHandle*) method_handle;

	return file_handle_get_info (handle, file_info, options);
}

static gboolean
do_is_local (GnomeVFSMethod    *vtable,
             const GnomeVFSURI *uri)
{
	menu_verbose ("method: Is local?\n");
	
        return TRUE;
}


static GnomeVFSResult
do_make_directory (GnomeVFSMethod  *vtable,
                   GnomeVFSURI     *uri,
                   guint            perm,
                   GnomeVFSContext *context)
{
        MenuMethod *method;
        GnomeVFSResult result;

	menu_verbose ("method: Make directory %s\n", uri->text);
	
        method = method_checkout ();

        result = menu_method_mkdir (method, uri, perm, context);

        method_return (method);
        
        return result;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod  *vtable,
                     GnomeVFSURI     *uri,
                     GnomeVFSContext *context)
{
        MenuMethod *method;
        GnomeVFSResult result;

	menu_verbose ("method: Remove directory %s\n", uri->text);
	
        method = method_checkout ();

        result = menu_method_rmdir (method, uri, context);

        method_return (method);
        
        return result;
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
        MenuMethod *method;
        GnomeVFSResult result;

	menu_verbose ("method: Move %s -> %s\n", old_uri->text, new_uri->text);
        
        method = method_checkout ();

        result = menu_method_move (method, old_uri, new_uri,
				   force_replace, context);

        method_return (method);
        
        return result;
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod  *vtable,
           GnomeVFSURI     *uri,
           GnomeVFSContext *context)
{
        MenuMethod *method;
        GnomeVFSResult result;

	menu_verbose ("method: Unlink %s\n", uri->text);
	
        method = method_checkout ();

        result = menu_method_unlink (method, uri, context);

        method_return (method);
        
        return result;
}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod  *vtable,
                         GnomeVFSURI     *uri,
                         const char      *target_reference,
                         GnomeVFSContext *context)
{
	menu_verbose ("method: Create symlink %s\n", uri->text);
	
        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod  *vtable,
                  GnomeVFSURI     *source_uri,
                  GnomeVFSURI     *target_uri,
                  gboolean        *same_fs_return,
                  GnomeVFSContext *context)
{
	menu_verbose ("method: Check same fs %s and %s\n",source_uri->text,
		      target_uri->text);
	
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod          *vtable,
                  GnomeVFSURI             *uri,
                  const GnomeVFSFileInfo  *info,
                  GnomeVFSSetFileInfoMask  mask,
                  GnomeVFSContext         *context)
{
	menu_verbose ("method: Set file info %s\n", uri->text);
	
        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_monitor_add (GnomeVFSMethod        *vtable,
                GnomeVFSMethodHandle **method_handle_return,
                GnomeVFSURI           *uri,
                GnomeVFSMonitorType    monitor_type)
{
	menu_verbose ("method: Monitor add %s\n", uri->text);
	
        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_monitor_cancel (GnomeVFSMethod       *vtable,
                   GnomeVFSMethodHandle *method_handle)
{
	menu_verbose ("method: Monitor cancel\n");
	
        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_file_control (GnomeVFSMethod       *vtable,
                 GnomeVFSMethodHandle *method_handle,
                 const char           *operation,
                 gpointer              operation_data,
                 GnomeVFSContext      *context)
{
	menu_verbose ("method: File control\n");
	
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

static const char*
scheme_to_menu (const char *scheme)
{
        if (strcmp (scheme, "menu-test") == 0) {
		return "applications.menu";
	} else {
		return NULL;
	}
}

static gboolean
unpack_uri (GnomeVFSURI  *uri,
	    const char  **menu_file_p,
	    char        **menu_path_p,
	    GError      **error)
{
	if (menu_file_p)
		*menu_file_p = NULL;
	if (menu_path_p)
		*menu_path_p = NULL;
	
	if (menu_file_p) {
		const char *scheme;
		
		scheme = gnome_vfs_uri_get_scheme (uri);
		g_assert (scheme != NULL);

		*menu_file_p = scheme_to_menu (scheme);

		if (*menu_file_p == NULL) {
			menu_verbose ("Unknown protocol %s\n", scheme);
			g_set_error (error, G_FILE_ERROR,
				     G_FILE_ERROR_FAILED,
				     _("Unknown protocol \"%s\"\n"),
				     scheme);
			return FALSE;
		}
	}

	if (menu_path_p) {
		char *unescaped;
		
		unescaped = gnome_vfs_unescape_string (uri->text, "");

		*menu_path_p = unescaped;
	}

	return TRUE;
}

static gboolean
menu_method_ensure_override (MenuMethod   *method,
			     GnomeVFSURI  *uri,
			     GError      **error)
{
        const char *menu_file;
        char *menu_path;

	if (!unpack_uri (uri, &menu_file, &menu_path, error)) {
		return FALSE;
	}
	
	if (!desktop_entry_tree_cache_create (method->cache,
					      menu_file, menu_path,
					      error)) {
		g_free (menu_path);
		return FALSE;
	}

	g_free (menu_path);
	return TRUE;
}

static DesktopEntryTree*
menu_method_get_tree (MenuMethod  *method,
                      const char  *menu_file,
                      GError     **error)
{
        DesktopEntryTree *tree;
	
	menu_verbose ("Getting tree for %s\n", menu_file);

	tree = desktop_entry_tree_cache_lookup (method->cache,
						menu_file, TRUE,
						error);

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
        const char *menu_file;
	char *menu_path;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;
	PathResolution res;
        
        if (tree_p)
                *tree_p = NULL;
        if (node_p)
                *node_p = NULL;
        if (real_path_p)
                *real_path_p = NULL;

	if (!unpack_uri (uri, &menu_file, &menu_path, error))
		return FALSE;

        tree = menu_method_get_tree (method, menu_file, error);
        if (tree == NULL) {
		menu_verbose ("Got NULL tree from menu method\n");
		g_free (menu_path);
                return FALSE;
	}

        /* May not find a node, perhaps because it's a .desktop not a
         * directory, which is very possible. resolve_path returns
	 * TRUE if the path is an entry.
         */
	res = desktop_entry_tree_resolve_path (tree, menu_path, &node,
					       real_path_p, NULL);
	if (res == PATH_RESOLUTION_NOT_FOUND) {		
                desktop_entry_tree_unref (tree);
                g_set_error (error, G_FILE_ERROR,
                             G_FILE_ERROR_EXIST,
                             _("No such file or directory \"%s\"\n"),
                             menu_path);
                g_free (menu_path);

		menu_verbose ("Failed to resolve path %s in desktop entry tree\n",
			      menu_path);
		
                return FALSE;
        }

        if (tree_p)
                *tree_p = tree;
        else
                desktop_entry_tree_unref (tree);
        
        if (node_p)
                *node_p = node; /* remember, node may be NULL */

        g_free (menu_path);

        return TRUE;
}

static gboolean
menu_method_resolve_uri_writable (MenuMethod               *method,
				  GnomeVFSURI              *uri,
				  gboolean                  create_if_not_found,
				  DesktopEntryTree        **tree_p,
				  DesktopEntryTreeNode    **node_p,
				  char                    **real_path_p,
				  GError                  **error)
{
	char *real_path;
	DesktopEntryTreeNode *node;
	DesktopEntryTree *tree;
	
	real_path = NULL;
	node = NULL;
	tree = NULL;

	/* Be sure we've overridden this entry, so we can write to
	 * it
	 */
	if (!menu_method_ensure_override (method, uri, error))
		return FALSE;

	/* Now resolve it */
	if (!menu_method_resolve_uri (method, uri,
				      &tree, &node, &real_path,
				      error))
		return FALSE;
	

        if (tree_p)
                *tree_p = tree;
        else
                desktop_entry_tree_unref (tree);
        
        if (node_p)
                *node_p = node;

	if (real_path_p)
		*real_path_p = real_path;
	else
		g_free (real_path);

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

	/* All dirs are considered 0755 always */
	info->permissions =
		GNOME_VFS_PERM_USER_ALL |
		GNOME_VFS_PERM_GROUP_READ |
		GNOME_VFS_PERM_GROUP_EXEC |
		GNOME_VFS_PERM_OTHER_READ |
		GNOME_VFS_PERM_OTHER_EXEC;
		
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

	/* We can always write to the file by creating the override
	 * copy in the user's home directory.  All files in the menu
	 * vfs are considered 0644, always.
	 */
	info->permissions =
		GNOME_VFS_PERM_USER_READ |
		GNOME_VFS_PERM_USER_WRITE |
		GNOME_VFS_PERM_GROUP_READ |
		GNOME_VFS_PERM_OTHER_READ;
	
	info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_TYPE |
		GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS;
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

	desktop_entry_tree_unref (tree);
	
        return retval;
}

static GnomeVFSResult
truncate_errno_to_vfs_result (int num)
{
	switch (num) {
	case EBADF:
	case EROFS:
		return GNOME_VFS_ERROR_READ_ONLY;
	case EINVAL:
		return GNOME_VFS_ERROR_NOT_SUPPORTED;
	default:
		return GNOME_VFS_ERROR_GENERIC;
	}
}

static GnomeVFSResult
menu_method_truncate (MenuMethod               *method,
		      GnomeVFSURI              *uri,
		      GnomeVFSFileSize          where,
		      GnomeVFSContext          *context)
{
        GnomeVFSResult retval;
        DesktopEntryTree *tree;
        DesktopEntryTreeNode *node;
	char *path;

	path = NULL;
	node = NULL;
	tree = NULL;

	retval = GNOME_VFS_OK;
	
        if (!menu_method_resolve_uri_writable (method, uri, FALSE,
					       &tree, &node,
					       &path, NULL))
                return GNOME_VFS_ERROR_NOT_FOUND; /* FIXME propagate GError ? */
	
        g_assert (tree != NULL);
	
	if (path == NULL) {
		retval = GNOME_VFS_ERROR_IS_DIRECTORY;
	} else {
		if (truncate (path, where) < 0)
			retval = truncate_errno_to_vfs_result (errno);
	}

	desktop_entry_tree_unref (tree);
	g_free (path);
	
        return retval;
}

static GnomeVFSResult
menu_method_move (MenuMethod               *method,
		  GnomeVFSURI              *old_uri,
		  GnomeVFSURI              *new_uri,
		  gboolean                  force_replace,
		  GnomeVFSContext          *context)
{


	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
menu_method_unlink (MenuMethod               *method,
		    GnomeVFSURI              *uri,
		    GnomeVFSContext          *context)
{
	const char *menu_file;
	char *menu_path;
	GnomeVFSResult retval;
	GError *tmp_error;
	
	retval = GNOME_VFS_OK;

	if (!unpack_uri (uri, &menu_file, &menu_path, NULL)) {
		return GNOME_VFS_ERROR_NOT_FOUND;
	}	
	
	menu_verbose ("Unlinking file %s path %s\n",
		      menu_file, menu_path);

	tmp_error = NULL;
	if (!desktop_entry_tree_cache_delete (method->cache,
					      menu_file,
					      menu_path,
					      &tmp_error)) {
		retval = GNOME_VFS_ERROR_GENERIC;
		menu_verbose ("Failed to delete item in tree cache: %s\n",
			      tmp_error->message);
		g_error_free (tmp_error);
	}

	g_free (menu_path);
	return retval;
}

static GnomeVFSResult
menu_method_mkdir (MenuMethod      *method,
		   GnomeVFSURI     *uri,
		   guint            perm,
		   GnomeVFSContext *context)
{
	const char *menu_file;
	char *menu_path;
	GnomeVFSResult retval;
	GError *tmp_error;
	
	retval = GNOME_VFS_OK;

	if (!unpack_uri (uri, &menu_file, &menu_path, NULL)) {
		return GNOME_VFS_ERROR_NOT_FOUND;
	}	
	
	menu_verbose ("Making directory in %s path %s\n",
		      menu_file, menu_path);

	tmp_error = NULL;
	if (!desktop_entry_tree_cache_mkdir (method->cache,
					     menu_file,
					     menu_path,
					     &tmp_error)) {
		if (g_error_matches (tmp_error,
				     G_FILE_ERROR,
				     G_FILE_ERROR_EXIST))
			retval = GNOME_VFS_ERROR_FILE_EXISTS;
		else
			retval = GNOME_VFS_ERROR_GENERIC;
		menu_verbose ("Failed to mkdir: %s\n",
			      tmp_error->message);
		g_error_free (tmp_error);
	}

	g_free (menu_path);
	return retval;
}

static GnomeVFSResult
menu_method_rmdir (MenuMethod      *method,
		   GnomeVFSURI     *uri,
		   GnomeVFSContext *context)
{
	const char *menu_file;
	char *menu_path;
	GnomeVFSResult retval;
	GError *tmp_error;
	
	retval = GNOME_VFS_OK;

	if (!unpack_uri (uri, &menu_file, &menu_path, NULL)) {
		return GNOME_VFS_ERROR_NOT_FOUND;
	}	
	
	menu_verbose ("Removing directory in %s path %s\n",
		      menu_file, menu_path);

	tmp_error = NULL;
	if (!desktop_entry_tree_cache_rmdir (method->cache,
					     menu_file,
					     menu_path,
					     &tmp_error)) {
		if (g_error_matches (tmp_error,
				     G_FILE_ERROR,
				     G_FILE_ERROR_NOENT))
			retval = GNOME_VFS_ERROR_NOT_FOUND;
		else
			retval = GNOME_VFS_ERROR_GENERIC;
		menu_verbose ("Failed to rmdir: %s\n",
			      tmp_error->message);
		g_error_free (tmp_error);
	}

	g_free (menu_path);
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

	desktop_entry_tree_unref (tree);
	
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
unix_flags_from_vfs_mode (GnomeVFSOpenMode mode,
			 int             *unix_flags)
{
	*unix_flags = 0;

	if (mode & GNOME_VFS_OPEN_READ) {
		if (mode & GNOME_VFS_OPEN_WRITE)
			*unix_flags = O_RDWR;
		else
			*unix_flags = O_RDONLY;
	} else {
		if (mode & GNOME_VFS_OPEN_WRITE)
			*unix_flags = O_WRONLY;
		else
			return FALSE; /* invalid mode - no read or write */
	}

	/* truncate file if we open for writing without random access */
	if ((!(mode & GNOME_VFS_OPEN_RANDOM)) &&
	    (mode & GNOME_VFS_OPEN_WRITE))
		*unix_flags |= O_TRUNC;

	return TRUE;
}

static GnomeVFSResult
unix_open (MenuMethod        *method,
	   GnomeVFSURI       *uri,
	   int                unix_flags,
	   mode_t             perms,
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

	if ((unix_flags & O_WRONLY) ||
	    (unix_flags & O_RDWR)) {		
		if (!menu_method_resolve_uri_writable (method, uri,
						       (unix_flags & O_CREAT) != 0,
						       &tree, &node,
						       &path, NULL))
			return GNOME_VFS_ERROR_NOT_FOUND; /* FIXME propagate GError ? */
	} else {
		if (!menu_method_resolve_uri (method, uri,
					      &tree, &node,
					      &path, NULL))
			return GNOME_VFS_ERROR_NOT_FOUND; /* FIXME propagate GError ? */
	}
	
        g_assert (tree != NULL);
	
	if (path == NULL) {
		retval = GNOME_VFS_ERROR_IS_DIRECTORY;
		goto out;
	}

 again:
	fd = open (path, unix_flags, perms);
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
	int unix_flags;
	
	if (!unix_flags_from_vfs_mode (mode, &unix_flags))
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;

	/* We hardcode 0644 always */
	return unix_open (method, uri, unix_flags, 0644, handle_p);
}

static GnomeVFSResult
file_handle_create (MenuMethod        *method,
		    GnomeVFSURI       *uri,
		    GnomeVFSOpenMode   mode,
		    FileHandle       **handle,
		    gboolean           exclusive,
		    mode_t             perms)
{
	int unix_flags;
	
	unix_flags = O_CREAT | O_TRUNC;
	
	if (!(mode & GNOME_VFS_OPEN_WRITE))
		return GNOME_VFS_ERROR_INVALID_OPEN_MODE;
	
	if (mode & GNOME_VFS_OPEN_READ)
		unix_flags |= O_RDWR;
	else
		unix_flags |= O_WRONLY;

	if (exclusive)
		unix_flags |= O_EXCL;

	return unix_open (method, uri, unix_flags, perms, handle);
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
file_handle_write (FileHandle               *handle,
		   gconstpointer             buffer,
		   GnomeVFSFileSize          num_bytes,
		   GnomeVFSFileSize         *bytes_written,
		   GnomeVFSContext          *context)
{
	int write_val;
	
	do {
		write_val = write (handle->fd, buffer, num_bytes);
	} while (write_val == -1
		 && errno == EINTR
		 && ! gnome_vfs_context_check_cancellation (context));
	
	if (write_val == -1) {
		*bytes_written = 0;
		return gnome_vfs_result_from_errno ();
	} else {
		*bytes_written = write_val;
		return GNOME_VFS_OK;
	}
}


static int
seek_position_to_unix (GnomeVFSSeekPosition position)
{
	switch (position) {
	case GNOME_VFS_SEEK_START:
		return SEEK_SET;
	case GNOME_VFS_SEEK_CURRENT:
		return SEEK_CUR;
	case GNOME_VFS_SEEK_END:
		return SEEK_END;
	default:
		g_warning ("Unknown GnomeVFSSeekPosition %d", position);
		return SEEK_SET; /* bogus */
	}
}

static GnomeVFSResult
file_handle_seek (FileHandle               *handle,
		  GnomeVFSSeekPosition      whence,
		  GnomeVFSFileOffset        offset,
		  GnomeVFSContext          *context)
{
	int lseek_whence;

	lseek_whence = seek_position_to_unix (whence);
	
	if (lseek (handle->fd, offset, lseek_whence) == -1) {
		if (errno == ESPIPE)
			return GNOME_VFS_ERROR_NOT_SUPPORTED;
		else
			return gnome_vfs_result_from_errno ();
	}

	return GNOME_VFS_OK;
}

static GnomeVFSResult
file_handle_tell (FileHandle               *handle,
		  GnomeVFSFileOffset       *offset_return)
{
	off_t offset;
	
	offset = lseek (handle->fd, 0, SEEK_CUR);
	if (offset == -1) {
		if (errno == ESPIPE)
			return GNOME_VFS_ERROR_NOT_SUPPORTED;
		else
			return gnome_vfs_result_from_errno ();
	}

	*offset_return = offset;
	return GNOME_VFS_OK;
}

static GnomeVFSResult
file_handle_truncate (FileHandle               *handle,
		      GnomeVFSFileSize          where,
		      GnomeVFSContext          *context)
{
	if (ftruncate (handle->fd, where) == 0) {
		return GNOME_VFS_OK;
	} else {
		return truncate_errno_to_vfs_result (errno);
	}
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

	/* We can always write to the file by creating the
	 * override copy in the user's home directory.
	 * All files are considered 0644 always.
	 */
	file_info->permissions =
		GNOME_VFS_PERM_USER_ALL |
		GNOME_VFS_PERM_GROUP_READ |
		GNOME_VFS_PERM_GROUP_EXEC |
		GNOME_VFS_PERM_OTHER_READ |
		GNOME_VFS_PERM_OTHER_EXEC;
		
	file_info->valid_fields = GNOME_VFS_FILE_INFO_FIELDS_TYPE;
	file_info->type = GNOME_VFS_FILE_TYPE_REGULAR;
	file_info->name = g_strdup (handle->name);

	if (options & GNOME_VFS_FILE_INFO_GET_MIME_TYPE) {
		file_info->mime_type = g_strdup ("application/x-gnome-app-info");
		file_info->valid_fields |= GNOME_VFS_FILE_INFO_FIELDS_MIME_TYPE;
	}
	
	return GNOME_VFS_OK;
}

