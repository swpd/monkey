Duda PostgreSQL package
=======================

This package aims to make the popular relational database PostgreSQL available to
the Duda framework.

## Introduction ##
This package is built with the asynchronous APIs that come with `libpq`. All
operations to the databases work in a non-blocking, asynchronous manner, which
makes it suitable to be used with the Monkey HTTP Daemon.

## Installation ##
This package has not been included in the official Duda release, so it must be
installed individually.

There are two ways to install it:

### Manually ###
clone the Monkey HTTP Daemon repository:

    git clone git://git.monkey-project.com/monkey

change to Monkey's directory:

    cd monkey/

clone the Duda Web Framework repository into the plugins subdirectory of Monkey:

    git clone git://git.monkey-project.com/duda plugins/duda/

clone this package into the packages subdirectory of Duda:

    git clone https://github.com/swpd/duda_postgresql plugins/duda/packages/postgresql

add postgresql package to documentation index:

    echo postgresql >> plugins/duda/docs/index.doc

edit `plugins/duda/package/Makefile.in` with your favorite text editor, add postgresql
package to variable DIRS, make sure it looks like:

    DIRS    = base64 json sha1 sha256 kv sqlite websocket ssls postgresql

#### Configure & Build ####

configure monkey with duda plugin enabled:

    ./configure --enable-plugins=duda

if you prefer verbose message output of monkey, configure it with `--trace` option
(this option should not be used in a production environment):

    ./configure --enable-plugins=duda --trace

build Monkey(go get yourself a cup of coffee, it might take a while):

    make

### All in One ###

clone the Monkey HTTP Daemon repository from my Github:

    git clone https://github.com/swpd/monkey

change to Monkey's directory:

    cd monkey

check out `postgresql` branch:

    git checkout postgresql

That's it, configure Monkey and build it (refer to [this](#configure--build))

## Usage ##
To start with this package, the header file shall be included in your duda web
service:

    #include "packages/postgresql/postgresql.h"

Also, don't forget to load the package in the `duda_main()` function of your web
service:

    int duda_main()
    {
        duda_load_package(postgresql, "postgresql");
        ...
    }

### Establish Connections ###
There are two methods you may use to establish a connection to your PostgreSQL
server:

#### connect: ####
    
    void some_request_callback(duda_request_t *dr)
    {
        const char *keys[] = {
            "user",
            "password",
            "dbname",
            NULL /* don't forget the terminate NULL */
        };
        const char *values[] = {
            "postgres",
            "foo",
            "test",
            NULL
        }

        response->http_status(dr, 200);
        response->http_header_n(dr, "Content-Type: text/plain", 24);

        /* we suspend the request before we get results from PostgreSQL server. */
        response->wait(dr);

        /* try to establish a connection */
        postgresql_conn_t *conn = postgresql->connect(dr, on_connect_callback,
                                                      keys, values, 0);
        if (!conn) {
            response->cont(dr);
            response->end(dr, NULL);
        }
        ...
        /* issue some queries or other stuffs to the connection */
    }

#### connect_uri: ####

    void some_request_callback(duda_request_t *dr)
    {
        response->http_status(dr, 200);
        response->http_header_n(dr, "Content-Type: text/plain", 24);

        /* we suspend the request before we get results from PostgreSQL server. */
        response->wait(dr);

        /* try to establish a connection */
        postgresql_conn_t *conn = postgresql->connect_uri(dr, on_connect_callback,
                                                          "user=postgres password=foo dbname=test");
        if (!conn) {
            response->cont(dr);
            response->end(dr, NULL);
        }
        ...
        /* issue some queries or other stuffs to the connection */
    }

### Terminate Connections ###
Terminating a connection is done as follows:

    void some_request_callback(duda_request_t* dr)
    {
        ...
        postgresql->disconnect(conn, on_disconnect_callback);
        ...
    }
    

Notice: `postgresql->disconnect` doesn't close the connection immediately if the
connection still have some pending queries to process, it just notify the
connection to close when all enqueued queries are finished. This makes the
connection termination process more graceful.

### Connection Pooling ###
Establishing a connection for every request may work fine for low concurrency,
but the overhead will become obvious when it got high, considerable time and
resource will be spent on connecting and closing connections.

This is when connection pooling comes into being, we created a pool of connections
and when a request requires one, we pick one connections from the pool, after using
the connection is returned back to the pool. This reduce the overhead by reusing
the connected connections.

To create a pool, you shall define a global variable for the pool in your web
service, and initialize it in `duda_main()`.

Just like there are two ways to establish a connection, the `postgresql` package
offers you two ways to create a connection pool:

#### create_pool_params: ####

    duda_global_t some_pool;

    int duda_main()
    {
        const char *keys[] = {
            "user",
            "password",
            "dbname",
            NULL /* don't forget the terminate NULL */
        };
        const char *values[] = {
            "postgres",
            "foo",
            "test",
            NULL
        }

        ...
        /* initialize a connection pool */
        duda_global_init(&some_pool, NULL, NULL);
        postgresql->create_pool_params(&some_pool, 0, 0, keys, vals, 0);
        ...
    }

