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
#include "postgresql.h"
#include "query_priv.h"
#include "connection_priv.h"
#include "async.h"
#include "pool.h"

static inline postgresql_conn_t *__postgresql_conn_create(duda_request_t *dr,
                                                          postgresql_connect_cb *cb)
{
    postgresql_conn_t *conn = monkey->mem_alloc(sizeof(postgresql_conn_t));
    if (!conn) {
        return NULL;
    }

    conn->dr                   = dr;
    conn->conn                 = NULL;
    conn->fd                   = 0;
    conn->connect_cb           = cb;
    conn->disconnect_cb        = NULL;
    conn->current_query        = NULL;
    conn->state                = CONN_STATE_CLOSED;
    conn->disconnect_on_finish = 0;
    conn->is_pooled            = 0;
    conn->pool                 = NULL;
    mk_list_init(&conn->queries);

    return conn;
}

static inline void __postgresql_conn_handle_connect(postgresql_conn_t *conn)
{
    if (!conn->conn) {
        FREE(conn);
        return;
    }

    if (PQstatus(conn->conn) == CONNECTION_BAD) {
        msg->err("PostgreSQL Connect Error: %s", PQerrorMessage(conn->conn));
        if (conn->connect_cb) {
            conn->connect_cb(conn, POSTGRESQL_ERR, conn->dr);
        }
        goto cleanup;
    }

    /* set soecket non-blocking mode */
    int ret = PQsetnonblocking(conn->conn, 1);
    if (ret == -1) {
        msg->err("PostgreSQL Set Non-blocking Error");
        goto cleanup;
    }

    conn->fd = PQsocket(conn->conn);

    int events = 0;
    int status = PQconnectPoll(conn->conn);

    if (status == PGRES_POLLING_FAILED) {
        msg->err("PostgreSQL Connect Error: %s", PQerrorMessage(conn->conn));
        if (conn->connect_cb) {
            conn->connect_cb(conn, POSTGRESQL_ERR, conn->dr);
        }
        goto cleanup;
    } else if (status == PGRES_POLLING_OK) {
        /* on connected */
        if (conn->connect_cb) {
            conn->connect_cb(conn, POSTGRESQL_OK, conn->dr);
        }
        conn->state = CONN_STATE_CONNECTED;
    } else {
        if (status & PGRES_POLLING_READING) {
            events |= DUDA_EVENT_READ;
        }
        if (status & PGRES_POLLING_WRITING) {
            events |= DUDA_EVENT_WRITE;
        }
        conn->state = CONN_STATE_CONNECTING;
    }

    event->add(conn->fd, events, DUDA_EVENT_LEVEL_TRIGGERED,
               postgresql_on_read, postgresql_on_write, postgresql_on_error,
               postgresql_on_close, postgresql_on_timeout, NULL);

    struct mk_list *conn_list = global->get(postgresql_conn_list);
    if (!conn_list) {
        conn_list = monkey->mem_alloc(sizeof(struct mk_list));
        if (!conn_list) {
            msg->err("PostgreSQL Connection List Init Error");
            goto cleanup;
        }
        mk_list_init(conn_list);
        global->set(postgresql_conn_list, (void *) conn_list);
    }
    mk_list_add(&conn->_head, conn_list);

    if (conn->state == CONN_STATE_CONNECTED) {
        postgresql_async_handle_query(conn);
    }

    return;

cleanup:
    PQfinish(conn->conn);
    FREE(conn);
}

postgresql_conn_t *postgresql_conn_connect(duda_request_t *dr, postgresql_connect_cb *cb,
                                           const char * const *keys,
                                           const char * const *values, int expand_dbname)
{
    postgresql_conn_t *conn = __postgresql_conn_create(dr, cb);
    if (!conn) {
        return NULL;
    }

    conn->conn = PQconnectStartParams(keys, values, expand_dbname);

    __postgresql_conn_handle_connect(conn);

    return conn;
}

postgresql_conn_t *postgresql_conn_connect_uri(duda_request_t *dr, postgresql_connect_cb *cb,
                                               const char *uri)
{
    postgresql_conn_t *conn = __postgresql_conn_create(dr, cb);
    if (!conn) {
        return NULL;
    }

    conn->conn = PQconnectStart(uri);

    __postgresql_conn_handle_connect(conn);

    return conn;
}

int postgresql_conn_send_query(postgresql_conn_t *conn, const char *query_str,
                               postgresql_query_result_cb *result_cb,
                               postgresql_query_row_cb *row_cb,
                               postgresql_query_end_cb *end_cb, void *privdata)
{
    postgresql_query_t *query = postgresql_query_init();
    if (!query) {
        msg->err("[FD %i] PostgreSQL Add Query Error", conn->fd);
        return POSTGRESQL_ERR;
    }
    mk_list_add(&query->_head, &conn->queries);

    query->query_str = monkey->str_dup(query_str);
    query->result_cb = result_cb;
    query->row_cb    = row_cb;
    query->end_cb    = end_cb;
    query->privdata  = privdata;
    query->type      = QUERY_TYPE_QUERY;

    if (conn->state == CONN_STATE_CONNECTED) {
        event->mode(conn->fd, DUDA_EVENT_WAKEUP, DUDA_EVENT_LEVEL_TRIGGERED);
        postgresql_async_handle_query(conn);
    }
    return POSTGRESQL_OK;
}

