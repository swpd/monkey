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

#ifndef DUDA_PACKAGE_MARIADB_H
#define DUDA_PACKAGE_MARIADB_H

#include "connection.h"

pthread_key_t mariadb_conn_list;

typedef struct duda_api_mariadb {
    int (*connect)(mariadb_conn_t *);
    int (*disconnect)(mariadb_conn_t *);
    int (*set_connect_cb)(mariadb_conn_t *, mariadb_connect_cb *);
    int (*set_disconnect_cb)(mariadb_conn_t *, mariadb_disconnect_cb *);
    int (*query)(mariadb_conn_t *, struct duda_request *, char *, mariadb_query_cb *, void *);
    int (*abort)(mariadb_query_t *);
} mariadb_object_t;

mariadb_object_t *mariadb;

static inline int mariadb_init_keys()
{
    if (pthread_key_create(&mariadb_conn_list, NULL) != 0)
        return MARIADB_ERR;
    return MARIADB_OK;
}

int mariadb_read(int fd, struct duda_request *dr);
int mariadb_write(int fd, struct duda_request *dr);
int mariadb_error(int fd, struct duda_request *dr);
int mariadb_close(int fd, struct duda_request *dr);
int mariadb_timeout(int fd, struct duda_request *dr);

#endif
