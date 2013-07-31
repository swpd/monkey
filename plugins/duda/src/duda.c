/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Duda I/O
 *  --------
 *  Copyright (C) 2012-2013, Eduardo Silva P. <edsiper@gmail.com>.
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

#define _GNU_SOURCE
#include <dlfcn.h>

#include "MKPlugin.h"
#include "duda.h"
#include "duda_gc.h"
#include "duda_qs.h"
#include "duda_map.h"
#include "duda_conf.h"
#include "duda_event.h"
#include "duda_queue.h"
#include "duda_console.h"
#include "duda_package.h"

MONKEY_PLUGIN("duda",                                     /* shortname */
              "Duda Web Services Framework",              /* name */
              VERSION,                                    /* version */
              MK_PLUGIN_CORE_PRCTX |
              MK_PLUGIN_CORE_THCTX | MK_PLUGIN_STAGE_30); /* hooks */


/* Register a new duda_request into the thread context list */
static void duda_dr_list_add(duda_request_t *dr)
{
    struct rb_root *root = pthread_getspecific(duda_global_dr_list);

    /* Red-Black tree insert routine */
    struct rb_node **new = &(root->rb_node);
    struct rb_node *parent = NULL;

    /* Figure out where to put new node */
    while (*new) {
        duda_request_t *this = container_of(*new, duda_request_t, _rb_head);

        parent = *new;
        if (dr->socket < this->socket)
            new = &((*new)->rb_left);
        else if (dr->socket > this->socket)
            new = &((*new)->rb_right);
        else {
            break;
        }
    }

    /* Add new node and rebalance tree. */
    mk_api->rb_link_node(&dr->_rb_head, parent, new);
    mk_api->rb_insert_color(&dr->_rb_head, root);
}

duda_request_t *duda_dr_list_get(int socket)
{
    struct rb_root *root;
    duda_request_t *dr;

    root = pthread_getspecific(duda_global_dr_list);

  	struct rb_node *node = root->rb_node;
  	while (node) {
  		dr = container_of(node, duda_request_t, _rb_head);
		if (socket < dr->socket)
  			node = node->rb_left;
		else if (socket > dr->socket)
  			node = node->rb_right;
		else {
  			return dr;
        }
	}

	return NULL;
}

static duda_request_t *duda_dr_list_del(int socket)
{
    duda_request_t *dr = duda_dr_list_get(socket);
    struct rb_root *root;

    if (!dr) {
        return NULL;
    }

    root = pthread_getspecific(duda_global_dr_list);
    mk_api->rb_erase(&dr->_rb_head, root);

    return dr;
}

void *duda_load_library(const char *path)
{
    void *handle;

    handle = dlopen(path, RTLD_LAZY);
    if (!handle) {
        mk_warn("dlopen() %s", dlerror());
    }

    return handle;
}

/* get specific symbol from the library */
void *duda_load_symbol(void *handle, const char *symbol)
{
    void *s;
    char *err;

    dlerror();
    s = dlsym(handle, symbol);
    if ((err = dlerror()) != NULL) {
        mk_warn("Duda: %s", err);
        return NULL;
    }

    return s;
}

