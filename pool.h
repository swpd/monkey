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

#ifndef POSTGRESQL_POOL_H
#define POSTGRESQL_POOL_H

#define POSTGRESQL_POOL_DEFAULT_SIZE 1
#define POSTGRESQL_POOL_DEFAULT_MIN_SIZE 2
#define POSTGRESQL_POOL_DEFAULT_MAX_SIZE 4

typedef enum {
    POOL_TYPE_PARAMS, POOL_TYPE_URL,
} postgresql_pool_type_t;

typedef struct postgresql_pool_config {
    postgresql_pool_type_t type;
    int min_size;
    int max_size;

    char **keys;
    char **values;
    int expand_dbname;

    char *url;

    struct mk_list _head;
} postgresql_pool_config_t;

struct mk_list postgresql_pool_config_list;

typedef struct postgresql_pool {
    int size;
    int free_size;
    postgresql_pool_config_t *config;

    struct mk_list busy_conns;
    struct mk_list free_conns;
} postgresql_pool_t;

int postgresql_pool_params_create(duda_global_t *pool_key, int min_size, int max_size,
                                  const char * const *keys, const char * const *values,
                                  int expand_dbname);

int postgresql_pool_url_create(duda_global_t *pool_key, int min_size, int max_size,
                               const char *url);

postgresql_conn_t *postgresql_pool_get_conn(duda_global_t *pool_key, duda_request_t *dr,
                                            postgresql_connect_cb *cb);

void postgresql_pool_reclaim_conn(postgresql_conn_t *conn);

#endif
