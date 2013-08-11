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
#include "connection_priv.h"

static inline postgresql_conn_t *__postgresql_conn_create(duda_request_t *dr,
                                                          postgresql_connect_cb *cb)
{
    postgresql_conn_t *conn = monkey->mem_alloc(sizeof(postgresql_conn_t));
    if (!conn) {
        return NULL;
    }

    conn->dr            = dr;
    conn->connect_cb    = cb;
    conn->disconnect_cb = NULL;
    mk_list_init(&conn->queries);

    return conn;
}

static inline void __postgresql_conn_handle_connect(postgresql_conn_t *conn)
{
    if (!conn->conn) {
        FREE(conn);
        return NULL;
    }

    conn->state = PQstatus(conn->conn);
    if (PQstatus(conn->conn) == CONNECTION_BAD) {
        msg->err("PostgreSQL Connect Error");
        if (conn->connect_cb) {
            conn->connect_cb(conn, POSTGRESQL_ERR, conn->dr);
        }
        PQfinish(conn->conn);
        FREE(conn);
        return NULL;
    }

    /* on success */
    conn->fd = PQsocket(conn->conn);
    if (conn->state == CONNECTION_OK || conn->state == CONNECTION_MADE) {
        if (conn->connect_cb) {
            conn->connect_cb(conn, POSTGRESQL_OK, conn->dr);
        }
    }
    
    int status = PQconnectPoll(conn->conn);
    int events = 0;
    if (status & PGRES_POLLING_READING) {
        events |= DUDA_EVENT_READ;
    }
    if (status & PGRES_POLLING_WRITING) {
        events |= DUDA_EVENT_WRITE;
    }

    event->add(conn->fd, events, DUDA_EVENT_LEVEL_TRIGGERED,
               postgresql_on_read, postgresql_on_write, postgresql_on_error,
               postgresql_on_close, postgresql_on_timeout, NULL);

    struct mk_list *conn_list = global->get(postgresql_conn_list);
    if (!conn_list) {
        conn_list = monkey->mem_alloc(sizeof(struct mk_list));
        if (!conn_list) {
            msg->err("Connection List Init Error");
            PQfinish(conn->conn);
            FREE(conn);
            return NULL;
        }
        mk_list_init(conn_list);
        global->set(postgresql_conn_list, (void *) conn_list);
    }
    mk_list_add(&conn->_head, conn_list);
}

postgresql_conn_t *postgresql_conn_connect(duda_request_t *dr, postgresql_connect_cb *cb,
                                           const char **keys, const char **values,
                                           int expand_dbname)
{
    postgresql_conn_t *conn = __postgresql_conn_create(dr, cb);
    if (!conn) {
        return NULL;
    }

    conn->conn = PQconnectStartParams(keys, values, expand_dbname);

    __postgresql_conn_handle_connect(conn);

    return conn;
}

postgresql_conn_t *postgresql_conn_connect_url(duda_request_t *dr, postgresql_connect_cb *cb,
                                               const char *url)
{
    postgresql_conn_t *conn = __postgresql_conn_create(dr, cb);
    if (!conn) {
        return NULL;
    }

    conn->conn = PQconnectStart(url);

    __postgresql_conn_handle_connect(conn);

    return conn;
}

static void __postgresql_conn_handle_release(postgresql_conn_t *conn, int status)
{
    event->delete(conn->fd);
    if (conn->disconnect_cb) {
        conn->disconnect_cb(conn, status, conn->dr);
    }
    mk_list_del(&conn->_head);
    PQfinish(conn->conn);
    FREE(conn);
}

void postgresql_conn_disconnect(postgresql_conn_t *conn, postgresql_disconnect_cb *cb)
{
}
