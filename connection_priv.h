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

#ifndef POSTGRESQL_CONNECTION_PRIV_H
#define POSTGRESQL_CONNECTION_PRIV_H

#include "connection.h"

typedef enum {
    CONN_STATE_CLOSED,
    CONN_STATE_CONNECTING, CONN_STATE_CONNECTED,
    CONN_STATE_QUERYING, CONN_STATE_QUERIED,
    CONN_STATE_ROW_FETCHING, CONN_STATE_ROW_FETCHED,
} postgresql_conn_state_t;

struct postgresql_conn {
    struct duda_request *dr;
    PGconn *conn;
    int fd;
    postgresql_conn_state_t state;

    postgresql_connect_cb *connect_cb;
    postgresql_disconnect_cb *disconnect_cb;

    postgresql_query_t *current_query;
    int disconnect_on_finish;

    struct mk_list queries;
    struct mk_list _head;
};

postgresql_conn_t *postgresql_conn_connect(duda_request_t *dr, postgresql_connect_cb *cb,
                                           const char * const *keys,
                                           const char * const *values, int expand_dbname);

postgresql_conn_t *postgresql_conn_connect_url(duda_request_t *dr, postgresql_connect_cb *cb,
                                               const char *url);

int postgresql_conn_send_query(postgresql_conn_t *conn, const char *query_str,
                               postgresql_query_result_cb *result_cb,
                               postgresql_query_row_cb *row_cb,
                               postgresql_query_end_cb *end_cb, void *privdata);

int postgresql_conn_send_query_params(postgresql_conn_t *conn, const char *query_str,
                                      int n_params, const char * const *params_values,
                                      const int *params_lengths, const int *params_formats,
                                      int result_format, postgresql_query_result_cb *result_cb,
                                      postgresql_query_row_cb *row_cb,
                                      postgresql_query_end_cb *end_cb, void *privdata);

int postgresql_conn_send_query_prepared(postgresql_conn_t *conn, const char *stmt_name,
                                         int n_params, const char * const *params_values,
                                         const int *parmas_lengths, const int *parmas_formats,
                                         int result_format, postgresql_query_result_cb *result_cb,
                                         postgresql_query_row_cb *row_cb,
                                         postgresql_query_end_cb *end_cb, void *privdata);

void postgresql_conn_handle_release(postgresql_conn_t *conn, int status);

void postgresql_conn_disconnect(postgresql_conn_t *conn, postgresql_disconnect_cb *cb);

#endif
