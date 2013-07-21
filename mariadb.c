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
#include "mariadb.h"
#include "query_priv.h"
#include "connection_priv.h"
#include "pool.h"

static inline mariadb_conn_t *mariadb_get_conn(int fd)
{
    struct mk_list *conn_list, *head;
    mariadb_conn_t *conn = NULL;

    conn_list = pthread_getspecific(mariadb_conn_list);
    mk_list_foreach(head, conn_list) {
        conn = mk_list_entry(head, mariadb_conn_t, _head);
        if (conn->fd == fd) {
            break;
        }
    }
    return conn;
}

static void __mariadb_handle_query(mariadb_conn_t *conn);
static void __mariadb_handle_result(mariadb_conn_t *conn);
static void __mariadb_handle_row(mariadb_conn_t *conn);
static void __mariadb_handle_result_free(mariadb_conn_t *conn);
static void __mariadb_handle_next_result(mariadb_conn_t *conn);
static void __mariadb_handle_disconnect(mariadb_conn_t* conn, int status);

static void __mariadb_handle_query(mariadb_conn_t *conn)
{
    int status;

    while (mk_list_is_empty(&conn->queries) != 0) {
        mariadb_query_t *query = mk_list_entry_first(&conn->queries,
                                                     mariadb_query_t, _head);
        conn->current_query = query;
        if (query->abort) {
            mariadb_query_free(query);
            conn->state = CONN_STATE_CONNECTED;
            continue;
        }

        if (query->query_str) {
            status = mysql_real_query_start(&query->error, &conn->mysql,
                                            query->query_str,
                                            strlen(query->query_str));
            if (status) {
                conn->state = CONN_STATE_QUERYING;
                return;
            }
            if (query->error) {
                msg->err("[FD %i] MariaDB Query Error: %s", conn->fd,
                         mysql_error(&conn->mysql));
                /* may add a query on error callback to be called here */
                mariadb_query_free(query);
            } else {
                conn->state = CONN_STATE_QUERIED;
                __mariadb_handle_result(conn);
                if (conn->state != CONN_STATE_CONNECTED) {
                    return;
                }
            }
        } else {
            msg->err("[FD %i] MariaDB Query Statement Missing", conn->fd);
            mariadb_query_free(query);
        }
    }
    conn->current_query = NULL;
    conn->state         = CONN_STATE_CONNECTED;
    /* all queries have been handled */
    event->mode(conn->fd, DUDA_EVENT_SLEEP, DUDA_EVENT_LEVEL_TRIGGERED);
    if (conn->disconnect_on_finish) {
        event->delete(conn->fd);
        __mariadb_handle_disconnect(conn, MARIADB_OK);
    }
}

static void __mariadb_handle_result(mariadb_conn_t *conn)
{
    mariadb_query_t *query = conn->current_query;

    query->result = mysql_use_result(&conn->mysql);
    if (query->result_cb) {
        query->result_cb(query, conn->dr);
    }
    if (!query->result) {
        mariadb_query_free(query);
        conn->state = CONN_STATE_CONNECTED;
        return;
    }
    if (query->abort) {
        if (query->result) {
            __mariadb_handle_result_free(conn);
        } else {
            mariadb_query_free(query);
            conn->state = CONN_STATE_CONNECTED;
        }
        return;
    }

    query->n_fields = mysql_num_fields(query->result);
    query->fields = monkey->mem_alloc(sizeof(char *) * query->n_fields);
    MYSQL_FIELD *fields = mysql_fetch_fields(query->result);
    unsigned int i;
    for (i = 0; i < query->n_fields; ++i) {
        (query->fields)[i] = monkey->str_dup(fields[i].name);
    }

    __mariadb_handle_row(conn);
}

static void __mariadb_handle_row(mariadb_conn_t *conn)
{
    int status;
    mariadb_query_t *query = conn->current_query;

    while (1) {
        status = mysql_fetch_row_start(&query->row, query->result);
        if (status) {
            conn->state = CONN_STATE_ROW_FETCHING;
            return;
        }
        conn->state = CONN_STATE_ROW_FETCHED;
        /* all rows have been fetched */
        if (!query->row) {
            if (mysql_errno(&conn->mysql) > 0) {
                msg->err("[FD %i] MariaDB Fetch Result Row Error: %s", conn->fd,
                         mysql_error(&conn->mysql));
            } else {
                if (query->end_cb) {
                    query->end_cb(query, conn->dr);
                }
            }
            __mariadb_handle_result_free(conn);
            break;
        }
        if (query->row_cb) {
            query->row_cb(query->row_cb_privdata, query->n_fields,
                                query->fields, query->row, conn->dr);
        }
    }
}

