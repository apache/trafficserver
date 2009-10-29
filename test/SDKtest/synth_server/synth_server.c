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
    fprintf(stderr, "Warning : did not see expected strings : sizes, length
or lost \n");
#endif
    return;
  }
}

/*  free_outbuf frees the output buffer and resets the state. if no */
/*  error has taken place, it decides to either read from, write to or */
/*  close the connection depending on the keepalive state and the data */
/*  already read from the connection */

void
free_outbuf(int fd, int error)
{
  assert(fd_table[fd].keepalive >= 0);


  memset(fd_table[fd].outbuf, 0, MAX_UNIQUE_OUTPUT);
  fd_table[fd].outbufsize = fd_table[fd].outbufwritten = fd_table[fd].X_string_to_write = 0;

#ifdef _PLUG_IN
  /*********************/
  fd_table[fd].bytes_last_sent = 0;
  fd_table[fd].bytes_last_response = 0;
  fd_table[fd].use_plugin_response = FALSE;
  /*********************/
#endif
  if ((!fd_table[fd].keepalive) || (error)) {
    /*  close the connection */
    fd_table[fd].state = 0;
    fd_table[fd].inbufptr = NULL;
    memset(fd_table[fd].inbuf, 0, MAX_REQUEST_STRING);
    fd_table[fd].read_offset = 0;
    close(fd);

#ifdef _PLUG_IN
    /*************************/
    fd_table[fd].response_id = NULL;
    /*************************/
#endif
  } else if (!(strstr(fd_table[fd].inbufptr, SYNTH_REQ_DELIM))) {
    /*  have to read more from the fd as the request in not complete */
    assert(fd_table[fd].state == WRITABLE);
    memset(fd_table[fd].inbuf, 1, fd_table[fd].inbufptr - fd_table[fd].inbuf);
    fd_table[fd].state = READABLE;
    fd_table[fd].keepalive--;
  } else {
    /*  have one more request : create the output */
    assert(fd_table[fd].state == WRITABLE);
    memset(fd_table[fd].inbuf, 1, fd_table[fd].inbufptr - fd_table[fd].inbuf);

#ifdef _PLUG_IN
    /*********************/
    if (plug_in.response_prepare_fcn) {
      fd_table[fd].use_plugin_response =
        (plug_in.response_prepare_fcn) (fd_table[fd].inbuf, fd_table[fd].read_offset, &fd_table[fd].response_id);
    }
    if (!fd_table[fd].use_plugin_response) {
      create_output(fd);
    }
    /*********************/
#else
    create_output(fd);
#endif

    fd_table[fd].keepalive--;
  }
  fd_table[fd].outbufwritten = 0;
}


void
read_request(int fd)
{
  int len;
  /*  printf("read_request called \n"); */
  if (!(strstr(fd_table[fd].inbufptr, SYNTH_REQ_DELIM))) {
    /*  the input buffer is delimited by SYNTH_REQ_DELIM */
#ifdef DEBUG
    fprintf(stderr, "read_offset=%d  MAX_REQUEST_STRING=%d  difference=%d\n",
            fd_table[fd].read_offset, MAX_REQUEST_STRING, MAX_REQUEST_STRING - fd_table[fd].read_offset);
    fflush(stderr);
#endif
    if (fd_table[fd].read_offset > (MAX_REQUEST_STRING * 3) / 4) {
      fprintf(stderr, "Warning: Input buffer almost full.  Not reading any more.\n");
      fflush(stderr);
      free_outbuf(fd, 1);
      return;
    }
    len = read(fd, fd_table[fd].inbuf + fd_table[fd].read_offset, MAX_REQUEST_STRING - fd_table[fd].read_offset);
#ifdef DEBUG
    fprintf(stderr, "read returned %d bytes\n", len);
    fflush(stderr);
#endif
    if (len < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN)
        return;                 /*  temporary error */
      free_outbuf(fd, 1);
      return;                   /*  badness! */
    }
    if (len == 0) {
      /*  poll said that data available => 0 bytes is bad */
      free_outbuf(fd, 1);
      return;
    }
    if (len > 0) {
      fd_table[fd].read_offset += len;
      /*  printf("inside read loop; the string now is %s \n", fd_table[fd].inbuf); */
    }
  }

  if (!(strstr(fd_table[fd].inbufptr, SYNTH_REQ_DELIM)))
    return;

  if (strstr(fd_table[fd].inbufptr, "LOST")) {
    /* example: "GET LOST length20.html" 
       used by the client to kill the server */
    fprintf(stderr, "SDKtest_server: shutting down because of client request to stop server.\n");

#ifdef _PLUG_IN
    /********************/
    if (plug_in.plugin_finish_fcn) {
      (plug_in.plugin_finish_fcn) ();
    }
    /********************/
#endif

    exit(0);
  }
