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

#ifndef DUDA_PACKAGE_POSTGRESQL_H
#define DUDA_PACKAGE_POSTGRESQL_H

#include "common.h"
#include "query.h"
#include "connection.h"

duda_global_t postgresql_conn_list;

typedef struct duda_api_postgresql {
    postgresql_conn_t *(*connect)(duda_request_t *, postgresql_connect_cb *,
                                  const char * const *, const char * const *, int);
    postgresql_conn_t *(*connect_uri)(duda_request_t *, postgresql_connect_cb *,
                                     const char *);
    int (*create_pool_params)(duda_global_t *, int , int , const char * const *,
                              const char * const *, int);
    int (*create_pool_uri)(duda_global_t *, int , int , const char *);
    postgresql_conn_t *(*get_conn)(duda_global_t *, duda_request_t *,
                                   postgresql_connect_cb *);
    int (*query)(postgresql_conn_t *, const char *, postgresql_query_result_cb *,
                 postgresql_query_row_cb *, postgresql_query_end_cb *, void *);
    int (*query_params)(postgresql_conn_t *, const char *, int, const char * const *,
                        const int *, const int *, int, postgresql_query_result_cb *,
                        postgresql_query_row_cb *, postgresql_query_end_cb *, void *);
    int (*query_prepared)(postgresql_conn_t *, const char *, int, const char * const *,
                          const int *, const int *, int, postgresql_query_result_cb *,
                          postgresql_query_row_cb *, postgresql_query_end_cb *, void *);
    char *(*escape_literal)(postgresql_conn_t *, const char *, size_t);
    char *(*escape_identifier)(postgresql_conn_t *, const char *, size_t);
    unsigned char *(*escape_binary)(postgresql_conn_t *, const unsigned char *,
                                    size_t, size_t *);
    unsigned char *(*unescape_binary)(const unsigned char *, size_t *);
    void (*abort)(postgresql_query_t *);
    void (*free)(void *);
    void (*disconnect)(postgresql_conn_t *, postgresql_disconnect_cb *);
} postgresql_object_t;

postgresql_object_t *postgresql;

int postgresql_on_read(int fd, void *data);
int postgresql_on_write(int fd, void *data);
int postgresql_on_error(int fd, void *data);
int postgresql_on_close(int fd, void *data);
int postgresql_on_timeout(int fd, void *data);

#endif
