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

#ifndef MENU_ENTRIES_H
#define MENU_ENTRIES_H

#include <glib.h>

typedef enum
{
  ENTRY_ERROR_BAD_PATH,
  ENTRY_ERROR_FAILED
} EntryError;

GQuark entry_error_quark (void);

#define ENTRY_ERROR entry_error_quark ()

typedef enum
{
  ENTRY_DESKTOP,
  ENTRY_DIRECTORY
} EntryType;

typedef enum
{
  ENTRY_LOAD_LEGACY      = 1 << 0,
  ENTRY_LOAD_DESKTOPS    = 1 << 1,
  ENTRY_LOAD_DIRECTORIES = 1 << 2
} EntryLoadFlags;

typedef struct _Entry Entry;
typedef struct _EntryDirectory EntryDirectory;
typedef struct _EntryDirectoryList EntryDirectoryList;

/* returns a new ref, never has Legacy category, relative path is entry basename */
Entry* entry_get_by_absolute_path (const char *path);

EntryDirectory* entry_directory_load  (const char     *path,
                                       EntryLoadFlags  flags,
                                       GError        **err);
void            entry_directory_ref   (EntryDirectory *dir);
void            entry_directory_unref (EntryDirectory *dir);

/* return a new ref */
Entry* entry_directory_get_desktop   (EntryDirectory *dir,
                                      const char     *relative_path);
Entry* entry_directory_get_directory (EntryDirectory *dir,
                                      const char     *relative_path);

GSList* entry_directory_get_all_desktops (EntryDirectory *dir);
GSList* entry_directory_get_by_category  (EntryDirectory *dir,
                                          const char     *category);

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

GSList* entry_directory_list_get_all_desktops (EntryDirectoryList *list);
GSList* entry_directory_list_get_by_category  (EntryDirectoryList *list,
                                               const char         *category);


void entry_ref   (Entry *entry);
void entry_unref (Entry *entry);

const char* entry_get_absolute_path (Entry      *entry);
const char* entry_get_relative_path (Entry      *entry);
gboolean    entry_has_category      (Entry      *entry,
                                     const char *category);

void entry_set_only_show_in_name (const char *name);
                   
#endif /* MENU_ENTRIES_H */
