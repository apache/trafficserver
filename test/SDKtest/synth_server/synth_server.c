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

/*
  synth_server.c : code for the origin server.
  features planned :
  variable speed server
*/

/* #define DEBUG3 */
#ifdef DEBUG3
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <syslog.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <values.h>
#include <math.h>
#include <assert.h>
#include "api/ServerAPI.h"

#if (HOST_OS != hpux)
#include <dlfcn.h>
#endif

#define LISTEN_BACKLOG 10240

#define MAX_USERS 5000
        /* Max simultaneous users (default value)            */
#define MAX_REQUEST_STRING 4000
        /*  Max request string read from the client          */
#define MAX_UNIQUE_OUTPUT 2000
        /*  Max unique part of output                        */
#define MAX_OUTPUT (5*1024*1024)
        /*  Max length of output string (default value)      */
#define MAX_KEEPALIVE 8
        /*  Max number of keepalive requests on a connection */
#define MAX_SIZES 100
        /*  Max number of document sizes in distr            */
#define MAX_LINE_SIZE 1000
        /* Max size of a line within the config file         */

#define SYNTH_REQ_DELIM "\r\n\r\n"
#define HEADER_LENGTH 200

#define READABLE 1
#define WRITABLE 2
#define TRUE 1
#define FALSE 0
#define CLOSED 3

#ifdef DEBUG3
unsigned long long poll_time;
int poll_stat_var;
#endif
int poll_retval;

/*
   initialize the default values for memory allocation. the size of
   the solaris executable with the default values is around 60MB
 */
long max_output_string = MAX_OUTPUT;
long max_users = MAX_USERS;
int max_keepalive = MAX_KEEPALIVE;
int fd_limit;

typedef struct
{
  int outbufsize;
  /*  the used size of outbuf                                   */
  int outbufwritten;
  /*  bytes written from the preallocated outbuf                */
  char inbuf[MAX_REQUEST_STRING];
  /*  number of characters of the output buffer already written */
  char *inbufptr;
  /*  constant sized buffer for the GET request                 */
  int read_offset;
  /*  point inside inbuf where request has been parsed          */
  int keepalive;
  int keepalive_requests;
  char outbuf[MAX_UNIQUE_OUTPUT];
  char *outbuf1;
  int X_string_to_write;
  char state;

#ifdef _PLUG_IN
  /***********************/
  int use_plugin_response;      /* determine if we use default response or not */
  void *response_id;
  int bytes_last_sent;
  int bytes_last_response;
  /***********************/
#endif

} RWState;

int num_sizes;
    /* Number of sizes in the docsize distribution         */
long sizes[MAX_SIZES];
    /* actual sizes                                        */
float cumulative_size_prob[MAX_SIZES];
    /* Cumulative probability of selecting different sizes
     * cumulative_size_prob[num_sizes-1] must be 1.0       */

#define COMM_OK           (0)
#define COMM_ERROR       (-1)
#define COMM_SHUTDOWN	 (-2)
#define COMM_TIMEOUT	 (-3)

#define COMM_NONBLOCKING  (0x1)
int theHttpConnection = -1;
int BIGGEST_FD = 0;

#define max(a,b) ((a)>(b)? (a) : (b))
#define min(a,b) ((a)<(b)? (a) : (b))

RWState *fd_table;
struct pollfd *fd_polltable;
char *Xstring;

/*
  Solaris specific call
  unsigned long long microsecond_rtimer (void)
  {
  hrtime_t timeNow = gethrtime();
  return (timeNow / 1000);
  }
*/

#if (HOST_OS == hpux)
#include <dl.h>
#define RTLD_NOW BIND_IMMEDIATE

void *
dlsym(void *handle, const char *name)
{
  void *p;

  shl_findsym((shl_t *) & handle, name, TYPE_UNDEFINED, &p);
  return p;
}

void *
dlopen(char *name, unsigned int flag)
{
  return shl_load(name, BIND_IMMEDIATE, 0);     /* hack, we know RTLD_NOW */
}

int
dlclose(void *handle)
{
  return shl_unload((shl_t) handle);
}

char *
dlerror(void)
{
  return strerror(errno);
}
#endif


#ifdef _PLUG_IN
/********************/

typedef void (*PluginInit) ();
typedef void (*OptionsProcess) (char *, char *);
typedef void (*OptionsProcessFinish) ();
typedef void (*PluginFinish) ();
typedef int (*ResponsePrepare) (char *, int, void **);
typedef void (*ResponsePut) (void **, void *, int *, int, int);

typedef struct
{
  void *handle;                 /* handle from dlopen */

  PluginInit plugin_init_fcn;
  OptionsProcess options_process_fcn;
  OptionsProcessFinish options_process_finish_fcn;
  PluginFinish plugin_finish_fcn;
  ResponsePrepare response_prepare_fcn;
  ResponsePut response_put_fcn;
} INKPlugin;

INKPlugin plug_in;