static void __mariadb_handle_result_free(mariadb_conn_t *conn)
{
    int status;
    mariadb_query_t *query = conn->current_query;

    status = mysql_free_result_start(query->result);
    if (status) {
        conn->state = CONN_STATE_RESULT_FREEING;
        return;
    }
    conn->state = CONN_STATE_RESULT_FREED;
    if ((conn->config.client_flag & CLIENT_MULTI_STATEMENTS) == 0) {
        conn->current_query = NULL;
        mariadb_query_free(query);
        conn->state = CONN_STATE_CONNECTED;
        return;
    }
    __mariadb_handle_next_result(conn);
}

static void __mariadb_handle_next_result(mariadb_conn_t *conn)
{
    int status;
    mariadb_query_t *query = conn->current_query;

    while (1) {
        status = mysql_next_result_start(&query->error, &conn->mysql);
        if (status) {
            conn->state = CONN_STATE_NEXT_RESULTING;
            return;
        }
        conn->state = CONN_STATE_NEXT_RESULTED;
        if (query->error == -1) {
            break;
        } else if (query->error == 0){
            __mariadb_handle_result(conn);
            if (conn->state != CONN_STATE_RESULT_FREED) {
                return;
            }
        } else {
            msg->err("[FD %i] MariaDB Execute Statement Error: %s", conn->fd,
                     mysql_error(&conn->mysql));
        }
    }
    conn->current_query = NULL;
    mariadb_query_free(query);
    conn->state = CONN_STATE_CONNECTED;
}

static void __mariadb_handle_disconnect(mariadb_conn_t *conn, int status)
{
    mk_list_del(&conn->_head);
    mysql_close(&conn->mysql);
    if (conn->state >= CONN_STATE_CONNECTED && conn->disconnect_cb) {
        conn->disconnect_cb(conn, status, conn->dr);
    }
    conn->state = CONN_STATE_CLOSED;
    if (conn->is_pooled) {
        mariadb_pool_reclaim_conn(conn);
    } else {
        mariadb_conn_free(conn);
    }
}

unsigned long mariadb_real_escape_string(mariadb_conn_t *conn, char *to,
                                         const char *from, unsigned long length)
{
    return mysql_real_escape_string(&conn->mysql, to, from, length);
}

int mariadb_query(mariadb_conn_t *conn, const char * query_str,
                  mariadb_query_result_cb *result_cb,
                  mariadb_query_row_cb *row_cb, void *row_cb_privdata,
                  mariadb_query_end_cb *end_cb)
{
    int ret = mariadb_conn_add_query(conn, query_str, result_cb, row_cb,
                                     row_cb_privdata, end_cb);
    if (ret != MARIADB_OK) {
        msg->err("[FD %i] MariaDB Add Query Error", conn->fd);
        return MARIADB_ERR;
    }
    if (conn->state == CONN_STATE_CONNECTED) {
        event->mode(conn->fd, DUDA_EVENT_WAKEUP, DUDA_EVENT_LEVEL_TRIGGERED);
        __mariadb_handle_query(conn);
    }
    return MARIADB_OK;
}