/* Register the service interfaces into the main list of web services */
int duda_service_register(struct duda_api_objects *api, struct web_service *ws)
{
    int (*service_init) (struct duda_api_objects *, struct web_service *);
    struct mk_list *head_iface, *head_method, *head_urls;
    struct duda_interface *entry_iface, *cs_iface;
    struct duda_method *entry_method, *cs_method;
    struct duda_map_static_cb *static_cb;

    /* Load and invoke duda_main() */
    service_init = (int (*)()) duda_load_symbol(ws->handler, "_duda_bootstrap");
    if (!service_init) {
        mk_err("Duda: invalid web service %s", ws->name.data);
        exit(EXIT_FAILURE);
    }

    if (service_init(api, ws) == 0) {
        PLUGIN_TRACE("[%s] duda_main()", ws->name.data);
        ws->map_interfaces = duda_load_symbol(ws->handler, "duda_map_interfaces");
        ws->map_urls       = duda_load_symbol(ws->handler, "duda_map_urls");
        ws->global   = duda_load_symbol(ws->handler, "duda_global_dist");
        ws->packages = duda_load_symbol(ws->handler, "duda_ws_packages");
        ws->workers  = duda_load_symbol(ws->handler, "duda_worker_list");

        /* Lookup mapped callbacks */
        mk_list_foreach(head_urls, ws->map_urls) {
            static_cb = mk_list_entry(head_urls, struct duda_map_static_cb, _head);
            static_cb->callback = duda_load_symbol(ws->handler, static_cb->cb_name);
            if (!static_cb->callback) {
                mk_err("Static Map: you have set the callback '%s' through\n"
                       "a static map, but the function could not be located. Aborting",
                       static_cb->cb_name);
                exit(EXIT_FAILURE);
            }
        }

        /* Register Duda built-in interfaces: console */
        cs_iface  = api->map->interface_new("console");

        /* app/console/messages */
        cs_method = api->map->method_builtin_new("messages",
                                                 duda_console_cb_messages, 0);
        api->map->interface_add_method(cs_method, cs_iface);

        /* app/console/map */
        cs_method = api->map->method_builtin_new("map", duda_console_cb_map, 0);
        api->map->interface_add_method(cs_method, cs_iface);

        mk_list_add(&cs_iface->_head, ws->map_interfaces);

        /* Lookup callback functions for each registered method */
        mk_list_foreach(head_iface, ws->map_interfaces) {
            entry_iface = mk_list_entry(head_iface, struct duda_interface, _head);
            mk_list_foreach(head_method, &entry_iface->methods) {
                entry_method = mk_list_entry(head_method, struct duda_method, _head);
                if (entry_method->callback) {
                    entry_method->cb_webservice = duda_load_symbol(ws->handler,
                                                                   entry_method->callback);
                    if (!entry_method->cb_webservice) {
                        mk_err("%s / callback not found '%s'", entry_method->uid, entry_method->uid);
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

        /* Spawn all workers set in duda_main() */
        duda_worker_spawn_all(ws->workers);
    }

    return 0;
}

/*
 * Load the web service shared library for each definition found in the
 * virtual host
 */
int duda_load_services()
{
    char *service_path;
    unsigned long len;
    struct file_info finfo;
    struct mk_list *head_vh;
    struct mk_list *head_ws, *head_temp_ws;
    struct vhost_services *entry_vs;
    struct web_service *entry_ws;
    struct duda_api_objects *api;

    mk_list_foreach(head_vh, &services_list) {
        entry_vs = mk_list_entry(head_vh, struct vhost_services, _head);
        mk_list_foreach_safe(head_ws, head_temp_ws, &entry_vs->services) {
            entry_ws = mk_list_entry(head_ws, struct web_service, _head);

            service_path = NULL;
            mk_api->str_build(&service_path, &len,
                              "%s/%s.duda", services_root, entry_ws->name.data);

            /* Validate path, file and library load */
            if (mk_api->file_get_info(service_path, &finfo) != 0 ||
                finfo.is_file != MK_TRUE ||
                !(entry_ws->handler = duda_load_library(service_path))) {

                entry_ws->enabled = 0;
                mk_api->mem_free(service_path);
                mk_warn("Duda: service '%s' not found", entry_ws->name.data);
                mk_list_del(head_ws);
                mk_api->mem_free(entry_ws);
                continue;
            }

            /* Success */
            mk_info("Duda: loading service '%s'", entry_ws->name.data);
            mk_api->mem_free(service_path);

            /* Register service */
            api = duda_api_master();
            duda_service_register(api, entry_ws);
        }
    }

    return 0;
}

void duda_mem_init()
{
    int len;
    time_t expire = COOKIE_EXPIRE_TIME;
    struct tm *gmt;

    /* Init mk_pointer's */
    mk_api->pointer_set(&dd_iov_none, "");
    mk_api->pointer_set(&dd_cookie_crlf,      COOKIE_CRLF);
    mk_api->pointer_set(&dd_cookie_equal,     COOKIE_EQUAL);
    mk_api->pointer_set(&dd_cookie_set,       COOKIE_SET);
    mk_api->pointer_set(&dd_cookie_expire,    COOKIE_EXPIRE);
    mk_api->pointer_set(&dd_cookie_path,      COOKIE_PATH);
    mk_api->pointer_set(&dd_cookie_semicolon, COOKIE_SEMICOLON);

    /* Default expire value */
    dd_cookie_expire_value.data = mk_api->mem_alloc_z(COOKIE_MAX_DATE_LEN);

    gmt = gmtime(&expire);
    len = strftime(dd_cookie_expire_value.data,
                   COOKIE_MAX_DATE_LEN,
                   "%a, %d %b %Y %H:%M:%S GMT\r\n",
                   gmt);
    dd_cookie_expire_value.len = len;
}

/*
 * These are the Monkey hooks for the event handler, each time an event
 * arrives here and depending of the event type, it will perform a lookup
 * over the thread list looking for possible event-handlers.
 *
 * By Monkey definition exists hooks:
 *
 *  _mkp_event_read(int sockfd)    -> socket is ready to read
 *  _mkp_event_write(int sockfd)   -> socket is ready to write
 *  _mkp_event_close(int sockfd)   -> socket has been closed
 *  _mkp_event_error(int sockfd)   -> some error happend at socket level
 *  _mkp_event_timeout(int sockfd) -> the socket have timed out
 */

int _mkp_event_read(int sockfd)
{
    int ret;

    struct duda_event_handler *eh = duda_event_lookup(sockfd);
    if (eh && eh->cb_on_read) {
        ret = eh->cb_on_read(eh->sockfd, eh->cb_data);

        /* we dont want a bad API usage.. */
        if (mk_unlikely(ret != DUDA_EVENT_OWNED && ret != DUDA_EVENT_CLOSE &&
                        ret != DUDA_EVENT_CONTINUE)) {
            mk_err("Bad return value on read event callback");
            exit(EXIT_FAILURE);
        }
        return ret;
    }

    return MK_PLUGIN_RET_EVENT_CONTINUE;
}

int _mkp_event_write(int sockfd)
{
    struct duda_event_handler *eh = duda_event_lookup(sockfd);

    if (eh && eh->cb_on_write) {
        eh->cb_on_write(eh->sockfd, eh->cb_data);
        return MK_PLUGIN_RET_EVENT_OWNED;
    }

    /*
     * By default we always check if we have some pending data in our
     * outgoing queue
     */
    return duda_queue_event_write_callback(sockfd);
}

int _mkp_event_close(int sockfd)
{
    struct duda_event_handler *eh = duda_event_lookup(sockfd);
    duda_request_t *dr = NULL;

    if (eh && eh->cb_on_close) {
        eh->cb_on_close(eh->sockfd, eh->cb_data);
        duda_event_delete(sockfd);
        return MK_PLUGIN_RET_EVENT_OWNED;
    }

    dr = duda_dr_list_del(sockfd);
    if (dr) {
        duda_gc_free(dr);
        mk_api->mem_free(dr);
    }

    return MK_PLUGIN_RET_EVENT_CONTINUE;
}

int _mkp_event_error(int sockfd)
{
    struct duda_event_handler *eh = duda_event_lookup(sockfd);

    if (eh && eh->cb_on_error) {
        eh->cb_on_error(eh->sockfd, eh->cb_data);
        duda_event_delete(sockfd);
        return MK_PLUGIN_RET_EVENT_CLOSE;
    }

    return MK_PLUGIN_RET_EVENT_CONTINUE;
}

int _mkp_event_timeout(int sockfd)
{
    struct duda_event_handler *eh = duda_event_lookup(sockfd);

    if (eh && eh->cb_on_timeout) {
        eh->cb_on_timeout(eh->sockfd, eh->cb_data);
        duda_event_delete(sockfd);
        return MK_PLUGIN_RET_EVENT_CLOSE;
    }

    return MK_PLUGIN_RET_EVENT_CONTINUE;
}

/* Thread context initialization */
void _mkp_core_thctx()
{
    struct mk_list *head_vs, *head_ws, *head_gl;
    struct mk_list *list_events_write;
    struct mk_list *events_list;
    struct rb_root *dr_list;
    struct vhost_services *entry_vs;
    struct web_service *entry_ws;
    duda_global_t *entry_gl;
    void *data;

    /* Events write list */
    list_events_write = mk_api->mem_alloc(sizeof(struct mk_list));
    mk_list_init(list_events_write);
    pthread_setspecific(duda_global_events_write, (void *) list_events_write);

    /* Events */
    events_list = mk_api->mem_alloc(sizeof(struct mk_list));
    mk_list_init(events_list);
    pthread_setspecific(duda_events_list, (void *) events_list);

    /* List of all duda_request_t alive */
    dr_list = mk_api->mem_alloc_z(sizeof(struct rb_root));
    pthread_setspecific(duda_global_dr_list, (void *) dr_list);

    /*
     * Load global data if applies, this is toooo recursive, we need to go through
     * every virtual host and check the services loaded for each one, then lookup
     * the global variables defined.
     */
    mk_list_foreach(head_vs, &services_list) {
        entry_vs = mk_list_entry(head_vs, struct vhost_services, _head);
        mk_list_foreach(head_ws, &entry_vs->services) {
            entry_ws = mk_list_entry(head_ws, struct web_service, _head);

            /* go around each global variable */
            mk_list_foreach(head_gl, entry_ws->global) {
                entry_gl = mk_list_entry(head_gl, duda_global_t, _head);
                /*
                 * If a global variable was defined we need to check if was requested
                 * to initialize it with a specific data returned by a callback
                 */
                data = NULL;
                if (entry_gl->callback) {
                    data = entry_gl->callback();
                }
                pthread_setspecific(entry_gl->key, data);
            }

            /*
             * Now go around each package and check for thread context callbacks
             */
            struct mk_list *head_pkg;
            struct mk_list *global_list;
            struct duda_package *entry_pkg;
            mk_list_foreach(head_pkg, entry_ws->packages) {
                entry_pkg = mk_list_entry(head_pkg, struct duda_package, _head);
                global_list = duda_load_symbol(entry_pkg->handler, "duda_global_dist");

                mk_list_foreach(head_gl, global_list) {
                    entry_gl = mk_list_entry(head_gl, duda_global_t, _head);
                    data = NULL;
                    if (entry_gl->callback) {
                        data = entry_gl->callback();
                    }
                    pthread_setspecific(entry_gl->key, data);
                }
            }
        }
    }
}

int _mkp_core_prctx(struct server_config *config)
{
    struct mk_list *head;
    struct web_service *ws;
    struct plugin *mk_plugin;

    /*
     * lookup this plugin instance in Monkey internals and create a
     * assign the reference to the global reference 'duda_plugin'.
     */
    duda_plugin = NULL;
    mk_list_foreach(head, config->plugins) {
        mk_plugin = mk_list_entry(head, struct plugin, _head);
        if (strcmp(mk_plugin->shortname, "duda") == 0) {
            duda_plugin = mk_plugin;
            break;
        }
    }
    mk_bug(!duda_plugin);

    /* Initialize some pointers */
    duda_mem_init();

    /*
     * Go around each registered web service and check some
     * specific configuration requests
     */
    mk_list_foreach(head, &services_loaded) {
        ws = mk_list_entry(head, struct web_service, _head_loaded);
        if (ws->bind_messages == MK_TRUE) {
            duda_conf_messages_to(ws);
        }
    }

    return 0;
}

int _mkp_init(struct plugin_api **api, char *confdir)
{
    mk_api = *api;

    /* Load configuration */
    duda_conf_main_init(confdir);
    duda_conf_vhost_init();
    duda_load_services();

    /* Global data / Thread scope */
    pthread_key_create(&duda_events_list, NULL);
    pthread_key_create(&duda_global_events_write, NULL);
    pthread_key_create(&duda_global_dr_list, NULL);

    return 0;
}

/* Sets the duda_method structure variable in duda_request */
int duda_request_set_method(duda_request_t *dr)
{
    struct mk_list *head_iface, *head_method;
    struct duda_interface *entry_iface;
    struct duda_method *entry_method = NULL;

    /* Finds the corresponding duda_method structure */
    mk_list_foreach(head_iface, dr->ws_root->map_interfaces) {
        entry_iface = mk_list_entry(head_iface, struct duda_interface, _head);

        if (entry_iface->uid_len == dr->interface.len &&
            strncmp(entry_iface->uid, dr->interface.data, dr->interface.len) == 0) {

            mk_list_foreach(head_method, &entry_iface->methods) {
                entry_method = mk_list_entry(head_method, struct duda_method, _head);
                if (entry_method->uid_len == dr->method.len &&
                    strncmp(entry_method->uid, dr->method.data, dr->method.len) == 0) {
                    dr->_method = entry_method;
                    break;
                }
            }
            if(dr->_method) {
                break;
            }
        }
    }

    if(!dr->_method) {
        PLUGIN_TRACE("Invoked method not found");
        return -1;
    }

#ifdef TRACE
    if (entry_method) {
        PLUGIN_TRACE("Method %s invoked", entry_method->uid);
    }
#endif

    return 0;
}


int duda_request_parse(struct session_request *sr,
                       struct duda_request *dr)
{
    short int last_field = MAP_WS_APP_NAME;
    int i = 0, len, val_len;
    int end;
    short int allowed_params = 0;
    struct mk_list *head_param = NULL;
    struct duda_param *entry_param;

    len = sr->uri_processed.len;

    while (i < len) {
        end = mk_api->str_search_n(sr->uri_processed.data + i, "/",
                                   MK_STR_SENSITIVE, len - i);

        if (end >= 0 && end + i < len) {
            end += i;

            if (i == end) {
                i++;
                continue;
            }

            val_len = end - i;
        }
        else {
            val_len = len - i;
            end = len;
        }

        switch (last_field) {
        case MAP_WS_APP_NAME:
            dr->appname.data = sr->uri_processed.data + i;
            dr->appname.len  = val_len;
            last_field = MAP_WS_INTERFACE;
            break;
        case MAP_WS_INTERFACE:
            dr->interface.data = sr->uri_processed.data + i;
            dr->interface.len  = val_len;
            last_field = MAP_WS_METHOD;
            break;
        case MAP_WS_METHOD:
            dr->method.data    = sr->uri_processed.data + i;
            dr->method.len     = val_len;
            last_field = MAP_WS_PARAM;
            if(duda_request_set_method(dr) == -1) {
                console_debug(dr, "Error: unknown method");
                return -1;
            }
            allowed_params = dr->_method->num_params;
            break;
        case MAP_WS_PARAM:
            if (dr->n_params >= MAP_WS_MAX_PARAMS || dr->n_params >= allowed_params) {
                PLUGIN_TRACE("too much parameters (max=%i)",
                             (dr->n_params >= MAP_WS_MAX_PARAMS)?
                             MAP_WS_MAX_PARAMS:allowed_params);
                return -1;
            }
            if (dr->n_params == 0) {
                head_param = (&dr->_method->params)->next;
            }
            entry_param = mk_list_entry(head_param, struct duda_param, _head);
            if (val_len > entry_param->max_len && entry_param->max_len != 0) {
                PLUGIN_TRACE("too long param (max=%i)", entry_param->max_len);
                console_debug(dr, "Error: param %i is too long", dr->n_params);
                return -1;
            }
            dr->params[dr->n_params].data = sr->uri_processed.data + i;
            dr->params[dr->n_params].len  = val_len;
            dr->n_params++;
            last_field = MAP_WS_PARAM;
            head_param = head_param->next;
            break;
        }

        i = end + 1;
    }

    if (last_field < MAP_WS_METHOD) {
        console_debug(dr, "invalid method");
        return -1;
    }

    if ((dr->n_params) != allowed_params) {
        PLUGIN_TRACE("%i parameters required", allowed_params);
        console_debug(dr, "Error: unexpected number of parameters");
        return -1;
    }

    return 0;
}

int duda_service_end(duda_request_t *dr)
{
    int ret;

    /* call service end_callback() */
    if (dr->end_callback) {
        dr->end_callback(dr);
    }

    /* free queue resources... */
    duda_queue_free(&dr->queue_out);
    duda_gc_free_content(dr);

    /* Finalize HTTP stuff with Monkey core */
    ret = mk_api->http_request_end(dr->socket);

    return ret;
}

/*
 * It override the initial session_request real path set by the virtual
 * host document root path.
 */
int duda_override_docroot(struct session_request *sr, int uri_offset, char *path, int len)
{
    unsigned int new_path_len;
    char *tmp;

    new_path_len = (sr->uri_processed.len + len);

    /*
     * Is the new path length minor than MK_PATH_BASE ?, sr->real_path_static have a length
     * of MK_PATH_BASE
     */
    if (new_path_len < MK_PATH_BASE) {
        if (sr->real_path.data != sr->real_path_static) {
            mk_api->mem_free(sr->real_path.data);
            sr->real_path.data = sr->real_path_static;
        }
    }
    else {
        if (new_path_len >= sr->real_path.len) {
            tmp = mk_api->mem_realloc(sr->real_path.data, new_path_len + 1);
            if (tmp) {
                sr->real_path.data = tmp;
            }
        }
    }

    /* Compose new file path */
    memcpy(sr->real_path.data, path, len);
    strncpy(sr->real_path.data + len,
            sr->uri_processed.data + uri_offset, sr->uri_processed.len - uri_offset);
    sr->real_path.len = abs(len + sr->uri_processed.len - uri_offset);
    sr->real_path.data[sr->real_path.len] = '\0';

    int ret;
    ret = mk_api->file_get_info(sr->real_path.data, &sr->file_info);
    if (ret == -1) {
        return -1;
    }

    sr->stage30_blocked = MK_TRUE;

    //printf("new path: '%s'\n", sr->real_path.data);
    return 0;
}

/*
 * Check if the requested path belongs to a static content owned by
 * the web service
 */
int duda_service_html(duda_request_t *dr)
{
    int ret;
    struct session_request *sr = dr->sr;

    /* Check if we have a local DocumentRoot for this web service */
    if (!dr->ws_root->docroot.data) {
        return -1;
    }

    /* Check the web service name in the URI */
    if (strncmp(sr->uri_processed.data + 1, dr->ws_root->name.data,
                dr->ws_root->name.len) != 0) {
        return -1;
    }

    /*
     * We need to override the document root set by the virtual host
     * logic in the session_request struct by the web service document root
     */
    ret = duda_override_docroot(sr, dr->ws_root->name.len + 1,
                                dr->ws_root->docroot.data,
                                dr->ws_root->docroot.len);

    return ret;
}

int duda_service_run(struct plugin *plugin,
                     struct client_session *cs,
                     struct session_request *sr,
                     struct web_service *web_service)
{
    struct duda_request *dr = duda_dr_list_get(cs->socket);

    if (!dr) {
        dr = mk_api->mem_alloc(sizeof(duda_request_t));
        if (!dr) {
            PLUGIN_TRACE("could not allocate enough memory");
            return -1;
        }

        /* Initialize garbage collector */
        duda_gc_init(dr);

        /* service details */
        dr->ws_root = web_service;
        dr->plugin = plugin;

        dr->socket = cs->socket;
        dr->cs = cs;
        dr->sr = sr;

        /* Register */
        duda_dr_list_add(dr);
    }

    /* method invoked */
    dr->_method = NULL;
    dr->n_params = 0;

    /* callbacks */
    dr->end_callback = NULL;

    /* data queues */
    mk_list_init(&dr->queue_out);

    /* statuses */
    dr->_st_http_content_length = -2;      /* not set */
    dr->_st_http_headers_sent = MK_FALSE;
    dr->_st_body_writes = 0;

    /* Query string */
    dr->qs.count = 0;

    /* Parse the query string */
    duda_qs_parse(dr);

    /* Parse request for 'Duda Map' format */
    if ((duda_request_parse(sr, dr) != 0) || (!dr->_method)) {
        /* Static Content file */
        if (duda_service_html(dr) == 0) {
            return -1;
        }

        /* Static Map */
        if (duda_map_static_check(dr) == 0) {
            return 0;
        }
    }
    else {
        if (dr->_method->cb_webservice) {
            PLUGIN_TRACE("CB %s()", dr->_method->callback);
            dr->_method->cb_webservice(dr);
        }
        else if (dr->_method->cb_builtin) {
            dr->_method->cb_builtin(dr);
        }
        else {
            return -1;
        }
        return 0;
    }

    return -1;
}

/*
 * Get webservice given the processed URI.
 *
 * Check the web services registered under the virtual host and try to do a
 * match with the web services name
 */
struct web_service *duda_get_service_from_uri(struct session_request *sr,
                                              struct vhost_services *vs_host)
{
    struct mk_list *head;
    struct web_service *ws_entry;

    /* match services */
    mk_list_foreach(head, &vs_host->services) {
        ws_entry = mk_list_entry(head, struct web_service, _head);
        if (strncmp(ws_entry->name.data,
                    sr->uri_processed.data + 1,
                    ws_entry->name.len) == 0) {
            PLUGIN_TRACE("WebService match: %s", ws_entry->name.data);
            return ws_entry;
        }
    }

    return NULL;
}


void _mkp_exit()
{

}

/*
 * Request handler: when the request arrives this callback is invoked.
 */
int _mkp_stage_30(struct plugin *plugin, struct client_session *cs,
                  struct session_request *sr)
{
    struct mk_list *head;
    struct vhost_services *vs_entry, *vs_match=NULL;
    struct web_service *web_service;

    /* we don't care about '/' request */
    if (sr->uri_processed.len > 1) {

        /* Check for a DDR request */
        if (document_root && strncmp(sr->uri_processed.data, "/ddr", 4) == 0) {
            duda_override_docroot(sr, 4, document_root, strlen(document_root));
            return MK_PLUGIN_RET_NOT_ME;
        }

        /* Match virtual host */
        mk_list_foreach(head, &services_list) {
            vs_entry = mk_list_entry(head, struct vhost_services, _head);
            if (sr->host_conf == vs_entry->host) {
                vs_match = vs_entry;
                break;
            }
        }

        if (!vs_match) {
            return MK_PLUGIN_RET_NOT_ME;
        }

        /* Match web service */
        web_service = duda_get_service_from_uri(sr, vs_match);
        if (!web_service) {
            return MK_PLUGIN_RET_NOT_ME;
        }

        if (duda_service_run(plugin, cs, sr, web_service) == 0) {
            return MK_PLUGIN_RET_CONTINUE;
        }
    }

    return MK_PLUGIN_RET_NOT_ME;
}