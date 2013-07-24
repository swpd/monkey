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

#include <mysql.h>
#include "common.h"
#include "query.h"
#include "connection_priv.h"
#include "pool.h"

int mariadb_connect(mariadb_conn_t *conn, mariadb_connect_cb *cb);
void mariadb_disconnect(mariadb_conn_t *conn, mariadb_disconnect_cb *cb);

static inline int  __mariadb_pool_spawn_conn(mariadb_pool_t *pool, int size)
{
    int i, ret;
    mariadb_conn_t *conn;
    mariadb_conn_config_t config = pool->config->conn_config;

    for (i = 0; i < size; ++i) {
        conn = mariadb_conn_create(NULL, config.user, config.password, config.ip,
                                   config.db, config.port, config.unix_socket,
                                   config.client_flag);
        if (!conn) {
            break;
        }

        ret = mariadb_connect(conn, NULL);
        if (ret != MARIADB_OK) {
            break;
        }

        conn->is_pooled = 1;
        conn->pool = pool;
        mk_list_add(&conn->_pool_head, &pool->free_conns);
        pool->size++;
        pool->free_size++;
    }

    if (pool->free_size == 0) {
        return MARIADB_ERR;
    }

    return MARIADB_OK;
}

static inline void __mariadb_pool_release_conn(mariadb_pool_t *pool, int size)
{
    int i;
    mariadb_conn_t *conn;

    for (i = 0; i < size; ++i) {
        conn = mk_list_entry_first(&pool->free_conns, mariadb_conn_t, _pool_head);
        mk_list_del(&conn->_pool_head);
        conn->is_pooled = 0;
        mariadb_disconnect(conn, NULL);
        pool->size--;
        pool->free_size--;
    }
}

static inline mariadb_pool_config_t *__mariadb_pool_get_config(duda_global_t *pool_key)
{
    struct mk_list *head;
    mariadb_pool_config_t *config = NULL;

    mk_list_foreach(head, &mariadb_pool_config_list) {
        config = mk_list_entry(head, mariadb_pool_config_t, _head);
        if (config->pool_key == pool_key) {
            break;
        }
    }

    return config;
}

int mariadb_pool_create(duda_global_t *pool_key, int min_size, int max_size,
                        const char *user, const char *password, const char *ip,
                        const char *db, unsigned int port, const char *unix_socket,
                        unsigned long client_flag)
{
    mariadb_pool_config_t *config = monkey->mem_alloc(sizeof(mariadb_pool_config_t));
    if (!config) {
        return MARIADB_ERR;
    }

    config->pool_key                = pool_key;
    config->conn_config.user        = monkey->str_dup(user);
    config->conn_config.password    = monkey->str_dup(password);
    config->conn_config.ip          = monkey->str_dup(ip);
    config->conn_config.db          = monkey->str_dup(db);
    config->conn_config.port        = port;
    config->conn_config.unix_socket = monkey->str_dup(unix_socket);
    config->conn_config.client_flag = client_flag;
    config->use_ssl                 = 0;

    if (min_size == 0) {
        config->min_size = MARIADB_POOL_DEFAULT_MIN_SIZE;
    } else {
        config->min_size = min_size;
    }
    if (max_size == 0) {
        config->max_size = MARIADB_POOL_DEFAULT_MAX_SIZE;
    } else {
        config->max_size = max_size;
    }

    mk_list_add(&config->_head, &mariadb_pool_config_list);
    return MARIADB_OK;
}

int mariadb_pool_set_ssl(duda_global_t *pool_key, const char *key, const char *cert,
                         const char *ca, const char *capath, const char *cipher)
{
    mariadb_pool_config_t *config = __mariadb_pool_get_config(pool_key);
    if (!config) {
        return MARIADB_ERR;
    }

    config->conn_config.ssl_key    = monkey->str_dup(key);
    config->conn_config.ssl_cert   = monkey->str_dup(cert);
    config->conn_config.ssl_ca     = monkey->str_dup(ca);
    config->conn_config.ssl_capath = monkey->str_dup(capath);
    config->conn_config.ssl_cipher = monkey->str_dup(cipher);
    config->use_ssl                = 1;

    return MARIADB_OK;
}

mariadb_conn_t *mariadb_pool_get_conn(duda_global_t *pool_key, duda_request_t *dr)
{
    mariadb_pool_t *pool;
    mariadb_pool_config_t *config;
    mariadb_conn_t *conn;
    mariadb_conn_config_t conn_config;
    int ret;

    pool = global->get(*pool_key);
    if (!pool) {
        pool = monkey->mem_alloc(sizeof(mariadb_pool_t));
        if (!pool) {
            return NULL;
        }

        pool->size      = 0;
        pool->free_size = 0;
        mk_list_init(&pool->free_conns);
        mk_list_init(&pool->busy_conns);
        global->set(*pool_key, (void *) pool);

        config = __mariadb_pool_get_config(pool_key);
        if (!config) {
            return NULL;
        }
        pool->config = config;
    }


    if (mk_list_is_empty(&pool->free_conns) == 0) {
        if (pool->size < config->max_size) {
            ret = __mariadb_pool_spawn_conn(pool, MARIADB_POOL_DEFAULT_SIZE);
            if (ret != MARIADB_OK) {
                return NULL;
            }
        } else {
            conn_config = config->conn_config;
            conn = mariadb_conn_create(dr, conn_config.user, conn_config.password,
                                       conn_config.ip, conn_config.db, conn_config.port,
                                       conn_config.unix_socket, conn_config.client_flag);
            if (!conn) {
                return NULL;
            }

            ret = mariadb_connect(conn, NULL);
            if (ret != MARIADB_OK) {
                return NULL;
            }
            return conn;
        }
    }

    conn = mk_list_entry_first(&pool->free_conns, mariadb_conn_t, _pool_head);
    conn->dr = dr;

    mk_list_del(&conn->_pool_head);
    mk_list_add(&conn->_pool_head, &pool->busy_conns);
    pool->free_size--;

    return conn;
}

void mariadb_pool_reclaim_conn(mariadb_conn_t *conn)
{
    mariadb_pool_t *pool = conn->pool;

    conn->dr                   = NULL;
    conn->connect_cb           = NULL;
    conn->disconnect_cb        = NULL;
    conn->disconnect_on_finish = 0;

    mk_list_del(&conn->_pool_head);
    mk_list_add(&conn->_pool_head, &pool->free_conns);
    pool->free_size++;

    /* shrink the pool */
    while (pool->free_size * 2 > pool->size &&
           pool->size > MARIADB_POOL_DEFAULT_MIN_SIZE) {
        __mariadb_pool_release_conn(pool, MARIADB_POOL_DEFAULT_SIZE);
    }
}
