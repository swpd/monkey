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

#ifndef MARIADB_POOL_H
#define MARIADB_POOL_H

#define MARIADB_POOL_DEFAULT_SIZE 2

typedef struct mariadb_pool {
    int size;
    int free_size;
    struct mk_list busy_conns;
    struct mk_list free_conns;
} mariadb_pool_t;

mariadb_conn_t *mariadb_pool_get_conn(duda_request_t *dr, char *user, char *password,
                                      char *ip, char *db, unsigned int port,
                                      char *unix_socket, unsigned long client_flag);

void mariadb_pool_reclaim_conn(mariadb_conn_t *conn);

#endif
