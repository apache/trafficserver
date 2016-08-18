/** @file

    Main file for the generator plugin for lighttpd

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"

#include "response.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

/**
 * this is a generator for a lighttpd plugin
 *
 * just replaces every occurance of 'generator' by your plugin name
 *
 * e.g. in vim:
 *
 *   :%s/generator/myhandler/
 *
 */

static char static_data[8192];

/* plugin config for all request/connections */

typedef struct {
  array *match;
} plugin_config;

typedef struct {
  PLUGIN_DATA;

  buffer *match_buf;

  plugin_config **config_storage;

  plugin_config conf;

} plugin_data;

typedef struct {
  size_t foo;
} handler_ctx;

// static handler_ctx * handler_ctx_init() {
// 	handler_ctx * hctx;

// 	hctx = calloc(1, sizeof(*hctx));

// 	return hctx;
// }

// static void handler_ctx_free(handler_ctx *hctx) {

// 	free(hctx);
// }

/* init the plugin data */
INIT_FUNC(mod_generator_init)
{
  plugin_data *p;

  p = calloc(1, sizeof(*p));

  p->match_buf = buffer_init();

  return p;
}

/* detroy the plugin data */
FREE_FUNC(mod_generator_free)
{
  plugin_data *p = p_d;

  UNUSED(srv);

  if (!p)
    return HANDLER_GO_ON;

  if (p->config_storage) {
    size_t i;

    for (i = 0; i < srv->config_context->used; i++) {
      plugin_config *s = p->config_storage[i];

      if (!s)
        continue;

      array_free(s->match);

      free(s);
    }
    free(p->config_storage);
  }

  buffer_free(p->match_buf);

  free(p);

  return HANDLER_GO_ON;
}

/* handle plugin config and check values */

SETDEFAULTS_FUNC(mod_generator_set_defaults)
{
  plugin_data *p = p_d;
  size_t i       = 0;

  config_values_t cv[] = {{"generator.array", NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION}, /* 0 */
                          {NULL, NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET}};

  if (!p)
    return HANDLER_ERROR;

  p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));

  for (i = 0; i < srv->config_context->used; i++) {
    plugin_config *s;

    s        = calloc(1, sizeof(plugin_config));
    s->match = array_init();

    cv[0].destination = s->match;

    p->config_storage[i] = s;

    if (0 != config_insert_values_global(srv, ((data_config *)srv->config_context->data[i])->value, cv)) {
      return HANDLER_ERROR;
    }
  }

  return HANDLER_GO_ON;
}

#define PATCH(x) p->conf.x = s->x;
static int
mod_generator_patch_connection(server *srv, connection *con, plugin_data *p)
{
  size_t i, j;
  plugin_config *s = p->config_storage[0];

  PATCH(match);

  /* skip the first, the global context */
  for (i = 1; i < srv->config_context->used; i++) {
    data_config *dc = (data_config *)srv->config_context->data[i];
    s               = p->config_storage[i];

    /* condition didn't match */
    if (!config_check_cond(srv, con, dc))
      continue;

    /* merge config */
    for (j = 0; j < dc->value->used; j++) {
      data_unset *du = dc->value->data[j];

      if (buffer_is_equal_string(du->key, CONST_STR_LEN("generator.array"))) {
        PATCH(match);
      }
    }
  }

  return 0;
}
#undef PATCH

URIHANDLER_FUNC(mod_generator_uri_handler)
{
  plugin_data *p = p_d;
  int s_len;
  size_t k;

  UNUSED(srv);

  if (con->mode != DIRECT)
    return HANDLER_GO_ON;

  if (con->uri.path->used == 0)
    return HANDLER_GO_ON;

  mod_generator_patch_connection(srv, con, p);

  s_len = con->uri.path->used - 1;

  for (k = 0; k < p->conf.match->used; k++) {
    data_string *ds = (data_string *)p->conf.match->data[k];
    int ct_len      = ds->value->used - 1;

    if (ct_len > s_len)
      continue;
    if (ds->value->used == 0)
      continue;

    if (0 == strncmp(con->uri.path->ptr + s_len - ct_len, ds->value->ptr, ct_len)) {
      con->http_status = 403;

      return HANDLER_FINISHED;
    }
  }

  /* not found */
  return HANDLER_GO_ON;
}

