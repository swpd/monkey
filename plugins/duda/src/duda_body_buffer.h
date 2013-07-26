/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Duda I/O
 *  --------
 *  Copyright (C) 2012-2013, Eduardo Silva P. <edsiper@gmail.com>
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

#ifndef DUDA_BODY_BUFFER_H
#define DUDA_BODY_BUFFER_H

/*
 * The response body holds an IOV array struct of BODY_BUFFER_SIZE,
 * when the limit is reached, the pointer is reallocated adding a new chunk
 */
#define BODY_BUFFER_SIZE  8


struct duda_body_buffer {
    struct mk_iov *buf;
    unsigned short int size;
    unsigned long int sent;
};

struct duda_body_buffer *duda_body_buffer_new();
int duda_body_buffer_expand(struct duda_body_buffer *bb);
int duda_body_buffer_flush(int sock, struct duda_body_buffer *bb);

#endif