void
plugin_initialize(char *api)
{
  char *path = (char *) malloc(strlen(api) + 3);

  plug_in.plugin_init_fcn = NULL;
  plug_in.options_process_fcn = NULL;
  plug_in.options_process_finish_fcn = NULL;
  plug_in.plugin_finish_fcn = NULL;
  plug_in.response_prepare_fcn = NULL;
  plug_in.response_put_fcn = NULL;

  if (strcmp(api, "")) {
    sprintf(path, "./%s", api);
    fprintf(stderr, "\nSDKtest_server: loading plugin %s ...\n", path + 2);

    plug_in.handle = dlopen(path, RTLD_NOW);
    if (!plug_in.handle) {
      fprintf(stderr, "unable to load synthetic server plugin\n");
      perror("server plugin");
      exit(1);
    }
    plug_in.plugin_init_fcn = (PluginInit) dlsym(plug_in.handle, "INKPluginInit");

    if (!plug_in.plugin_init_fcn) {
      fprintf(stderr, "unable to find INKPluginInit function: %s", dlerror());
      dlclose(plug_in.handle);
      exit(1);
    }
  }
}

void
INKFuncRegister(INKPluginFuncId fid)
{
  switch (fid) {
  case INK_FID_OPTIONS_PROCESS:
    plug_in.options_process_fcn = (OptionsProcess) dlsym(plug_in.handle, "INKOptionsProcess");
    break;
  case INK_FID_OPTIONS_PROCESS_FINISH:
    plug_in.options_process_finish_fcn = (OptionsProcessFinish) dlsym(plug_in.handle, "INKOptionsProcessFinish");
    break;
  case INK_FID_PLUGIN_FINISH:
    plug_in.plugin_finish_fcn = (PluginFinish) dlsym(plug_in.handle, "INKPluginFinish");
    break;
  case INK_FID_RESPONSE_PREPARE:
    plug_in.response_prepare_fcn = (ResponsePrepare) dlsym(plug_in.handle, "INKResponsePrepare");
    break;
  case INK_FID_RESPONSE_PUT:
    plug_in.response_put_fcn = (ResponsePut) dlsym(plug_in.handle, "INKResponsePut");
    break;
  default:
    fprintf(stderr, "Can't register function: unknown type of INKPluginFuncId");
    break;
  }
}

/********************/
#endif


int
commSetNonBlocking(int fd)
{
  int flags;
  if ((flags = fcntl(fd, F_GETFL)) < 0) {
#ifdef DEBUG
    fprintf(stderr, "FD %d: fcntl F_GETFL\n", fd);
#endif
    return COMM_ERROR;
  }
  if (fcntl(fd, F_SETFL, flags | O_NDELAY) < 0) {
#ifdef DEBUG
    fprintf(stderr, "FD %d: error setting O_NDELAY\n", fd);
#endif
    return COMM_ERROR;
  }
  return 0;
}


/* Wait for an incoming connection on FD.  FD should be a socket returned
 * from comm_listen. */
int
comm_accept(int fd, struct sockaddr_in *peer, struct sockaddr_in *me)
{
  int sock;
  struct sockaddr_in P;
  int Slen;

  Slen = sizeof(P);
  if ((sock = accept(fd, (struct sockaddr *) &P, &Slen)) < 0) {
#ifdef DEBUG
    fprintf(stderr, "comm_accept: FD %d: accept failure\n", fd);
#endif
    return COMM_ERROR;
  }

  if (peer)
    *peer = P;

  commSetNonBlocking(sock);
  return sock;
}

void
commSetNoLinger(int fd)
{
  struct linger L;
  L.l_onoff = 0;                /* off */
  L.l_linger = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &L, sizeof(L)) < 0) {
#ifdef DEBUG
    fprintf(stderr, "commSetNoLinger: FD %d\n", fd);
#endif
  }
}

void
commSetReuseAddr(int fd)
{
  int on = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) < 0) {
#ifdef DEBUG
    fprintf(stderr, "commSetReuseAddr: FD %d\n", fd);
#endif
  }
}


/*  this is the function called on the socket which is listening for */
/*  the connections : it accepts a connection, reads the request string */
/*  and creates the output buffer for the new file descriptor */

void
accept_connection()
{
  struct sockaddr_in peer;
  struct sockaddr_in me;
  int fd;

  if ((fd = comm_accept(theHttpConnection, &peer, &me)) < 0) {
    char err[200];
    if (errno == EMFILE) {
      sprintf(err, "SDKtest_server: accept_connection accept failure (current fd limit = %d)", fd_limit);
    } else {
      sprintf(err, "SDKtest_server: accept_connection accept failure");
    }
    perror(err);
    return;
  }

  if (fd >= max_users) {
    fprintf(stderr, "Error : too many simultaneous connections. \n");
    printf("Error : too many simultaneous connections. \n");
    exit(-1);
  }

  BIGGEST_FD = max(BIGGEST_FD, fd);
  fd_table[fd].state = READABLE;
  fd_table[fd].read_offset = 0;
  fd_table[fd].keepalive = 0;
  fd_table[fd].keepalive_requests = 0;
  fd_table[fd].inbufptr = fd_table[fd].inbuf;
}