int mariadb_connect(mariadb_conn_t *conn, mariadb_connect_cb *cb)
{
    int status;
    if(!conn->connect_cb) {
        conn->connect_cb = cb;
    }

    /* whether the connection has already been established */
    if (conn->state == CONN_STATE_CLOSED) { 
        status = mysql_real_connect_start(&conn->mysql_ret, &conn->mysql,
                                          conn->config.ip, conn->config.user,
                                          conn->config.password, conn->config.db,
                                          conn->config.port,
                                          conn->config.unix_socket,
                                          conn->config.client_flag);
        if (!conn->mysql_ret && mysql_errno(&conn->mysql) > 0) {
            msg->err("MariaDB Connect Error: %s", mysql_error(&conn->mysql));
            if (conn->connect_cb) {
                conn->connect_cb(conn, MARIADB_ERR, conn->dr);
            }
            if (conn->is_pooled) {
                mariadb_pool_reclaim_conn(conn);
            } else {
                mariadb_conn_free(conn);
            }
            return MARIADB_ERR;
        }
        conn->fd = mysql_get_socket(&conn->mysql);

        if (status) {
            conn->state = CONN_STATE_CONNECTING;
        } else {
            if (!conn->mysql_ret) {
                msg->err("[FD %i] MariaDB Connect Error: %s", conn->fd,
                         mysql_error(&conn->mysql));
                if (conn->connect_cb) {
                    conn->connect_cb(conn, MARIADB_ERR, conn->dr);
                }
                if (conn->is_pooled) {
                    mariadb_pool_reclaim_conn(conn);
                } else {
                    mariadb_conn_free(conn);
                }
                return MARIADB_ERR;
            }
            conn->state = CONN_STATE_CONNECTED;
            if (conn->connect_cb) {
                conn->connect_cb(conn, MARIADB_OK, conn->dr);
            }
        }

        event->add(conn->fd, DUDA_EVENT_READ, DUDA_EVENT_LEVEL_TRIGGERED,
                   mariadb_on_read, mariadb_on_write, mariadb_on_error,
                   mariadb_on_close, mariadb_on_timeout, NULL);

        struct mk_list *conn_list = pthread_getspecific(mariadb_conn_list);
        if (!conn_list) {
            conn_list = monkey->mem_alloc(sizeof(struct mk_list));
            if (!conn_list) {
                if (conn->is_pooled) {
                    mariadb_pool_reclaim_conn(conn);
                } else {
                    mariadb_conn_free(conn);
                }
                return MARIADB_ERR;
            }
            mk_list_init(conn_list);
            pthread_setspecific(mariadb_conn_list, (void *) conn_list);
        }
        mk_list_add(&conn->_head, conn_list);
        /* handle pending queries on connected */
        if (conn->state == CONN_STATE_CONNECTED) {
            __mariadb_handle_query(conn);
        }
    }

    return MARIADB_OK;
}

void mariadb_disconnect(mariadb_conn_t *conn, mariadb_disconnect_cb *cb)
{
    if (!conn->disconnect_cb) {
        conn->disconnect_cb = cb;
    }

    if (conn->state != CONN_STATE_CONNECTED) {
        conn->disconnect_on_finish = 1;
        return;
    }
    event->delete(conn->fd);
    __mariadb_handle_disconnect(conn, MARIADB_OK);
    return;
}

