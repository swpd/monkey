#include "webservice.h"
#include "packages/json/json.h"
#include "packages/postgresql/postgresql.h"

DUDA_REGISTER("PostgreSQL package usage demonstration", "postgresql_demo");

static inline void print_header(duda_request_t *dr)
{
    response->printf(dr, "\
<!DOCTYPE html>\
<html lang=\"en\">\
<head>\
  <meta charset=\"UTF-8\">\
  <title>PostgreSQL Package Demo</title>\
  <link rel=\"stylesheet\" href=\"//netdna.bootstrapcdn.com/bootstrap/3.0.0-rc1/css/bootstrap.min.css\">\
  <style type=\"text/css\">\
    body {\
      position: relative;\
      padding-top: 80px;\
    }\
  </style>\
</head>\
<body>\
  <div class=\"navbar navbar-inverse navbar-fixed-top\">\
    <div class=\"container\">\
      <a href=\"#\" class=\"navbar-brand\">PostgreSQL Package</a>\
      <ul class=\"nav navbar-nav\">\
        <li id=\"home\">\
          <a href=\"/postgresql_demo/postgresql/home/\">Home</a>\
        </li>\
        <li id=\"dashboard\">\
          <a href=\"/postgresql_demo/postgresql/dashboard/\">Dashboard</a>\
        </li>\
      </ul>\
    </div>\
  </div>");
}

static inline void print_footer(duda_request_t *dr, const char *js)
{
    response->printf(dr, "\
  <script src=\"//ajax.googleapis.com/ajax/libs/jquery/1.10.1/jquery.min.js\"></script>\
  <script src=\"//netdna.bootstrapcdn.com/bootstrap/3.0.0-rc1/js/bootstrap.min.js\"></script>");
    if (js) {
        response->printf(dr, js);
    }
    response->printf(dr, "\
</body>\
</html>");
}

void cb_on_connect(postgresql_conn_t *conn, int status, duda_request_t *dr)
{
    if (status != POSTGRESQL_OK) {
        response->printf(dr, "Can't connect to PostgreSQL server\n");
        response->cont(dr);
        response->end(dr, NULL);
    }
}

void cb_on_disconnect(postgresql_conn_t *conn, int status, duda_request_t *dr)
{
    if (status != POSTGRESQL_OK) {
        response->printf(dr, "Disconnect due to some errors\n");
    }
    response->cont(dr);
    response->end(dr, NULL);
}

void cb_ls_row_simple(void *data, postgresql_query_t *query, int n_fields,
                      char **fields, char **values, duda_request_t *dr)
{
    json_t *array = data;
    json_t *item;
    (void) query;
    (void) fields;
    (void) dr;
    if (n_fields == 1) {
        item = json->create_string(values[0]);
        json->add_to_array(array, item);
    }
}

void cb_ls_result(void *data, postgresql_query_t *query, int n_fields,
                  char **fields, duda_request_t *dr)
{
    json_t *root = data;
    json_t *array;
    json_t *item;
    (void) query;
    (void) dr;
    unsigned long i;
    array = json->create_array();
    for (i = 0; i < n_fields; ++i) {
        item = json->create_string(fields[i]);
        json->add_to_array(array, item);
    }
    json->add_to_object(root, "fields", array);
    array = json->create_array();
    json->add_to_object(root, "rows", array);
}

void cb_ls_row(void *data, postgresql_query_t *query, int n_fields, char **fields,
               char **values, duda_request_t *dr)
{
    json_t *root = data;
    json_t *array = json->get_object_item(root, "rows");
    json_t *item = json->create_object();
    json_t *column;
    (void) query;
    (void) dr;
    unsigned long i;
    for (i = 0; i < n_fields; ++i) {
        if (values[i] == NULL) {
            column = json->create_null();
        } else {
            column = json->create_string(values[i]);
        }
        json->add_to_object(item, fields[i], column);
    }
    json->add_to_array(array, item);
}

void cb_ls_end(void *data, postgresql_query_t *query, duda_request_t *dr)
{
    (void) query;
    json_t *array = data;
    char *encoded_str = json->print_gc(dr, array);
    response->printf(dr, "%s", encoded_str);
}

