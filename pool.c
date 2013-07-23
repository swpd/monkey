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
#include "query_priv.h"
#include "connection.h"
#include "connection_priv.h"
#include "pool.h"

duda_global_t mariadb_conn_pool;

static inline int  __mariadb_pool_spawn_conn(mariadb_pool_t *pool, int size)
{
    int i;
    mariadb_conn_t *conn;

    for (i = 0; i < size; ++i) {
        conn = mariadb_conn_init(NULL, NULL, NULL, NULL, NULL, 0, NULL, 0);
        conn->is_pooled = 1;
        if (!conn) {
            break;
        }
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
        mariadb_conn_free(conn);
        pool->size--;
        pool->free_size--;
    }
}

mariadb_conn_t *mariadb_pool_get_conn(duda_request_t *dr, char *user, char *password,
                                      char *ip, char *db, unsigned int port,
                                      char *unix_socket, unsigned long client_flag)
{
    mariadb_pool_t *pool;
    mariadb_conn_t *conn;
    int ret;

    pool = global->get(mariadb_conn_pool);
    if (!pool) {
        pool = monkey->mem_alloc(sizeof(mariadb_pool_t));
        if (!pool) {
            return NULL;
        }
        pool->size      = 0;
        pool->free_size = 0;
        mk_list_init(&pool->free_conns);
        mk_list_init(&pool->busy_conns);
        global->set(mariadb_conn_pool, (void *) pool);

        ret = __mariadb_pool_spawn_conn(pool, MARIADB_POOL_DEFAULT_SIZE);
        if (ret != MARIADB_OK) {
            return NULL;
        }
    }

    if (mk_list_is_empty(&pool->free_conns) == 0) {
        ret = __mariadb_pool_spawn_conn(pool, MARIADB_POOL_DEFAULT_SIZE);
        if (ret != MARIADB_OK) {
            return NULL;
        }
    }
    conn = mk_list_entry_first(&pool->free_conns, mariadb_conn_t, _pool_head);

    conn->dr                  = dr;
    conn->config.user         = monkey->str_dup(user);
    conn->config.password     = monkey->str_dup(password);
    conn->config.ip           = monkey->str_dup(ip);
    conn->config.db           = monkey->str_dup(db);
    conn->config.port         = port;
    conn->config.unix_socket  = monkey->str_dup(unix_socket);
    conn->config.client_flag  = client_flag;
    mysql_options(&conn->mysql, MYSQL_OPT_NONBLOCK, 0);

    mk_list_del(&conn->_pool_head);
    mk_list_add(&conn->_pool_head, &pool->busy_conns);
    pool->free_size--;

    return conn;
}

void mariadb_pool_reclaim_conn(mariadb_conn_t *conn)
{
    mariadb_pool_t *pool = global->get(mariadb_conn_pool);

    FREE(conn->config.user);
    FREE(conn->config.password);
    FREE(conn->config.ip);
    FREE(conn->config.db);
    FREE(conn->config.unix_socket);
    FREE(conn->config.ssl_key);
    FREE(conn->config.ssl_cert);
    FREE(conn->config.ssl_ca);
    FREE(conn->config.ssl_capath);
    FREE(conn->config.ssl_cipher);

    conn->dr                   = NULL;
    conn->config.port          = 0;
    conn->config.client_flag   = 0;
    conn->mysql_ret            = NULL;
    conn->fd                   = 0;
    conn->state                = CONN_STATE_CLOSED;
    conn->connect_cb           = NULL;
    conn->disconnect_cb        = NULL;
    conn->current_query        = NULL;
    conn->disconnect_on_finish = 0;

    while (mk_list_is_empty(&conn->queries) != 0) {
        mariadb_query_t *query = mk_list_entry_first(&conn->queries,
                                                     mariadb_query_t, _head);
        mariadb_query_free(query);
    }

    mk_list_del(&conn->_pool_head);
    mk_list_add(&conn->_pool_head, &pool->free_conns);
    pool->free_size++;

    /* shrink the pool */
    while (pool->free_size * 2 > pool->size &&
           pool->size > MARIADB_POOL_DEFAULT_SIZE) {
        __mariadb_pool_release_conn(pool, MARIADB_POOL_DEFAULT_SIZE);
    }
}
