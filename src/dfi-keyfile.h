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

#ifndef __dfi_keyfile_h__
#define __dfi_keyfile_h__

#include <glib.h>

typedef struct _DfiKeyfile                                  DfiKeyfile;

DfiKeyfile *            dfi_keyfile_new                                 (const gchar          *filename,
                                                                         GError              **error);

void                    dfi_keyfile_free                                (DfiKeyfile           *keyfile);

const gchar *           dfi_keyfile_get_value                           (DfiKeyfile           *keyfile,
                                                                         const gchar * const  *locale_variants,
                                                                         const gchar          *group_name,
                                                                         const gchar          *key);

guint                   dfi_keyfile_get_n_groups                        (DfiKeyfile           *keyfile);

guint                   dfi_keyfile_get_n_items                         (DfiKeyfile           *keyfile);

const gchar *           dfi_keyfile_get_group_name                      (DfiKeyfile           *keyfile,
                                                                         guint                 group);

void                    dfi_keyfile_get_group_range                     (DfiKeyfile           *keyfile,
                                                                         guint                 group,
                                                                         guint                *start,
                                                                         guint                *end);

void                    dfi_keyfile_get_item                            (DfiKeyfile           *keyfile,
                                                                         guint                 item,
                                                                         const gchar         **key,
                                                                         const gchar         **locale,
                                                                         const gchar         **value);

#endif /* __dfi_keyfile_h__ */