#### create_pool_uri: ####

    duda_global_t some_pool;

    int duda_main()
    {
        ...
        /* initialize a connection pool */
        duda_global_init(&some_pool, NULL, NULL);
        postgresql->create_pool_uri(&some_pool, 0, 0, "user=postgres password=foo dbname=test");
        ...
    }
    
When a duda request claims for a connection, we can borrow one from the pool:

    void some_request_callback(duda_request_t* dr)
    {
        ...
        postgresql_conn_t *conn = postgresql->get_conn(&demo_pool, dr, on_connect_callback);
        ...
    }

And when the connection is no longer needed, we return it back to the pool by using
the same call when we terminate a connection, only this time it doesn't close the
connection, it just mark it as usable again:

    void some_request_callback(duda_request_t* dr)
    {
        ...
        postgresql->disconnect(conn, on_disconnect_callback);
        ...
    }

### Secure Connections ###
The SSL support for PostgreSQL client-side can be enabled by editing the configuration
file of PostgreSQL. For full reference please refer to the official documentation
[here](http://www.postgresql.org/docs/9.2/static/libpq-ssl.html).

### Issue Queries ###
We can issue queries once we get a PostgreSQL connection from the package, the queries
will be enqueued into that connection and be processed one by one.

As always, we got variant methods to send a query to the PostgreSQL server.

#### query: ####

    postgresql->query(conn, "SELECT * FROM demo", on_result_available_callback,
                      on_row_callback, on_finish_processing_callback, NULL);

#### query_params: ####
    
This method got a long list of parameters. For the meaning of parameter you can
either refer to the [official documentation](http://www.postgresql.org/docs/9.2/static/libpq-exec.html)
of PostgreSQL or the full APIs [reference](#api-documentation) of this package.

    const char *vals[] = "target";
    postgresql->query_params(conn, "SELECT * FROM demo WHERE t = $1",
                             1,          /* one param */
                             vals, NULL, /* don't need param lengths */
                             NULL,       /* default to all text params */
                             0,          /* ask for text result */
                             on_result_available_callback, on_row_callback,
                             on_finish_processing_callback, NULL);

#### query_prepared: ####

This method shall be used to send a request to execute prepared statments that have
been submitted to the PostgreSQL server. Remember to use one of the two methods
described above to send a prepared statements before you call this method.

    /* submit a prepared statements, stmt_name serves as an identity. */
    postgresql->query(conn, "PREPARE stmt_name (int, text) AS \
                      INSERT INTO demo VALUES($1, $2)", on_result_available_callback,
                      on_row_callback, on_finish_processing_callback, NULL);

    
    const char *vals[] = {"1", "username"};
    postgresql->query_prepared(conn, "stmt_name", 2, vals, NULL, NULL, 0,
                               on_result_available_callback, on_row_callback,
                               on_finish_processing_callback, NULL);

Note: By default all the `query` sibling methods will let the server infers a data
type for the parameter symbol. If you want to specify type of parameter by yourself
or the server can not detect the correct type, you can attach an explicit cast to
the parameter symbol to show what data type you will send. For example:

    SELECT * FROM mytable WHERE x = $1::bigint;

### Escape Query String ###
We may need to escape a query string to make sure that all the special characters
in that string are encoded. To prevent SQL injection attacks, it is important to
do proper escaping when handling strings received from untrustworthy source.

There are 4 methods related to string escape in the postgresql package:

#### escape_literal: ####
    
This method escapes a string for use within an SQL command.

    const char *query = "SELECT * FROM demo";
    char *escaped_query = postgresql->escape_literal(conn, query, strlen(query));
    ...
    /* do something with the query */
    postgresql->free(escaped_query); /* remember to free the dynamic allocated memory */
    ...

#### escape_identifier: ####

This method escapes a string for use as an SQL identifier, such as a table, column,
or function name.

    const char *table_name = "demo_table";
    char *escaped_table_name = postgresql->escape_identifier(conn, table_name, strlen(table_name));

#### escape_binary: ####

Thie method escapes binary data for use within an SQL command.

    const unsigned char *binary_data = ...; /* n bytes */
    size_t to_length;
    unsigned char *escaped_binary_data = postgresql->escape_binary(conn, binary_data, n, &to_length);
    ...

#### unescape_binary: ####

This method converts a string representation of binary data into binary data.

    const unsigned char *escaped_binary_data = ...;
    size_t to_length;
    unsigned char *binary_data = postgresql->unescape_binary(escaped_binary_data, &to_length);
    ...

### Abort Query ###
A query can be aborted while it is being processed, if abort takes actions before
the query has been passed to the server, it is simply dropped, otherwise a cancel
request will be sent to the server and the current query will be terminated.
    
    postgresql->abort(query);

### API Documentation ###
For full API reference of this package, please consult `plugins/duda/docs/html/packages/postgresql.html`.
