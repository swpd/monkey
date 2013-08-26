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

static inline postgresql_conn_t *__postgresql_get_conn(int fd)
{
    struct mk_list *conn_list, *head;
    postgresql_conn_t *conn = NULL;

    conn_list = global->get(postgresql_conn_list);
    mk_list_foreach(head, conn_list) {
        conn = mk_list_entry(head, postgresql_conn_t, _head);
        if (conn->fd == fd) {
            break;
        }
    }
    return conn;
}

int postgresql_on_read(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / read", fd);
    postgresql_conn_t *conn = __postgresql_get_conn(fd);

    if (!conn) {
        msg->err("[FD %i] Error: PostgreSQL Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }

    int events = 0;
    int status;
    switch (conn->state) {
    case CONN_STATE_CONNECTING:
        status = PQconnectPoll(conn->conn);
        if (status == PGRES_POLLING_FAILED) {
            msg->err("PostgreSQL Connect Error: %s", PQerrorMessage(conn->conn));
            if (conn->connect_cb) {
                conn->connect_cb(conn, POSTGRESQL_ERR, conn->dr);
            }
            return DUDA_EVENT_CLOSE;
        } else if (status == PGRES_POLLING_OK) {
            /* on connected */
            if (conn->connect_cb) {
                conn->connect_cb(conn, POSTGRESQL_OK, conn->dr);
            }
            conn->state = CONN_STATE_CONNECTED;
            postgresql_async_handle_query(conn);
        } else {
            if (status & PGRES_POLLING_READING) {
                events |= DUDA_EVENT_READ;
            }
            if (status & PGRES_POLLING_WRITING) {
                events |= DUDA_EVENT_WRITE;
            }
            event->mode(conn->fd, events, DUDA_EVENT_LEVEL_TRIGGERED);
        }
        break;
    case CONN_STATE_ROW_FETCHING:
        postgresql_async_handle_row(conn);
        if (conn->state == CONN_STATE_CONNECTED) {
            postgresql_async_handle_query(conn);
        }
        break;
    default:
        break;
    }
    return DUDA_EVENT_OWNED;
}

int postgresql_on_write(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / write", fd);
    postgresql_conn_t *conn = __postgresql_get_conn(fd);

    if (!conn) {
        msg->err("[FD %i] Error: PostgreSQL Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }

    int status;
    switch (conn->state) {
    case CONN_STATE_QUERYING:
        status = PQflush(conn->conn);
        if (status == -1) {
            msg->err("[FD %i] PostgreSQL Send Query Error: %s", conn->fd,
                     PQerrorMessage(conn->conn));
            postgresql_query_free(conn->current_query);
        } else if (status == 0) {
            /* successfully send query */
            conn->state = CONN_STATE_QUERIED;
            event->mode(conn->fd, DUDA_EVENT_READ, DUDA_EVENT_LEVEL_TRIGGERED);
            postgresql_async_handle_row(conn);
            if (conn->state == CONN_STATE_CONNECTED) {
                postgresql_async_handle_query(conn);
            }
        }
        break;
    default:
        break;
    }
    return DUDA_EVENT_OWNED;
}

int postgresql_on_error(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / error", fd);
    postgresql_conn_t *conn = __postgresql_get_conn(fd);

    if (!conn) {
        msg->err("[FD %i] Error: PostgreSQL Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }
    conn->is_pooled = 0;
    postgresql_conn_handle_release(conn, POSTGRESQL_ERR);
    return DUDA_EVENT_OWNED;
}

int postgresql_on_close(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / close", fd);
    postgresql_conn_t *conn = __postgresql_get_conn(fd);

    if (!conn) {
        msg->err("[FD %i] Error: PostgreSQL Connection Not Found", fd);
    }
    conn->is_pooled = 0;
    postgresql_conn_handle_release(conn, POSTGRESQL_ERR);
    return DUDA_EVENT_CLOSE;
}

int postgresql_on_timeout(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / timeout", fd);
    return DUDA_EVENT_OWNED;
}
