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

/*
 * @OBJ_NAME: postgresql
 * @OBJ_MENU: PostgreSQL
 * @OBJ_DESC: The PostgreSQL package exposes a set of methods to communicate with
 * the PostgreSQL relational database.
 * @PKG_HEADER: #include "packages/postgresql/postgresql.h"
 * @PKG_INIT: duda_load_package(postgresql, "postgresql");
 */

#include <libpq-fe.h>
#include "duda_package.h"
#include "postgresql.h"
#include "query_priv.h"
#include "connection_priv.h"
#include "util.h"
#include "pool.h"

postgresql_object_t *get_postgresql_api()
{
    postgresql_object_t *postgresql;

    /* Alloc PostgreSQL object */
    postgresql = monkey->mem_alloc(sizeof(postgresql_object_t));

    /* Map API calls */

    postgresql->connect            = postgresql_conn_connect;
    postgresql->connect_uri        = postgresql_conn_connect_uri;
    postgresql->create_pool_params = postgresql_pool_params_create;
    postgresql->create_pool_uri    = postgresql_pool_uri_create;
    postgresql->get_conn           = postgresql_pool_get_conn;
    postgresql->query              = postgresql_conn_send_query;
    postgresql->query_params       = postgresql_conn_send_query_params;
    postgresql->query_prepared     = postgresql_conn_send_query_prepared;
    postgresql->escape_literal     = postgresql_util_escape_literal;
    postgresql->escape_identifier  = postgresql_util_escape_identifier;
    postgresql->escape_binary      = postgresql_util_escape_binary;
    postgresql->unescape_binary    = postgresql_util_unescape_binary;
    postgresql->abort              = postgresql_query_abort;
    postgresql->free               = postgresql_util_free;
    postgresql->disconnect         = postgresql_conn_disconnect;

    return postgresql;
}

duda_package_t *duda_package_main()
{
    duda_package_t *dpkg;

    duda_global_init(&postgresql_conn_list, NULL, NULL);
    mk_list_init(&postgresql_pool_config_list);

    dpkg          = monkey->mem_alloc(sizeof(duda_package_t));
    dpkg->name    = "PostgreSQL";
    dpkg->version = "0.1";
    dpkg->api     = get_postgresql_api();

    return dpkg;
}
