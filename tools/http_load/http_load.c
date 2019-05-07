/* http_load - multiprocessing http test client
**
** Copyright © 1998,1999,2001 by Jef Poskanzer <jef@mail.acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#include "tscore/ink_config.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>

#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include "port.h"
#include "timers.h"

#if defined(AF_INET6) && defined(IN6_IS_ADDR_V4MAPPED)
#define USE_IPV6
#endif

#define max(a, b) ((a) >= (b) ? (a) : (b))
#define min(a, b) ((a) <= (b) ? (a) : (b))

/* How long a connection can stay idle before we give up on it. */
#define IDLE_SECS 60

/* Default max bytes/second in throttle mode. */
#define THROTTLE 3360

/* How often to show progress reports. */
#define PROGRESS_SECS 60

/* How many file descriptors to not use. */
#define RESERVED_FDS 3

typedef struct {
  char *url_str;
  int protocol;
  char *hostname;
  unsigned short port;
#ifdef USE_IPV6
  struct sockaddr_in6 sa;
#else  /* USE_IPV6 */
  struct sockaddr_in sa;
#endif /* USE_IPV6 */
  int sa_len, sock_family, sock_type, sock_protocol;
  char *filename;
  int got_bytes;
  long bytes;
  int got_checksum;
  long checksum;
  char *buf;
  int buf_bytes;
  int unique_id_offset;
  struct {
    int completed;
    int max_response;
    int min_response;
  } stats;

} url;
static url *urls;
static int num_urls, max_urls, cur_url;

typedef struct {
  char *str;
  struct sockaddr_in sa;
} sip;
static sip *sips;
static int num_sips, max_sips;

/* Protocol symbols. */
#define PROTO_HTTP 0
#define PROTO_HTTPS 1

/* Connection states */
typedef enum {
  CNST_FREE = 0,
  CNST_CONNECTING,
  CNST_HEADERS,
  CNST_READING,
  CNST_PAUSING,
} connection_states;

/* States for the Header State Machine */
typedef enum {
  /* SM basic states */
  HDST_LINE1_PROTOCOL = 0,
  HDST_LINE1_WS,
  HDST_LINE1_STATUS,
  HDST_BOL,
  HDST_TEXT,
  HDST_LF,
  HDST_CR,
  HDST_CRLF,
  HDST_CRLFCR,
  /* SM states for Content-Length header */
  HDST_C,
  HDST_CO,
  HDST_CON,
  HDST_CONT,
  HDST_CONTE,
  HDST_CONTEN,
  HDST_CONTENT,
  HDST_CONTENT_,
  HDST_CONTENT_L,
  HDST_CONTENT_LE,
  HDST_CONTENT_LEN,
  HDST_CONTENT_LENG,
  HDST_CONTENT_LENGT,
  HDST_CONTENT_LENGTH,
  HDST_CONTENT_LENGTH_COLON,
  HDST_CONTENT_LENGTH_COLON_WS,
  HDST_CONTENT_LENGTH_COLON_WS_NUM,
  /* SM states for Connection: close */
  HDST_CONN,
  HDST_CONNE,
  HDST_CONNEC,
  HDST_CONNECT,
  HDST_CONNECTI,
  HDST_CONNECTIO,
  HDST_CONNECTION,
  HDST_CONNECTION_COLON,
  HDST_CONNECTION_COLON_WS,
  HDST_CONNECTION_COLON_WS_C,
  HDST_CONNECTION_COLON_WS_CL,
  HDST_CONNECTION_COLON_WS_CLO,
  HDST_CONNECTION_COLON_WS_CLOS,
  HDST_CONNECTION_COLON_WS_CLOSE,
  /* SM states for Connection: keep-alive */
  HDST_CONNECTION_COLON_WS_K,
  HDST_CONNECTION_COLON_WS_KE,
  HDST_CONNECTION_COLON_WS_KEE,
  HDST_CONNECTION_COLON_WS_KEEP,
  HDST_CONNECTION_COLON_WS_KEEP_,
  HDST_CONNECTION_COLON_WS_KEEP_A,
  HDST_CONNECTION_COLON_WS_KEEP_AL,
  HDST_CONNECTION_COLON_WS_KEEP_ALI,
  HDST_CONNECTION_COLON_WS_KEEP_ALIV,
  HDST_CONNECTION_COLON_WS_KEEP_ALIVE,
  /* SM states for Transfer-Encoding: chunked */
  HDST_T,
  HDST_TR,
  HDST_TRA,
  HDST_TRAN,
  HDST_TRANS,
  HDST_TRANSF,
  HDST_TRANSFE,
  HDST_TRANSFER,
  HDST_TRANSFER_DASH,
  HDST_TRANSFER_DASH_E,
  HDST_TRANSFER_DASH_EN,
  HDST_TRANSFER_DASH_ENC,
  HDST_TRANSFER_DASH_ENCO,
  HDST_TRANSFER_DASH_ENCOD,
  HDST_TRANSFER_DASH_ENCODI,
  HDST_TRANSFER_DASH_ENCODIN,
  HDST_TRANSFER_DASH_ENCODING,
  HDST_TRANSFER_DASH_ENCODING_COLON,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_C,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_CH,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHU,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUN,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNK,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNKE,
  HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNKED
} header_states;

typedef struct {
  int url_num;
  struct sockaddr_in sa;
  int sa_len;
  int conn_fd;
  SSL *ssl;
  connection_states conn_state;
  header_states header_state;
  int did_connect, did_response;
  struct timeval started_at;
  struct timeval connect_at;
  struct timeval request_at;
  struct timeval response_at;
  Timer *idle_timer;
  Timer *wakeup_timer;
  long content_length;
  long bytes;
  long checksum;
  int http_status;
  int reusable;
  int keep_alive;
  int chunked;
  unsigned int unique_id;
  struct {
    int connections;
    int requests;
    int responses;
    int requests_per_connection;
  } stats;
} connection;

static connection *connections;
static int max_connections, num_connections, max_parallel, num_ka_conns;

static int http_status_counts[1000]; /* room for all three-digit statuses */
static char *argv0;
static int do_checksum, do_throttle, do_verbose, do_jitter, do_proxy;
static int do_accept_gzip, do_sequential;
static float throttle;
static int idle_secs;
static char *proxy_hostname;
static unsigned short proxy_port;
static char *user_agent;
static char *cookie;
static char *http_version;
static int is_http_1_1;
static int ignore_bytes;
static int keep_alive;
static char *extra_headers;
static unsigned int unique_id_counter;
static int unique_id = 0;
static int socket_pool;
static int epfd;
static int max_connect_failures = 0;

static struct timeval start_at;
static int fetches_started, connects_completed, responses_completed, fetches_completed;
static long long total_bytes;
static long long total_connect_usecs, max_connect_usecs, min_connect_usecs;
static long long total_response_usecs, max_response_usecs, min_response_usecs;
int total_timeouts, total_badbytes, total_badchecksums;

static long start_interval, low_interval, high_interval, range_interval;

static SSL_CTX *ssl_ctx = (SSL_CTX *)0;
static char *cipher     = (char *)0;

/* Forwards. */
static void usage(void);
static void read_url_file(char *url_file);
static void lookup_address(int url_num);
static void read_sip_file(char *sip_file);
static void start_connection(struct timeval *nowP);
static void start_socket(int url_num, int cnum, struct timeval *nowP);
static void handle_connect(int cnum, struct timeval *nowP, int double_check);
static void handle_read(int cnum, struct timeval *nowP);
static void idle_connection(ClientData client_data, struct timeval *nowP);
static void wakeup_connection(ClientData client_data, struct timeval *nowP);
static void close_connection(int cnum);
static void progress_report(ClientData client_data, struct timeval *nowP);
static void start_timer(ClientData client_data, struct timeval *nowP);
static void end_timer(ClientData client_data, struct timeval *nowP);
static void finish(struct timeval *nowP);
static long long delta_timeval(struct timeval *start, struct timeval *finish);
static void *malloc_check(size_t size);
static void *realloc_check(void *ptr, size_t size);
static char *strdup_check(char *str);
static void check(void *ptr);

