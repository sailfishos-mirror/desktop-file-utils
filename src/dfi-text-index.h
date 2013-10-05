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

#ifndef __dfi_text_index_h__
#define __dfi_text_index_h__

#include "dfi-string-table.h"
#include "dfi-id-list.h"

typedef struct _DfiTextIndex                                DfiTextIndex;

DfiTextIndex *          dfi_text_index_new                              (void);

void                    dfi_text_index_free                             (gpointer         text_index);

void                    dfi_text_index_add_ids                          (DfiTextIndex    *text_index,
                                                                         const gchar     *token,
                                                                         const guint16   *ids,
                                                                         gint             n_ids);

void                    dfi_text_index_add_ids_tokenised                (DfiTextIndex    *text_index,
                                                                         const gchar     *string_to_tokenise,
                                                                         const guint16   *ids,
                                                                         gint             n_ids);

void                    dfi_text_index_convert                          (DfiTextIndex    *text_index);

const gchar * const *   dfi_text_index_get_tokens                       (DfiTextIndex    *text_index,
                                                                         guint           *n_tokens);

DfiIdList *             dfi_text_index_get_id_list_for_token            (DfiTextIndex    *text_index,
                                                                         const gchar     *token);

void                    dfi_text_index_populate_strings                 (DfiTextIndex    *text_index,
                                                                         DfiStringTable  *string_table);

#endif /* __dfi_text_index_h__ */
