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

/*
 * @METHOD_NAME: connect
 * @METHOD_DESC: Establish a new connection to the PostgreSQL server with the given parameters.
 * @METHOD_PROTO: postgresql_conn_t *connect(duda_request_t *dr, postgresql_connect_cb *cb, const char * const *keys, const char * const *values, int expand_dbname)
 * @METHOD_PARAM: dr The request context information hold by a duda_request_t type.
 * @METHOD_PARAM: cb The callback function that will take actions when a connection success or fail to establish.
 * @METHOD_PARAM: keys A NULL-terminated string array that stands for the keywords you want to customize. If the array is empty, then it will try to connect the server with default paramter keywords. If any keyword is unspecified, then the corresponding environment variable is checked. The currently recognized parameter key words are listed <a href="http://www.postgresql.org/docs/9.2/static/libpq-connect.html#LIBPQ-PARAMKEYWORDS">here</a>.
 * @METHOD_PARAM: values A NULL-terminated strin array that gives the corresponding values for each keyword in the keys array.
 * @METHOD_PARAM: expand_dbname A flag that determines whether the dbname key word value is allowed to be recognized as a connection string. Use zero to disable it or non-zero to enable it.
 * @METHOD_RETURN: A newly initialized PostgreSQL connection handle on success, or NULL on failure.
 */

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

/*
 * @METHOD_NAME: connect_uri
 * @METHOD_DESC: Establish a new connection to the PostgreSQL server with the given connection string.
 * @METHOD_PROTO: postgresql_conn_t *connect_uri(duda_request_t *dr, postgresql_connect_cb *cb, const char *uri)
 * @METHOD_PARAM: dr The request context information hold by a duda_request_t type.
 * @METHOD_PARAM: cb The callback function that will take actions when a connection success or fail to establish.
 * @METHOD_PARAM: uri The string which specifies the connection parameters and their values. There are two accepted formats for these strings: plain keyword = value strings and RFC 3986 URIs. For full reference of the connection strings please consult the official <a href="http://www.postgresql.org/docs/9.2/static/libpq-connect.html#LIBPQ-CONNSTRING">documentation</a> of PostgreSQL.
 * @METHOD_RETURN: A newly initialized PostgreSQL connection handle on success, or NULL on failure.
 */

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

/*
 * @METHOD_NAME: query
 * @METHOD_DESC: Enqueue a new query to a PostgreSQL connection.
 * @METHOD_PROTO: int query(postgresql_conn_t *conn, const char *query_str, postgresql_query_result_cb *result_cb, postgresql_query_row_cb *row_cb, postgresql_query_end_cb *end_cb, void *privdata)
 * @METHOD_PARAM: conn The PostgreSQL connection handle.
 * @METHOD_PARAM: query_str The SQL statement string of this query.
 * @METHOD_PARAM: result_cb The callback function that will take actions when the result set of this query is available.
 * @METHOD_PARAM: row_cb The callback function that will take actions when every row of the result set is fetched.
 * @METHOD_PARAM: end_cb The callback function that will take actions after all the row in the result set are fetched.
 * @METHOD_PARAM: privdata The user defined private data that will be passed to callback.
 * @METHOD_RETURN: POSTGRESQL_OK on success, or POSTGRESQL_ERR on failure.
 */

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

/*
 * @METHOD_NAME: query_params
 * @METHOD_DESC: Enqueue a new query to a PostgreSQL connection, with the ability to pass parameters separately from the SQL command text.
 * @METHOD_PROTO: int query_params(postgresql_conn_t *conn, const char *query_str, int n_params, const char * const *params_values, const int *params_lengths, const int *params_formats, int result_format, postgresql_query_result_cb *result_cb, postgresql_query_row_cb *row_cb, postgresql_query_end_cb *end_cb, void *privdata)
 * @METHOD_PARAM: conn The PostgreSQL connection handle.
 * @METHOD_PARAM: query_str The SQL statement string of this query. If parameters are used, they are referred to in the command string as $1, $2, etc.
 * @METHOD_PARAM: n_params The number of parameters supplied; it is the length of the arrays paramValues[], paramLengths[], and paramFormats[]. (The array pointers can be NULL when nParams is zero.)
 * @METHOD_PARAM: params_values The actual values of the parameters. A null pointer in this array means the corresponding parameter is null; otherwise the pointer points to a null-terminated text string (for text format) or binary data in the format expected by the server (for binary format).
 * @METHOD_PARAM: params_lengths The actual data lengths of binary-format parameters. It is ignored for null parameters and text-format parameters. The array pointer can be null when there are no binary parameters.
 * @METHOD_PARAM: params_formats This array specifies whether parameters are text (put a zero in the array entry for the corresponding parameter) or binary (put a one in the array entry for the corresponding parameter). If the array pointer is null then all parameters are presumed to be text strings.
 * @METHOD_PARAM: result_format A flag that determines to fetch results in text or binary format. Use zero to obtain results in text format, or one to obtain results in binary format.
 * @METHOD_PARAM: result_cb The callback function that will take actions when the result set of this query is available.
 * @METHOD_PARAM: row_cb The callback function that will take actions when every row of the result set is fetched.
 * @METHOD_PARAM: end_cb The callback function that will take actions after all the row in the result set are fetched.
 * @METHOD_PARAM: privdata The user defined private data that will be passed to callback.
 * @METHOD_RETURN: POSTGRESQL_OK on success, or POSTGRESQL_ERR on failure.
 */

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