URIHANDLER_FUNC(mod_generator_subrequest_handler)
{
  (void)p_d;
  // plugin_data *p = p_d;
  buffer *b;
  b = chunkqueue_get_append_buffer(con->write_queue);

  // get the url information
  // int length = strlen(con->uri.path->ptr);
  char *start = con->uri.path->ptr;
  char *end;
  //  char *end = start + length;
  if (*start != '/') {
    log_error_write(srv, __FILE__, __LINE__, "s", "url doesn't start with a slash");
    return HANDLER_GO_ON;
  }
  ++start;

  // get the size in bytes form the url
  int64_t bytes = strtoll(start, &end, 10);

  switch (*end) {
  case 'k':
  case 'K':
    bytes *= 1024;
    ++end;
    break;
  case 'm':
  case 'M':
    bytes *= 1024 * 1024;
    ++end;
    break;
  case 'g':
  case 'G':
    bytes *= 1024 * 1024 * 1024;
    ++end;
    break;
  default:
    break;
  }

  if (start == end && bytes <= 0 && *start != '-') {
    log_error_write(srv, __FILE__, __LINE__, "s", "can't find size in bytes");
    return HANDLER_GO_ON;
  }
  start = end + 1;

  // get the id from the url.
  end = strchr(start, '-');
  if (end == NULL) {
    log_error_write(srv, __FILE__, __LINE__, "s", "problems finding the id");
    return HANDLER_GO_ON;
  }

  start = end + 1;

  // get the time to sleep from the url
  int64_t sleepval = strtoll(start, &end, 10);

  if (start == end && sleepval < 0 && *start != '-') {
    log_error_write(srv, __FILE__, __LINE__, "s", "problems finding the sleepval");
    return HANDLER_GO_ON;
  }
  start = end + 1;
  if (sleepval > 0) {
    usleep(1000 * sleepval);
  }

  // check to see if we are going to set cacheable headers
  int cache = -1;
  if (strcmp(start, "cache") == 0) {
    cache = 1;
  } else if (strcmp(start, "no_cache") == 0) {
    cache = 0;
  } else {
    log_error_write(srv, __FILE__, __LINE__, "s", "didn't see cache or no_cache in the url");
    return HANDLER_GO_ON;
  }

  // print the body of the message
  uint64_t to_write = 0;
  --bytes; // leave a char left over for \n
  while (bytes > 0) {
    if ((uint64_t)bytes > sizeof(static_data)) {
      // biger then the static buffer, so write the entire buffer
      to_write = sizeof(static_data);
    } else {
      to_write = bytes;
    }

    buffer_append_string_len(b, static_data, to_write);
    bytes -= to_write;
  }

  if (bytes == 0) {
    buffer_append_string_len(b, "\n", 1); // add a \n to the end of the body
  }

  // write the headers if it is cacheable or not
  if (cache == 0) {
    response_header_insert(srv, con, CONST_STR_LEN("Cache-Control"), CONST_STR_LEN("private"));
  } else {
    response_header_insert(srv, con, CONST_STR_LEN("Last-Modified"), CONST_STR_LEN("Thu, 12 Feb 2009 23:00:00 GMT"));
    response_header_insert(srv, con, CONST_STR_LEN("Cache-Control"), CONST_STR_LEN("max-age=86400, public"));
  }

  con->http_status   = 200;
  con->file_finished = 1;
  return HANDLER_FINISHED;
}

/* this function is called at dlopen() time and inits the callbacks */

int
mod_generator_plugin_init(plugin *p)
{
  p->version = LIGHTTPD_VERSION_ID;
  p->name    = buffer_init_string("generator");

  p->init             = mod_generator_init;
  p->handle_uri_clean = mod_generator_uri_handler;
  p->handle_physical  = mod_generator_subrequest_handler;
  p->set_defaults     = mod_generator_set_defaults;
  p->cleanup          = mod_generator_free;

  p->data = NULL;

  // set the static data used in the response;
  memset(static_data, 'x', sizeof(static_data));

  return 0;
}
