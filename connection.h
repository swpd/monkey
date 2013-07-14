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

#include "common.h"
#include "query.h"

typedef enum {
    CONN_STATE_NULL, CONN_STATE_CLOSED,
    CONN_STATE_CONNECTING, CONN_STATE_CONNECTED,
    CONN_STATE_QUERYING, CONN_STATE_QUERIED,
    CONN_STATE_ROW_FETCHING, CONN_STATE_ROW_FETCHED,
    CONN_STATE_NEXTQUERY, CONN_STATE_NEXTQUERYING,
    CONN_STATE_RESULT_FREEING, CONN_STATE_RESULT_FREED,
    CONN_STATE_ABORT
} mariadb_conn_state_t;

#define DEFAULT_CIPHER "ECDHE-RSA-AES128-SHA256:AES128-GCM-SHA256:RC4:HIGH:!MD5:!aNULL:!EDH"

typedef struct mariadb_conn_config {
    char *user;
    char *password;
    char *ip;
    char *db;
    unsigned int port;
    char *unix_socket;
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
    struct duda_request *dr;
    mariadb_conn_config_t config;
    MYSQL *mysql, *mysql_ret;
    int fd;
    mariadb_conn_state_t state;

    mariadb_connect_cb *connect_cb;
    mariadb_disconnect_cb *disconnect_cb;

    mariadb_query_t *current_query;
    int disconnect_on_empty;
    struct mk_list queries;
    struct mk_list _head;
} mariadb_conn_t;

int mariadb_conn_add_query(mariadb_conn_t *conn, const char *query_str,
                           mariadb_query_row_cb *row_cb, void *row_cb_privdata,
                           mariadb_query_end_cb *end_cb, void *end_cb_privdata);

void mariadb_conn_free(mariadb_conn_t *conn);
#endif