int mariadb_on_read(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] MariaDB Connection Handler / read", fd);
    mariadb_conn_t *conn = mariadb_get_conn(fd);

    if (!conn) {
        msg->err("[FD %i] Error: MariaDB Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }

    int status;
    switch (conn->state) {
    case CONN_STATE_CONNECTING:
        status = mysql_real_connect_cont(&conn->mysql_ret, &conn->mysql,
                                         MYSQL_WAIT_READ);
        if (!status) {
            if (!conn->mysql_ret) {
                msg->err("[FD %i] MariaDB Connect Error: %s", fd,
                         mysql_error(&conn->mysql));
                if (conn->connect_cb)
                    conn->connect_cb(conn, MARIADB_ERR, conn->dr);
                return DUDA_EVENT_CLOSE;
            }
            conn->state = CONN_STATE_CONNECTED;
            if (conn->connect_cb)
                conn->connect_cb(conn, MARIADB_OK, conn->dr);
            __mariadb_handle_query(conn);
        }
        break;
    case CONN_STATE_QUERYING:
        status = mysql_real_query_cont(&conn->current_query->error, &conn->mysql,
                                       MYSQL_WAIT_READ);
        if (!status) {
            if (conn->current_query->error) {
                msg->err("[FD %i] MariaDB Query Error: %s", conn->fd,
                         mysql_error(&conn->mysql));
                /* may add a query on error cb to be called here */
                mariadb_query_free(conn->current_query);
                conn->state = CONN_STATE_CONNECTED;
                __mariadb_handle_query(conn);
            } else {
                conn->state = CONN_STATE_QUERIED;
                __mariadb_handle_result(conn);
                if (conn->state == CONN_STATE_CONNECTED) {
                    __mariadb_handle_query(conn);
                }
            }
        }
        break;
    case CONN_STATE_ROW_FETCHING:
        while (1) {
            status = mysql_fetch_row_cont(&conn->current_query->row,
                                          conn->current_query->result,
                                          MYSQL_WAIT_READ);
            if (status) {
                break;
            }
            conn->state = CONN_STATE_ROW_FETCHED;
            if (!conn->current_query->row) {
                if (mysql_errno(&conn->mysql) > 0) {
                    msg->err("[FD %i] MariaDB Fetch Result Row Error: %s", conn->fd,
                             mysql_error(&conn->mysql));
                } else {
                    if (conn->current_query->end_cb) {
                        conn->current_query->end_cb(conn->current_query,
                                                          conn->dr);
                    }
                }
                __mariadb_handle_result_free(conn);
                if (conn->state == CONN_STATE_CONNECTED) {
                    __mariadb_handle_query(conn);
                }
                break;
            }
            if (conn->current_query->row_cb) {
                conn->current_query->row_cb(conn->current_query->row_cb_privdata,
                                                  conn->current_query->n_fields,
                                                  conn->current_query->fields,
                                                  conn->current_query->row,
                                                  conn->dr);
            }
        }
        break;
    case CONN_STATE_RESULT_FREEING:
        status = mysql_free_result_cont(conn->current_query->result, MYSQL_WAIT_READ);
        if (!status) {
            conn->state = CONN_STATE_RESULT_FREED;
            if ((conn->config.client_flag & CLIENT_MULTI_STATEMENTS) == 0) {
                conn->current_query = NULL;
                mariadb_query_free(conn->current_query);
                conn->state = CONN_STATE_CONNECTED;
                __mariadb_handle_query(conn);
            } else {
                __mariadb_handle_next_result(conn);
            }
        }
        break;
    case CONN_STATE_NEXT_RESULTING:
        while (1) {
            status = mysql_next_result_cont(&conn->current_query->error,
                                            &conn->mysql, MYSQL_WAIT_READ);
            if (status) {
                break;
            }
            conn->state = CONN_STATE_NEXT_RESULTED;
            if (conn->current_query->error == -1) {
                conn->current_query = NULL;
                mariadb_query_free(conn->current_query);
                conn->state = CONN_STATE_CONNECTED;
                break;
            } else if (conn->current_query->error == 0) {
                __mariadb_handle_result(conn);
                if (conn->state != CONN_STATE_RESULT_FREED) {
                    break;
                }
            } else {
                msg->err("[FD %i] MariaDB Execute Statement Error: %s", conn->fd,
                         mysql_error(&conn->mysql));
            }
        }
        if (conn->state == CONN_STATE_CONNECTED) {
            __mariadb_handle_query(conn);
        }
        break;
    default:
        break;
    }
    return DUDA_EVENT_OWNED;
}

int mariadb_on_write(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] MariaDB Connection Hander / write", fd);

    mariadb_conn_t *conn = mariadb_get_conn(fd);
    if (!conn) {
        msg->err("[fd %i] Error: MariaDB Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }
    return DUDA_EVENT_OWNED;
}

int mariadb_on_error(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] MariaDB Connection Handler / error", fd);

    mariadb_conn_t *conn = mariadb_get_conn(fd);
    if (!conn) {
        msg->err("[fd %i] Error: MariaDB Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }

    if (conn->current_query) {
        if (conn->current_query->result) {
            mysql_free_result(conn->current_query->result);
        }
    }
    __mariadb_handle_disconnect(conn, MARIADB_ERR);

    return DUDA_EVENT_OWNED;
}

int mariadb_on_close(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] MariaDB Connection Handler / close", fd);

    mariadb_conn_t *conn = mariadb_get_conn(fd);
    if (!conn) {
        msg->err("[fd %i] Error: MariaDB Connection Not Found", fd);
        return DUDA_EVENT_CLOSE;
    }
    __mariadb_handle_disconnect(conn, MARIADB_ERR);
    return DUDA_EVENT_CLOSE;
}

int mariadb_on_timeout(int fd, void *data)
{
    (void) data;
    msg->info("[FD %i] MariaDB Connection Handler / timeout", fd);
    return DUDA_EVENT_OWNED;
}
