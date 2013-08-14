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
    return DUDA_EVENT_OWNED;
}

int postgresql_on_write(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / write", fd);
    return DUDA_EVENT_OWNED;
}

int postgresql_on_error(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / error", fd);
    return DUDA_EVENT_OWNED;
}

int postgresql_on_close(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / close", fd);
    return DUDA_EVENT_CLOSE;
}

int postgresql_on_timeout(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] PostgreSQL Connection Handler / timeout", fd);
    return DUDA_EVENT_OWNED;
}
