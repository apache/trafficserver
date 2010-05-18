/** @file

  A brief file description

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

/* ------------------------------------------------------------------------- */
/* -                               RemapAPI.h                              - */
/* ------------------------------------------------------------------------- */
#ifndef H_REMAPAPI_H
#define H_REMAPAPI_H

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

#define TSREMAP_VMAJOR   2      /* major version number */
#define TSREMAP_VMINOR   0      /* minor version number */
#define TSREMAP_VERSION ((TSREMAP_VMAJOR << 16)|TSREMAP_VMINOR)

  typedef int tsremap_interface(int cmd, ...);

  typedef struct _tsremap_api_info
  {
    unsigned long size;         /* in: sizeof(struct _tsremap_api_info) */
    unsigned long tsremap_version;      /* in: TS supported version ((major << 16) | minor) */
    tsremap_interface *fp_tsremap_interface;    /* in: TS interface function pointer */
  } TSRemapInterface;

  typedef TSRemapInterface TSREMAP_INTERFACE;   /* This is deprecated. */

  typedef void *base_handle;
  typedef base_handle ihandle;  /* plugin instance handle (per unique remap rule) */
  typedef base_handle rhandle;  /* request handle */


  /* Plugin initialization - called first.
     Mandatory interface function.
     Return: 0 - success
             != 0 - error, errbuf can include error message from plugin
  */
  int tsremap_init(TSRemapInterface * api_info, char *errbuf, int errbuf_size);
  typedef int _tsremap_init(TSRemapInterface * api_info, char *errbuf, int errbuf_size);
#define TSREMAP_FUNCNAME_INIT "tsremap_init"


  /* Plugin shutdown
     Optional function. */
  int tsremap_done(void);
  typedef int _tsremap_done(void);
#define TSREMAP_FUNCNAME_DONE "tsremap_done"

  /* Plugin new instance. Create new plugin processing entry for unique remap record.
     First two arguments in argv vector are - fromURL and toURL from remap record.
     Please keep in mind that fromURL and toURL will be converted to canonical view.
     Return: != 0 - instance creation error
                0 - success
  */
  int tsremap_new_instance(int argc, char *argv[], ihandle * ih, char *errbuf, int errbuf_size);
  typedef int _tsremap_new_instance(int argc, char *argv[], ihandle * ih, char *errbuf, int errbuf_size);
#define TSREMAP_FUNCNAME_NEW_INSTANCE "tsremap_new_instance"

  void tsremap_delete_instance(ihandle);
  typedef void _tsremap_delete_instance(ihandle);
#define TSREMAP_FUNCNAME_DELETE_INSTANCE "tsremap_delete_instance"

#define TSREMAP_RRI_MAX_HOST_SIZE    256
#define TSREMAP_RRI_MAX_PATH_SIZE    (1024*2)
#define TSREMAP_RRI_MAX_REDIRECT_URL (1024*2)

  typedef struct _tm_remap_request_info
  {
    /* the following fields are read only */
    unsigned long size;         /* sizeof(TSRemapRequestInfo) */

    int request_port;           /* request port number */
    int remap_from_port;        /* fromURL port number (from remap rule string) */
    int remap_to_port;          /* toURL port number (from remap rule string) */

    const char *orig_url;       /* request URL */
    int orig_url_size;          /* request URL size */

    const char *request_host;   /* request host string (without '\0' at the end of the string) */
    int request_host_size;      /* request host string size */

    const char *remap_from_host;        /* fromURL host (from remap rule string) */
    int remap_from_host_size;   /* fromURL host size */

    const char *remap_to_host;  /* toURL host (from remap rule string) */
    int remap_to_host_size;     /* toURL host size */

    const char *request_path;   /* request path */
    int request_path_size;      /* request path size */

    const char *remap_from_path;        /* fromURL path (from remap rule string) */
    int remap_from_path_size;   /* fromURL path size */

    const char *remap_to_path;  /* toURL path (from remap rule string) */
    int remap_to_path_size;     /* toURL path size */

    const char *request_cookie; /* request cookie string */
    int request_cookie_size;    /* request cookie string size */

    const char *request_query;  /* request query string */
    int request_query_size;     /* request query string size. A negative size means remove it completely. */

    const char *request_matrix; /* request matrix string */
    int request_matrix_size;    /* request matrix string size. A negative size means remove it completely. */

    const char *from_scheme;    /* The "from" scheme (e.g. HTTP) */
    int from_scheme_len;        /* The len of the "from" scheme */

    const char *to_scheme;      /* The "to" scheme (e.g. HTTP) */
    int to_scheme_len;          /* The len of the "to" scheme */

    unsigned int client_ip;     /* The client IP is an unsigned network (big-endian) 32-bit number. */
    /* Each of the dotted components is a byte, so: */
    /* 0x25364758 = 0x25.0x36.0x47.0x58 = 37.54.71.88 in decimal. */

    /* plugin can change the following fields */
    char new_host[TSREMAP_RRI_MAX_HOST_SIZE];   /* new host string */
    int new_host_size;          /* new host string size (if 0 - do not change request host) */
    int new_port;               /* new port number (0 - do not change request port) */
    char new_path[TSREMAP_RRI_MAX_PATH_SIZE];   /* new path string */
    int new_path_size;          /* new path string size (0 - do not change request path) */
    char new_query[TSREMAP_RRI_MAX_PATH_SIZE];  /* new query string */
    int new_query_size;         /* new query string size (0 - do not change request query) */
    char new_matrix[TSREMAP_RRI_MAX_PATH_SIZE]; /* new matrix parameter string */
    int new_matrix_size;        /* new matrix parameter string size (0 - do not change matrix parameters) */
    char redirect_url[TSREMAP_RRI_MAX_REDIRECT_URL];    /* redirect url (to redirect/reject request) */
    int redirect_url_size;      /* redirect url size (0 - empty redirect url string) */
    int require_ssl;            /* Require the toScheme to become SSL (e.g. HTTPS).  */
    /*    0 -> Disable SSL if toScheme is SSL */
    /*    1 -> Enable SSL if toScheme is not SSL */
    /*   -1 (default) -> Don't modify scheme */
  } TSRemapRequestInfo;

  /* Remap new request
     Return: != 0 - request was remapped, TS must look at new_... fields in TSRemapRequestInfo
             == 0 - request was not processed. TS must perform default remap
     Note: rhandle == INKHttpTxn (see ts/ts.h for more details)
     Remap API plugin can use InkAPI function calls inside tsremap_remap()
  */
  int tsremap_remap(ihandle ih, rhandle rh, TSRemapRequestInfo * rri);
  typedef int _tsremap_remap(ihandle ih, rhandle rh, TSRemapRequestInfo * rri);
#define TSREMAP_FUNCNAME_REMAP "tsremap_remap"

  /* Check response code from Origin Server
     Return: none
     Note: rhandle == INKHttpTxn (see ts/ts.h for more details)
     os_response_type -> INKServerState
     Remap API plugin can use InkAPI function calls inside tsremap_remap()
  */
  void tsremap_os_response(ihandle ih, rhandle rh, int os_response_type);
  typedef void _tsremap_os_response(ihandle ih, rhandle rh, int os_response_type);
#define TSREMAP_FUNCNAME_OS_RESPONSE "tsremap_os_response"

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* #ifndef H_REMAPAPI_H */