/*  parses the input buffer and creates the document output */
void
create_output(int fd)
{

  char *found_size_str = NULL, *found_length_str = NULL, *h, *h1;
  char *p1;                     /* pointers to the beginning of URL address */
  int size_no;
  long content_length;
  int unique_content_length;    /* length of the unique part of data */

  if (strstr(fd_table[fd].inbufptr, "Connection: ")) {
    fd_table[fd].keepalive_requests++;
    if (fd_table[fd].keepalive_requests < max_keepalive) {
      fd_table[fd].keepalive++;
    }
#ifdef DEBUG
    fprintf(stderr, "saw 'Connection:'  keepalive=%d  keepalive_requests = %d\n",
            fd_table[fd].keepalive, fd_table[fd].keepalive_requests);
    fflush(stderr);
#endif
  }

  if ((found_size_str = strstr(fd_table[fd].inbufptr, "size")) ||
      (found_length_str = strstr(fd_table[fd].inbufptr, "length"))) {
    p1 = strstr(fd_table[fd].inbufptr, "GET");
#ifdef DEBUG
    fprintf(stderr, "SDKtest_server: pid %d got request '%s'\n", getpid(), p1);
#endif
    if (!p1) {
#ifdef DEBUG
      fprintf(stderr, "badly formed request %s \n", fd_table[fd].inbuf);
#endif
      return;
    }
    assert(strstr(fd_table[fd].inbufptr, SYNTH_REQ_DELIM));
    fd_table[fd].inbufptr = strstr(fd_table[fd].inbufptr, SYNTH_REQ_DELIM) + strlen(SYNTH_REQ_DELIM);
    if (found_size_str) {
      unique_content_length = found_size_str - p1;
      assert(unique_content_length < MAX_UNIQUE_OUTPUT);
      size_no = atoi(found_size_str + strlen("size"));
      if (size_no > num_sizes - 1)
        size_no = num_sizes - 1;
      content_length = sizes[size_no];
    } else {
      unique_content_length = found_length_str - p1;
      content_length = atoi(found_length_str + strlen("length"));
    }
    h = (h1 = (char *) malloc(HEADER_LENGTH));
    if (h1 == NULL) {
      fprintf(stderr, "SDKtest_server: can't allocate %d bytes for header.  Exiting.\n", HEADER_LENGTH);
      exit(-1);
    }
    h += sprintf(h, "HTTP/1.0 200 OK\r\n");
    if (fd_table[fd].keepalive)
      h += sprintf(h, "Connection: Keep-Alive\r\n");
    h += sprintf(h, "Content-type: text/html\r\n");
    h += sprintf(h, "Content-length: %ld\r\n\r\n", content_length);

    if (content_length >= (MAX_UNIQUE_OUTPUT + max_output_string)) {
      fprintf(stderr, "Error : document size too large \n");
      printf("Error : document size too large \n");
      exit(-1);
    }
    if (content_length < unique_content_length) {
      fprintf(stderr,
              "SDKtest_server: the unique part of the returned data is larger than the content. Probable error in document distribution function\n");
      fprintf(stderr, "SDKtest_server: the request is %s \n", fd_table[fd].inbuf);
      exit(-1);
    } /* end delta small content length case */
    else if ((content_length + (h - h1)) <= MAX_UNIQUE_OUTPUT) {
      /*  everything can be created in the outbuf in this case ... */
      fd_table[fd].outbufsize = content_length + (h - h1);
      memset(fd_table[fd].outbuf, 'X', content_length + (h - h1));
      fd_table[fd].outbuf[content_length + (h - h1) - 1] = 'E';
      memcpy(fd_table[fd].outbuf, h1, (h - h1));
      memcpy(fd_table[fd].outbuf + (h - h1), p1, unique_content_length);
      if (h1) {
        free(h1);
        h1 = NULL;
      }
    } else {
      /*  need to use the outbuf and the Xstring together for the */
      /*  output in this case */
      fd_table[fd].outbufsize = MAX_UNIQUE_OUTPUT;

      memset(fd_table[fd].outbuf, 'X', MAX_UNIQUE_OUTPUT);
      fd_table[fd].X_string_to_write = content_length + (h - h1) - MAX_UNIQUE_OUTPUT;
      fd_table[fd].outbuf1 = Xstring + max_output_string - fd_table[fd].X_string_to_write;
      memcpy(fd_table[fd].outbuf, h1, (h - h1));
      memcpy(fd_table[fd].outbuf + (h - h1), p1, unique_content_length);
      if (h1) {
        free(h1);
        h1 = NULL;
      }
    }
  } else {
#ifdef DEBUG
