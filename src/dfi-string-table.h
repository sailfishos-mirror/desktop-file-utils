/*
 * Copyright © 2013 Canonical Limited
 *
 * update-desktop-database is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * update-desktop-database is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with update-desktop-database; see the file COPYING.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite
 * 330, Boston, MA 02111-1307, USA.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#ifndef __dfi_string_table_h__
#define __dfi_string_table_h__

#include <glib.h>

typedef GHashTable DfiStringTables;
typedef GHashTable DfiStringTable;

DfiStringTables *       dfi_string_tables_create                        (void);

DfiStringTable *        dfi_string_tables_get_table                     (DfiStringTables *string_tables,
                                                                         const gchar     *locale);

void                    dfi_string_tables_add_string                    (DfiStringTables *string_tables,
                                                                         const gchar     *locale,
                                                                         const gchar     *string);

void                    dfi_string_table_add_string                     (DfiStringTable  *string_table,
                                                                         const gchar     *string);

guint                   dfi_string_tables_get_offset                    (DfiStringTable  *string_table,
                                                                         const gchar     *locale,
                                                                         const gchar     *string);

guint                   dfi_string_table_get_offset                     (DfiStringTable  *string_table,
                                                                         const gchar     *string);

gboolean                dfi_string_table_is_written                     (DfiStringTable  *string_table);

void                    dfi_string_table_write                          (DfiStringTable  *string_table,
                                                                         DfiStringTable  *shared_table,
                                                                         GString         *file);

#endif /* __dfi_string_table_h__ */