#ifdef _PLUG_IN
  /***********************/
  if (plug_in.response_prepare_fcn) {
    fd_table[fd].use_plugin_response =
      (plug_in.response_prepare_fcn) (fd_table[fd].inbuf, fd_table[fd].read_offset, &fd_table[fd].response_id);
  }
  if (!fd_table[fd].use_plugin_response) {
    create_output(fd);
  }
#else
  create_output(fd);
  /***********************/
#endif

  fd_table[fd].state = WRITABLE;
}

/**********************************************************/
void
process_line(int line_no, char *line, int line_size, char *lhs, char *rhs)
{
  int i, j;
  i = 0;
  lhs[0] = rhs[0] = '\0';
  while (i < line_size && isspace(line[i]))
    i++;
  if (i == line_size)
    return;
  if (line[i] == '#')
    return;
  j = 0;
  while (i < line_size && !isspace(line[i]) && (line[i] != '=')) {
    lhs[j++] = line[i++];
  }
  if (i == line_size) {
    printf("Syntax error in config file line %d\n", line_no);
    exit(1);
  }
  lhs[j++] = '\0';
  while (i < line_size && isspace(line[i]))
    i++;
  if (i == line_size) {
    printf("Syntax error in config file line %d\n", line_no);
    exit(1);
  }
  if (line[i] != '=') {
    printf("Syntax error in config file line %d\n", line_no);
    exit(1);
  }
  i++;
  while (i < line_size && isspace(line[i]))
    i++;
  if (i == line_size) {
    printf("Syntax error in config file line %d\n", line_no);
    exit(1);
  }
  j = 0;
  while (i < line_size && !isspace(line[i])) {
    rhs[j++] = line[i++];
  }
  rhs[j++] = '\0';
}


void
read_config_file()
{
  FILE *config_file_p;
  char line[MAX_LINE_SIZE], lhs[MAX_LINE_SIZE], rhs[MAX_LINE_SIZE];
  int end_of_file = 0;
  int line_no, i, c;

  if (!(config_file_p = fopen("SDKtest_server.config", "r"))) {
    fprintf(stderr, "Error: could not open the config file SDKtest_server.config\n");
    perror("Config File Open");
    exit(1);
  }
  line_no = 1;
  do {
    i = 0;
    do {
      line[i++] = (c = getc(config_file_p));
    } while (c != '\n' && c != EOF && i < MAX_LINE_SIZE);
    if (i == MAX_LINE_SIZE) {
      fprintf(stderr, "Error in Config File: Lines can only be %d chars long\n", MAX_LINE_SIZE);
      exit(1);
    }
    if (c == EOF) {
      end_of_file = 1;
    }
    i--;
    line[i] = '\0';
    if (i > 0) {
      process_line(line_no, line, i, lhs, rhs);

#ifdef _PLUG_IN
      /***********************/
      if (plug_in.options_process_fcn) {
        if (strcmp(lhs, "")) {
          (plug_in.options_process_fcn) (lhs, rhs);
        }
      }
      /***********************/
#endif

    }
    line_no++;
  } while (!end_of_file);
  fclose(config_file_p);
}

/**********************************************************/


void
read_docsize_dist(FILE * docsize_dist_file_p, int *num_sizes_p, long *sizes, float *cumulative_size_prob)
{
  int end_of_file = 0;
  int n, i;
  long size;
  float prob, avg_doc_size = 0.0;
  *num_sizes_p = 0;
  do {
    n = fscanf(docsize_dist_file_p, "%ld %f", &size, &prob);
    if (n == EOF) {             /*fscanf will return number of matched items. If it
                                   returns EOF that means none were matched */
      end_of_file = 1;
    } else if (n == 2) {
      sizes[(*num_sizes_p)] = size;
      if ((*num_sizes_p) == 0) {
        cumulative_size_prob[(*num_sizes_p)] = prob;
      } else {
        cumulative_size_prob[(*num_sizes_p)] = cumulative_size_prob[(*num_sizes_p) - 1] + prob;
      }
      (*num_sizes_p)++;
      avg_doc_size += prob * size;
    } else {
      fprintf(stderr, "SDKtest_server: Error in docsize_dist_file\n");
      exit(1);
    }
  } while (!end_of_file);
  if ((cumulative_size_prob[(*num_sizes_p) - 1]<0.999) || (cumulative_size_prob[(*num_sizes_p) - 1]> 1.001)) {
    fprintf(stderr, "SDKtest_server: Error in docsize_dist_file: prob add up to %f\n",
            cumulative_size_prob[(*num_sizes_p) - 1]);
    exit(1);
  }
#ifdef DEBUG
  /*  fprintf(stderr,"Average Doc Size according to the specified distribution %.2f\n", avg_doc_size); */
#endif
  for (i = 0; i < num_sizes; i++) {
#ifdef DEBUG
    /*  fprintf(stderr,"%ld %.2f\n", sizes[i], cumulative_size_prob[i]); */
#endif
  }
}

