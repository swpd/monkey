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
#include "duda_package.h"
#include "mariadb.h"
#include "query_priv.h"
#include "connection_priv.h"
#include "pool.h"

mariadb_object_t *get_mariadb_api()
{
    mariadb_object_t *mariadb;

    /* Alloc MariaDB object */
    mariadb = monkey->mem_alloc(sizeof(mariadb_object_t));

    /* Map API calls */
    mariadb->init_conn     = mariadb_conn_init;
    mariadb->pool_get_conn = mariadb_pool_get_conn;
    mariadb->ssl_set       = mariadb_conn_ssl_set;
    mariadb->connect       = mariadb_connect;
    mariadb->disconnect    = mariadb_disconnect;
    mariadb->escape        = mariadb_real_escape_string;
    mariadb->query         = mariadb_query;
    mariadb->abort         = mariadb_query_abort;

    return mariadb;
}

duda_package_t *duda_package_main(struct duda_api_objects *api)
{
    duda_package_t *dpkg;

    duda_package_init();

    global->init(&mariadb_conn_list, NULL);
    global->init(&mariadb_conn_pool, NULL);

    dpkg          = monkey->mem_alloc(sizeof(duda_package_t));
    dpkg->name    = "MariaDB";
    dpkg->version = "0.1";
    dpkg->api     = get_mariadb_api();

    return dpkg;
}