/*
 * @METHOD_NAME: query_prepared
 * @METHOD_DESC: Enqueue a new query to a PostgreSQL connection. This method is like method query, but the command to be executed is specified by naming a previously-prepared statement, instead of giving a query string. This feature allows commands that will be used repeatedly to be parsed and planned just once, rather than each time they are executed. The statement must have been prepared previously in the current session.
 * @METHOD_PROTO: int query_prepared(postgresql_conn_t *conn, const char *stmt_name, int n_params, const char * const *params_values, const int *params_lengths, const int *params_formats, int result_format, postgresql_query_result_cb *result_cb, postgresql_query_row_cb *row_cb, postgresql_query_end_cb *end_cb, void *privdata)
 * @METHOD_PARAM: conn The PostgreSQL connection handle.
 * @METHOD_PARAM: stmt_name The name of a prepared statement.
 * @METHOD_PARAM: n_params The number of parameters supplied; it is the length of the arrays paramValues[], paramLengths[], and paramFormats[]. (The array pointers can be NULL when nParams is zero.)
 * @METHOD_PARAM: params_values The actual values of the parameters. A null pointer in this array means the corresponding parameter is null; otherwise the pointer points to a null-terminated text string (for text format) or binary data in the format expected by the server (for binary format).
 * @METHOD_PARAM: params_lengths The actual data lengths of binary-format parameters. It is ignored for null parameters and text-format parameters. The array pointer can be null when there are no binary parameters.
 * @METHOD_PARAM: params_formats This array specifies whether parameters are text (put a zero in the array entry for the corresponding parameter) or binary (put a one in the array entry for the corresponding parameter). If the array pointer is null then all parameters are presumed to be text strings.
 * @METHOD_PARAM: result_format A flag that determines to fetch results in text or binary format. Use zero to obtain results in text format, or one to obtain results in binary format.
 * @METHOD_PARAM: result_cb The callback function that will take actions when the result set of this query is available.
 * @METHOD_PARAM: row_cb The callback function that will take actions when every row of the result set is fetched.
 * @METHOD_PARAM: end_cb The callback function that will take actions after all the row in the result set are fetched.
 * @METHOD_PARAM: privdata The user defined private data that will be passed to callback.
 * @METHOD_RETURN: POSTGRESQL_OK on success, or POSTGRESQL_ERR on failure.
 */

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

/*
 * @METHOD_NAME: disconnect
 * @METHOD_DESC: Disconnect a previous opened connection and release all the resource with it. It will ensure that all previous enqueued queries of that connection are processed before it is disconnected.
 * @METHOD_PROTO: void disconnect(postgresql_conn_t *conn, postgresql_disconnect_cb *cb)
 * @METHOD_PARAM: conn The PostgreSQL connection handle, it must be a valid, open connection.
 * @METHOD_PARAM: cb The callback function that will take actions when a connection is disconnected.
 * @METHOD_RETURN: None.
 */

void postgresql_conn_disconnect(postgresql_conn_t *conn, postgresql_disconnect_cb *cb)
{
    conn->disconnect_cb = cb;
    if (conn->state != CONN_STATE_CONNECTED) {
        conn->disconnect_on_finish = 1;
        return;
    }
    postgresql_conn_handle_release(conn, POSTGRESQL_OK);
}