/* Create a socket. Default is blocking, stream (TCP) socket.  IO_TYPE
 * is OR of COMM flags defined above */
int
comm_open(int sock_type, int proto, u_short port, int flags)
{
  struct sockaddr_in sa;
  int new_socket;

  /* Create socket for accepting new connections. */
  if ((new_socket = socket(AF_INET, sock_type, proto)) < 0) {
#ifdef DEBUG
    fprintf(stderr, "comm_open: socket failure\n");
#endif
    return (COMM_ERROR);
  }
  commSetNoLinger(new_socket);
  commSetReuseAddr(new_socket);

  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(new_socket, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    return COMM_ERROR;

  if (flags & COMM_NONBLOCKING)
    if (commSetNonBlocking(new_socket) == COMM_ERROR)
      return COMM_ERROR;
  return new_socket;
}

int
comm_listen(int sock)
{
  int x;
  if ((x = listen(sock, LISTEN_BACKLOG)) < 0) {
#ifdef DEBUG
    fprintf(stderr, "comm_listen: listen(%d, %d)\n", LISTEN_BACKLOG, sock);
#endif
    return x;
  }
  return sock;
}

#ifdef DEBUG1
void
leak_cop(int signum)
{
  int i;
  /*  signal ( SIGALRM , leak_cop ) ; */
  signal(SIGUSR1, leak_cop);

  /*   for (i = 0; i < BIGGEST_FD ; i++) { */
  for (i = 0; i < 0; i++) {
    if (fd_table[i].outbuf && fd_table[i].state == READABLE)
      fprintf(stderr, "%d fd is readable with request %s \n the outbufsize is %d and outbufwritten is %d", i,
              fd_table[i].inbuf, fd_table[i].outbufsize, fd_table[i].outbufwritten);
    else if (fd_table[i].outbuf && fd_table[i].state == WRITABLE)
      fprintf(stderr, "%d fd is writable with request %s \n the outbufsize is %d and outbufwritten is %d", i,
              fd_table[i].inbuf, fd_table[i].outbufsize, fd_table[i].outbufwritten);
    else if (fd_table[i].outbuf)
      fprintf(stderr, "%d fd is closed with request %s \n the outbufsize is %d and outbufwritten is %d", i,
              fd_table[i].inbuf, fd_table[i].outbufsize, fd_table[i].outbufwritten);
  }
}
#endif

void
comm_init(void)
{
  int n1, n2, n3;

  fd_table = (RWState *) malloc(n1 = (max_users * sizeof(RWState)));
  fd_polltable = (struct pollfd *) malloc(n2 = (max_users * sizeof(struct pollfd)));
  Xstring = (char *) malloc(n3 = max_output_string);

  if (!(fd_table && fd_polltable && Xstring)) {
    /* error occured in one of the large initialization mallocs */
    fprintf(stderr, "SDKtest_server: error in allocating %d bytes for the initialization buffers\n", n1 + n2 + n3);
    exit(-1);
  }

  memset((void *) fd_table, 0, max_users * sizeof(RWState));
  memset((void *) fd_polltable, 0, max_users * sizeof(struct pollfd));

  /*  initialize the Xstring to be XXXX....XXXXE */
  memset((void *) Xstring, 'X', max_output_string - 1);
  Xstring[max_output_string - 1] = 'E';

#ifdef DEBUG1
  signal(SIGUSR1, leak_cop);
  /*   signal(SIGALRM, leak_cop); */
  /*  ualarm ( 5000000 , 0 ) ; */
# endif
}

void
serverConnectionsOpen(int port)
{
  theHttpConnection = comm_open(SOCK_STREAM, 0, port, COMM_NONBLOCKING);
  if (theHttpConnection < 0) {
    fprintf(stderr, "SDKtest_server: unable to open socket connection to listen for requests on port %d\n", port);
    exit(0);
  }

  BIGGEST_FD = theHttpConnection;
  comm_listen(theHttpConnection);
  fd_table[theHttpConnection].state = READABLE;
  fd_table[theHttpConnection].outbufsize = 0;
}


int
comm_write(int fd)
{
  int len = 0;
  int nleft, tmp_nleft;
  assert(fd_table[fd].state == WRITABLE);

#ifdef _PLUG_IN
  /***********************************************************/
  if (fd_table[fd].use_plugin_response) {
    if (fd_table[fd].bytes_last_response <= fd_table[fd].bytes_last_sent) {
      if (plug_in.response_put_fcn) {
        (plug_in.response_put_fcn) (&fd_table[fd].response_id /* return */ ,
                                    (void *) fd_table[fd].outbuf /* return */ ,
                                    &tmp_nleft /* return */ ,
                                    MAX_UNIQUE_OUTPUT, fd_table[fd].bytes_last_response);
      }
      fd_table[fd].bytes_last_response = tmp_nleft;
      fd_table[fd].bytes_last_sent = 0;
    }
    assert(fd_table[fd].bytes_last_response >= 0);
    if (fd_table[fd].bytes_last_response > 0) {
      len = write(fd,
                  fd_table[fd].outbuf + fd_table[fd].bytes_last_sent,
                  fd_table[fd].bytes_last_response - fd_table[fd].bytes_last_sent);

      /*fprintf(stderr, "len%d\n", len); */
      if (len == 0) {
#ifdef DEBUG
        fprintf(stderr, "commwrite: FD %d: write failure: connection closed\n", fd);
#endif
        free_outbuf(fd, 1);
      } /*  end if len==0 */
      else if (len < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
#ifdef DEBUG
          fprintf(stderr, "commHandleWrite: FD %d: write failure.\n", fd);
#endif
          fd_table[fd].bytes_last_sent = 0;
          return COMM_OK;
        } else {
#ifdef DEBUG
          fprintf(stderr, "commHandleWrite: FD %d: write failure.\n", fd);
#endif
          free_outbuf(fd, 1);
        }
      } /*  end if len < 0 */
      else {                    /* len > 0 */
        fd_table[fd].outbufwritten += len;
        fd_table[fd].bytes_last_sent += len;
        if (fd_table[fd].bytes_last_sent == fd_table[fd].bytes_last_response && fd_table[fd].response_id == NULL) {
          free_outbuf(fd, 0);
        }
      }
    } else {
      free_outbuf(fd, 0);
    }
    return COMM_OK;
    /***********************************************************/
  }
#endif /* ifdef _PLUG_IN */

  nleft = fd_table[fd].outbufsize - fd_table[fd].outbufwritten;
  assert(fd_table[fd].outbuf);
  assert(nleft >= 0);
  if (nleft > 0) {
    /* data left to be written in the unique portion */
    tmp_nleft = min(nleft, MAX_UNIQUE_OUTPUT);
    len = write(fd, fd_table[fd].outbuf + fd_table[fd].outbufwritten, tmp_nleft);
    if (len == 0) {
#ifdef DEBUG
      fprintf(stderr, "commwrite: FD %d: write failure: connection closed with
%d bytes remaining.\n", fd, nleft);
#endif
#ifdef DEBUG
      /* fprintf(stderr,"commwrite : FD %d : write finished \n",fd); */
#endif
      free_outbuf(fd, 1);
    } /*  end if len==0 */
    else if (len < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
#ifdef DEBUG
        fprintf(stderr, "commHandleWrite: FD %d: write failure.\n", fd);
#endif
        return COMM_OK;
      } else {
#ifdef DEBUG
        fprintf(stderr, "commHandleWrite: FD %d: write failure.\n", fd);
#endif
        free_outbuf(fd, 1);
      }
    } /*  end if len<0 */
    else {
      /*  A successful write, continue */
      fd_table[fd].outbufwritten += len;
      if (len == nleft && fd_table[fd].X_string_to_write == 0) {
        free_outbuf(fd, 0);
#ifdef DEBUG
        /*  fprintf(stderr,"commwrite : FD %d : write finished \n",fd); */
#endif
      }
    }
  } /*  nleft <= 0; end case of unique data left */
  else if (fd_table[fd].X_string_to_write > 0) {
    /* case when data needs to be written from the Xstrin */
    len = write(fd, fd_table[fd].outbuf1, fd_table[fd].X_string_to_write);
    if (len == 0) {
      free_outbuf(fd, 1);
    } /*  end if len==0 */
    else if (len < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN)
        return COMM_OK;
      else {
        free_outbuf(fd, 1);
      }
    } /*  end if len<0 */
    else {
      /*  A successful write, continue */
      fd_table[fd].outbuf1 += len;
      fd_table[fd].X_string_to_write -= len;
      if ((fd_table[fd].X_string_to_write == 0)) {
        free_outbuf(fd, 0);
      }
    }
  } /*  end case of write from the Xstring */
  else {
    /* no more data left to be written in either outbuf or Xstring */
    free_outbuf(fd, 0);
  }
  return COMM_OK;
}

