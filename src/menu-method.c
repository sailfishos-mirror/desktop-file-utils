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

static DesktopEntryTree*
find_menu_file (const char *name)
{
        
	
        

}

static GnomeVFSResult
convert_uri (GnomeVFSURI       *uri,
             DesktopEntryTree **entry_tree,
             char             **path)
{
        const char *scheme;
        char *unescaped;
        const char *menu_file;
        
        scheme = gnome_vfs_uri_get_scheme (uri);
        
        if (strcmp (scheme, "applications") == 0) {


        }

        unescaped = gnome_vfs_unescape_string (uri->text, "");
        
        g_free (unescaped);
}

static GnomeVFSResult
do_open (GnomeVFSMethod        *method,
         GnomeVFSMethodHandle **method_handle,
         GnomeVFSURI           *uri,
         GnomeVFSOpenMode       mode,
         GnomeVFSContext       *context)
{
        
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_create (GnomeVFSMethod        *method,
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
do_close (GnomeVFSMethod       *method,
          GnomeVFSMethodHandle *method_handle,
          GnomeVFSContext      *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_read (GnomeVFSMethod       *method,
         GnomeVFSMethodHandle *method_handle,
         gpointer              buffer,
         GnomeVFSFileSize      num_bytes,
         GnomeVFSFileSize     *bytes_read,
         GnomeVFSContext      *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_write (GnomeVFSMethod       *method,
          GnomeVFSMethodHandle *method_handle,
          gconstpointer         buffer,
          GnomeVFSFileSize      num_bytes,
          GnomeVFSFileSize     *bytes_written,
          GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_seek (GnomeVFSMethod       *method,
         GnomeVFSMethodHandle *method_handle,
         GnomeVFSSeekPosition  whence,
         GnomeVFSFileOffset    offset,
         GnomeVFSContext      *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_tell (GnomeVFSMethod       *method,
         GnomeVFSMethodHandle *method_handle,
         GnomeVFSFileOffset   *offset_return)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}


static GnomeVFSResult
do_truncate_handle (GnomeVFSMethod       *method,
                    GnomeVFSMethodHandle *method_handle,
                    GnomeVFSFileSize      where,
                    GnomeVFSContext      *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_truncate (GnomeVFSMethod   *method,
             GnomeVFSURI      *uri,
             GnomeVFSFileSize  where,
             GnomeVFSContext  *context)
{

        return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_open_directory (GnomeVFSMethod           *method,
                   GnomeVFSMethodHandle    **method_handle,
                   GnomeVFSURI              *uri,
                   GnomeVFSFileInfoOptions   options,
                   GnomeVFSContext          *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_close_directory (GnomeVFSMethod       *method,
		    GnomeVFSMethodHandle *method_handle,
		    GnomeVFSContext      *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_read_directory (GnomeVFSMethod       *method,
		   GnomeVFSMethodHandle *method_handle,
		   GnomeVFSFileInfo     *file_info,
		   GnomeVFSContext      *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_get_file_info (GnomeVFSMethod         *method,
		  GnomeVFSURI            *uri,
		  GnomeVFSFileInfo       *file_info,
		  GnomeVFSFileInfoOptions options,
		  GnomeVFSContext        *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_get_file_info_from_handle (GnomeVFSMethod         *method,
			      GnomeVFSMethodHandle   *method_handle,
			      GnomeVFSFileInfo       *file_info,
			      GnomeVFSFileInfoOptions options,
			      GnomeVFSContext        *context)
{
	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static gboolean
do_is_local (GnomeVFSMethod    *method,
	     const GnomeVFSURI *uri)
{

        return TRUE;
}


static GnomeVFSResult
do_make_directory (GnomeVFSMethod  *method,
		   GnomeVFSURI     *uri,
		   guint            perm,
		   GnomeVFSContext *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_remove_directory (GnomeVFSMethod  *method,
		     GnomeVFSURI     *uri,
		     GnomeVFSContext *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_find_directory (GnomeVFSMethod           *method,
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
do_move (GnomeVFSMethod  *method,
	 GnomeVFSURI     *old_uri,
	 GnomeVFSURI     *new_uri,
	 gboolean         force_replace,
	 GnomeVFSContext *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_unlink (GnomeVFSMethod  *method,
	   GnomeVFSURI     *uri,
	   GnomeVFSContext *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_create_symbolic_link (GnomeVFSMethod  *method,
			 GnomeVFSURI     *uri,
			 const char      *target_reference,
			 GnomeVFSContext *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_check_same_fs (GnomeVFSMethod  *method,
                  GnomeVFSURI     *source_uri,
                  GnomeVFSURI     *target_uri,
                  gboolean        *same_fs_return,
                  GnomeVFSContext *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_set_file_info (GnomeVFSMethod          *method,
                  GnomeVFSURI             *uri,
                  const GnomeVFSFileInfo  *info,
                  GnomeVFSSetFileInfoMask  mask,
                  GnomeVFSContext         *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_monitor_add (GnomeVFSMethod        *method,
                GnomeVFSMethodHandle **method_handle_return,
                GnomeVFSURI           *uri,
                GnomeVFSMonitorType    monitor_type)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_monitor_cancel (GnomeVFSMethod       *method,
		   GnomeVFSMethodHandle *method_handle)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSResult
do_file_control (GnomeVFSMethod       *method,
                 GnomeVFSMethodHandle *method_handle,
                 const char           *operation,
                 gpointer              operation_data,
                 GnomeVFSContext      *context)
{

	return GNOME_VFS_ERROR_NOT_SUPPORTED;
}

static GnomeVFSMethod method = {
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
	return &method;
}

void
vfs_module_shutdown (GnomeVFSMethod *method)
{
}
