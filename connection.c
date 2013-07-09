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

#include "connection.h"

int mariadb_conn_add_query(mariadb_conn_t *conn, const char *query_str,
                           mariadb_query_row_cb *row_cb, void *privdata)
{
    mariadb_query_t *query = monkey->mem_alloc(sizeof(mariadb_query_t));
    if (!query)
        return MARIADB_ERR;
    query->query_str       = monkey->str_dup(query_str);
    query->row_callback    = row_cb;
    query->privdata        = privdata;
    query->error           = 0;
    query->result          = NULL;
    query->abort           = QUERY_ABORT_NONE;
    mk_list_add(&query->_head, &conn->queries);
    return MARIADB_OK;
}