/*  select on all fds  */
int
comm_select(void)
{
#ifdef DEBUG3
  unsigned long long time1, time2;
  int temp_var = 1, temp_count = 0, temp_count_watermark = 0;
#endif
  struct pollfd tmp_fd_poll;
  int i, nfds, maxfd, tmp_poll_retval;
  nfds = 0;
  maxfd = BIGGEST_FD + 1;
  for (i = 1; i < maxfd; i++) {
    if (fd_table[i].state == READABLE) {
      fd_polltable[nfds].fd = i;
      fd_polltable[nfds].events = POLLIN;
      fd_polltable[nfds++].revents = 0;
    } else if (fd_table[i].state == WRITABLE) {
      fd_polltable[nfds].fd = i;
      fd_polltable[nfds].events = POLLOUT;
      fd_polltable[nfds++].revents = 0;
    }
  }

  BIGGEST_FD = fd_polltable[nfds - 1].fd;
#ifdef DEBUG3
  /*  Ajit : need to investigate this carefully later.  */
  if (nfds > 25) {
    /*  fprintf(stderr,".\n"); */
    temp_count_watermark = 0;
  } else
    temp_count_watermark = 0;
  while (temp_var && (temp_count < temp_count_watermark)) {
    tmp_fd_poll.fd = theHttpConnection;
    tmp_fd_poll.events = POLLIN;
    tmp_fd_poll.revents = 0;
    temp_var = poll(&tmp_fd_poll, 1, 1000);
    if (temp_var)
      accept_connection();
    temp_count++;
  }
#endif
  if (nfds == 0)
    return COMM_SHUTDOWN;
#if 0
  time1 = microsecond_rtimer();
#endif
  tmp_poll_retval = poll(fd_polltable, nfds, 1000);
#if 0
  time2 = microsecond_rtimer();
  if (nfds > 1) {
    poll_stat_var++;
    poll_time += time2 - time1;
  }
#endif
  if (tmp_poll_retval)
    poll_retval = tmp_poll_retval;
  if (tmp_poll_retval < 0) {
    if (errno == EINTR)
      return COMM_OK;
#ifdef DEBUG
    fprintf(stderr, "comm_select: poll failure\n");
#endif
    return COMM_ERROR;
  }
  for (i = 0; i < nfds; i++) {
    if (!(fd_polltable[i].revents & (POLLIN | POLLOUT)))
      continue;
    if (fd_polltable[i].revents & POLLIN) {
      if (fd_polltable[i].fd == theHttpConnection)
        accept_connection();
      else {
        read_request(fd_polltable[i].fd);
        if (fd_table[fd_polltable[i].fd].state == WRITABLE) {
          tmp_fd_poll.fd = fd_polltable[i].fd;
          tmp_fd_poll.events = POLLOUT;
          tmp_fd_poll.revents = 0;
          if (poll(&tmp_fd_poll, 1, 1000) && (tmp_fd_poll.revents & POLLOUT))
            comm_write(tmp_fd_poll.fd);
        }
      }
    } /*  end readable fd */
    else if (fd_polltable[i].revents & POLLOUT) {
      comm_write(fd_polltable[i].fd);
    }                           /*  end writable fd */
  }                             /*  end loop over all fd's */
  return COMM_OK;
}

