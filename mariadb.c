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

#include "duda_api.h"
#include "mariadb.h"

#define mariadb_real_escape_string(conn, to, from, length) \
    mysql_real_escape_string(conn->mysql, to, from ,lengeth)

mariadb_conn_t *mariadb_init(duda_request_t *dr)
{
    mariadb_conn_t *conn = monkey->mem_alloc(sizeof(mariadb_conn_t));
    if (conn == NULL)
        return NULL;

    conn->dr                 = dr;
    conn->config.user        = NULL;
    conn->config.password    = NULL;
    conn->config.ip          = NULL;
    conn->config.db          = NULL;
    conn->config.unix_socket = NULL;
    conn->config.port        = 0;
    conn->config.client_flag = 0;
    conn->config.ssl_key     = NULL;
    conn->config.ssl_cert    = NULL;
    conn->config.ssl_ca      = NULL;
    conn->config.ssl_capath  = NULL;
    conn->config.ssl_cipher  = NULL;
    conn->fd                 = 0;
    conn->state              = CONN_STATE_CLOSED;
    conn->connect_ct         = NULL;
    conn->disconnect_cb      = NULL;

    conn->mysql              = mysql_init(NULL);
    mysql_options(conn->mysql, MYSQL_OPT_NONBLOCK, 0);

    mk_list_init(&conn->queries);

    return conn;
}

int mariadb_connect(mariadb_conn_t *conn, duda_request_t *dr)
{
    int status;

    if (conn->state == CONN_STATE_CLOSED) { 
        conn->state = CONN_STATE_CONNECT;
        status = mysql_real_connect_start(&conn->mysql_ret, conn->mysql,
                                          conn->config.ip, conn->config.user,
                                          conn->config.password, conn->config.db,
                                          conn->config.port, conn->config.unix_socket,
                                          conn->config.client_flag);
        if (!conn->mysql_ret && mysql_errno(conn->mysql) > 0) {
            if (conn->connect_cb)
                conn->connect_cb(conn, MARIADB_ERR);
            // shall we end the request and free all the resource of db connection here?
            return MARIADB_ERR;
        }
        conn->fd = mysql_get_socket(conn->mysql);
        event->add(conn->fd, dr, DUDA_EVENT_RW, DUDA_EVENT_LEVEL_TRIGGERED,
                   mariadb_read, mariadb_write, mariadb_error, mariadb_close,
                   mariadb_timeout);
        if (status) {
            conn->state = CONN_STATE_CONNECTING;
        } else {
            conn->state = CONN_STATE_CONNECTED;
            if (conn->connect_cb)
                conn->connect_cb(conn, MARIADB_OK);
        }

        struct mk_list *conn_list = pthread_getspecific(mariadb_conn_list);
        mk_list_add(&conn->_head, mariadb_conn_list);
    }

    return MARIADB_OK;
}

int mariadb_disconnect(mariadb_conn_t *conn)
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

    mysql_close(conn->mysql);
    return MARIADB_OK;
}

int mariadb_read(int fd, void *data)
{
    msg->info("[FD %i] MariaDB Connection Handler / read\n", fd);

    duda_request_t *dr = data;
    struct mk_list *conn_list, *head;
    mariadb_conn_t *conn_entry, *conn = NULL;

    conn_list = pthread_getspecific(mariadb_conn_list);

    mk_list_foreach(head, conn_list) {
        conn_entry = mk_list_entry(head, mariadb_conn_t, _head);
        if (conn_entry->fd == fd) {
            conn = conn_entry;
            break;
        }
    }

    if (conn == NULL) {
        msg->err("[fd %i] error: mariadb connection not found\n", fd);
        return DUDA_EVENT_CLOSE;
    }

    int status;
    switch (conn->state) {
    case CONN_STATE_CONNECTING:
        status = mysql_real_connect_cont(&conn->mysql_ret, conn->mysql,
                                         MYSQL_WAIT_READ);
        if (!status) {
            if (!conn->mysql_ret) {
                msg->err("MariaDB Connect Error: %s", mysql_error(conn->mysql));
                if (conn->connect_cb)
                    conn->connect_cb(conn, MARIADB_ERR);
                return DUDA_EVENT_CLOSE;
            }
            conn->state = CONN_STATE_CONNECTED;
            if (conn->connect_cb)
                conn->connect_cb(conn, MARIADB_OK);
        }
        break;
    }
    return DUDA_EVENT_OWNED;
}

int mariadb_write(int fd, void *data)
{
    msg->info("[FD %i] MariaDB Connection Hander / write\n", fd);

    duda_request_t *dr = data;
    struct mk_list *conn_list, *head;
    mariadb_conn_t *conn_entry, *conn = NULL;

    conn_list = pthread_getspecific(mariadb_conn_list);

    mk_list_foreach(head, conn_list) {
        conn_entry = mk_list_entry(head, mariadb_conn_t, _head);
        if (conn_entry->fd == fd) {
            conn = conn_entry;
            break;
        }
    }

    if (conn == NULL) {
        msg->err("[fd %i] error: mariadb connection not found\n", fd);
        return DUDA_EVENT_CLOSE;
    }
    return DUDA_EVENT_OWNED;
}

int mariadb_error(int fd, void *data)
{
    msg->info("[FD %i] MariaDB Connection Handler / error\n", fd);
    return DUDA_EVENT_OWNED;
}

int mariadb_close(int fd, void *data)
{
    msg->info("[FD %i] MariaDB Connection Handler / close\n", fd);
    return DUDA_EVENT_CLOSE;
}

int mariadb_timeout(int fd, void *data)
{
    msg->info("[FD %i] MariaDB Connection Handler / timeout\n", fd);
    return DUDA_EVENT_OWNED;
}