int
main(int argc, char **argv)
{
  int argn;
  int start;
#define START_NONE 0
#define START_PARALLEL 1
#define START_RATE 2
  int start_parallel = -1, start_rate = -1;
  int end;
#define END_NONE 0
#define END_FETCHES 1
#define END_SECONDS 2
  int end_fetches = -1, end_seconds = -1;
  int cnum;
  char *url_file;
  char *sip_file;
#ifdef RLIMIT_NOFILE
  struct rlimit limits;
#endif /* RLIMIT_NOFILE */
  struct epoll_event *events;
  struct timeval now;
  int i, r, periodic_tmr;

  max_connections = 64 - RESERVED_FDS; /* a guess */
#ifdef RLIMIT_NOFILE
  /* Try and increase the limit on # of files to the maximum. */
  if (getrlimit(RLIMIT_NOFILE, &limits) == 0) {
    if (limits.rlim_cur != limits.rlim_max) {
      if (limits.rlim_max == RLIM_INFINITY)
        limits.rlim_cur = 8192; /* arbitrary */
      else if (limits.rlim_max > limits.rlim_cur)
        limits.rlim_cur = limits.rlim_max;
      (void)setrlimit(RLIMIT_NOFILE, &limits);
    }
    max_connections = limits.rlim_cur - RESERVED_FDS;
  }
#endif /* RLIMIT_NOFILE */

  /* Parse args. */
  argv0       = argv[0];
  argn        = 1;
  do_checksum = do_throttle = do_verbose = do_jitter = do_proxy = 0;
  do_accept_gzip = do_sequential = 0;
  throttle                       = THROTTLE;
  sip_file                       = (char *)0;
  user_agent                     = VERSION;
  cookie                         = NULL;
  http_version                   = "1.1";
  is_http_1_1                    = 1;
  idle_secs                      = IDLE_SECS;
  start                          = START_NONE;
  end                            = END_NONE;
  keep_alive                     = 0;
  socket_pool                    = 0;
  extra_headers                  = NULL;
  while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
    if (strncmp(argv[argn], "-checksum", strlen(argv[argn])) == 0)
      do_checksum = 1;
    else if (strncmp(argv[argn], "-sequential", strlen(argv[argn])) == 0)
      do_sequential = 1;
    else if (strncmp(argv[argn], "-throttle", strlen(argv[argn])) == 0)
      do_throttle = 1;
    else if (strncmp(argv[argn], "-Throttle", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      do_throttle = 1;
      throttle    = atoi(argv[++argn]) / 10.0;
    } else if (strncmp(argv[argn], "-verbose", strlen(argv[argn])) == 0)
      do_verbose = 1;
    else if (strncmp(argv[argn], "-timeout", strlen(argv[argn])) == 0 && argn + 1 < argc)
      idle_secs = atoi(argv[++argn]);
    else if (strncmp(argv[argn], "-jitter", strlen(argv[argn])) == 0)
      do_jitter = 1;
    else if (strncmp(argv[argn], "-accept_gzip", strlen(argv[argn])) == 0)
      do_accept_gzip = 1;
    else if (strncmp(argv[argn], "-parallel", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      start          = START_PARALLEL;
      start_parallel = atoi(argv[++argn]);
      if (start_parallel < 1) {
        (void)fprintf(stderr, "%s: parallel must be at least 1\n", argv0);
        exit(1);
      }
      if (start_parallel > max_connections) {
        (void)fprintf(stderr, "%s: parallel may be at most %d\n", argv0, max_connections);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-rate", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      start      = START_RATE;
      start_rate = atoi(argv[++argn]);
      if (start_rate < 1) {
        (void)fprintf(stderr, "%s: rate must be at least 1\n", argv0);
        exit(1);
      }
      if (start_rate > 1000) {
        (void)fprintf(stderr, "%s: rate may be at most 1000\n", argv0);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-sockets", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      socket_pool = atoi(argv[++argn]) - 1;
      if (socket_pool < 0) {
        (void)fprintf(stderr, "%s: sockets must be at least 1\n", argv0);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-fetches", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      end         = END_FETCHES;
      end_fetches = atoi(argv[++argn]);
      if (end_fetches < 1) {
        (void)fprintf(stderr, "%s: fetches must be at least 1\n", argv0);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-seconds", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      end         = END_SECONDS;
      end_seconds = atoi(argv[++argn]);
      if (end_seconds < 1) {
        (void)fprintf(stderr, "%s: seconds must be at least 1\n", argv0);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-keep_alive", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      keep_alive = atoi(argv[++argn]);
      if (keep_alive < 1) {
        (void)fprintf(stderr, "%s: keep_alive must be at least 1\n", argv0);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-unique_id", strlen(argv[argn])) == 0) {
      unique_id = 1;
    } else if (strncmp(argv[argn], "-sip", strlen(argv[argn])) == 0 && argn + 1 < argc)
      sip_file = argv[++argn];
    else if (strncmp(argv[argn], "-agent", strlen(argv[argn])) == 0 && argn + 1 < argc)
      user_agent = argv[++argn];
    else if (strncmp(argv[argn], "-cookie", strlen(argv[argn])) == 0 && argn + 1 < argc)
      cookie = argv[++argn];
    else if (strncmp(argv[argn], "-ignore_bytes", strlen(argv[argn])) == 0)
      ignore_bytes = 1;
    else if (strncmp(argv[argn], "-max_connect_failures", strlen(argv[argn])) == 0) {
      max_connect_failures = atoi(argv[++argn]);
      if (max_connect_failures < 1) {
        (void)fprintf(stderr, "%s: max_connection failures should be 1 or higher\n", argv0);
        exit(1);
      }
    } else if (strncmp(argv[argn], "-header", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      if (extra_headers) {
        strcat(extra_headers, "\r\n");
        strcat(extra_headers, argv[++argn]);
      } else {
        extra_headers = malloc_check(65536);
        strncpy(extra_headers, argv[++argn], 65536 - 1);
        extra_headers[65536] = '\0';
      }
    } else if (strncmp(argv[argn], "-http_version", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      http_version = argv[++argn];
      is_http_1_1  = (strcmp(http_version, "1.1") == 0);
    } else if (strncmp(argv[argn], "-cipher", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      cipher = argv[++argn];
      if (strcasecmp(cipher, "fastsec") == 0)
        cipher = "RC4-MD5";
      else if (strcasecmp(cipher, "highsec") == 0)
        cipher = "DES-CBC3-SHA";
      else if (strcasecmp(cipher, "paranoid") == 0)
        cipher = "AES256-SHA";
    } else if (strncmp(argv[argn], "-proxy", strlen(argv[argn])) == 0 && argn + 1 < argc) {
      char *colon;
      do_proxy       = 1;
      proxy_hostname = argv[++argn];
      colon          = strchr(proxy_hostname, ':');
      if (colon == (char *)0)
        proxy_port = 80;
      else {
        proxy_port = (unsigned short)atoi(colon + 1);
        *colon     = '\0';
      }
    } else
      usage();
    ++argn;
  }
  if (argn + 1 != argc)
    usage();
  if (start == START_NONE || end == END_NONE)
    usage();
  if (do_jitter && start != START_RATE)
    usage();
  url_file = argv[argn];

  /* Read in and parse the URLs. */
  read_url_file(url_file);

  /* Read in the source IP file, if specified. */
  if (sip_file != (char *)0)
    read_sip_file(sip_file);

  /* Initialize the connections table. */
  if (start == START_PARALLEL)
    max_connections = start_parallel;
  connections = (connection *)malloc_check(max_connections * sizeof(connection));
  for (cnum = 0; cnum < max_connections; ++cnum) {
    connections[cnum].conn_state        = CNST_FREE;
    connections[cnum].reusable          = 0;
    connections[cnum].stats.requests    = 0;
    connections[cnum].stats.responses   = 0;
    connections[cnum].stats.connections = 0;
  }
  num_connections = max_parallel = num_ka_conns = 0;

  /* Initialize the HTTP status-code histogram. */
  for (i = 0; i < 1000; ++i)
    http_status_counts[i] = 0;

  /* Initialize the statistics. */
  fetches_started      = 0;
  connects_completed   = 0;
  responses_completed  = 0;
  fetches_completed    = 0;
  total_bytes          = 0;
  total_connect_usecs  = 0;
  max_connect_usecs    = 0;
  min_connect_usecs    = 1000000000L;
  total_response_usecs = 0;
  max_response_usecs   = 0;
  min_response_usecs   = 1000000000L;
  total_timeouts       = 0;
  total_badbytes       = 0;
  total_badchecksums   = 0;

  /* Initialize epoll() and kqueue() etc. */
  epfd = epoll_create(max_connections);
  if (epfd == -1) {
    perror("epoll_create");
    exit(1);
  }
  events = malloc(sizeof(struct epoll_event) * max_connections);

/* Initialize the random number generator. */
#ifdef HAVE_SRANDOMDEV
  srandomdev();
#else
  srandom((int)time((time_t *)0) ^ getpid());
#endif

  /* Initialize the rest. */
  tmr_init();
  (void)gettimeofday(&now, (struct timezone *)0);
  start_at = now;
  if (do_verbose)
    (void)tmr_create(&now, progress_report, JunkClientData, PROGRESS_SECS * 1000L, 1);
  if (start == START_RATE) {
    start_interval = 1000L / start_rate;
    if (do_jitter) {
      low_interval   = start_interval * 9 / 10;
      high_interval  = start_interval * 11 / 10;
      range_interval = high_interval - low_interval + 1;
    }
    (void)tmr_create(&now, start_timer, JunkClientData, start_interval, !do_jitter);
  }
  if (end == END_SECONDS)
    (void)tmr_create(&now, end_timer, JunkClientData, end_seconds * 1000L, 0);
  (void)signal(SIGPIPE, SIG_IGN);

  /* Main loop. */
  for (;;) {
    if (end == END_FETCHES && fetches_completed >= end_fetches)
      finish(&now);

    if (start == START_PARALLEL) {
      /* See if we need to start any new connections; but at most 10. */
      for (i = 0; i < 10 && num_connections < start_parallel && (end != END_FETCHES || fetches_started < end_fetches); ++i) {
        start_connection(&now);
        (void)gettimeofday(&now, (struct timezone *)0);
        tmr_run(&now);
      }
    }

    r = epoll_wait(epfd, events, max_connections, tmr_mstimeout(&now));
#ifdef DEBUG
    fprintf(stderr, "epoll_wait() got %d events\n", r);
#endif
    if (r < 0) {
      perror("epoll_wait");
      exit(1);
    }
    (void)gettimeofday(&now, (struct timezone *)0);

    /* Service them. */
    periodic_tmr = 50;
    while (r-- > 0) {
      if (--periodic_tmr == 0) {
        periodic_tmr = 50;
        tmr_run(&now);
      }
      cnum = events[r].data.u32;
#ifdef DEBUG
      fprintf(stderr, "processing event %d (%d), for CNUM %d\n", r + 1, events[r].events, cnum);
#endif
      switch (connections[cnum].conn_state) {
      case CNST_CONNECTING:
        handle_connect(cnum, &now, 1);
        break;
      case CNST_HEADERS:
      case CNST_READING:
        handle_read(cnum, &now);
        break;
      default:
        /* Nothing */
        break;
      }
    }
    /* And run the timers. */
    tmr_run(&now);
  }

  /* NOT_REACHED */
}

static void
usage(void)
{
  (void)fprintf(stderr,
                "usage:	%s [-checksum] [-throttle] [-sequential] [-proxy host:port]\n"
                "		[-verbose] [-timeout secs] [-sip sip_file] [-agent user_agent]\n"
                "		[-cookie http_cookie] [-accept_gzip] [-http_version version_str]\n"
                "		[-keep_alive num_reqs_per_conn] [-unique_id]\n"
                "		[-max_connect_failures N] [-ignore_bytes] [ [-header str] ... ]\n",
                argv0);
  (void)fprintf(stderr, "	[-cipher str]\n");
  (void)fprintf(stderr, "	-parallel N | -rate N [-jitter]\n");
  (void)fprintf(stderr, "	-fetches N | -seconds N\n");
  (void)fprintf(stderr, "	url_file\n");
  (void)fprintf(stderr, "One start specifier, either -parallel or -rate, is required.\n");
  (void)fprintf(stderr, "One end specifier, either -fetches or -seconds, is required.\n");
  exit(1);
}

static void
read_url_file(char *url_file)
{
  char line[5000], hostname[5000];
  char *http    = "http://";
  int http_len  = strlen(http);
  char *https   = "https://";
  int https_len = strlen(https);
  int proto_len, host_len;
  char *cp;

  FILE *fp = fopen(url_file, "r");
  if (fp == NULL) {
    perror(url_file);
    exit(1);
  }

  max_urls = 100;
  urls     = (url *)malloc_check(max_urls * sizeof(url));
  num_urls = 0;
  cur_url  = 0;

  /* The Host: header can either be user provided (via -header), or
     constructed by the URL host and possibly port (if not port 80) */

  char hdr_buf[2048];
  int hdr_bytes = 0;
  hdr_bytes += snprintf(&hdr_buf[hdr_bytes], sizeof(hdr_buf) - hdr_bytes, "User-Agent: %s\r\n", user_agent);
  if (cookie)
    hdr_bytes += snprintf(&hdr_buf[hdr_bytes], sizeof(hdr_buf) - hdr_bytes, "Cookie: %s\r\n", cookie);
  if (do_accept_gzip)
    hdr_bytes += snprintf(&hdr_buf[hdr_bytes], sizeof(hdr_buf) - hdr_bytes, "Accept-Encoding: gzip\r\n");
  /* Add Connection: keep-alive header if keep_alive requested, and version != "1.1" */
  if ((keep_alive > 0) && !is_http_1_1)
    hdr_bytes += snprintf(&hdr_buf[hdr_bytes], sizeof(hdr_buf) - hdr_bytes, "Connection: keep-alive\r\n");
  if (extra_headers != NULL) {
    hdr_bytes += snprintf(&hdr_buf[hdr_bytes], sizeof(hdr_buf) - hdr_bytes, "%s\r\n", extra_headers);
  }
  snprintf(&hdr_buf[hdr_bytes], sizeof(hdr_buf) - hdr_bytes, "\r\n");

  while (fgets(line, sizeof(line), fp) != (char *)0) {
    char req_buf[2048];
    int req_bytes = 0;

    /* Nuke trailing newline. */
    if (line[strlen(line) - 1] == '\n')
      line[strlen(line) - 1] = '\0';

    /* Check for room in urls. */
    if (num_urls >= max_urls) {
      max_urls *= 2;
      urls = (url *)realloc_check((void *)urls, max_urls * sizeof(url));
    }

    /* Add to table. */
    urls[num_urls].url_str = strdup_check(line);

    /* Parse it. */
    if (strncmp(http, line, http_len) == 0) {
      proto_len               = http_len;
      urls[num_urls].protocol = PROTO_HTTP;
    } else if (strncmp(https, line, https_len) == 0) {
      proto_len               = https_len;
      urls[num_urls].protocol = PROTO_HTTPS;
    } else {
      fprintf(stderr, "%s: unknown protocol - %s\n", argv0, line);
      exit(1);
    }
    for (cp = line + proto_len; *cp != '\0' && *cp != ':' && *cp != '/'; ++cp)
      ;
    host_len = cp - line;
    host_len -= proto_len;
    strncpy(hostname, line + proto_len, host_len);
    hostname[host_len]      = '\0';
    urls[num_urls].hostname = strdup_check(hostname);
    if (*cp == ':') {
      urls[num_urls].port = (unsigned short)atoi(++cp);
      while (*cp != '\0' && *cp != '/')
        ++cp;
    } else if (urls[num_urls].protocol == PROTO_HTTPS)
      urls[num_urls].port = 443;
    else
      urls[num_urls].port = 80;
    if (*cp == '\0')
      urls[num_urls].filename = strdup_check("/");
    else
      urls[num_urls].filename = strdup_check(cp);

    lookup_address(num_urls);

    urls[num_urls].got_bytes        = 0;
    urls[num_urls].got_checksum     = 0;
    urls[num_urls].unique_id_offset = 0;

    /* Pre-generate the request string, major performance improvement. */
    if (do_proxy) {
      req_bytes = snprintf(req_buf, sizeof(req_buf), "GET %s://%.500s:%d%.500s HTTP/%s\r\n",
                           urls[num_urls].protocol == PROTO_HTTPS ? "https" : "http", urls[num_urls].hostname,
                           (int)urls[num_urls].port, urls[num_urls].filename, http_version);
    } else
      req_bytes = snprintf(req_buf, sizeof(req_buf), "GET %.500s HTTP/%s\r\n", urls[num_urls].filename, http_version);

    if (extra_headers == NULL || !strstr(extra_headers, "Host:")) {
      if (urls[num_urls].port != 80)
        req_bytes += snprintf(&req_buf[req_bytes], sizeof(req_buf) - req_bytes, "Host: %s:%d\r\n", urls[num_urls].hostname,
                              urls[num_urls].port);
      else
        req_bytes += snprintf(&req_buf[req_bytes], sizeof(req_buf) - req_bytes, "Host: %s\r\n", urls[num_urls].hostname);
    }
    if (unique_id == 1) {
      req_bytes += snprintf(&req_buf[req_bytes], sizeof(req_buf) - req_bytes, "X-ID: ");
      urls[num_urls].unique_id_offset = req_bytes;
      req_bytes += snprintf(&req_buf[req_bytes], sizeof(req_buf) - req_bytes, "%09u\r\n", 0);
    }

    // add the common hdr here
    req_bytes += snprintf(&req_buf[req_bytes], sizeof(req_buf) - req_bytes, hdr_buf, 0);

    urls[num_urls].buf_bytes = req_bytes;
    urls[num_urls].buf       = strdup_check(req_buf);

    ++num_urls;
  }
  fclose(fp);
}

static void
lookup_address(int url_num)
{
  if (do_proxy && url_num > 0) {
    urls[url_num].sock_family   = urls[url_num - 1].sock_family;
    urls[url_num].sock_type     = urls[url_num - 1].sock_type;
    urls[url_num].sock_protocol = urls[url_num - 1].sock_protocol;
    urls[url_num].sa_len        = urls[url_num - 1].sa_len;
    urls[url_num].sa            = urls[url_num - 1].sa;
    return;
  }
  int i;
  char *hostname;
  unsigned short port;
#ifdef USE_IPV6
  struct addrinfo hints;
  char portstr[10];
  int gaierr;
  struct addrinfo *ai;
  struct addrinfo *ai2;
  struct addrinfo *aiv4;
  struct addrinfo *aiv6;
#else  /* USE_IPV6 */
  struct hostent *he;
#endif /* USE_IPV6 */

  urls[url_num].sa_len = sizeof(urls[url_num].sa);
  (void)memset((void *)&urls[url_num].sa, 0, urls[url_num].sa_len);

  if (do_proxy)
    hostname = proxy_hostname;
  else
    hostname = urls[url_num].hostname;
  if (do_proxy)
    port = proxy_port;
  else
    port = urls[url_num].port;

  /* Try to do this using existing information  */
  for (i = 0; i < url_num; i++) {
    if ((strcmp(hostname, urls[i].hostname) == 0) && (port == urls[i].port)) {
      urls[url_num].sock_family   = urls[i].sock_family;
      urls[url_num].sock_type     = urls[i].sock_type;
      urls[url_num].sock_protocol = urls[i].sock_protocol;
      urls[url_num].sa            = urls[i].sa;
      urls[url_num].sa_len        = urls[i].sa_len;
      return;
    }
  }

#ifdef USE_IPV6

  (void)memset(&hints, 0, sizeof(hints));
  hints.ai_family   = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  (void)snprintf(portstr, sizeof(portstr), "%d", (int)port);
  if ((gaierr = getaddrinfo(hostname, portstr, &hints, &ai)) != 0) {
    (void)fprintf(stderr, "%s: getaddrinfo %s - %s\n", argv0, hostname, gai_strerror(gaierr));
    exit(1);
  }

  /* Find the first IPv4 and IPv6 entries. */
  aiv4 = (struct addrinfo *)0;
  aiv6 = (struct addrinfo *)0;
  for (ai2 = ai; ai2 != (struct addrinfo *)0; ai2 = ai2->ai_next) {
    switch (ai2->ai_family) {
    case AF_INET:
      if (aiv4 == (struct addrinfo *)0)
        aiv4 = ai2;
      break;
    case AF_INET6:
      if (aiv6 == (struct addrinfo *)0)
        aiv6 = ai2;
      break;
    }
  }

  /* If there's an IPv4 address, use that, otherwise try IPv6. */
  if (aiv4 != (struct addrinfo *)0) {
    if (sizeof(urls[url_num].sa) < aiv4->ai_addrlen) {
      (void)fprintf(stderr, "%s - sockaddr too small (%lu < %lu)\n", hostname, (unsigned long)sizeof(urls[url_num].sa),
                    (unsigned long)aiv4->ai_addrlen);
      exit(1);
    }
    urls[url_num].sock_family   = aiv4->ai_family;
    urls[url_num].sock_type     = aiv4->ai_socktype;
    urls[url_num].sock_protocol = aiv4->ai_protocol;
    urls[url_num].sa_len        = aiv4->ai_addrlen;
    (void)memmove(&urls[url_num].sa, aiv4->ai_addr, aiv4->ai_addrlen);
    freeaddrinfo(ai);
    return;
  }
  if (aiv6 != (struct addrinfo *)0) {
    if (sizeof(urls[url_num].sa) < aiv6->ai_addrlen) {
      (void)fprintf(stderr, "%s - sockaddr too small (%lu < %lu)\n", hostname, (unsigned long)sizeof(urls[url_num].sa),
                    (unsigned long)aiv6->ai_addrlen);
      exit(1);
    }
    urls[url_num].sock_family   = aiv6->ai_family;
    urls[url_num].sock_type     = aiv6->ai_socktype;
    urls[url_num].sock_protocol = aiv6->ai_protocol;
    urls[url_num].sa_len        = aiv6->ai_addrlen;
    (void)memmove(&urls[url_num].sa, aiv6->ai_addr, aiv6->ai_addrlen);
    freeaddrinfo(ai);
    return;
  }

  (void)fprintf(stderr, "%s: no valid address found for host %s\n", argv0, hostname);
  exit(1);

#else /* USE_IPV6 */

  /* No match in previous lookups */
  he = gethostbyname(hostname);
  if (he == (struct hostent *)0) {
    (void)fprintf(stderr, "%s: unknown host - %s\n", argv0, hostname);
    exit(1);
  }
  urls[url_num].sock_family = urls[url_num].sa.sin_family = he->h_addrtype;
  urls[url_num].sock_type                                 = SOCK_STREAM;
  urls[url_num].sock_protocol                             = 0;
  urls[url_num].sa_len                                    = sizeof(urls[url_num].sa);
  (void)memmove(&urls[url_num].sa.sin_addr, he->h_addr, he->h_length);
  urls[url_num].sa.sin_port = htons(port);

#endif /* USE_IPV6 */
}

static void
read_sip_file(char *sip_file)
{
  FILE *fp;
  char line[5000];

  fp = fopen(sip_file, "r");
  if (fp == (FILE *)0) {
    perror(sip_file);
    exit(1);
  }

  max_sips = 100;
  sips     = (sip *)malloc_check(max_sips * sizeof(sip));
  num_sips = 0;
  while (fgets(line, sizeof(line), fp) != (char *)0) {
    /* Nuke trailing newline. */
    if (line[strlen(line) - 1] == '\n')
      line[strlen(line) - 1] = '\0';

    /* Check for room in sips. */
    if (num_sips >= max_sips) {
      max_sips *= 2;
      sips = (sip *)realloc_check((void *)sips, max_sips * sizeof(sip));
    }

    /* Add to table. */
    sips[num_sips].str = strdup_check(line);
    (void)memset((void *)&sips[num_sips].sa, 0, sizeof(sips[num_sips].sa));
    if (!inet_aton(sips[num_sips].str, &sips[num_sips].sa.sin_addr)) {
      (void)fprintf(stderr, "%s: cannot convert source IP address %s\n", argv0, sips[num_sips].str);
      exit(1);
    }
    ++num_sips;
  }
  fclose(fp);
}

static void
start_connection(struct timeval *nowP)
{
  int cnum, url_num;
  static int cycle_slot = 0;

  /* Find an empty connection slot. */
  if (socket_pool > 0) {
    int prev_cycle_slot = cycle_slot;

    while (1) {
      ++cycle_slot;
      if (cycle_slot > socket_pool)
        cycle_slot = 0;
      if (prev_cycle_slot == cycle_slot) {
        return;
#if 0
        /* Unused right now, not sure why */
        printf("Warning: cycling through all socket slots\n");
        tmr_run(nowP);
#endif
      }
      if (connections[cycle_slot].conn_state == CNST_FREE) {
        /* Choose a URL. */
        if (do_sequential) {
          url_num = cur_url++;
          if (cur_url >= num_urls)
            cur_url = 0;
        } else {
          url_num = ((unsigned long)random()) % ((unsigned int)num_urls);
        }

        /* Start the socket. */
        start_socket(url_num, cycle_slot, nowP);
        if (connections[cycle_slot].conn_state != CNST_FREE) {
          ++num_connections;
          /*
             if ( num_connections > max_parallel )
             max_parallel = num_connections;
           */
        }
        ++fetches_started;
        return;
      }
    }
  } else {
    for (cnum = 0; cnum < max_connections; ++cnum)
      if (connections[cnum].conn_state == CNST_FREE) {
        /* Choose a URL. */
        if (do_sequential) {
          url_num = cur_url++;
          if (cur_url >= num_urls)
            cur_url = 0;
        } else {
          url_num = ((unsigned long)random()) % ((unsigned int)num_urls);
        }
        /* Start the socket. */
        start_socket(url_num, cnum, nowP);
        if (connections[cnum].conn_state != CNST_FREE) {
          ++num_connections;
          /*
             if ( num_connections > max_parallel )
             max_parallel = num_connections;
           */
        }
        ++fetches_started;
        return;
      }
  }
  /* No slots left. */
  (void)fprintf(stderr, "%s: ran out of connection slots\n", argv0);
  finish(nowP);
}

static void
start_socket(int url_num, int cnum, struct timeval *nowP)
{
  ClientData client_data;
  int flags;
  int sip_num;
  int reusable = connections[cnum].reusable;

  /* Start filling in the connection slot. */
  connections[cnum].url_num        = url_num;
  connections[cnum].started_at     = *nowP;
  client_data.i                    = cnum;
  connections[cnum].did_connect    = 0;
  connections[cnum].did_response   = 0;
  connections[cnum].idle_timer     = tmr_create(nowP, idle_connection, client_data, idle_secs * 1000L, 0);
  connections[cnum].wakeup_timer   = (Timer *)0;
  connections[cnum].content_length = -1;
  connections[cnum].bytes          = 0;
  connections[cnum].checksum       = 0;
  connections[cnum].http_status    = -1;
  connections[cnum].reusable       = 0;
  connections[cnum].chunked        = 0;
  connections[cnum].unique_id      = 0;

  // set unique id
  if (unique_id == 1 && urls[url_num].unique_id_offset > 0) {
    char buffer[10];
    snprintf(buffer, 10, "%09u", ++unique_id_counter);
    //      fprintf(stderr, "%s %s\n", buffer, &urls[url_num].buf[unique_id_offset]);
    memcpy((void *)&urls[url_num].buf[urls[url_num].unique_id_offset], (void *)buffer, 9);
    connections[cnum].unique_id = unique_id_counter;
  }

  /* Make a socket. */
  if (!reusable) {
    struct epoll_event ev;

    connections[cnum].keep_alive = keep_alive;
    connections[cnum].conn_fd    = socket(urls[url_num].sock_family, urls[url_num].sock_type, urls[url_num].sock_protocol);
    if (connections[cnum].conn_fd < 0) {
      perror(urls[url_num].url_str);
      return;
    }
    connections[cnum].stats.connections++;

    /* Set the file descriptor to no-delay mode. */
    flags = fcntl(connections[cnum].conn_fd, F_GETFL, 0);
    if (flags == -1) {
      perror(urls[url_num].url_str);
      (void)close(connections[cnum].conn_fd);
      return;
    }
    if (fcntl(connections[cnum].conn_fd, F_SETFL, flags | O_NDELAY) < 0) {
      perror(urls[url_num].url_str);
      (void)close(connections[cnum].conn_fd);
      return;
    }

    if (num_sips > 0) {
      /* Try a random source IP address. */
      sip_num = ((unsigned long)random()) % ((unsigned int)num_sips);
      if (bind(connections[cnum].conn_fd, (struct sockaddr *)&sips[sip_num].sa, sizeof(sips[sip_num].sa)) < 0) {
        perror("binding local address");
        (void)close(connections[cnum].conn_fd);
        return;
      }
    }
    ev.events   = EPOLLOUT;
    ev.data.u32 = cnum;
#ifdef DEBUG
    fprintf(stderr, "Adding FD %d for CNUM %d\n", connections[cnum].conn_fd, cnum);
#endif
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, connections[cnum].conn_fd, &ev)) {
      perror("epoll add fd");
      (void)close(connections[cnum].conn_fd);
      return;
    }
    /* Connect to the host. */
    connections[cnum].sa_len = urls[url_num].sa_len;
    (void)memmove((void *)&connections[cnum].sa, (void *)&urls[url_num].sa, urls[url_num].sa_len);
    connections[cnum].connect_at = *nowP;
    if (connect(connections[cnum].conn_fd, (struct sockaddr *)&connections[cnum].sa, connections[cnum].sa_len) < 0) {
      if (errno == EINPROGRESS) {
        connections[cnum].conn_state = CNST_CONNECTING;
        return;
      } else {
        /* remove the FD from the epoll descriptor */
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, connections[cnum].conn_fd, &ev) < 0)
          perror("epoll delete fd");
        perror(urls[url_num].url_str);
        (void)close(connections[cnum].conn_fd);
        return;
      }
    }

    /* Connect succeeded instantly, so handle it now. */
    (void)gettimeofday(nowP, (struct timezone *)0);
    handle_connect(cnum, nowP, 0);
  } else {
    /* Send the request on a reused connection */
    int r;

    connections[cnum].stats.requests++;
    connections[cnum].stats.requests_per_connection++;
    connections[cnum].request_at = *nowP;

    if (urls[url_num].protocol == PROTO_HTTPS)
      r = SSL_write(connections[cnum].ssl, urls[url_num].buf, urls[url_num].buf_bytes);
    else
      r = write(connections[cnum].conn_fd, urls[url_num].buf, urls[url_num].buf_bytes);
    if (r < 0) {
      perror(urls[url_num].url_str);
      connections[cnum].reusable = 0;
      close_connection(cnum);
      return;
    }
    connections[cnum].conn_state   = CNST_HEADERS;
    connections[cnum].header_state = HDST_LINE1_PROTOCOL;
  }
}

static int
cert_verify_callback(int ok __attribute__((unused)), X509_STORE_CTX *ctx __attribute__((unused)))
{
  return 1;
}

static void
handle_connect(int cnum, struct timeval *nowP, int double_check)
{
  static int connect_failures = 0;
  int url_num;
  int r;
  struct epoll_event ev;

#ifdef DEBUG
  fprintf(stderr, "Entering handle_connect() for CNUM %d\n", cnum);
#endif

  url_num                                         = connections[cnum].url_num;
  connections[cnum].stats.requests_per_connection = 0;
  if (double_check) {
    /* Check to make sure the non-blocking connect succeeded. */
    int err, errlen;

    if (connect(connections[cnum].conn_fd, (struct sockaddr *)&connections[cnum].sa, connections[cnum].sa_len) < 0) {
      if (max_connect_failures && (++connect_failures > max_connect_failures))
        exit(0);
      switch (errno) {
      case EISCONN:
        /* Ok! */
        break;
      case EINVAL:
        errlen = sizeof(err);
        if (getsockopt(connections[cnum].conn_fd, SOL_SOCKET, SO_ERROR, (void *)&err, (socklen_t *)&errlen) < 0)
          (void)fprintf(stderr, "%s: unknown connect error\n", urls[url_num].url_str);
        else
          (void)fprintf(stderr, "%s: %s\n", urls[url_num].url_str, strerror(err));
        close_connection(cnum);
        return;
      default:
        perror(urls[url_num].url_str);
        close_connection(cnum);
        return;
      }
    }
  }

  if (urls[url_num].protocol == PROTO_HTTPS) {
    int flags;

    /* Make SSL connection. */
    if (ssl_ctx == (SSL_CTX *)0) {
      SSL_load_error_strings();
      SSL_library_init();
      ssl_ctx = SSL_CTX_new(SSLv23_client_method());
      /* For some reason this does not seem to work, but indications are that it should...
         Maybe something with how we create connections? TODO: Fix it... */
      SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, cert_verify_callback);
      if (cipher != (char *)0) {
        if (!SSL_CTX_set_cipher_list(ssl_ctx, cipher)) {
          (void)fprintf(stderr, "%s: cannot set cipher list\n", argv0);
          ERR_print_errors_fp(stderr);
          close_connection(cnum);
          return;
        }
      }
    }

    if (!RAND_status()) {
      unsigned char bytes[1024];
      for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = random() % 0xff;
      RAND_seed(bytes, sizeof(bytes));
    }
    flags = fcntl(connections[cnum].conn_fd, F_GETFL, 0);
    if (flags != -1)
      (void)fcntl(connections[cnum].conn_fd, F_SETFL, flags & ~(int)O_NDELAY);
    connections[cnum].ssl = SSL_new(ssl_ctx);
    SSL_set_fd(connections[cnum].ssl, connections[cnum].conn_fd);
    r = SSL_connect(connections[cnum].ssl);
    if (r <= 0) {
      (void)fprintf(stderr, "%s: SSL connection failed - %d\n", argv0, r);
      ERR_print_errors_fp(stderr);
      close_connection(cnum);
      return;
    }
  }

  ev.events   = EPOLLIN;
  ev.data.u32 = cnum;

#ifdef DEBUG
  fprintf(stderr, "Mod FD %d to read for CNUM %d\n", connections[cnum].conn_fd, cnum);
#endif
  if (epoll_ctl(epfd, EPOLL_CTL_MOD, connections[cnum].conn_fd, &ev)) {
    perror("epoll mod fd");
    (void)close(connections[cnum].conn_fd);
    return;
  }
  /* Send the request. */
  connections[cnum].did_connect = 1;
  connections[cnum].request_at  = *nowP;
  connections[cnum].stats.requests++;
  if (urls[url_num].protocol == PROTO_HTTPS)
    r = SSL_write(connections[cnum].ssl, urls[url_num].buf, urls[url_num].buf_bytes);
  else
    r = write(connections[cnum].conn_fd, urls[url_num].buf, urls[url_num].buf_bytes);
  if (r < 0) {
    perror(urls[url_num].url_str);
    connections[cnum].reusable = 0;
    close_connection(cnum);
    return;
  }
  connections[cnum].conn_state   = CNST_HEADERS;
  connections[cnum].header_state = HDST_LINE1_PROTOCOL;
}

static void
handle_read(int cnum, struct timeval *nowP)
{
  char buf[30000]; /* must be larger than throttle / 2 */
  int bytes_to_read, bytes_read, bytes_handled;
  float elapsed;
  ClientData client_data;
  long checksum;

  tmr_reset(nowP, connections[cnum].idle_timer);

  if (do_throttle)
    bytes_to_read = throttle / 2.0;
  else
    bytes_to_read = sizeof(buf);
  if (!connections[cnum].did_response) {
    connections[cnum].did_response = 1;
    connections[cnum].response_at  = *nowP;
    if (connections[cnum].did_connect) {
      if (connections[cnum].keep_alive == keep_alive) {
        num_ka_conns++;
        if (num_ka_conns > max_parallel) {
          max_parallel = num_ka_conns;
        }
      }
    }
    if (connections[cnum].keep_alive == 0) {
      num_ka_conns--;
    }
  }
  if (urls[connections[cnum].url_num].protocol == PROTO_HTTPS)
    bytes_read = SSL_read(connections[cnum].ssl, buf, bytes_to_read - 1);
  else
    bytes_read = read(connections[cnum].conn_fd, buf, bytes_to_read - 1);
  if (bytes_read <= 0) {
    connections[cnum].reusable = 0;
    close_connection(cnum);
    return;
  }

  buf[bytes_read] = 0;
  for (bytes_handled = 0; bytes_handled < bytes_read;) {
    switch (connections[cnum].conn_state) {
    case CNST_HEADERS:
      /* State machine to read until we reach the file part.  Looks for
       ** Content-Length header too.
       */
      for (; bytes_handled < bytes_read && connections[cnum].conn_state == CNST_HEADERS; ++bytes_handled) {
        switch (connections[cnum].header_state) {
        case HDST_LINE1_PROTOCOL:
          switch (buf[bytes_handled]) {
          case ' ':
          case '\t':
            connections[cnum].header_state = HDST_LINE1_WS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          }
          break;

        case HDST_LINE1_WS:
          switch (buf[bytes_handled]) {
          case ' ':
          case '\t':
            break;
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            connections[cnum].http_status  = buf[bytes_handled] - '0';
            connections[cnum].header_state = HDST_LINE1_STATUS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_LINE1_STATUS:
          switch (buf[bytes_handled]) {
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            connections[cnum].http_status = connections[cnum].http_status * 10 + buf[bytes_handled] - '0';
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_BOL:
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_C;
            break;
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_T;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TEXT:
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            break;
          }
          break;

        case HDST_LF:
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].conn_state = CNST_READING;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_C;
            break;
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_T;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CR:
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].header_state = HDST_CRLF;
            break;
          case '\r':
            connections[cnum].conn_state = CNST_READING;
            break;
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_C;
            break;
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_T;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CRLF:
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].conn_state = CNST_READING;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CRLFCR;
            break;
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_C;
            break;
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_T;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CRLFCR:
          switch (buf[bytes_handled]) {
          case '\n':
          case '\r':
            connections[cnum].conn_state = CNST_READING;
            break;
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_C;
            break;
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_T;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_C:
          switch (buf[bytes_handled]) {
          case 'O':
          case 'o':
            connections[cnum].header_state = HDST_CO;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CO:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_CON;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CON:
          switch (buf[bytes_handled]) {
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_CONT;
            break;
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_CONN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONT:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONTE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTE:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_CONTEN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTEN:
          switch (buf[bytes_handled]) {
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_CONTENT;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT:
          switch (buf[bytes_handled]) {
          case '-':
            connections[cnum].header_state = HDST_CONTENT_;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_:
          switch (buf[bytes_handled]) {
          case 'L':
          case 'l':
            connections[cnum].header_state = HDST_CONTENT_L;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_L:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONTENT_LE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LE:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_CONTENT_LEN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LEN:
          switch (buf[bytes_handled]) {
          case 'G':
          case 'g':
            connections[cnum].header_state = HDST_CONTENT_LENG;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LENG:
          switch (buf[bytes_handled]) {
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_CONTENT_LENGT;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LENGT:
          switch (buf[bytes_handled]) {
          case 'H':
          case 'h':
            connections[cnum].header_state = HDST_CONTENT_LENGTH;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LENGTH:
          switch (buf[bytes_handled]) {
          case ':':
            connections[cnum].header_state = HDST_CONTENT_LENGTH_COLON;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LENGTH_COLON:
          switch (buf[bytes_handled]) {
          case ' ':
          case '\t':
            connections[cnum].header_state = HDST_CONTENT_LENGTH_COLON_WS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LENGTH_COLON_WS:
          switch (buf[bytes_handled]) {
          case ' ':
          case '\t':
            break;
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            connections[cnum].content_length = buf[bytes_handled] - '0';
            connections[cnum].header_state   = HDST_CONTENT_LENGTH_COLON_WS_NUM;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONTENT_LENGTH_COLON_WS_NUM:
          switch (buf[bytes_handled]) {
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            connections[cnum].content_length = connections[cnum].content_length * 10 + buf[bytes_handled] - '0';
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        /* Stuff for Connection: close */
        case HDST_CONN:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONNE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNE:
          switch (buf[bytes_handled]) {
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_CONNEC;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNEC:
          switch (buf[bytes_handled]) {
          case 'T':
          case 't':
            connections[cnum].header_state = HDST_CONNECT;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECT:
          switch (buf[bytes_handled]) {
          case 'I':
          case 'i':
            connections[cnum].header_state = HDST_CONNECTI;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTI:
          switch (buf[bytes_handled]) {
          case 'O':
          case 'o':
            connections[cnum].header_state = HDST_CONNECTIO;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTIO:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_CONNECTION;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION:
          switch (buf[bytes_handled]) {
          case ':':
            connections[cnum].header_state = HDST_CONNECTION_COLON;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON:
          switch (buf[bytes_handled]) {
          case ' ':
          case '\t':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS:
          switch (buf[bytes_handled]) {
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_C;
            break;
          case 'K':
          case 'k':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_K;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_C:
          switch (buf[bytes_handled]) {
          case 'L':
          case 'l':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_CL;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_CL:
          switch (buf[bytes_handled]) {
          case 'O':
          case 'o':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_CLO;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_CLO:
          switch (buf[bytes_handled]) {
          case 'S':
          case 's':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_CLOS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_CLOS:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_CLOSE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_CLOSE:
          /* Got the complete HTTP/1.1 "Connection: close" header, make sure this
             is the last request on this connection. */
          /* Close ToDo: Fix this */
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_K:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KE:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEE:
          switch (buf[bytes_handled]) {
          case 'P':
          case 'p':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP:
          switch (buf[bytes_handled]) {
          case '-':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP_;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP_:
          switch (buf[bytes_handled]) {
          case 'A':
          case 'a':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP_A;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP_A:
          switch (buf[bytes_handled]) {
          case 'L':
          case 'l':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP_AL;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP_AL:
          switch (buf[bytes_handled]) {
          case 'I':
          case 'i':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP_ALI;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP_ALI:
          switch (buf[bytes_handled]) {
          case 'V':
          case 'v':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP_ALIV;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP_ALIV:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_CONNECTION_COLON_WS_KEEP_ALIVE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_CONNECTION_COLON_WS_KEEP_ALIVE:
          /* Handle Connection: keep-alive response header, make the
             connection reusable if we have some keep_alive quota left. */
          /* ToDo: Fix this */
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        /* States for Transfer-Encoding: chunked */
        case HDST_T:
          switch (buf[bytes_handled]) {
          case 'R':
          case 'r':
            connections[cnum].header_state = HDST_TR;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TR:
          switch (buf[bytes_handled]) {
          case 'A':
          case 'a':
            connections[cnum].header_state = HDST_TRA;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRA:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_TRAN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRAN:
          switch (buf[bytes_handled]) {
          case 'S':
          case 's':
            connections[cnum].header_state = HDST_TRANS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANS:
          switch (buf[bytes_handled]) {
          case 'F':
          case 'f':
            connections[cnum].header_state = HDST_TRANSF;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSF:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_TRANSFE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFE:
          switch (buf[bytes_handled]) {
          case 'R':
          case 'r':
            connections[cnum].header_state = HDST_TRANSFER;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER:
          switch (buf[bytes_handled]) {
          case '-':
            connections[cnum].header_state = HDST_TRANSFER_DASH;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_TRANSFER_DASH_E;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_E:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_TRANSFER_DASH_EN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_EN:
          switch (buf[bytes_handled]) {
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENC;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENC:
          switch (buf[bytes_handled]) {
          case 'O':
          case 'o':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCO;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCO:
          switch (buf[bytes_handled]) {
          case 'D':
          case 'd':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCOD;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCOD:
          switch (buf[bytes_handled]) {
          case 'I':
          case 'i':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODI;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODI:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODIN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODIN:
          switch (buf[bytes_handled]) {
          case 'G':
          case 'g':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING:
          switch (buf[bytes_handled]) {
          case ':':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON:
          switch (buf[bytes_handled]) {
          case ' ':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS:
          switch (buf[bytes_handled]) {
          case 'C':
          case 'c':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_C;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_C:
          switch (buf[bytes_handled]) {
          case 'H':
          case 'h':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_CH;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_CH:
          switch (buf[bytes_handled]) {
          case 'U':
          case 'u':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHU;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHU:
          switch (buf[bytes_handled]) {
          case 'N':
          case 'n':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUN;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUN:
          switch (buf[bytes_handled]) {
          case 'K':
          case 'k':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNK;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNK:
          switch (buf[bytes_handled]) {
          case 'E':
          case 'e':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNKE;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNKE:
          switch (buf[bytes_handled]) {
          case 'D':
          case 'd':
            connections[cnum].header_state = HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNKED;
            break;
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;

        case HDST_TRANSFER_DASH_ENCODING_COLON_WS_CHUNKED:
          /* ToDo: what to do here? */
          connections[cnum].chunked = 1;
          switch (buf[bytes_handled]) {
          case '\n':
            connections[cnum].header_state = HDST_LF;
            break;
          case '\r':
            connections[cnum].header_state = HDST_CR;
            break;
          default:
            connections[cnum].header_state = HDST_TEXT;
            break;
          }
          break;
        }
      }

      if (connections[cnum].conn_state == CNST_READING && connections[cnum].content_length == 0) {
#ifdef DEBUG
        fprintf(stderr, "[handle_read] content_length is 0, close connection\n");
#endif
        if (connections[cnum].keep_alive > 0)
          connections[cnum].reusable = 1;

        close_connection(cnum);

        return;
      }

      break;

    case CNST_READING:
      connections[cnum].bytes += bytes_read - bytes_handled;
      if (do_throttle) {
        /* Check if we're reading too fast. */
        elapsed = delta_timeval(&connections[cnum].started_at, nowP) / 1000000.0;
        if (elapsed > 0.01 && connections[cnum].bytes / elapsed > throttle) {
          connections[cnum].conn_state   = CNST_PAUSING;
          client_data.i                  = cnum;
          connections[cnum].wakeup_timer = tmr_create(nowP, wakeup_connection, client_data, 1000L, 0);
        }
      }
      if (do_checksum) {
        checksum = connections[cnum].checksum;
        for (; bytes_handled < bytes_read; ++bytes_handled) {
          if (checksum & 1)
            checksum = (checksum >> 1) + 0x8000;
          else
            checksum >>= 1;
          checksum += buf[bytes_handled];
          checksum &= 0xffff;
        }
        connections[cnum].checksum = checksum;
      } else
        bytes_handled = bytes_read;

      /* This is an utter hack, to try to support chunked encodings... I only
         examine the "footer", to see if it looks like a chunked "end".
         ToDo: We should properly parse the body, and find the chunked sizes. */
      if (connections[cnum].chunked && !strncmp(buf + bytes_read - 5, "0\r\n\r\n", 5))
        connections[cnum].content_length = connections[cnum].bytes;

      if (connections[cnum].content_length != -1 && connections[cnum].bytes >= connections[cnum].content_length) {
        if (connections[cnum].keep_alive > 0)
          connections[cnum].reusable = 1;
        close_connection(cnum);
        return;
      }

      break;
    default:
      /* Nothing */
      break;
    }
  }
}

static void
idle_connection(ClientData client_data, struct timeval *nowP __attribute__((unused)))
{
  int cnum;
  struct timeval tv;
  char strTime[32];

  gettimeofday(&tv, NULL);
  strftime(strTime, 32, "%T", localtime(&tv.tv_sec));

  cnum                         = client_data.i;
  connections[cnum].idle_timer = (Timer *)0;
  if (unique_id) {
    (void)fprintf(stderr, "[%s.%lld] %s: timed out (%d sec) in state %d, requests %d, unique id: %u\n", strTime,
                  (long long)tv.tv_usec, urls[connections[cnum].url_num].url_str, idle_secs, connections[cnum].conn_state,
                  connections[cnum].stats.requests_per_connection, connections[cnum].unique_id);
  } else {
    (void)fprintf(stderr, "[%s.%lld] %s: timed out (%d sec) in state %d, requests %d\n", strTime, (long long)tv.tv_usec,
                  urls[connections[cnum].url_num].url_str, idle_secs, connections[cnum].conn_state,
                  connections[cnum].stats.requests_per_connection);
  }
  connections[cnum].reusable = 0;
  close_connection(cnum);
  ++total_timeouts;
}

static void
wakeup_connection(ClientData client_data, struct timeval *nowP __attribute__((unused)))
{
  int cnum;

  cnum                           = client_data.i;
  connections[cnum].wakeup_timer = (Timer *)0;
  connections[cnum].conn_state   = CNST_READING;
}

static void
close_connection(int cnum)
{
  int url_num;

  if (!connections[cnum].reusable) {
    struct epoll_event ev;

    ev.events   = EPOLLIN | EPOLLOUT;
    ev.data.u32 = cnum;
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, connections[cnum].conn_fd, &ev) < 0)
      perror("epoll delete fd");
    if (urls[connections[cnum].url_num].protocol == PROTO_HTTPS)
      SSL_free(connections[cnum].ssl);
    (void)close(connections[cnum].conn_fd);
  } else {
    --connections[cnum].keep_alive;
  }
  connections[cnum].conn_state = CNST_FREE;
  if (connections[cnum].idle_timer != (Timer *)0)
    tmr_cancel(connections[cnum].idle_timer);
  if (connections[cnum].wakeup_timer != (Timer *)0)
    tmr_cancel(connections[cnum].wakeup_timer);
  --num_connections;
  ++fetches_completed;
  total_bytes += connections[cnum].bytes;
  if (connections[cnum].did_connect) {
    long long connect_usecs = delta_timeval(&connections[cnum].connect_at, &connections[cnum].request_at);
    /*
            if ( connect_usecs > ( max_connect_usecs << 3 ) && max_connect_usecs )
                connect_usecs = max_connect_usecs;
    */
    total_connect_usecs += connect_usecs;
    max_connect_usecs = max(max_connect_usecs, connect_usecs);
    min_connect_usecs = min(min_connect_usecs, connect_usecs);
    ++connects_completed;
  }
  if (connections[cnum].did_response) {
    long long response_usecs = delta_timeval(&connections[cnum].request_at, &connections[cnum].response_at);
    /*
            if ( response_usecs > ( max_response_usecs << 1 ) && max_response_usecs )
                response_usecs = max_response_usecs;
    */
    total_response_usecs += response_usecs;
    max_response_usecs = max(max_response_usecs, response_usecs);
    min_response_usecs = min(min_response_usecs, response_usecs);
    ++responses_completed;
  }
  if (connections[cnum].http_status >= 0 && connections[cnum].http_status <= 999) {
    ++http_status_counts[connections[cnum].http_status];
    connections[cnum].stats.responses++;
  }

  url_num = connections[cnum].url_num;

  /* Only check to update got_bytes, byte count errors and/or checksums
     if the request was successful (i.e. no HTTP error). */
  if (connections[cnum].http_status >= 0 && connections[cnum].http_status < 400) {
    if (do_checksum) {
      if (!urls[url_num].got_checksum) {
        urls[url_num].checksum     = connections[cnum].checksum;
        urls[url_num].got_checksum = 1;
      } else {
        if (connections[cnum].checksum != urls[url_num].checksum) {
          (void)fprintf(stderr, "%s: checksum wrong\n", urls[url_num].url_str);
          ++total_badchecksums;
        }
      }
    } else {
      if (!urls[url_num].got_bytes) {
        urls[url_num].bytes     = connections[cnum].bytes;
        urls[url_num].got_bytes = 1;
      } else {
        if (connections[cnum].bytes != urls[url_num].bytes) {
          if (!ignore_bytes)
            (void)fprintf(stderr, "%s: byte count wrong (expected %ld, got %ld)\n", urls[url_num].url_str, urls[url_num].bytes,
                          connections[cnum].bytes);
          ++total_badbytes;
        }
      }
    }
  }
}

static void
progress_report(ClientData client_data __attribute__((unused)), struct timeval *nowP __attribute__((unused)))
{
  float elapsed;

  elapsed = delta_timeval(&start_at, nowP) / 1000000.0;
  (void)fprintf(stderr, "--- %g secs, %d fetches started, %d completed, %d current\n", elapsed, fetches_started, fetches_completed,
                num_connections);
}

static void
start_timer(ClientData client_data __attribute__((unused)), struct timeval *nowP __attribute__((unused)))
{
  start_connection(nowP);
  if (do_jitter)
    (void)tmr_create(nowP, start_timer, JunkClientData, (long)(random() % range_interval) + low_interval, 0);
}

static void
end_timer(ClientData client_data __attribute__((unused)), struct timeval *nowP __attribute__((unused)))
{
  finish(nowP);
}

static void
finish(struct timeval *nowP)
{
  float elapsed;
  int i;

  /* Report statistics. */
  elapsed = delta_timeval(&start_at, nowP) / 1000000.0;
  (void)printf("%d fetches on %d conns, %d max parallel, %g bytes, in %g seconds\n", fetches_completed, connects_completed,
               max_parallel, (float)total_bytes, elapsed);
  if (fetches_completed > 0)
    (void)printf("%g mean bytes/fetch\n", (float)total_bytes / (float)fetches_completed);
  if (elapsed > 0.01) {
    (void)printf("%g fetches/sec, %g bytes/sec\n", (float)fetches_completed / elapsed, (float)total_bytes / elapsed);
  }
  if (connects_completed > 0)
    (void)printf("msecs/connect: %g mean, %g max, %g min\n", (float)total_connect_usecs / (float)connects_completed / 1000.0,
                 (float)max_connect_usecs / 1000.0, (float)min_connect_usecs / 1000.0);
  if (responses_completed > 0)
    (void)printf("msecs/first-response: %g mean, %g max, %g min\n",
                 (float)total_response_usecs / (float)responses_completed / 1000.0, (float)max_response_usecs / 1000.0,
                 (float)min_response_usecs / 1000.0);
  if (total_timeouts != 0)
    (void)printf("%d timeouts\n", total_timeouts);
  if (do_checksum) {
    if (total_badchecksums != 0)
      (void)printf("%d bad checksums\n", total_badchecksums);
  } else {
    if (total_badbytes != 0)
      (void)printf("%d bad byte counts\n", total_badbytes);
  }

  (void)printf("HTTP response codes:\n");
  for (i = 0; i < 1000; ++i)
    if (http_status_counts[i] > 0)
      (void)printf("  code %03d -- %d\n", i, http_status_counts[i]);
  if (do_verbose) {
    (void)printf("Socket slot stats:\n");
    for (i = 0; i < max_connections; i++)
      if (connections[i].stats.connections > 0)
        (void)printf("  slot %04d -- %d connections, %d requests, %d responses\n", i, connections[i].stats.connections,
                     connections[i].stats.requests, connections[i].stats.responses);
  }

  tmr_destroy();
  if (ssl_ctx != (SSL_CTX *)0)
    SSL_CTX_free(ssl_ctx);
  exit(0);
}

static long long
delta_timeval(struct timeval *start, struct timeval *finish)
{
  long long delta_secs  = finish->tv_sec - start->tv_sec;
  long long delta_usecs = finish->tv_usec - start->tv_usec;
  return delta_secs * (long long)1000000L + delta_usecs;
}

static void *
malloc_check(size_t size)
{
  void *ptr = malloc(size);
  check(ptr);
  return ptr;
}

static void *
realloc_check(void *ptr, size_t size)
{
  ptr = realloc(ptr, size);
  check(ptr);
  return ptr;
}

static char *
strdup_check(char *str)
{
  str = strdup(str);
  check((void *)str);
  return str;
}

static void
check(void *ptr)
{
  if (ptr == (void *)0) {
    (void)fprintf(stderr, "%s: out of memory\n", argv0);
    exit(1);
  }
}
