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
#include "util.h"

/*
 * @METHOD_NAME: escape_literal
 * @METHOD_DESC: Escape a string for use within an SQL command. This is useful when inserting data values as literal constants in SQL commands.
 * @METHOD_PROTO: char *escape_literal(postgresql_conn_t *conn, const char *str, size_t length)
 * @METHOD_PARAM: conn The PostgreSQL connection handle, it must be a valid, open connection.
 * @METHOD_PARAM: str The literal string to be escaped.
 * @METHOD_PARAM: length The length of parameter str.
 * @METHOD_RETURN: On success an escaped version of the str parameter in memory allocated with malloc() is returned. This memory should be freed using postgresql->free() when the result is no longer needed. On error it will return NULL.
 */

char *postgresql_util_escape_literal(postgresql_conn_t *conn, const char *str,
                                     size_t length)
{
    char *escaped = PQescapeLiteral(conn->conn, str, length);
    if (!escaped) {
        msg->err("[FD %i] PostgreSQL Escape Literal Error: %s", PQerrorMessage(conn->conn));
    }
    return escaped;
}

/*
 * @METHOD_NAME: escape_identifier
 * @METHOD_DESC:  Escape a string for use as an SQL identifier, such as a table, column, or function name. This is useful when a user-supplied identifier might contain special characters that would otherwise not be interpreted as part of the identifier by the SQL parser, or when the identifier might contain upper case characters whose case should be preserved.
 * @METHOD_PROTO: char *escape_identifier(postgresql_conn_t *conn, const char *str, size_t length)
 * @METHOD_PARAM: conn The PostgreSQL connection handle, it must be a valid, open connection.
 * @METHOD_PARAM: str The identifier string to be escaped.
 * @METHOD_PARAM: length The length of parameter str.
 * @METHOD_RETURN: On success an escaped version of the str parameter in memory allocated with malloc() is returned. This memory should be freed using postgresql->free() when the result is no longer needed. On error it will return NULL.
 */

char *postgresql_util_escape_identifier(postgresql_conn_t *conn, const char *str,
                                        size_t length)
{
    char *escaped = PQescapeIdentifier(conn->conn, str, length);
    if (!escaped) {
        msg->err("[FD %i] PostgreSQL Escape Identifier Error: %s", PQerrorMessage(conn->conn));
    }
    return escaped;
}

/*
 * @METHOD_NAME: escape_binary
 * @METHOD_DESC: Escape binary data for use within an SQL command.
 * @METHOD_PROTO: unsigned char *escape_binary(postgresql_conn_t *conn, const unsigned char *from, size_t from_length, size_t *to_length)
 * @METHOD_PARAM: conn The PostgreSQL connection handle, it must be a valid, open connection.
 * @METHOD_PARAM: from The string to be escaped.
 * @METHOD_PARAM: from_length The number of bytes in this binary string.
 * @METHOD_PARAM: to_length A variable that will hold the resultant escaped string length.
 * @METHOD_RETURN: On success an escaped version of the from parameter binary string in memory allocated with malloc() is returned. This memory should be freed using postgresql->free() when the result is no longer needed. On error it will return NULL.
 */

unsigned char *postgresql_util_escape_binary(postgresql_conn_t *conn,
                                             const unsigned char *from,
                                             size_t from_length,
                                             size_t *to_length)
{
    unsigned char *escaped = PQescapeByteaConn(conn->conn, from, from_length, to_length);
    if (!escaped) {
        msg->err("[FD %i] PostgreSQL Escape Binary Error: %s", PQerrorMessage(conn->conn));
    }
    return escaped;
}

/*
 * @METHOD_NAME: unescape_binary
 * @METHOD_DESC: Convert a string representation of binary data into binary data, it is the reverse of method escape_binary.
 * @METHOD_PROTO: unsigned char *unescape_binary(const unsigned char *from, size_t *to_length)
 * @METHOD_PARAM: from The text representation of binary data to be unescaped.
 * @METHOD_PARAM: to_length A variable that will hold the resultant unescaped string length.
 * @METHOD_RETURN: On success the binary representation of the from parameter sting in memory allocated with malloc() is returned. This memory should be freed using postgresql->free() when the result is no longer needed. On error it will return NULL.
 */

unsigned char *postgresql_util_unescape_binary(const unsigned char *from,
                                               size_t *to_length)
{
    return PQunescapeBytea(from, to_length);
}

/*
 * @METHOD_NAME: free
 * @METHOD_DESC: Free memory allocated by libpq.
 * @METHOD_PROTO: void free(void *ptr)
 * @METHOD_PARAM: ptr The pointer to the memory space that will be freed.
 * @METHOD_RETURN: None.
 */

void postgresql_util_free(void *ptr)
{
    PQfreemem(ptr);
}
