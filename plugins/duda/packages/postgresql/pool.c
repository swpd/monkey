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
#include "query.h"
#include "connection_priv.h"
#include "pool.h"

static inline int __postgresql_pool_spawn_conn(postgresql_pool_t *pool, int size)
{
    int i;
    postgresql_conn_t *conn;
    postgresql_pool_config_t *config = pool->config;

    for (i = 0; i < size; ++i) {
        if (config->type == POOL_TYPE_PARAMS) {
            conn = postgresql_conn_connect(NULL, NULL, (const char * const *)config->keys,
                                           (const char * const *)config->values,
                                           config->expand_dbname);
        } else if (config->type == POOL_TYPE_URL) {
            conn = postgresql_conn_connect_url(NULL, NULL, config->url);
        }

        if (!conn) {
            break;
        }

        conn->is_pooled = 1;
        conn->pool = pool;
        mk_list_add(&conn->_pool_head, &pool->free_conns);
        pool->size++;
        pool->free_size++;
    }

    if (pool->free_size == 0) {
        return POSTGRESQL_ERR;
    }

    return POSTGRESQL_OK;
}

static inline void __postgresql_pool_release_conn(postgresql_pool_t *pool, int size)
{
    int i;
    postgresql_conn_t *conn;

    for (i = 0; i < size; ++i) {
        conn = mk_list_entry_first(&pool->free_conns, postgresql_conn_t, _pool_head);
        mk_list_del(&conn->_pool_head);
        conn->is_pooled = 0;
        postgresql_conn_handle_release(conn, POSTGRESQL_OK);
        pool->size--;
        pool->free_size--;
    }
}

static inline postgresql_pool_config_t *__postgresql_pool_get_config(duda_global_t *pool_key)
{
    struct mk_list *head;
    postgresql_pool_config_t *config = NULL;

    mk_list_foreach(head, &postgresql_pool_config_list) {
        config = mk_list_entry(head, postgresql_pool_config_t, _head);
        if (config->pool_key == pool_key) {
            break;
        }
    }

    return config;
}

int postgresql_pool_params_create(duda_global_t *pool_key, int min_size, int max_size,
                                  const char * const *keys, const char * const *values,
                                  int expand_dbname)
{
    postgresql_pool_config_t *config = monkey->mem_alloc(sizeof(postgresql_pool_config_t));
    if (!config) {
        return POSTGRESQL_ERR;
    }

    config->pool_key = pool_key;
    int length = 0;
    const char * const *ptr = keys;
    while (*ptr != NULL) {
        length++;
        ptr++;
    }
    
    int i;
    if (keys) {
        config->keys = monkey->mem_alloc(sizeof(char *) * (length + 1));
        for (i = 0; i < length; ++i) {
            config->keys[i] = monkey->str_dup(keys[i]);
        }
        config->keys[length] = NULL;
    }

    if (values) {
        config->values = monkey->mem_alloc(sizeof(char *) * (length + 1));
        for (i = 0; i < length; ++i) {
            config->values[i] = monkey->str_dup(values[i]);
        }
        config->values[length] = NULL;
    }

    config->expand_dbname = expand_dbname;

    if (min_size == 0) {
        config->min_size = POSTGRESQL_POOL_DEFAULT_MIN_SIZE;
    } else {
        config->min_size = min_size;
    }
    if (max_size == 0) {
        config->max_size = POSTGRESQL_POOL_DEFAULT_MAX_SIZE;
    } else {
        config->max_size = max_size;
    }

    config->type = POOL_TYPE_PARAMS;
    mk_list_add(&config->_head, &postgresql_pool_config_list);
    return POSTGRESQL_OK;
}

int postgresql_pool_url_create(duda_global_t *pool_key, int min_size, int max_size,
                               const char *url)
{
    postgresql_pool_config_t *config = monkey->mem_alloc(sizeof(postgresql_pool_config_t));
    if (!config) {
        return POSTGRESQL_ERR;
    }

    config->pool_key = pool_key;
    config->url = monkey->str_dup(url);

    if (min_size == 0) {
        config->min_size = POSTGRESQL_POOL_DEFAULT_MIN_SIZE;
    } else {
        config->min_size = min_size;
    }
    if (max_size == 0) {
        config->max_size = POSTGRESQL_POOL_DEFAULT_MAX_SIZE;
    } else {
        config->max_size = max_size;
    }

    config->type = POOL_TYPE_URL;
    mk_list_add(&config->_head, &postgresql_pool_config_list);
    return POSTGRESQL_OK;
}

postgresql_conn_t *postgresql_pool_get_conn(duda_global_t *pool_key, duda_request_t *dr,
                                            postgresql_connect_cb *cb)
{
    postgresql_pool_t *pool;
    postgresql_pool_config_t *config;
    postgresql_conn_t *conn;

    pool = global->get(*pool_key);
    if (!pool) {
        pool = monkey->mem_alloc(sizeof(postgresql_pool_t));
        if (!pool) {
            return NULL;
        }

        pool->size = 0;
        pool->free_size = 0;
        mk_list_init(&pool->free_conns);
        mk_list_init(&pool->busy_conns);
        global->set(*pool_key, (void *) pool);
        
        config = __postgresql_pool_get_config(pool_key);
        if (!config) {
            return NULL;
        }
        pool->config = config;
    }

    int ret;
    if (mk_list_is_empty(&pool->free_conns) == 0) {
        if (pool->size < config->max_size) {
            ret = __postgresql_pool_spawn_conn(pool, POSTGRESQL_POOL_DEFAULT_SIZE);
            if (ret != POSTGRESQL_OK) {
                return NULL;
            }
        } else {
            if (config->type == POOL_TYPE_PARAMS) {
                conn = postgresql_conn_connect(dr, cb, (const char * const *)config->keys,
                                               (const char * const *)config->values,
                                               config->expand_dbname);
            } else if (config->type == POOL_TYPE_URL) {
                conn = postgresql_conn_connect_url(dr, cb, config->url);
            }
            
            return conn;
        }
    }

    conn = mk_list_entry_first(&pool->free_conns, postgresql_conn_t, _pool_head);
    conn->dr = dr;
    conn->connect_cb = cb;

    if (conn->connect_cb) {
        conn->connect_cb(conn, POSTGRESQL_OK, conn->dr);
    }

    mk_list_del(&conn->_pool_head);
    mk_list_add(&conn->_pool_head, &pool->busy_conns);
    pool->free_size--;

    return conn;
}

void postgresql_pool_reclaim_conn(postgresql_conn_t *conn)
{
    postgresql_pool_t *pool = conn->pool;

    conn->dr            = NULL;
    conn->connect_cb    = NULL;
    conn->disconnect_cb = NULL;
    conn->disconnect_on_finish = 0;

    mk_list_del(&conn->_pool_head);
    mk_list_add(&conn->_pool_head, &pool->free_conns);
    pool->free_size++;

    while (pool->free_size * 2 > pool->size &&
           pool->size > POSTGRESQL_POOL_DEFAULT_MIN_SIZE) {
        __postgresql_pool_release_conn(pool, POSTGRESQL_POOL_DEFAULT_SIZE);
    }
}
