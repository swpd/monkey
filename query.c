/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Duda I/O
 *  --------
 *  Copyright (C) 2013, Zeying Xie <swpdtz at gmail dot com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <libpq-fe.h>
#include "common.h"
#include "query_priv.h"

postgresql_query_t *postgresql_query_init()
{
    postgresql_query_t *query = monkey->mem_alloc(sizeof(postgresql_query_t));
    if (!query) {
        return NULL;
    }

    query->query_str       = NULL;
    query->n_fields        = 0;
    query->fields          = NULL;
    query->values          = NULL;
    query->abort           = QUERY_ABORT_NO;
    query->type            = QUERY_TYPE_NULL;
    query->single_row_mode = 0;
    query->result_start    = 0;
    query->stmt_name       = NULL;
    query->n_params        = 0;
    query->params_values   = NULL;
    query->params_lengths  = NULL;
    query->params_formats  = NULL;
    query->result_format   = 0;
    query->result_cb       = NULL;
    query->row_cb          = NULL;
    query->end_cb          = NULL;
    query->privdata        = NULL;
    query->result          = NULL;
    return query;
}

void postgresql_query_free(postgresql_query_t *query)
{
    mk_list_del(&query->_head);
    int i;
    FREE(query->query_str);
    FREE(query->stmt_name);
    for (i = 0; i < query->n_params; ++i) {
        FREE(query->params_values[i]);
    }
    FREE(query->params_values);
    FREE(query->params_lengths);
    FREE(query->params_formats);
    FREE(query);
}