void
usage()
{
  printf("Usage:\n");
  printf
    ("\t \"SDKtest_server [-dExecution-Directory] [-cDocsize-Distribution-File] [-pPort-Number] [-oMax-Output-Length] [-uMax-Users] [-aPlugin-file]\" (no extra spaces)\n");
  printf("\t Default port: 8080\n");
  exit(0);
}

int
main(int argc, char *argv[])
{
  char *docsize_dist_file = NULL;
  int port = 8080;
  int i;
  FILE *docsize_dist_file_p = NULL;
  int errcount;
  struct rlimit rlp;
  char *api, *dir;
  api = "";

  if (getrlimit(RLIMIT_NOFILE, &rlp) == 0) {
    fd_limit = rlp.rlim_cur;
  } else {
    fd_limit = 0;
  }
#ifdef DEBUG
  fprintf(stderr, "fd_limit = %d\n", fd_limit);
  fflush(stderr);
#endif
  signal(SIGPIPE, SIG_IGN);
  for (i = 1; i < argc && argv[i][0] == '-'; i++) {
    switch (argv[i][1]) {
    case '?':
    case 'h':
      usage();
      break;                    /* NOTREACHED */
    case 'd':
      dir = argv[i] + 2;
      if (chdir(dir) != 0) {
        perror("chdir");
        exit(1);
      }
      break;
    case 'c':
      if (*(argv[i] + 2) == '\0')
        usage();
      docsize_dist_file = argv[i] + 2;
      break;
    case 'p':
      if (*(argv[i] + 2) == '\0')
        usage();
      port = atoi(argv[i] + 2);
      break;
    case 'o':
      if (*(argv[i] + 2) == '\0')
        usage();
      max_output_string = atoi(argv[i] + 2);
      printf("The maximum output string requested should be less than %ld bytes\n", max_output_string);
      break;
    case 'u':
      if (*(argv[i] + 2) == '\0')
        usage();
      max_users = atoi(argv[i] + 2);
      printf("The maximum number of users allowed is %ld \n", max_users);
      break;
    case 'k':
      if (*(argv[i] + 2) == '\0')
        usage();
      max_keepalive = atoi(argv[i] + 2);
      printf("The maximum keepalive allowed is %d \n", max_keepalive);
      break;
    case 't':                  /* set by inkbench script to help people identify process using ps (/usr/bin/ps -ef) */
      /* ignore option */
      break;
    case 'a':
      api = argv[i] + 2;
      break;
    default:
      usage();
#ifdef DEBUG
      fprintf(stderr, "%s: unknown switch '%c', try -h for help\n", argv[0], argv[i][1]);
#endif
      break;                    /* NOTREACHED */
    }
  }
#ifdef _PLUG_IN
  /********************/
  plugin_initialize(api);
  if (plug_in.plugin_init_fcn) {
    (plug_in.plugin_init_fcn) ();
    fprintf(stderr, "SDKtest_server: finished loading plugin\n");
  }
  /********************/
#endif

  read_config_file();

#ifdef _PLUG_IN
  /********************/
  if (plug_in.options_process_finish_fcn) {
    (plug_in.options_process_finish_fcn) ();
  }
  /********************/
#endif

  if (argc > i)
    usage();

#ifdef DEBUG
  if (docsize_dist_file)
    printf("the parameters are -c%s -p%d \n", docsize_dist_file, port);
  else
    printf("the parameters are  -p%d \n", port);
#endif

  if (docsize_dist_file) {
    if (!(docsize_dist_file_p = fopen(docsize_dist_file, "r"))) {
#ifdef DEBUG
      fprintf(stderr, "Could not open the docsize_dist_file %s\n", docsize_dist_file);
#endif
      perror("DocSize Dist File Open");
      exit(1);
    }
    read_docsize_dist(docsize_dist_file_p, &num_sizes, sizes, cumulative_size_prob);
  }

  comm_init();
  serverConnectionsOpen(port);

  /*  main loop */
  for (;;) {
    switch (comm_select()) {
    case COMM_OK:
      errcount = 0;             /* reset if successful */
      break;
    case COMM_ERROR:
      errcount++;
#ifdef DEBUG
      fprintf(stderr, "Select loop Error. Retry %d\n", errcount);
#endif
      if (errcount == 10) {
#ifdef _PLUG_IN
        /********************/
        if (plug_in.plugin_finish_fcn) {
          (plug_in.plugin_finish_fcn) ();
        }
        /********************/
#endif
        exit(0);
      }
      break;
    case COMM_SHUTDOWN:
      break;
    case COMM_TIMEOUT:
      break;
    default:

#ifdef _PLUG_IN
      /********************/
      if (plug_in.plugin_finish_fcn) {
        (plug_in.plugin_finish_fcn) ();
      }
      /********************/
#endif

      exit(0);
      break;
    }
  }
}
