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

struct postgresql_conn {
    struct duda_request *dr;
    PGconn *conn;
    int fd;
    ConnStatusType state;

    postgresql_connect_cb *connect_cb;
    postgresql_disconnect_cb *disconnect_cb;

    struct mk_list queries;
    struct mk_list _head;
};

postgresql_conn_t *postgresql_conn_connect(duda_request_t *dr, postgresql_connect_cb *cb,
                                           const char **keys, const char **values,
                                           int expand_dbname);

postgresql_conn_t *postgresql_conn_connect_url(duda_request_t *dr, postgresql_connect_cb *cb,
                                               const char *url);

void postgresql_conn_disconnect(postgresql_conn_t *conn, postgresql_disconnect_cb *cb);

#endif
