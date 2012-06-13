/** @file

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

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__url_remap_cc[] = "@(#) $Id: yfor_remap.cc 218 2009-04-25 01:29:16Z leifh $ built on " __DATE__ " " __TIME__;

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <string>

#include <ts/remap.h>
#include <ts/ts.h>

static const char* PLUGIN_NAME = "hipes";
static const char* HIPES_SERVER_NAME = "hipes.example.com";


///////////////////////////////////////////////////////////////////////////////
// Escape a URL string.
//
int
escapify_url(const char *src, int src_len, char* dst, int dst_len)
{
  // This bitmap is generated using the gen_escape.c prog.
  static unsigned char codes_to_escape[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 
    0xFF, 0xF9, 0x00, 0x3F, 
    0x80, 0x00, 0x00, 0x1E, 
    0x80, 0x00, 0x00, 0x1F, 
    0xFF, 0xFF, 0xFF, 0xFF, 
    0xFF, 0xFF, 0xFF, 0xFF, 
    0xFF, 0xFF, 0xFF, 0xFF, 
    0xFF, 0xFF, 0xFF, 0xFF
  };

  static char hex_digit[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C',
    'D', 'E', 'F'
  };

  const char* from = src;
  char* to = dst;
  int len = 0;

  // Sanity check
  if (!src)
    return -1;

  while (from < (src + src_len)) {
    register unsigned char c = *from;

    if (len >= dst_len)
      return -1; // Does not fit.... abort!

    if (codes_to_escape[c / 8] & (1 << (7 - c % 8))) {
      *to++ = '%';
      *to++ = hex_digit[c / 16];
      *to++ = hex_digit[c % 16];
      len += 3;
    } else {
      *to++ = *from;
      ++len;
    }
    ++from;
  }
  *to = '\0';

  return len;
}


///////////////////////////////////////////////////////////////////////////////
// Unescape a string. Have to make sure the destination buffer is at least as
// long as the source buffer.
//
char*
unescapify(const char* src, char* dst, int len) {
  const char* cur = src;
  char* next;
  char subStr[3];
  int size;

  subStr[2] = '\0';
  while ((next = (char*)memchr(cur, '%', len))) {
    size = next - cur;
    if (size > 0) {
      memcpy(dst, cur, size);
      dst += size;
      cur += size;
      len -= size;
    }

    if (len > 2  && (*cur+1) != '\0' && *(cur+2) != '\0') {
      subStr[0] = *(++cur);
      subStr[1] = *(++cur);
      len -= 2;
      *dst = (char)strtol(subStr, (char**)NULL, 16);
    } else {
      *dst = *cur;
    }
    ++dst;
    ++cur;
    --len;
  }

  if (len > 0) {
    memcpy(dst, cur, len);
    dst += len;
  }

  return dst;
}


///////////////////////////////////////////////////////////////////////////////
// Class encapsulating one service configuration
//
struct HIPESService
{
  HIPESService()
    : url_param("url"), path(""), svc_server(""), svc_port(80), ssl(false), hipes_server(HIPES_SERVER_NAME),
      hipes_port(80), default_redirect_flag(1), x_hipes_header("X-HIPES-Redirect"),
      active_timeout(-1), no_activity_timeout(-1), connect_timeout(-1), dns_timeout(-1)
  { };

  std::string url_param;
  std::string path;
  std::string svc_server;
  int svc_port;
  bool ssl;
  std::string hipes_server;
  int hipes_port;
  unsigned int default_redirect_flag;
  std::string x_hipes_header;
  int active_timeout;
  int no_activity_timeout;
  int connect_timeout;
  int dns_timeout;
};


///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
int
TSRemapInit(TSREMAP_INTERFACE *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSREMAP_INTERFACE argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] - Incorrect API version %ld.%ld",
             api_info->tsremap_version >> 16, (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  INKDebug("hipes", "plugin is succesfully initialized");
  return TS_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
// One instance per remap.config invocation.
//
int
tsremap_new_instance(int argc, char *argv[], ihandle *ih, char *errbuf, int errbuf_size)
{
  HIPESService* ri = new HIPESService;

  *ih = static_cast<ihandle>(ri);

  if (ri == NULL) {
    INKError("Unable to create remap instance");
    return -5;
  }

  for (int ix=2; ix < argc; ++ix) {
    std::string arg = argv[ix];
    std::string::size_type sep = arg.find_first_of(":");

    if (sep == std::string::npos) {
      INKError("Malformed options in url_remap: %s", argv[ix]);
    } else {
      std::string arg_val = arg.substr(sep + 1, std::string::npos);

      if (arg.compare(0, 4, "urlp") == 0) {
        ri->url_param = arg_val;
      } else if (arg.compare(0, 4, "path") == 0) {
        ri->path = arg_val;
        if (arg_val[0] == '/')
          ri->path = arg_val.substr(1);
        else
          ri->path = arg_val;
      } else if (arg.compare(0, 3, "ssl") == 0) {
        ri->ssl = true;
      } else if (arg.compare(0, 7, "service") == 0) {
        std::string::size_type port = arg_val.find_first_of(":");

        if (port == std::string::npos) {
          ri->svc_server = arg_val;
        } else {
          ri->svc_server = arg_val.substr(0, port);
          ri->svc_port = atoi(arg_val.substr(port+1).c_str());
        }
      } else if (arg.compare(0, 6, "server") == 0) {
        std::string::size_type port = arg_val.find_first_of(":");

        if (port == std::string::npos) {
          ri->hipes_server = arg_val;
        } else {
          ri->hipes_server = arg_val.substr(0, port);
          ri->hipes_port = atoi(arg_val.substr(port+1).c_str());
        }
      } else if (arg.compare(0, 14, "active_timeout") == 0) {
        ri->active_timeout = atoi(arg_val.c_str());
      } else if (arg.compare(0, 19, "no_activity_timeout") == 0) {
        ri->no_activity_timeout = atoi(arg_val.c_str());
      } else if (arg.compare(0, 15, "connect_timeout") == 0) {
        ri->connect_timeout = atoi(arg_val.c_str());
      } else if (arg.compare(0, 11, "dns_timeout") == 0) {
        ri->dns_timeout = atoi(arg_val.c_str());
      } else {
        INKError("Unknown url_remap option: %s", argv[ix]);
      }
    }
  }

  return 0;
}

void
tsremap_delete_instance(ihandle ih)
{
  HIPESService* ri = static_cast<HIPESService*>(ih);

  delete ri;
}


///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
int
tsremap_remap(ihandle ih, rhandle rh, REMAP_REQUEST_INFO *rri)
{
  const char* slash;
  char* ptr;
  HIPESService* h_conf = static_cast<HIPESService*>(ih);

  if (NULL == h_conf) {
    INKDebug("hipes", "Falling back to default URL on URL remap without rules");
    return 0;
  }

  // Make sure we have a matrix parameter, anything without is a bogus request.
  if (rri->request_matrix_size <= 0) {
    INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_BAD_REQUEST);
    return 0;
  }

  // Don't think this can/should happen, but safety first ...
  if (rri->request_matrix_size > TSREMAP_RRI_MAX_PATH_SIZE) {
    INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
    return 0;
  }

  // If there is a '/' in the matrix parameters, we know there are multiple service requests in
  // the incoming URL, so nibble off the first one, and pass the rest as a HIPES URL to the service.
  if ((slash = static_cast<const char*>(memchr(rri->request_matrix, '/', rri->request_matrix_size)))) {
    char svc_url[TSREMAP_RRI_MAX_PATH_SIZE + 1];
    char svc_url_esc[TSREMAP_RRI_MAX_PATH_SIZE + 1];
    int len, query_len;

    // Create the escaped URL, which gets passed over to the service as a url= param.
    len = 8 + h_conf->hipes_server.size() + (rri->request_matrix_size - (slash - rri->request_matrix) - 1);
    if (len > TSREMAP_RRI_MAX_PATH_SIZE) {
      INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
      return 0;
    }
    snprintf(svc_url, TSREMAP_RRI_MAX_PATH_SIZE, "http://%s/%.*s", h_conf->hipes_server.c_str(), len, slash + 1);
    INKDebug("hipes", "Service URL is %s", svc_url);

    len = escapify_url(svc_url, len, svc_url_esc, TSREMAP_RRI_MAX_PATH_SIZE);
    if (len < 0) {
      return 0;
    }
    INKDebug("hipes", "Escaped service URL is %s(%d)", svc_url_esc, len);

    // Prepare the new query arguments, make sure it fits
    if (( (slash - rri->request_matrix) + 2 + h_conf->url_param.size() + len) > TSREMAP_RRI_MAX_PATH_SIZE) {
      INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
      return 0;
    }

    query_len = (slash - rri->request_matrix);
    memcpy(rri->new_query, rri->request_matrix, query_len);
    ptr = rri->new_query;
    while ((ptr = static_cast<char*>(memchr(ptr, ';', (rri->new_query + query_len) - ptr))))
      *ptr = '&';

    rri->new_query[query_len++] = '&';
    memcpy(rri->new_query + query_len, h_conf->url_param.c_str(), h_conf->url_param.size());
    query_len += h_conf->url_param.size();
    rri->new_query[query_len++] = '=';

    memcpy(rri->new_query + query_len, svc_url_esc, len);
    rri->new_query_size = query_len + len;
    INKDebug("hipes", "New query is %.*s(%d)", rri->new_query_size, rri->new_query, rri->new_query_size);
  } else {
    // This is the "final" step in this HIPES URL, so don't point back to HIPES (or we'll never leave)
    rri->new_query_size = rri->request_matrix_size;
    memcpy(rri->new_query, rri->request_matrix, rri->request_matrix_size);
    ptr = rri->new_query;
    while ((ptr = static_cast<char*>(memchr(ptr, ';', (rri->new_query + rri->new_query_size) - ptr))))
      *ptr = '&';

    INKDebug("hipes", "New query is %.*s(%d)", rri->new_query_size, rri->new_query, rri->new_query_size);
  }

  // Test if we should redirect or not
  bool do_redirect = false;
  int redirect_flag = h_conf->default_redirect_flag;
  char* pos = rri->new_query;

  while (pos && (pos = (char*)memchr(pos, '_', rri->new_query_size - (pos - rri->new_query)))) {
    if (pos) {
      ++pos;
      if ((rri->new_query_size - (pos - rri->new_query)) < 10) { // redirect=n
        pos = NULL;
      } else {
        if ((*pos == 'r') && (!strncmp(pos, "redirect=", 9))) {
          redirect_flag = *(pos + 9) - '0';
          if ((redirect_flag < 0) || (redirect_flag > 2))
            redirect_flag = h_conf->default_redirect_flag;
          INKDebug("hipes", "Found _redirect flag in URL: %d\n", redirect_flag);
          pos = NULL;
        }
      }
    }
  }

  if (redirect_flag > 0) {
    // Now check the incoming request header, and match up.
    INKMBuffer bufp;
    INKMLoc hdr_loc, field_loc;
    bool has_error = false;

    if (INKHttpTxnClientReqGet((INKHttpTxn)rh, &bufp, &hdr_loc)) {
      field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, h_conf->x_hipes_header.c_str(), h_conf->x_hipes_header.size());
      if (field_loc) {
        int hdr_flag;

        if (INKMimeHdrFieldValueIntGet(bufp, hdr_loc, field_loc, 0, &hdr_flag) == INK_SUCCESS) {
          // Alright, now match up this header flag with the request (or default) flag
          INKDebug("hipes", "Extracted %s header with value %d", h_conf->x_hipes_header.c_str(), hdr_flag);
          switch (redirect_flag) {
          case 0:
            if (hdr_flag == 2) {
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_BAD_REQUEST);
              has_error = true;
            } // Everything else is a "no"
            break;
          case 1:
            if (hdr_flag == 2) {
              do_redirect = true;
            } // Everything else is a "no"
            break;
          case 2:
            if (hdr_flag == 2) {
              do_redirect = true;
            } else {
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_BAD_REQUEST);
              has_error = true;
            }
            break;
          default:
            INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_BAD_REQUEST);
            has_error = true;
            break;
          }
        }
        INKHandleMLocRelease(bufp, hdr_loc, field_loc);
      }
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    }
    if (has_error)
      return 1;
  }

  // If we redirect, just generate a 302 URL, otherwise update the RRI struct properly.
  if (do_redirect) {
    int len; 

    if (h_conf->ssl) {
      // https://<host>:<port>/<path>?<query?\0
      len = 5 + 3 + h_conf->svc_server.size() + 6 + 1 + h_conf->path.size() + 1 + rri->new_query_size + 1;
    } else {
      // http://<host>:<port>/<path>?<query?\0
      len = 4 + 3 + h_conf->svc_server.size() + 6 + 1 + h_conf->path.size() + 1 + rri->new_query_size + 1;
    }

    if (len > TSREMAP_RRI_MAX_REDIRECT_URL) {
      INKError("Redirect in HIPES URL too long");
      INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)414);
    } else {
      int port = -1;

      pos = rri->redirect_url;

      // HTTP vs HTTPS
      if (h_conf->ssl) {
        memcpy(pos, "https://", 8);
        pos += 8;
        if (h_conf->svc_port != 443)
          port = h_conf->svc_port;
      } else {
        memcpy(pos, "http://", 7);
        pos += 7;
        if (h_conf->svc_port != 80)
          port = h_conf->svc_port;
      }

      // Server
      memcpy(pos, h_conf->svc_server.c_str(), h_conf->svc_server.size());
      pos += h_conf->svc_server.size();

      // Port
      if (port != -1)
        pos += snprintf(pos, 6, ":%d", port);

      // Path
      *(pos++) = '/';
      if (h_conf->path.size() > 0) {
        memcpy(pos, h_conf->path.c_str(), h_conf->path.size());
        pos += h_conf->path.size();
      }

      // Query
      if (rri->new_query_size > 0) {
        *(pos++) = '?';
        memcpy(pos, rri->new_query, rri->new_query_size);
        pos += rri->new_query_size;
      }

      // NULL terminate the URL.
      *pos = '\0';

      rri->redirect_url_size = pos - rri->redirect_url + 1;
      INKDebug("hipes", "Redirecting to %.*s", rri->redirect_url_size, rri->redirect_url);
      *(rri->new_query) = '\0';
      rri->new_query_size = 0;
      INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_MOVED_TEMPORARILY);
    }
  } else { // Not a redirect, so proceed normally
    // Set timeouts (if requested)
    if (h_conf->active_timeout > -1) {
      INKDebug("hipes", "Setting active timeout to %d", h_conf->active_timeout);
      INKHttpTxnActiveTimeoutSet((INKHttpTxn)rh, h_conf->active_timeout);
    }
    if (h_conf->no_activity_timeout > -1) {
      INKDebug("hipes", "Setting no activity timeout to %d", h_conf->no_activity_timeout);
      INKHttpTxnNoActivityTimeoutSet((INKHttpTxn)rh, h_conf->no_activity_timeout);
    }
    if (h_conf->connect_timeout > -1) {
      INKDebug("hipes", "Setting connect timeout to %d", h_conf->connect_timeout);
      INKHttpTxnConnectTimeoutSet((INKHttpTxn)rh, h_conf->connect_timeout);
    }
    if (h_conf->dns_timeout > -1) {
      INKDebug("hipes", "Setting DNS timeout to %d", h_conf->dns_timeout);
      INKHttpTxnDNSTimeoutSet((INKHttpTxn)rh, h_conf->dns_timeout);
    }

    // Set server ...
    rri->new_host_size = h_conf->svc_server.size();
    memcpy(rri->new_host, h_conf->svc_server.c_str(), rri->new_host_size);
    INKDebug("hipes", "New server is %.*s", rri->new_host_size, rri->new_host);

    // ... and port
    rri->new_port = h_conf->svc_port;
    INKDebug("hipes", "New port is %d", rri->new_port);

    // Update the path
    rri->new_path_size = h_conf->path.size();
    memcpy(rri->new_path, h_conf->path.c_str(), rri->new_path_size);
    INKDebug("hipes", "New path is %.*s", rri->new_path_size, rri->new_path);

    // Enable SSL?
    if (h_conf->ssl)
      rri->require_ssl = 1;

    // Clear previous matrix params
    rri->new_matrix_size = -1;
  }

  // Step 3: Profit
  return 1;
}



/*
  local variables:
  mode: C++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-comment-only-line-offset: 0
  c-file-offsets: ((statement-block-intro . +)
  (label . 0)
  (statement-cont . +)

  end:

  Indent with: /usr/bin/indent -ncs -nut -npcs -l 120 logstats.cc
*/
