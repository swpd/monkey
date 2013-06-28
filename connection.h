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

#ifndef MARIADB_CONNECTION_H
#define MARIADB_CONNECTION_H

#include <mysql/mysql.h>
#include "query.h"

#define MARIADB_OK 0
#define MARIADB_ERR -1

typedef enum {
    CONN_STATE_NULL, CONN_STATE_CLOSE, CONN_STATE_CLOSED,
    CONN_STATE_CONNECT, CONN_STATE_CONNECTING, CONN_STATE_CONNECTED,
    CONN_STATE_QUERY, CONN_STATE_QUERYING, CONN_STATE_QUERIED,
    CONN_STATE_ROW_STREAM, CONN_STATE_ROW_STREAMING, CONN_STATE_ROW_STREAMED,
    CONN_STATE_NEXTQUERY, CONN_STATE_NEXTQUERYING,
    CONN_STATE_RESULT_FREE, CONN_STATE_RESULT_FREEING, CONN_STATE_RESULT_FREED,
    CONN_STATE_ERR_ROW, CONN_STATE_ERR_QUERY, CONN_STATE_ABORT
} mariadb_conn_state_t;

typedef struct mariadb_conn_config {
    char *user;
    char *password;
    char *ip;
    char *db;
    char *unix_socket;
    unsigned int port;
    unsigned long client_flag;

    //ssl config
    char *ssl_key;
    char *ssl_cert;
    char *ssl_ca;
    char *ssl_capath;
    char *ssl_cipher;
} mariadb_conn_config_t;

struct mariadb_conn; /* forward declaration */
typedef void (mariadb_connect_cb)(struct mariadb_conn *conn, int status);
typedef void (mariadb_disconnect_cb)(struct mariadb_conn *conn, int status);

typedef struct mariadb_conn {
    mariadb_conn_config_t config;
    MYSQL *mysql, *mysql_ret;
    int fd;
    mariadb_conn_state_t state;

    mariadb_connect_cb *connect_cb;
    mariadb_disconnect_cb *disconnect_cb;

    struct mk_list queries;
} mariadb_conn_t;

static inline int mariadb_set_connect_cb(mariadb_conn_t *conn, mariadb_connect_cb *cb)
{
    if (!conn->connect_cb)
    {
        conn->connect_cb = cb;
        return MARIADB_OK;
    }
    return MARIADB_ERR;
}

static inline int mariadb_set_disconnect_cb(mariadb_conn_t *conn, mariadb_disconnect_cb *cb)
{
    if (!conn->disconnect_cb)
    {
        conn->disconnect_cb = cb;
        return MARIADB_OK;
    }
    return MARIADB_ERR;
}

int mariadb_connect(mariadb_conn_t *conn);
int mariadb_disconnect(mariadb_conn_t *conn);
int mariadb_conn_add_query(mariadb_conn_t *conn, struct duda_request *dr,
                           char *query_str, mariadb_query_cb *cb, void *privdata);

#endif
