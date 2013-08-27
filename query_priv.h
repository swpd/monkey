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

#ifndef POSTGRESQL_QUERY_PRIV_H
#define POSTGRESQL_QUERY_PRIV_H

#include "query.h"

typedef enum {
    QUERY_TYPE_NULL, QUERY_TYPE_QUERY, QUERY_TYPE_PARAMS, QUERY_TYPE_PREPARED,
} postgresql_query_type_t;

typedef enum {
    QUERY_ABORT_NO, QUERY_ABORT_YES,
} postgresql_query_abort_t;

struct postgresql_query {
    char *query_str;
    PGresult *result;
    int n_fields;
    char **fields;
    char **values;
    postgresql_query_abort_t abort;

    postgresql_query_type_t type;
    int single_row_mode;
    int result_start;

    /* fields used by query_params and query_prepared */
    char *stmt_name;
    int n_params;
    char **params_values;
    int *params_lengths;
    int *params_formats; /* 0 for text, 1 for binary */
    int result_format;

    postgresql_query_result_cb *result_cb;
    postgresql_query_row_cb *row_cb;
    postgresql_query_end_cb *end_cb;
    void *privdata;

    struct mk_list _head;
};

postgresql_query_t *postgresql_query_init();

void postgresql_query_free(postgresql_query_t *query);

/*
 * @METHOD_NAME: abort
 * @METHOD_DESC: Abort a query.
 * @METHOD_PROTO: void abort(postgresql_query_t *query)
 * @METHOD_PARAM: query The query to be aborted.
 * @METHOD_RETURN: None.
 */

static inline void postgresql_query_abort(postgresql_query_t *query)
{
    query->abort = QUERY_ABORT_YES;
}

#endif