void cb_list_databases(duda_request_t *dr)
{
    json_t *db_array = json->create_array();
    response->http_status(dr, 200);
    response->http_header_n(dr, "Content-Type: application/json", 30);
    response->wait(dr);

    postgresql_conn_t *conn = postgresql->connect_uri(dr, cb_on_connect,
                                                      "user=postgres password=foo");
    if (!conn) {
        response->printf(dr, "No Connection Available\n");
        response->cont(dr);
        response->end(dr, NULL);
    }

    postgresql->query(conn, "SELECT datname FROM pg_database WHERE datistemplate = false",
                      NULL, cb_ls_row_simple, cb_ls_end, (void *) db_array);
    postgresql->disconnect(conn, cb_on_disconnect);
}

void cb_list_tables(duda_request_t *dr)
{
    json_t *tb_array = json->create_array();
    char *db = param->get(dr, 0);
    char uri[128];

    response->http_status(dr, 200);
    response->http_header_n(dr, "Content-Type: application/json", 30);
    response->wait(dr);

    sprintf(uri, "user=postgres password=foo dbname=%s", db);
    postgresql_conn_t *conn = postgresql->connect_uri(dr, cb_on_connect, uri);
    if (!conn) {
        response->printf(dr, "No Connection Available\n");
        response->cont(dr);
        response->end(dr, NULL);
    }

    postgresql->query(conn, "SELECT table_name FROM information_schema.tables \
                      WHERE table_schema = 'public'", NULL, cb_ls_row_simple,
                      cb_ls_end, (void *)tb_array);
    postgresql->disconnect(conn, cb_on_disconnect);
}

void cb_list_rows(duda_request_t *dr)
{
    json_t *row_root = json->create_object();
    char *db    = param->get(dr, 0);
    char *table = param->get(dr, 1);
    long page;
    param->get_number(dr, 2, &page);
    char uri[128];
    char select_query[128];
    int page_size = 20;

    response->http_status(dr, 200);
    response->http_header_n(dr, "Content-Type: application/json", 30);
    response->wait(dr);

    sprintf(uri, "user=postgres password=foo dbname=%s", db);
    postgresql_conn_t *conn = postgresql->connect_uri(dr, cb_on_connect, uri);
    if (!conn) {
        response->printf(dr, "No Connection Available\n");
        response->cont(dr);
        response->end(dr, NULL);
    }

    sprintf(select_query, "SELECT * FROM %s LIMIT %d OFFSET %ld", table, page_size,
            (page - 1) * page_size);
    postgresql->query(conn, select_query, cb_ls_result, cb_ls_row, cb_ls_end, (void *) row_root);
    postgresql->disconnect(conn, cb_on_disconnect);
}

void cb_home_page(duda_request_t* dr)
{
    response->http_status(dr, 200);
    response->http_header_n(dr, "Content-Type: text/html", 23);
    print_header(dr);
    response->printf(dr, "\
  <div class=\"container\">\
    <div class=\"jumbotron\">\
      <h1>Non-blocking PostgreSQL Access</h1>\
      <p>\
        This is a Duda web service that demonstrates the usage of Duda PostgreSQL package.\
        If you're interested in the package itself, please refer to\
        <a href=\"https://github.com/swpd/duda_postgresql\">this</a>.\
      </p>\
      <p><a class=\"btn btn-primary btn-large\" href=\"/postgresql_demo/postgresql/dashboard/\">Get Started »</a></p>\
    </div>\
  </div>");

    char *js = "\
  <script>\
    $(document).ready(function() {\
      $('#home').addClass('active');\
    });\
  </script>";
    print_footer(dr, js);
    response->end(dr, NULL);
}

