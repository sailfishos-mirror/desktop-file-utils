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

#ifndef __dfi_string_list_h__
#define __dfi_string_list_h__

#include "dfi-string-table.h"

typedef struct _DfiStringList                               DfiStringList;

DfiStringList *         dfi_string_list_new                             (void);

void                    dfi_string_list_free                            (DfiStringList  *string_list);

void                    dfi_string_list_ensure                          (DfiStringList  *string_list,
                                                                         const gchar    *string);

void                    dfi_string_list_convert                         (DfiStringList  *string_list);

void                    dfi_string_list_populate_strings                (DfiStringList  *string_list,
                                                                         DfiStringTable *string_table);

const gchar * const *   dfi_string_list_get_strings                     (DfiStringList  *string_list,
                                                                         guint          *n_strings);

guint                   dfi_string_list_get_id                          (DfiStringList  *string_list,
                                                                         const gchar    *string);

#endif /* __dfi_string_list_h__ */
