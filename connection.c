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

#include <mysql.h>
#include "common.h"
#include "query.h"
#include "query_priv.h"
#include "pool.h"
#include "connection.h"
#include "connection_priv.h"

mariadb_conn_t *mariadb_conn_init(duda_request_t *dr, const char *user,
                                  const char *password, const char *ip,
                                  const char *db, unsigned int port,
                                  const char *unix_socket, unsigned long client_flag)
{
    mariadb_conn_t *conn = monkey->mem_alloc(sizeof(mariadb_conn_t));
    if (!conn) {
        return NULL;
    }

    conn->dr                   = dr;
    conn->config.user          = monkey->str_dup(user);
    conn->config.password      = monkey->str_dup(password);
    conn->config.ip            = monkey->str_dup(ip);
    conn->config.db            = monkey->str_dup(db);
    conn->config.port          = port;
    conn->config.unix_socket   = monkey->str_dup(unix_socket);
    conn->config.client_flag   = client_flag;
    conn->config.ssl_key       = NULL;
    conn->config.ssl_cert      = NULL;
    conn->config.ssl_ca        = NULL;
    conn->config.ssl_capath    = NULL;
    conn->config.ssl_cipher    = NULL;
    conn->mysql_ret            = NULL;
    conn->fd                   = 0;
    conn->state                = CONN_STATE_CLOSED;
    conn->connect_cb           = NULL;
    conn->disconnect_cb        = NULL;
    conn->current_query        = NULL;
    conn->disconnect_on_finish = 0;
    conn->is_pooled            = 0;
    conn->pool                 = NULL;
    mysql_init(&conn->mysql);

    mysql_options(&conn->mysql, MYSQL_OPT_NONBLOCK, 0);
    mk_list_init(&conn->queries);

    return conn;
}

/* This function should be called before mariadb_connect */
void mariadb_conn_ssl_set(mariadb_conn_t *conn, const char *key, const char *cert,
                          const char *ca, const char *capath, const char *cipher)
{
    conn->config.ssl_key    = monkey->str_dup(key);
    conn->config.ssl_cert   = monkey->str_dup(cert);
    conn->config.ssl_ca     = monkey->str_dup(ca);
    conn->config.ssl_capath = monkey->str_dup(capath);
    if (cipher) {
        conn->config.ssl_cipher = monkey->str_dup(cipher);
    } else {
        conn->config.ssl_cipher = DEFAULT_CIPHER;
    }
    mysql_ssl_set(&conn->mysql, conn->config.ssl_key, conn->config.ssl_cert,
                  conn->config.ssl_ca, conn->config.ssl_capath,
                  conn->config.ssl_cipher);
}

int mariadb_conn_add_query(mariadb_conn_t *conn, const char *query_str,
                           mariadb_query_result_cb *result_cb,
                           mariadb_query_row_cb *row_cb, void *row_cb_privdata,
                           mariadb_query_end_cb *end_cb)
{
    mariadb_query_t *query = mariadb_query_init(query_str, result_cb, row_cb,
                                                row_cb_privdata, end_cb);
    if (!query)
        return MARIADB_ERR;
    mk_list_add(&query->_head, &conn->queries);
    return MARIADB_OK;
}

void mariadb_conn_free(mariadb_conn_t *conn)
{
    FREE(conn->config.user);
    FREE(conn->config.password);
    FREE(conn->config.ip);
    FREE(conn->config.db);
    FREE(conn->config.unix_socket);
    FREE(conn->config.ssl_key);
    FREE(conn->config.ssl_cert);
    FREE(conn->config.ssl_ca);
    FREE(conn->config.ssl_capath);
    FREE(conn->config.ssl_cipher);
    while (mk_list_is_empty(&conn->queries) != 0) {
        mariadb_query_t *query = mk_list_entry_first(&conn->queries,
                                                     mariadb_query_t, _head);
        mariadb_query_free(query);
    }
    FREE(conn);
}