void cb_dashboard(duda_request_t *dr)
{
    response->http_status(dr, 200);
    response->http_header_n(dr, "content-type: text/html", 23);
    print_header(dr);
    response->printf(dr, "\
  <div class=\"container\">\
    <div class=\"panel\">\
      <div class=\"panel-heading\">\
        <h4 class=\"panel-title\">Dashboard</h4>\
      </div>\
      <div class=\"row\">\
        <div class=\"col-lg-4\">\
          <form class=\"form-inline\">\
            <label>choose a database:</label>\
            <div class=\"btn-group\">\
              <button id=\"current-db\" type=\"button\" class=\"btn btn-default\"></button>\
              <button type=\"button\" class=\"btn btn-default dropdown-toggle\" data-toggle=\"dropdown\">\
                <span class=\"caret\"></span>\
              </button>\
              <ul id=\"db-list\" class=\"dropdown-menu\">\
              </ul>\
            </div>\
          </form>\
        </div>\
        <div class=\"col-lg-4\">\
          <form class=\"form-inline\">\
            <label>choose a table:</label>\
            <div class=\"btn-group\">\
              <button id=\"current-tb\" type=\"button\" class=\"btn btn-default\"></button>\
              <button type=\"button\" class=\"btn btn-default dropdown-toggle\" data-toggle=\"dropdown\">\
                <span class=\"caret\"></span>\
              </button>\
              <ul id=\"tb-list\" class=\"dropdown-menu\">\
              </ul>\
            </div>\
          </form>\
        </div>\
      </div>\
    </div>\
    <table id=\"rows\" class=\"table table-bordered\">\
    </table>\
  </div>");

    char *js = "\
  <script>\
    $(document).ready(function() {\
      $('#dashboard').addClass('active');\
      $.ajax({\
        url: '/postgresql_demo/postgresql/list-databases/',\
        success: function(data) {\
          for (var i in data) {\
            $('#db-list').append('<li><a>' + data[i] + '</a></li>');\
          }\
          $('#db-list li').click(function() {\
            var db = $(this).text();\
            $('#current-db').text(db);\
            $.ajax({\
              url: '/postgresql_demo/postgresql/list-tables/' + db + '/',\
              success: function(data) {\
                $('#tb-list').children().remove();\
                for (var i in data) {\
                  $('#tb-list').append('<li><a>' + data[i] + '</a></li>');\
                }\
                $('#tb-list li').click(function() {\
                    var db = $('#current-db').text();\
                    var tb = $(this).text();\
                    $('#current-tb').text(tb);\
                    $.ajax({\
                      url: '/postgresql_demo/postgresql/list-rows/' + db + '/' + tb + '/1/',\
                      success: function(data) {\
                        $('#rows').children().remove();\
                        $('#rows').append('<tr>');\
                        for (var i in data.fields) {\
                          $('#rows tr').last().append('<td>' + data.fields[i] + '</td>');\
                        }\
                        $('#rows').append('</tr>');\
                        for (var ri in data.rows) {\
                          $('#rows').append('<tr>');\
                          for (var fi in data.fields) {\
                            $('#rows tr').last().append('<td>' + data.rows[ri][data.fields[fi]] + '</td>');\
                          }\
                          $('#rows').append('</tr>');\
                        }\
                      }\
                    });\
                });\
                $('#tb-list li').first().click();\
              }\
            });\
          });\
          $('#db-list li').first().click();\
        }\
      });\
    });\
  </script>";
    print_footer(dr, js);
    response->end(dr, NULL);
}

int duda_main()
{
    duda_load_package(postgresql, "postgresql");
    duda_load_package(json, "json");

    duda_interface_t *if_system;
    duda_method_t *method;
    duda_param_t *params;

    if_system = map->interface_new("postgresql");

    method = map->method_new("home", "cb_home_page", 0);
    map->interface_add_method(method, if_system);

    method = map->method_new("dashboard", "cb_dashboard", 0);
    map->interface_add_method(method, if_system);

    method = map->method_new("list-databases", "cb_list_databases", 0);
    map->interface_add_method(method, if_system);

    method = map->method_new("list-tables", "cb_list_tables", 1);
    params = map->param_new("database", 64);
    map->method_add_param(params, method);
    map->interface_add_method(method, if_system);

    method = map->method_new("list-rows", "cb_list_rows", 3);
    params = map->param_new("database", 64);
    map->method_add_param(params, method);
    params = map->param_new("table", 64);
    map->method_add_param(params, method);
    params = map->param_new("page", 8);
    map->method_add_param(params, method);
    map->interface_add_method(method, if_system);

    duda_service_add_interface(if_system);
    return 0;
}