int postgresql_conn_send_query_params(postgresql_conn_t *conn, const char *query_str,
                                      int n_params, const char * const *params_values,
                                      const int *params_lengths, const int *params_formats,
                                      int result_format, postgresql_query_result_cb *result_cb,
                                      postgresql_query_row_cb *row_cb,
                                      postgresql_query_end_cb *end_cb, void *privdata)
{
    postgresql_query_t *query = postgresql_query_init();
    if (!query) {
        msg->err("[FD %i] PostgreSQL Add Query Error", conn->fd);
        return POSTGRESQL_ERR;
    }
    mk_list_add(&query->_head, &conn->queries);

    int i;
    query->query_str = monkey->str_dup(query_str);
    query->n_params  = n_params;

    if (params_values) {
        query->params_values = monkey->mem_alloc(sizeof(char *) * n_params);
        for (i = 0; i < n_params; ++i) {
            query->params_values[i] = monkey->str_dup(params_values[i]);
        }
    }

    if (params_lengths) {
        query->params_lengths = monkey->mem_alloc(sizeof(int) * n_params);
        for (i = 0; i < n_params; ++i) {
            query->params_lengths[i] = params_lengths[i];
        }
    }

    if (params_formats) {
        query->params_formats = monkey->mem_alloc(sizeof(int) * n_params);
        for (i = 0; i < n_params; ++i) {
            query->params_formats[i] = params_formats[i];
        }
    }

    query->result_format = result_format;
    query->result_cb     = result_cb;
    query->row_cb        = row_cb;
    query->end_cb        = end_cb;
    query->privdata      = privdata;
    query->type          = QUERY_TYPE_PARAMS;

    if (conn->state == CONN_STATE_CONNECTED) {
        event->mode(conn->fd, DUDA_EVENT_WAKEUP, DUDA_EVENT_LEVEL_TRIGGERED);
        postgresql_async_handle_query(conn);
    }
    return POSTGRESQL_OK;
}

int postgresql_conn_send_query_prepared(postgresql_conn_t *conn, const char *stmt_name,
                                        int n_params, const char * const *params_values,
                                        const int *params_lengths, const int *params_formats,
                                        int result_format, postgresql_query_result_cb *result_cb,
                                        postgresql_query_row_cb *row_cb,
                                        postgresql_query_end_cb *end_cb, void *privdata)
{
    postgresql_query_t *query = postgresql_query_init();
    if (!query) {
        msg->err("[FD %i] PostgreSQL Add Query Error", conn->fd);
        return POSTGRESQL_ERR;
    }
    mk_list_add(&query->_head, &conn->queries);

    int i;
    query->stmt_name = monkey->str_dup(stmt_name);
    query->n_params  = n_params;

    if (params_values) {
        query->params_values = monkey->mem_alloc(sizeof(char *) * n_params);
        for (i = 0; i < n_params; ++i) {
            query->params_values[i] = monkey->str_dup(params_values[i]);
        }
    }

    if (params_lengths) {
        query->params_lengths = monkey->mem_alloc(sizeof(int) * n_params);
        for (i = 0; i < n_params; ++i) {
            query->params_lengths[i] = params_lengths[i];
        }
    }

    if (params_formats) {
        query->params_formats = monkey->mem_alloc(sizeof(int) * n_params);
        for (i = 0; i < n_params; ++i) {
            query->params_formats[i] = params_formats[i];
        }
    }

    query->result_format = result_format;
    query->result_cb     = result_cb;
    query->row_cb        = row_cb;
    query->end_cb        = end_cb;
    query->privdata      = privdata;
    query->type          = QUERY_TYPE_PREPARED;

    if (conn->state == CONN_STATE_CONNECTED) {
        event->mode(conn->fd, DUDA_EVENT_WAKEUP, DUDA_EVENT_LEVEL_TRIGGERED);
        postgresql_async_handle_query(conn);
    }
    return POSTGRESQL_OK;
}

void postgresql_conn_handle_release(postgresql_conn_t *conn, int status)
{
    if (conn->is_pooled) {
        event->mode(conn->fd, DUDA_EVENT_SLEEP, DUDA_EVENT_LEVEL_TRIGGERED);
    } else {
        event->delete(conn->fd);
    }
    if (conn->disconnect_cb) {
        conn->disconnect_cb(conn, status, conn->dr);
    }
    if (conn->is_pooled) {
        postgresql_pool_reclaim_conn(conn);
    } else {
        conn->state = CONN_STATE_CLOSED;
        mk_list_del(&conn->_head);
        PQfinish(conn->conn);
        while (mk_list_is_empty(&conn->queries) != 0) {
            postgresql_query_t *query = mk_list_entry_first(&conn->queries,
                                                            postgresql_query_t, _head);
            postgresql_query_free(query);
        }
        FREE(conn);
    }
}

void postgresql_conn_disconnect(postgresql_conn_t *conn, postgresql_disconnect_cb *cb)
{
    conn->disconnect_cb = cb;
    if (conn->state != CONN_STATE_CONNECTED) {
        conn->disconnect_on_finish = 1;
        return;
    }
    postgresql_conn_handle_release(conn, POSTGRESQL_OK);
}
