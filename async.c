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
#include "query_priv.h"
#include "connection_priv.h"
#include "async.h"

void postgresql_async_handle_query(postgresql_conn_t *conn)
{
    int status;
    while (mk_list_is_empty(&conn->queries) != 0) {
        postgresql_query_t *query = mk_list_entry_first(&conn->queries,
                                                        postgresql_query_t, _head);
        conn->current_query = query;
        status = PQsendQuery(conn->conn, query->query_str);
        if (status != 1) {
            postgresql_query_free(query);
            continue;
        }

        status = PQsetSingleRowMode(conn->conn);
        if (status != 1) {
            msg->info("[FD %i] PostgreSQL Fail to Set Single Row Mode: %s", conn->fd,
                      PQerrorMessage(conn->conn));
            query->single_row_mode = 0;
        } else {
            query->single_row_mode = 1;
        }

        status = PQflush(conn->conn);
        if (status == -1) {
            msg->err("[FD %i] PostgreSQL Send Query Error: %s", conn->fd,
                     PQerrorMessage(conn->conn));
            postgresql_query_free(query);
        } else if (status == 0) {
            /* successfully send query */
            conn->state = CONN_STATE_QUERIED;
            event->mode(conn->fd, DUDA_EVENT_READ, DUDA_EVENT_LEVEL_TRIGGERED);
            postgresql_async_handle_row(conn);
            if (conn->state != CONN_STATE_CONNECTED) return;
        } else if (status == 1) {
            conn->state = CONN_STATE_QUERYING;
            event->mode(conn->fd, DUDA_EVENT_WRITE, DUDA_EVENT_LEVEL_TRIGGERED);
            return;
        }
    }
    conn->current_query = NULL;
    conn->state         = CONN_STATE_CONNECTED;

    event->mode(conn->fd, DUDA_EVENT_SLEEP, DUDA_EVENT_LEVEL_TRIGGERED);
    if (conn->disconnect_on_finish) {
        postgresql_conn_handle_release(conn, POSTGRESQL_OK);
    }
}

void postgresql_async_handle_row(postgresql_conn_t *conn)
{
    int status, i, j;
    postgresql_query_t *query = conn->current_query;

    while (1) {
        status = PQconsumeInput(conn->conn);
        if (status == 0) {
            msg->err("[FD %i] PostgreSQL Consume Input Error: %s", conn->fd,
                     PQerrorMessage(conn->conn));
            postgresql_query_free(query);
        }

        status = PQisBusy(conn->conn);
        if (status != 0) {
            conn->state = CONN_STATE_ROW_FETCHING;
            break;
        }

        conn->state = CONN_STATE_ROW_FETCHED;
        conn->res = PQgetResult(conn->conn);
        if (conn->res) {
            if (conn->single_row_mode) {
                if (PQResultStatus(conn->res) == PGRES_SINGLE_TUPLE) {
                    if (query->n_fields == 0) {
                        query->n_fields = PQnfields(conn->res);
                    }

                    if (!query->fields) {
                        query->fields = monkey->mem_alloc(sizeof(char *) * query->n_fields);
                        for (i = 0; i < query->n_fields; ++i) {
                            query->fields[i] = monkey->str_dup(PQfname(res, i));
                        }
                    }

                    if (query->result_start == 0) {
                        if (query->result_cb) {
                            query->result_cb(query->privdata, query, );
                        }
                        query->result_start = 1;
                    }

                    query->values = monkey->mem_alloc(sizeof(char *) * query->n_fields);
                    for (i = 0; i < query->n_fields; ++i) {
                        query->values[i] = monkey->str_dup(PQgetvalue(res, 0, i));
                    }

                    if (query->row_cb) {
                        query->row_cb(query->privdata, query, query->n_fields,
                                      query->fields, query->values, conn->dr);
                    }

                    /* free row */
                    for (i = 0; i < query->n_fields; ++i) {
                        FREE(query->values[i]);
                    }
                    FREE(query->values);
                } else if (PQResultStatus(conn->res) == PGRES_TUPLES_OK) {
                    for (i = 0; i < query->n_fields; ++i) {
                        FREE(query->fields[i]);
                    }
                    FREE(query->fields);
                    query->n_fields = 0;
                } else {
                    msg->err("[FD %i] PostgreSQL Get Result Error: %s", conn->fd,
                             PQerrorMessage(conn->conn));
                }
            } else {
                if (PQResultStatus(conn->res) == PGRES_TUPLES_OK) {
                    if (query->n_fields == 0) {
                        query->n_fields = PQnfields(conn->res);
                    }

                    if (!query->fields) {
                        query->fields = monkey->mem_alloc(sizeof(char *) * query->n_fields);
                        for (i = 0; i < query->n_fields; ++i) {
                            query->fields[i] = monkey->str_dup(PQfname(res, i));
                        }
                    }

                    if (query->result_start == 0) {
                        if (query->result_cb) {
                            query->result_cb(query->privdata, query, );
                        }
                        query->result_start = 1;
                    }

                    for (i = 0; i < PQntuples(conn->res); ++i) {
                        query->values = monkey->mem_alloc(sizeof(char *) * query->n_fields);
                        for (j = 0; j < query->n_fields; ++j) {
                            query->values[j] = monkey->str_dup(PQgetvalue(res, i, j));
                        }
                        if (query->row_cb) {
                            query->row_cb(query->privdata, query, query->n_fields,
                                          query->fields, query->values, conn->dr);
                        }
                        for (j = 0; j < query->n_fields; ++j) {
                            FREE(query->values[j]);
                        }
                        FREE(query->values);
                    }

                    for (i = 0; i < query->n_fields; ++i) {
                        FREE(query->fields[i]);
                    }
                    FREE(query->fields);
                    query->n_fields = 0;
                } else {
                    msg->err("[FD %i] PostgreSQL Get Result Error: %s", conn->fd,
                             PQerrorMessage(conn->conn));
                }
            }
            PQclear(conn->res);
        } else {
            /* no more results */
            if (query->end_cb) {
                query->end_cb(query->privdata, query, conn->dr);
            }
            postgresql_query_free(query);
            conn->state = CONN_STATE_CONNECTED;
            break;
        }
    }
}
