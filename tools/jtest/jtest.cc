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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <limits.h>
#include <sys/mman.h>
#include <cmath>
#include <openssl/md5.h>

#include <inttypes.h>

#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#include "tscore/ink_defs.h"
#include "tscore/ink_error.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_assert.h"
#include "tscore/INK_MD5.h"
#include "tscore/ParseRules.h"
#include "tscore/ink_time.h"
#include "tscore/ink_args.h"
#include "tscore/I_Version.h"
#include "tscpp/util/TextView.h"

/*
 FTP - Traffic Server Template
   220 i5 FTP server (Version wu-2.4(3) Mon Jul 8 14:39:48 PDT 1996) ready.
   USER anonymous
   331 Guest login ok, send your complete e-mail address as password.
   PASS traffic_server@inktomi.com
   230 Guest login ok, access restrictions apply.
   CWD .
   250 CWD command successful.
   TYPE I
   200 Type set to I.
   PASV
   227 Entering Passive Mode (128,174,5,14,16,238)
   RETR foo
   LIST
   150 Opening ASCII mode data connection for /bin/ls.
*/

#define MAX_URL_LEN 1024

//
// Compilation Options
//

#define SERVER_BUFSIZE 4096
#define CLIENT_BUFSIZE 2048
#define MAX_BUFSIZE (65536 + 4096)

//
// Contants
//
#define MAXFDS 65536
#define HEADER_DONE -1
#define POLL_GROUP_SIZE 800
#define MAX_RESPONSE_LENGTH 1000000
#define HEADER_SIZE 10000
#define POLL_TIMEOUT 10
#define STATE_FTP_DATA_READY 0xFAD
#define MAX_DEFERED_URLS 10000
#define DEFERED_URLS_BLOCK 2000

#define MAX_REQUEST_BODY_LENGTH MAX_RESPONSE_LENGTH

#define JTEST_DONE 0
#define JTEST_CONT 1

static AppVersionInfo appVersionInfo;

static const char *hexdigits      = "0123456789ABCDEFabcdef";
static const char *dontunescapify = "#;/?+=&:@%";
static const char *dontescapify   = "#;/?+=&:@~.-_%";

enum FTP_MODE {
  FTP_NULL,
  FTP_PORT,
  FTP_PASV,
};

typedef int (*accept_fn_t)(int);
typedef int (*poll_cb)(int);

static int read_request(int sock);
static int write_request(int sock);
static int make_client(unsigned int addr, int port);
static void make_bfc_client(unsigned int addr, int port);
static int make_url_client(const char *url, const char *base_url = 0, bool seen = false, bool unthrottled = false);
static int write_ftp_response(int sock);
static void interval_report();
static void undefer_url(bool unthrottled = false);
static void done();
static int is_done();
static int open_server(unsigned short int port, accept_fn_t accept_fn);
static int accept_ftp_data(int sock);

static char **defered_urls     = nullptr;
static int n_defered_urls      = 0;
static int server_fd           = 0;
static int server_port         = 0;
static int proxy_port          = 8080;
static unsigned int proxy_addr = 0;
static unsigned int local_addr = 0;
static char proxy_host[81]     = "localhost";
static char local_host[255 + 1];
static int verbose           = 0;
static int verbose_errors    = 1;
static int debug             = 0;
static int nclients          = 100;
static int current_clients   = 0;
static int client_speed      = 0;
static int check_content     = 0;
static int nocheck_length    = 0;
static int obey_redirects    = 1;
static int only_clients      = 0;
static int only_server       = 0;
static int drop_after_CL     = 0;
static int server_speed      = 0;
static int server_delay      = 0;
static int interval          = 1;
static int sbuffersize       = SERVER_BUFSIZE;
static int cbuffersize       = CLIENT_BUFSIZE;
static int test_time         = 0;
static int last_fd           = -1;
static char *response_buffer = nullptr;
static int errors            = 0;
static int clients = 0, running_clients = 0, new_clients = 0, total_clients = 0;
static int servers = 0, running_servers = 0, new_servers = 0, total_servers = 0;
static float running_ops = 0;
static int new_ops       = 0;
static float total_ops   = 0;
static int running_sops = 0, new_sops = 0, total_sops = 0;
static int running_latency = 0, latency = 0;
static int lat_ops = 0, b1_ops = 0, running_b1latency = 0, b1latency = 0;
static uint64_t running_cbytes = 0, new_cbytes = 0, total_cbytes = 0;
static uint64_t running_tbytes = 0, new_tbytes = 0, total_tbytes = 0;
static int average_over    = 5;
static double hitrate      = 0.4;
static int hotset          = 1000;
static int keepalive       = 4;
static int keepalive_cons  = 4;
static int follow_arg      = 0;
static int follow          = 0;
static int follow_same_arg = 0;
static int follow_same     = 0;
static char current_host[512];
static int fullpage                = 0;
static int show_before             = 0;
static int show_headers            = 0;
static int server_keepalive        = 4;
static int urls_mode               = 0;
static int pipeline                = 1;
static int hostrequest             = 0;
static int ftp                     = 0;
static double ftp_mdtm_err_rate    = 0.0;
static int ftp_mdtm_rate           = 0;
static time_t ftp_mdtm_last_update = 0;
static char ftp_mdtm_str[64];
static int embed_url            = 1;
static double ims_rate          = 0.5;
static double client_abort_rate = 0.0;
static double server_abort_rate = 0.0;
static int compd_port           = 0;
static int compd_suite          = 0;
static int ka_cache_head[500];
static int ka_cache_tail[500];
static int n_ka_cache                              = 0;
static char urls_file[256]                         = "";
static FILE *urls_fp                               = nullptr;
static char urlsdump_file[256]                     = "";
static FILE *urlsdump_fp                           = nullptr;
static int drand_seed                              = 0;
static int docsize                                 = -1;
static int url_hash_entries                        = 1000000;
static char url_hash_filename[256]                 = "";
static int bandwidth_test                          = 0;
static int bandwidth_test_to_go                    = 0;
static uint64_t total_client_request_bytes         = 0;
static uint64_t total_proxy_request_bytes          = 0;
static uint64_t total_server_response_body_bytes   = 0;
static uint64_t total_server_response_header_bytes = 0;
static uint64_t total_proxy_response_body_bytes    = 0;
static uint64_t total_proxy_response_header_bytes  = 0;
static ink_hrtime now = 0, start_time = 0;
static int extra_headers       = 0;
static int alternates          = 0;
static int abort_retry_speed   = 0;
static int abort_retry_bytes   = 0;
static int abort_retry_secs    = 5;
static int client_rate         = 0;
static double reload_rate      = 0;
static int vary_user_agent     = 0;
static int server_content_type = 0;
static int request_extension   = 0;
static int no_cache            = 0;
static double evo_rate         = 0.0;
static double zipf             = 0.0;
static int zipf_bucket_size    = 1;
static int range_mode          = 0;
static int post_support        = 0;
static int post_size           = 0;

static const ArgumentDescription argument_descriptions[] = {
  {"proxy_port", 'p', "Proxy Port", "I", &proxy_port, "JTEST_PROXY_PORT", nullptr},
  {"proxy_host", 'P', "Proxy Host", "S80", &proxy_host, "JTEST_PROXY_HOST", nullptr},
  {"server_port", 's', "Server Port (0:auto select)", "I", &server_port, "JTEST_SERVER_PORT", nullptr},
  {"server_host", 'S', "Server Host (null:localhost)", "S80", &local_host, "JTEST_SERVER_HOST", nullptr},
  {"server_speed", 'r', "Server Bytes Per Second (0:unlimit)", "I", &server_speed, "JTEST_SERVER_SPEED", nullptr},
  {"server_delay", 'w', "Server Initial Delay (msec)", "I", &server_delay, "JTEST_SERVER_INITIAL_DELAY", nullptr},
  {"clients", 'c', "Clients", "I", &nclients, "JTEST_CLIENTS", nullptr},
  {"client_speed", 'R', "Client Bytes Per Second (0:unlimit)", "I", &client_speed, "JTEST_CLIENT_SPEED", nullptr},
  {"sbuffersize", 'b', "Server Buffer Size", "I", &sbuffersize, "JTEST_SERVER_BUFSIZE", nullptr},
  {"cbuffersize", 'B', "Client Buffer Size", "I", &cbuffersize, "JTEST_CLIENT_BUFSIZE", nullptr},
  {"average_over", 'a', "Seconds to Average Over", "I", &average_over, "JTEST_AVERAGE_OVER", nullptr},
  {"hitrate", 'z', "Hit Rate", "D", &hitrate, "JTEST_HITRATE", nullptr},
  {"hotset", 'Z', "Hotset Size", "I", &hotset, "JTEST_HOTSET", nullptr},
  {"interval", 'i', "Reporting Interval (seconds)", "I", &interval, "JTEST_INTERVAL", nullptr},
  {"keepalive", 'k', "Keep-Alive Length", "I", &keepalive, "JTEST_KEEPALIVE", nullptr},
  {"keepalive_cons", 'K', "# Keep-Alive Connections (0:unlimit)", "I", &keepalive_cons, "JTEST_KEEPALIVE_CONNECTIONS", nullptr},
  {"docsize", 'L', "Document Size (-1:varied)", "I", &docsize, "JTEST_DOCSIZE", nullptr},
  {"skeepalive", 'j', "Server Keep-Alive (0:unlimit)", "I", &server_keepalive, "JTEST_SERVER_KEEPALIVE", nullptr},
  {"show_urls", 'x', "Show URLs before they are accessed", "F", &show_before, "JTEST_SHOW_URLS", nullptr},
  {"show_headers", 'X', "Show Headers", "F", &show_headers, "JTEST_SHOW_HEADERS", nullptr},
  {"ftp", 'f', "FTP Requests", "F", &ftp, "JTEST_FTP", nullptr},
  {"ftp_mdtm_err_rate", ' ', "FTP MDTM 550 Error Rate", "D", &ftp_mdtm_err_rate, "JTEST_FTP_MDTM_ERR_RATE", nullptr},
  {"ftp_mdtm_rate", ' ', "FTP MDTM Update Rate (sec, 0:never)", "I", &ftp_mdtm_rate, "JTEST_FTP_MDTM_RATE", nullptr},
  {"fullpage", 'l', "Full Page (Images)", "F", &fullpage, "JTEST_FULLPAGE", nullptr},
  {"follow", 'F', "Follow Links", "F", &follow_arg, "JTEST_FOLLOW", nullptr},
  {"same_host", 'J', "Only follow URLs on same host", "F", &follow_same_arg, "JTEST_FOLLOW_SAME", nullptr},
  {"test_time", 't', "run for N seconds (0:unlimited)", "I", &test_time, "TEST_TIME", nullptr},
  {"urls", 'u', "URLs from File", "S256", urls_file, "JTEST_URLS", nullptr},
  {"urlsdump", 'U', "URLs to File", "S256", urlsdump_file, "JTEST_URLS_DUMP", nullptr},
  {"hostrequest", 'H', "Host Request(1=yes,2=transparent)", "I", &hostrequest, "JTEST_HOST_REQUEST", nullptr},
  {"check_content", 'C', "Check returned content", "F", &check_content, "JTEST_CHECK_CONTENT", nullptr},
  {"nocheck_length", ' ', "Don't check returned length", "F", &nocheck_length, "JTEST_NOCHECK_LENGTH", nullptr},
  {"obey_redirects", 'm', "Obey Redirects", "f", &obey_redirects, "JTEST_OBEY_REDIRECTS", nullptr},
  {"embed URL", 'M', "Embed URL in synth docs", "f", &embed_url, "JTEST_EMBED_URL", nullptr},
  {"url_hash_entries", 'q', "URL Hash Table Size (-1:use file size)", "I", &url_hash_entries, "JTEST_URL_HASH_ENTRIES", nullptr},
  {"url_hash_filename", 'Q', "URL Hash Table Filename", "S256", url_hash_filename, "JTEST_URL_HASH_FILENAME", nullptr},
  {"only_clients", 'y', "Only Clients", "F", &only_clients, "JTEST_ONLY_CLIENTS", nullptr},
  {"only_server", 'Y', "Only Server", "F", &only_server, "JTEST_ONLY_SERVER", nullptr},
  {"bandwidth_test", 'A', "Bandwidth Test", "I", &bandwidth_test, "JTEST_BANDWIDTH_TEST", nullptr},
  {"drop_after_CL", 'T', "Drop after Content-Length", "F", &drop_after_CL, "JTEST_DROP", nullptr},
  {"verbose", 'v', "Verbose Flag", "F", &verbose, "JTEST_VERBOSE", nullptr},
  {"verbose_errors", 'E', "Verbose Errors Flag", "f", &verbose_errors, "JTEST_VERBOSE_ERRORS", nullptr},
  {"drand", 'D', "Random Number Seed", "I", &drand_seed, "JTEST_DRAND", nullptr},
  {"ims_rate", 'I', "IMS Not-Changed Rate", "D", &ims_rate, "JTEST_IMS_RATE", nullptr},
  {"client_abort_rate", 'g', "Client Abort Rate", "D", &client_abort_rate, "JTEST_CLIENT_ABORT_RATE", nullptr},
  {"server_abort_rate", 'G', "Server Abort Rate", "D", &server_abort_rate, "JTEST_SERVER_ABORT_RATE", nullptr},
  {"extra_headers", 'n', "Number of Extra Headers", "I", &extra_headers, "JTEST_EXTRA_HEADERS", nullptr},
  {"alternates", 'N', "Number of Alternates", "I", &alternates, "JTEST_ALTERNATES", nullptr},
  {"client_rate", 'e', "Clients Per Sec", "I", &client_rate, "JTEST_CLIENT_RATE", nullptr},
  {"abort_retry_speed", 'o', "Abort/Retry Speed", "I", &abort_retry_speed, "JTEST_ABORT_RETRY_SPEED", nullptr},
  {"abort_retry_bytes", ' ', "Abort/Retry Threshhold (bytes)", "I", &abort_retry_bytes, "JTEST_ABORT_RETRY_THRESHHOLD_BYTES",
   nullptr},
  {"abort_retry_secs", ' ', "Abort/Retry Threshhold (secs)", "I", &abort_retry_secs, "JTEST_ABORT_RETRY_THRESHHOLD_SECS", nullptr},
  {"reload_rate", 'W', "Reload Rate", "D", &reload_rate, "JTEST_RELOAD_RATE", nullptr},
  {"compd_port", 'O', "Compd port", "I", &compd_port, "JTEST_COMPD_PORT", nullptr},
  {"compd_suite", '1', "Compd Suite", "F", &compd_suite, "JTEST_COMPD_SUITE", nullptr},
  {"vary_user_agent", '2', "Vary on User-Agent (use w/ alternates)", "I", &vary_user_agent, "JTEST_VARY_ON_USER_AGENT", nullptr},
  {"content_type", '3', "Server Content-Type (1 html, 2 jpeg)", "I", &server_content_type, "JTEST_CONTENT_TYPE", nullptr},
  {"request_extension", '4', "Request Extn (1\".html\" 2\".jpeg\" 3\"/\")", "I", &request_extension, "JTEST_REQUEST_EXTENSION",
   nullptr},
  {"no_cache", '5', "Send Server no-cache", "I", &no_cache, "JTEST_NO_CACHE", nullptr},
  {"zipf_bucket", '7', "Bucket size (of 1M buckets) for Zipf", "I", &zipf_bucket_size, "JTEST_ZIPF_BUCKET_SIZE", nullptr},
  {"zipf", '8', "Use a Zipf distribution with this alpha (say 1.2)", "D", &zipf, "JTEST_ZIPF", nullptr},
  {"evo_rate", '9', "Evolving Hotset Rate (evolutions/hour)", "D", &evo_rate, "JTEST_EVOLVING_HOTSET_RATE", nullptr},
  {"debug", 'd', "Debug Flag", "F", &debug, "JTEST_DEBUG", nullptr},
  {"range_mode", ' ', "Range Mode", "I", &range_mode, "JTEST_RANGE_MODE", nullptr},
  {"post_support", ' ', "POST Mode (0 disable(default), 1 random, 2 specified size by post_size)", "I", &post_support,
   "JTEST_POST_MODE", nullptr},
  {"post_size", ' ', "POST SIZE", "I", &post_size, "JTEST_POST_SIZE", nullptr},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION()};
int n_argument_descriptions = countof(argument_descriptions);

struct FD {
  int fd;
  poll_cb read_cb;
  poll_cb write_cb;
  ink_hrtime start;
  ink_hrtime active;
  ink_hrtime ready;

  double doc;
  int doc_length;
  struct sockaddr_in name;

  int state;   // request parsing state
  int req_pos; // request read position
  char *base_url        = nullptr;
  char *req_header      = nullptr;
  char *response        = nullptr;
  char *response_header = nullptr;
  int length;
  int response_length;
  int response_remaining;
  int keepalive = 0;
  int next;
  int nalternate  = 0;
  unsigned int ip = 0;
  unsigned int binary : 1;
  unsigned int ims : 1;
  unsigned int range : 1;
  unsigned int drop_after_CL : 1;
  unsigned int client_abort : 1;
  unsigned int jg_compressed : 1;
  int *count;
  int bytes;
  int ftp_data_fd = 0;
  FTP_MODE ftp_mode;
  unsigned int ftp_peer_addr;
  unsigned short ftp_peer_port;
  unsigned long range_bytes;
  unsigned long range_end;
  unsigned long range_start;
  int post_size;
  int total_length;
  int post_cl;
  int send_header;
  int header_size;

  void
  reset()
  {
    next        = 0;
    fd          = -1;
    read_cb     = nullptr;
    write_cb    = nullptr;
    state       = 0;
    start       = 0;
    active      = 0;
    ready       = 0;
    req_pos     = 0;
    length      = 0;
    range       = 0;
    range_bytes = 0;
    range_start = 0;
    range_end   = 0;
    post_size   = 0;
    send_header = 0;

    if (!urls_mode) {
      response = nullptr;
    }

    if (response_header) {
      response_header[0] = 0;
    }

    response_length    = 0;
    response_remaining = 0;
    count              = nullptr;
    bytes              = 0;
    doc                = 0.0;
    doc_length         = 0;
    ims                = 0;
    drop_after_CL      = ::drop_after_CL;
    client_abort       = 0;
    jg_compressed      = 0;
    ftp_mode           = FTP_NULL;
    ftp_peer_addr      = 0;
    ftp_peer_port      = 0;
    total_length       = 0;
    post_cl            = 0;
    header_size        = 0;
  }

  void close();
  FD() : binary(0)
  {
    ink_zero(name);
    reset();
  }
};

FD *fd = nullptr;

void
FD::close()
{
  if (verbose) {
    printf("close: %d\n", fd);
  }
  ::close(fd);
  if (is_done()) {
    done();
  }
  keepalive = 0;
  ip        = 0;
  if (count) {
    (*count)--;
  }
  if (count == &clients) {
    current_clients--;
  }
  reset();
  if (urls_mode) {
    undefer_url();
  }
  ftp_data_fd = 0;
}

#define MAX_FILE_ARGUMENTS 100

typedef struct {
  char sche[MAX_URL_LEN + 1];
  char host[MAX_URL_LEN + 1];
  char port[MAX_URL_LEN + 1];
  char path[MAX_URL_LEN + 1];
  char frag[MAX_URL_LEN + 1];
  char quer[MAX_URL_LEN + 1];
  char para[MAX_URL_LEN + 1];

  int sche_exists;
  int host_exists;
  int port_exists;
  int path_exists;
  int frag_exists;
  int quer_exists;
  int para_exists;

  int rel_url;
  int leading_slash;
  int is_path_name;
} InkWebURLComponents;

static int ink_web_remove_dots(char *src, char *dest, int *leadingslash, int max_dest_len);

static int ink_web_unescapify_string(char *dest_in, char *src_in, int max_dest_len);

static int ink_web_escapify_string(char *dest_in, char *src_in, int max_dest_len);

static void ink_web_decompose_url(const char *src_url, char *sche, char *host, char *port, char *path, char *frag, char *quer,
                                  char *para, int *real_sche_exists, int *real_host_exists, int *real_port_exists,
                                  int *real_path_exists, int *real_frag_exists, int *real_quer_exists, int *real_para_exists,
                                  int *real_relative_url, int *real_leading_slash);

static void ink_web_canonicalize_url(const char *base_url, const char *emb_url, char *dest_url, int max_dest_url_len);

static void ink_web_decompose_url_into_structure(const char *url, InkWebURLComponents *c);

static void
remove_last_seg(char *src, char *dest)
{
  char *ptr;
  for (ptr = src + strlen(src) - 1; ptr >= src; ptr--) {
    if (*ptr == '/') {
      break;
    }
  }
  while (src <= ptr) {
    *dest++ = *src++;
  }
  *dest = '\0';
}

static inline void
remove_multiple_slash(char *src, char *dest)
{
  char *ptr = nullptr;

  for (ptr = src; *ptr;) {
    *(dest++) = *ptr;
    if (*ptr == '/') {
      while ((*ptr == '/') && *ptr) {
        ptr++;
      }
    } else {
      ptr++;
    }
  }
  *dest = '\0';
}

static inline void
append_string(char *dest, const char *src, int *offset_ptr, int max_len)
{
  int num = strlen(src);
  if (*offset_ptr + num >= max_len) {
    num = max_len - (*offset_ptr + 1);
    if (num <= 1) {
      return;
    }
  }
  memcpy(dest + *offset_ptr, src, num);
  dest[*offset_ptr + num] = '\0';
  (*offset_ptr) += num;
}

// End Library functions

static void
panic(const char *s)
{
  fputs(s, stderr);
  exit(1);
}

static void
panic_perror(const char *s)
{
  perror(s);
  exit(1);
}

static int
max_limit_fd()
{
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) >= 0) {
#ifdef OPEN_MAX
    // Darwin
    rl.rlim_cur = std::min(static_cast<rlim_t>(OPEN_MAX), rl.rlim_max);
#else
    rl.rlim_cur = rl.rlim_max;
#endif
    if (setrlimit(RLIMIT_NOFILE, &rl) >= 0) {
      if (getrlimit(RLIMIT_NOFILE, &rl) >= 0) {
        return rl.rlim_cur;
      }
    }
  }
  panic_perror("couldn't set RLIMIT_NOFILE\n");
  return -1;
}

static int
read_ready(int fd)
{
  struct pollfd p;
  p.events = POLLIN;
  p.fd     = fd;
  int r    = poll(&p, 1, 0);
  if (r <= 0) {
    return r;
  }
  if (p.revents & (POLLERR | POLLNVAL)) {
    return -1;
  }
  if (p.revents & (POLLIN | POLLHUP)) {
    return 1;
  }
  return 0;
}

static void
poll_init(int sock)
{
  if (!fd[sock].req_header) {
    fd[sock].req_header = (char *)malloc(HEADER_SIZE * pipeline + MAX_REQUEST_BODY_LENGTH);
  }
  if (!fd[sock].response_header) {
    fd[sock].response_header = (char *)malloc(HEADER_SIZE);
  }
  if (!fd[sock].base_url) {
    fd[sock].base_url = (char *)malloc(HEADER_SIZE);
  }
  fd[sock].reset();
}

static void
poll_set(int sock, poll_cb read_cb, poll_cb write_cb = nullptr)
{
  if (verbose) {
    printf("adding poll %d\n", sock);
  }
  fd[sock].fd       = sock;
  fd[sock].read_cb  = read_cb;
  fd[sock].write_cb = write_cb;
  if (last_fd < sock) {
    last_fd = sock;
  }
}

static void
poll_init_set(int sock, poll_cb read_cb, poll_cb write_cb = nullptr)
{
  poll_init(sock);
  poll_set(sock, read_cb, write_cb);
}

static int
fast(int sock, int speed, int d)
{
  if (!speed) {
    return 0;
  }
  int64_t t  = now - fd[sock].start + 1;
  int target = (int)(((t / HRTIME_MSECOND) * speed) / 1000);
  int delta  = d - target;
  if (delta > 0) {
    int mwait      = (delta * 1000) / speed;
    fd[sock].ready = now + (mwait * HRTIME_MSECOND);
    return 1;
  } else {
    fd[sock].ready = now;
  }
  return 0;
}

// Return the number of milliseconds elapsed since the start of the request.
static ink_hrtime
elapsed_from_start(int sock)
{
  ink_hrtime now = ink_get_hrtime_internal();
  return ink_hrtime_diff_msec(now, fd[sock].start);
}

static int
faster_than(int sock, int speed, int d)
{
  if (!speed) {
    return 1;
  }
  int64_t t  = now - fd[sock].start + 1;
  int target = (int)(((t / HRTIME_MSECOND) * speed) / 1000);
  int delta  = d - target;
  if (delta > 0) {
    return 1;
  }
  return 0;
}

static void
get_path_from_req(char *buf, char **purl_start, char **purl_end)
{
  char *url_start = buf;
  char *url_end   = nullptr;
  if (!strncasecmp(url_start, "GET ", sizeof("GET ") - 1)) {
    url_start += sizeof("GET ") - 1;
    url_end = (char *)memchr(url_start, ' ', 70);
  } else if (!strncasecmp(url_start, "POST ", sizeof("POST ") - 1)) {
    url_start += sizeof("POST ") - 1;
    url_end = (char *)memchr(url_start, ' ', 70);
  } else {
    url_end = (char *)memchr(url_start, 0, 70);
  }
  if (!url_end) {
    panic("malformed request\n");
  }
  if (url_end - url_start > 10) {
    if (!strncasecmp(url_start, "http://", sizeof("http://") - 1)) {
      url_start += sizeof("http://") - 1;
      url_start = (char *)memchr(url_start, '/', 70);
    }
  }
  *purl_start = url_start;
  *purl_end   = url_end;
}

static int
make_response_header(int sock, char *url_start, char *url_end, int *url_len, char *header, int header_limit)
{
  const char *content_type;
  switch (server_content_type) {
  case 1:
    content_type = "text/html";
    break;
  case 2:
    content_type = "image/jpeg";
    break;
  default:
    content_type = ((compd_suite || alternates) ? "image/jpeg" : "text/html");
    if (only_server && strstr(fd[sock].req_header, "Cookie:")) {
      content_type = "image/jpeg";
    }
  }
  if (!ftp && embed_url && fd[sock].response_length > 16) {
    get_path_from_req(fd[sock].req_header, &url_start, &url_end);
    *url_end = 0;
    *url_len = url_end - url_start;
  }
  int print_len = 0;
  if (!ftp) {
    if (fd[sock].range) {
      char buff[1024];
      memset(buff, 0, 1024);
      if (fd[sock].range_end > fd[sock].range_start) {
        snprintf(buff, 1024, "Content-Range: bytes %lu-%lu/%d", fd[sock].range_start, fd[sock].range_end, fd[sock].total_length);
      } else {
        snprintf(buff, 1024, "Content-Range: bytes %lu-%d/%d", fd[sock].range_start, fd[sock].total_length, fd[sock].total_length);
      }
      print_len = snprintf(header, header_limit,
                           "HTTP/1.1 206 Partial-Content\r\n"
                           "Content-Type: %s\r\n"
                           "Cache-Control: max-age=630720000\r\n"
                           "Last-Modified: Mon, 05 Oct 2010 01:00:00 GMT\r\n"
                           "%s"
                           "Content-Length: %d\r\n"
                           "%s\r\n"
                           "%s"
                           "\r\n%s",
                           content_type, fd[sock].keepalive > 0 ? "Connection: Keep-Alive\r\n" : "Connection: close\r\n",
                           fd[sock].response_length, buff, no_cache ? "Pragma: no-cache\r\nCache-Control: no-cache\r\n" : "",
                           url_start ? url_start : "");
    } else if (fd[sock].ims) {
      print_len = snprintf(header, header_limit,
                           "HTTP/1.0 304 Not-Modified\r\n"
                           "Content-Type: %s\r\n"
                           "Last-Modified: Mon, 05 Oct 2010 01:00:00 GMT\r\n"
                           "%s"
                           "\r\n",
                           content_type, fd[sock].keepalive > 0 ? "Connection: Keep-Alive\r\n" : "");
      *url_len  = 0;
    } else {
      print_len = snprintf(header, header_limit,
                           "HTTP/1.0 200 OK\r\n"
                           "Content-Type: %s\r\n"
                           "Cache-Control: max-age=630720000\r\n"
                           "Last-Modified: Mon, 05 Oct 2010 01:00:00 GMT\r\n"
                           "%s"
                           "Content-Length: %d\r\n"
                           "%s"
                           "\r\n%s",
                           content_type, fd[sock].keepalive > 0 ? "Connection: Keep-Alive\r\n" : "", fd[sock].response_length,
                           no_cache ? "Pragma: no-cache\r\nCache-Control: no-cache\r\n" : "", url_start ? url_start : "");
    }
  } else {
    *url_len = print_len = sprintf(header, "ftp://%s:%d/%12.10f/%d", local_host, server_port, fd[sock].doc, fd[sock].length);
  }

  if (show_headers) {
    printf("Response to Proxy: {\n%s}\n", header);
  }

  return print_len;
}

static int
send_response(int sock)
{
  char *url_start = nullptr;
  char *url_end   = nullptr;
  int err         = 0, towrite;
  int url_len     = 0;

  if (fd[sock].req_pos >= 0) {
    char header[1024];

    int print_len = make_response_header(sock, url_start, url_end, &url_len, header, 1024);

    int len = print_len - fd[sock].req_pos;
    ink_assert(len > 0);
    do {
      err = write(sock, header + fd[sock].req_pos, len);
    } while ((err == -1) && (errno == EINTR));
    if (err <= 0) {
      if (!err) {
        return -1;
      }
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      return -1;
    }
    if (verbose) {
      printf("wrote %d %d\n", sock, err);
    }
    new_tbytes += err;
    fd[sock].req_pos += err;
    fd[sock].bytes += err;
    if (fd[sock].req_pos >= len) {
      fd[sock].req_pos = -1;
    } else {
      return 0;
    }
    fd[sock].response += url_len;
    fd[sock].length -= url_len;
    if (fd[sock].range) {
      fd[sock].range_bytes -= url_len;
    }
    total_server_response_header_bytes += print_len - url_len;
    total_server_response_body_bytes += url_len;
  }

  /* then the response */
  towrite = server_speed ? server_speed : MAX_RESPONSE_LENGTH;
  if (!fd[sock].range) {
    if (fd[sock].length < towrite) {
      towrite = fd[sock].length;
    }
  } else {
    if (fd[sock].range_bytes < (unsigned long)towrite) {
      towrite = fd[sock].range_bytes;
    }
  }

  if (towrite > 0) {
    if (fast(sock, server_speed, fd[sock].bytes)) {
      return 0;
    }
    do {
      err = write(sock, fd[sock].response, towrite);
    } while ((err == -1) && (errno == EINTR));
    if (err < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      fprintf(stderr, "write errno %d length %d sock %d\n", errno, towrite, sock);
      errors++;
      return -1;
    }
    if (verbose) {
      printf("wrote %d %d\n", sock, err);
    }

    if (fd[sock].range) {
      ink_assert(err <= (int)(fd[sock].range_end - fd[sock].range_start + 1));
    }

    new_tbytes += err;
    total_server_response_body_bytes += err;
    fd[sock].response += err;
    fd[sock].length -= err;
    fd[sock].bytes += err;
  }

  if (fast(sock, server_speed, fd[sock].bytes)) {
    return 0;
  }
  if (fd[sock].length <= 0 || !err) {
    if (fd[sock].response) {
      new_sops++;
    }
    if (verbose) {
      printf("write %d done\n", sock);
    }
    if (fd[sock].keepalive > 0 && !ftp) {
      poll_init_set(sock, read_request);
      fd[sock].start = now;
      fd[sock].ready = now + server_delay * HRTIME_MSECOND;
      return 0;
    }
    return 1;
  }

  return 0;
}

static char *
strncasestr(char *s, const char *find, int len)
{
  int findlen = strlen(find);
  char *e     = s + len;
  while (1) {
    char *x = (char *)memchr(s, *find, e - s);
    if (!x) {
      if (ParseRules::is_upalpha(*find)) {
        x = (char *)memchr(s, ParseRules::ink_tolower(*find), e - s);
      } else {
        x = (char *)memchr(s, ParseRules::ink_toupper(*find), e - s);
      }
      if (!x) {
        break;
      }
    }
    if (!strncasecmp(find, x, findlen)) {
      return x;
    }
    s = x + 1;
  }
  return nullptr;
}

static char *
check_keepalive(char *r, int length)
{
  char *ka       = strncasestr(r, "Connection:", length);
  char *http_1_1 = strncasestr(r, "HTTP/1.1", length);
  if (http_1_1 && !ka) {
    return http_1_1;
  }
  if (ka) {
    int l   = length - (ka - r);
    char *e = (char *)memchr(ka, '\n', l);
    if (!e) {
      e = (char *)memchr(ka, '\r', l);
    }
    if (strncasestr(ka, "close", e - ka)) {
      return nullptr;
    }
  }
  return ka;
}

static int
check_alt(char *r, int length)
{
  char *s = strncasestr(r, "Cookie:", length);
  if (!s) {
    s = strncasestr(r, "User-Agent:", length);
    if (s) {
      s += sizeof("User-Agent:");
    }
  } else {
    s += sizeof("Cookie:");
  }
  if (s) {
    int l   = length - (s - r);
    char *e = (char *)memchr(s, '\n', l);
    if (!e) {
      e = (char *)memchr(s, '\r', l);
    }
    if (!(s = strncasestr(s, "jtest", e - s))) {
      return 0;
    }
    s = (char *)memchr(s, '-', l);
    if (!s) {
      return 0;
    }
    s = (char *)memchr(s + 1, '-', l);
    if (!s) {
      return 0;
    }
    return ink_atoi(s + 1);
  }
  return 0;
}

static void
make_response(int sock, int code)
{
  fd[sock].response        = fd[sock].req_header;
  fd[sock].length          = sprintf(fd[sock].req_header, "%d\r\n", code);
  fd[sock].req_pos         = 0;
  fd[sock].response_length = strlen(fd[sock].req_header);
  poll_set(sock, nullptr, write_ftp_response);
}

static void
make_long_response(int sock)
{
  fd[sock].response        = fd[sock].req_header;
  fd[sock].req_pos         = 0;
  fd[sock].response_length = strlen(fd[sock].req_header);
  poll_set(sock, nullptr, write_ftp_response);
}

static int
send_ftp_data_when_ready(int sock)
{
  if (fd[sock].state == STATE_FTP_DATA_READY && fd[sock].doc_length) {
    fd[sock].response        = fd[sock].req_header;
    fd[sock].response_length = fd[sock].length = fd[sock].doc_length;
    if (verbose) {
      printf("ftp data %d >-< %d\n", sock, fd[sock].ftp_data_fd);
    }
    fd[sock].response = response_buffer + fd[sock].doc_length % 256;
    fd[sock].req_pos  = 0;
    poll_set(sock, nullptr, send_response);
  }
  return 0;
}

static int
send_ftp_data(int sock, char *start /*, char * end */)
{
  int data_fd = fd[sock].ftp_data_fd;
  if (sscanf(start, "%d", &fd[data_fd].doc_length) != 1) {
    return -1;
  }
  fd[data_fd].doc = fd[sock].doc;
  send_ftp_data_when_ready(data_fd);
  return 0;
}

static int
process_header(int sock, char *buffer, int offset)
{
  char host[80];
  int port, length;
  float r;
  int post_request = 0;
  if (sscanf(buffer, "GET http://%[^:]:%d/%f/%d", host, &port, &r, &length) == 4) {
  } else if (sscanf(buffer, "GET /%f/%d", &r, &length) == 2) {
  } else if (sscanf(buffer, "POST http://%[^:]:%d/%f/%d", host, &port, &r, &length) == 4) {
    post_request = 1;
  } else if (sscanf(buffer, "POST /%f/%d", &r, &length) == 2) {
    post_request = 1;
  } else {
    if (verbose) {
      printf("misscan: %s\n", buffer);
    }
    fd[sock].close();
    return -1;
  }

  if (verbose) {
    printf("read_request %d got request %d\n", sock, length);
  }
  char *ims     = strncasestr(buffer, "If-Modified-Since:", offset);
  char *range   = strncasestr(buffer, "Range:", offset);
  char *post_cl = nullptr;
  if (post_support) {
    post_cl          = strncasestr(buffer, "Content-Length:", offset);
    fd[sock].post_cl = atoi(post_cl + strlen("Content-Length: "));
    ink_assert(post_cl && post_request && fd[sock].post_cl);
  }
  // coverity[dont_call]
  if (drand48() > ims_rate) {
    ims = nullptr;
  }
  if (range) {
    fd[sock].range = 1;
    if (sscanf(range, "Range: bytes=%lu-%lu", &fd[sock].range_start, &fd[sock].range_end) == 2) {
      fd[sock].range_bytes = fd[sock].range_end - fd[sock].range_start + 1;
    } else if (sscanf(range, "Range: bytes=%lu-", &fd[sock].range_start) == 1) {
      fd[sock].range_bytes = length - fd[sock].range_start + 1;
    } else {
      if (verbose)
        printf("unvalid 206");
    }
    ims = nullptr;
    if (verbose) {
      printf("sending Range: 206 Partial %lu-%lu\n", fd[sock].range_start, fd[sock].range_end);
    }
  }

  fd[sock].ims = ims ? 1 : 0;
  if (!ims) {
    if (range) {
      fd[sock].total_length    = length;
      fd[sock].response_length = fd[sock].length = fd[sock].range_bytes;
    } else {
      fd[sock].response_length = fd[sock].length = length;
    }
    fd[sock].nalternate = check_alt(fd[sock].req_header, strlen(fd[sock].req_header));
    fd[sock].response   = response_buffer + length % 256 + fd[sock].nalternate;
  } else {
    fd[sock].nalternate = 0;
    if (verbose) {
      printf("sending IMS 304: Not-Modified\n");
    }
    fd[sock].response        = nullptr;
    fd[sock].response_length = fd[sock].length = 0;
  }
  fd[sock].header_size = offset;

  return post_request;
}

static int
parse_header(int sock, int err)
{
  int i;
  int post_request = 0;

  if (verbose) {
    printf("read %d got %d\n", sock, err);
  }
  total_proxy_request_bytes += err;
  new_tbytes += err;
  fd[sock].req_pos += err;
  fd[sock].req_header[fd[sock].req_pos] = 0;
  char *buffer                          = fd[sock].req_header;
  for (i = fd[sock].req_pos - err; i < fd[sock].req_pos; i++) {
    switch (fd[sock].state) {
    case 0:
      if (buffer[i] == '\r') {
        fd[sock].state = 1;
      } else if (buffer[i] == '\n') {
        fd[sock].state = 2;
      }
      break;
    case 1:
      if (buffer[i] == '\n') {
        fd[sock].state = 2;
      } else {
        fd[sock].state = 0;
      }
      break;
    case 2:
      if (buffer[i] == '\r') {
        fd[sock].state = 3;
      } else if (buffer[i] == '\n') {
        fd[sock].state = 3;
        goto L3;
      } else {
        fd[sock].state = 0;
      }
      break;
    L3:
    case 3:
      if (buffer[i] == '\n') {
        if (show_headers) {
          printf("Request from Proxy: {\n%s}\n", buffer);
        }

        post_request = process_header(sock, buffer, i);
        if (post_request < 0) {
          return JTEST_DONE;
        }

        if (post_request) {
          fd[sock].state = 4;
          break;
        }

        fd[sock].req_pos = 0;
        if (!check_keepalive(fd[sock].req_header, strlen(fd[sock].req_header))) {
          fd[sock].keepalive = 0;
        } else {
          fd[sock].keepalive--;
        }
        // coverity[dont_call]
        if (fd[sock].length && drand48() < server_abort_rate) {
          // coverity[dont_call]
          fd[sock].length    = (int)(drand48() * (fd[sock].length - 1));
          fd[sock].keepalive = 0;
        }
        poll_set(sock, nullptr, send_response);
        return JTEST_DONE;
      } else {
        fd[sock].state = 0;
      }
      break;
    case 4:
      if (fd[sock].req_pos - fd[sock].header_size - 1 >= fd[sock].post_cl) {
        fd[sock].req_pos = 0;
        if (!check_keepalive(fd[sock].req_header, strlen(fd[sock].req_header))) {
          fd[sock].keepalive = 0;
        } else {
          fd[sock].keepalive--;
        }
        // coverity[dont_call]
        if (fd[sock].length && drand48() < server_abort_rate) {
          // coverity[dont_call]
          fd[sock].length    = (int)(drand48() * (fd[sock].length - 1));
          fd[sock].keepalive = 0;
        }
        poll_set(sock, nullptr, send_response);
        fd[sock].state = 0;
        return JTEST_DONE;
      }
      return JTEST_CONT;
    }
  }
  return JTEST_CONT;
}

static int
read_request(int sock)
{
  if (verbose) {
    printf("read_request %d\n", sock);
  }
  int err     = 0;
  int maxleft = 0;

  if (!post_support)
    maxleft = HEADER_SIZE - fd[sock].req_pos - 1;
  else
    maxleft = HEADER_SIZE + MAX_REQUEST_BODY_LENGTH - fd[sock].req_pos - 1;

  while (true) {
    do {
      err = read(sock, &fd[sock].req_header[fd[sock].req_pos], maxleft);
    } while ((err < 0) && (errno == EINTR));

    if (err < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      if (fd[sock].req_pos || errno != ECONNRESET) {
        perror("read");
      }
      return -1;
    } else if (err == 0) {
      if (verbose) {
        printf("eof\n");
      }
      return -1;
    } else {
      if (verbose) {
        printf("read %d got %d\n", sock, err);
      }

      if (parse_header(sock, err) == JTEST_DONE)
        return 0;
    }
  }
  return 0;
}

static int
send_compd_response(int sock)
{
  int err = 0;

  struct {
    unsigned int code;
    unsigned int len;
  } compd_header;
  if (fd[sock].req_pos < (int)sizeof(compd_header)) {
    compd_header.code = 0;
    compd_header.len  = htonl((fd[sock].length * 2) / 3);
    do {
      err = write(sock, (char *)&compd_header + fd[sock].req_pos, sizeof(compd_header) - fd[sock].req_pos);
    } while ((err == -1) && (errno == EINTR));
    if (err <= 0) {
      if (!err) {
        if (verbose_errors) {
          printf("write %d closed early\n", sock);
        }
        goto Lerror;
      }
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      perror("write");
      goto Lerror;
    }
    if (verbose) {
      printf("write %d %d\n", sock, err);
    }

    new_tbytes += err;
    fd[sock].req_pos += err;
    fd[sock].bytes += err;
    fd[sock].response = response_buffer + (((fd[sock].length * 2) / 3) % 256);
  }

  if (fd[sock].req_pos < ((fd[sock].length * 2) / 3) + (int)sizeof(compd_header)) {
    int towrite = cbuffersize;
    int desired = ((fd[sock].length * 2) / 3) + sizeof(compd_header) - fd[sock].req_pos;
    if (towrite > desired) {
      towrite = desired;
    }
    if (fast(sock, client_speed, fd[sock].bytes)) {
      return 0;
    }
    do {
      err = write(sock, fd[sock].response + fd[sock].req_pos - sizeof(compd_header), towrite);
    } while ((err == -1) && (errno == EINTR));
    if (err < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      fprintf(stderr, "write errno %d length %d sock %d\n", errno, towrite, sock);
      errors++;
      return -1;
    }
    if (verbose) {
      printf("wrote %d %d\n", sock, err);
    }

    new_tbytes += err;
    total_server_response_body_bytes += err;
    fd[sock].req_pos += err;
    fd[sock].bytes += err;
  }

  if (fd[sock].req_pos >= ((fd[sock].length * 2) / 3) + 4) {
    return -1;
  }

  return 0;
Lerror:
  errors++;
  return 1;
}

static int
read_compd_request(int sock)
{
  if (verbose) {
    printf("read_compd_request %d\n", sock);
  }
  int err = 0;

  if (fd[sock].req_pos < 4) {
    int maxleft = HEADER_SIZE - fd[sock].req_pos - 1;
    do {
      err = read(sock, &fd[sock].req_header[fd[sock].req_pos], maxleft);
    } while ((err < 0) && (errno == EINTR));

    if (err < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      perror("read");
      return -1;
    } else if (err == 0) {
      if (verbose) {
        printf("eof\n");
      }
      return -1;
    } else {
      if (verbose) {
        printf("read %d got %d\n", sock, err);
      }
      total_proxy_request_bytes += err;
      new_tbytes += err;
      fd[sock].req_pos += err;
      if (fd[sock].req_pos < 4) {
        return 0;
      }
      fd[sock].length = ntohl(*(unsigned int *)fd[sock].req_header);
    }
  }

  if (fd[sock].req_pos >= fd[sock].length + 4) {
    goto Lcont;
  }

  {
    char buf[MAX_BUFSIZE];
    int toread = cbuffersize;
    if (fast(sock, client_speed, fd[sock].bytes)) {
      return 0;
    }
    do {
      err = read(sock, buf, toread);
    } while ((err == -1) && (errno == EINTR));
    if (err < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      if (errno == ECONNRESET) {
        if (verbose || verbose_errors) {
          perror("read");
        }
        errors++;
        return -1;
      }
      panic_perror("read");
    }
    if (!err) {
      if (verbose || verbose_errors) {
        perror("read");
      }
      errors++;
      return -1;
    }
    total_proxy_request_bytes += err;
    new_tbytes += err;
    fd[sock].req_pos += err;
  }

  if (fd[sock].req_pos >= fd[sock].length + 4) {
    goto Lcont;
  }

  return 0;

Lcont:
  fd[sock].req_pos   = 0;
  fd[sock].keepalive = 0;
  poll_set(sock, nullptr, send_compd_response);
  return 0;
}

static int
read_ftp_request(int sock)
{
  if (verbose) {
    printf("read_ftp_request %d\n", sock);
  }
  int err = 0;
  int i;

  int maxleft = HEADER_SIZE - fd[sock].req_pos - 1;

  do {
    err = read(sock, &fd[sock].req_header[fd[sock].req_pos], maxleft);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0) {
    if (errno == EAGAIN || errno == ENOTCONN) {
      return 0;
    }
    perror("read");
    return -1;
  } else if (err == 0) {
    if (verbose) {
      printf("eof\n");
    }
    return -1;
  } else {
    if (verbose) {
      printf("read %d got %d\n", sock, err);
    }
    new_tbytes += err;
    fd[sock].req_pos += err;
    fd[sock].req_header[fd[sock].req_pos] = 0;
    char *buffer                          = fd[sock].req_header, *n;
    int res                               = 0;
    buffer[fd[sock].req_pos]              = 0;
    if (verbose) {
      printf("buffer [%s]\n", buffer);
    }
#define STREQ(_x, _s) (!strncasecmp(_x, _s, sizeof(_s) - 1))
    if (STREQ(buffer, "USER")) {
      res = 331;
      goto Lhere;
    } else if (STREQ(buffer, "PASS")) {
      res = 230;
      goto Lhere;
    } else if (STREQ(buffer, "CWD")) {
      // TS used to send "CWD 1.2110000000..."
      // TS now sends "CWD /1.2110000000^M\n", so skip 5 instead of 4
      fd[sock].doc = (buffer[4] == '/') ? atof(buffer + 5) : atof(buffer + 4);
      res          = 250;
      goto Lhere;
    } else if (STREQ(buffer, "TYPE")) {
      res = 200;
    Lhere:
      n = (char *)memchr(buffer, '\n', fd[sock].req_pos);
      if (!n) {
        return 0;
      }
      make_response(sock, res);
      return 0;
    } else if (STREQ(buffer, "SIZE")) {
      fd[sock].length = sprintf(fd[sock].req_header, "213 %d\r\n", atoi(buffer + 5));
      make_long_response(sock);
      return 0;
    } else if (STREQ(buffer, "MDTM")) {
      double err_rand = 1.0;
      if (ftp_mdtm_err_rate != 0.0) {
        // coverity[dont_call]
        err_rand = drand48();
      }
      if (err_rand < ftp_mdtm_err_rate) {
        fd[sock].length = sprintf(fd[sock].req_header, "550 mdtm file not found\r\n");
      } else {
        if (ftp_mdtm_rate == 0) {
          fd[sock].length = sprintf(fd[sock].req_header, "213 19900615100045\r\n");
        } else {
          time_t mdtm_now;
          time(&mdtm_now);
          if (mdtm_now - ftp_mdtm_last_update > ftp_mdtm_rate) {
            struct tm *mdtm_tm;
            ftp_mdtm_last_update = mdtm_now;
            mdtm_tm              = localtime(&ftp_mdtm_last_update);
            sprintf(ftp_mdtm_str, "213 %.4d%.2d%.2d%.2d%.2d%.2d", mdtm_tm->tm_year + 1900, mdtm_tm->tm_mon + 1, mdtm_tm->tm_mday,
                    mdtm_tm->tm_hour, mdtm_tm->tm_min, mdtm_tm->tm_sec);
          }
          fd[sock].length = sprintf(fd[sock].req_header, "%s\r\n", ftp_mdtm_str);
        }
      }
      make_long_response(sock);
      return 0;
    } else if (STREQ(buffer, "PASV")) {
      n = (char *)memchr(buffer, '\n', fd[sock].req_pos);
      if (!n) {
        return 0;
      }
      if ((fd[sock].ftp_data_fd = open_server(0, accept_ftp_data)) < 0) {
        panic("could not open ftp data PASV accept port\n");
      }
      fd[fd[sock].ftp_data_fd].ftp_data_fd = sock;
      if (verbose) {
        printf("ftp PASV %d <-> %d\n", sock, fd[sock].ftp_data_fd);
      }
      unsigned short p = fd[fd[sock].ftp_data_fd].name.sin_port;
      fd[sock].length  = sprintf(fd[sock].req_header, "227 (%u,%u,%u,%u,%u,%u)\r\n", ((unsigned char *)&local_addr)[0],
                                ((unsigned char *)&local_addr)[1], ((unsigned char *)&local_addr)[2],
                                ((unsigned char *)&local_addr)[3], ((unsigned char *)&p)[0], ((unsigned char *)&p)[1]);
      if (verbose) {
        puts(fd[sock].req_header);
      }
      make_long_response(sock);
      fd[sock].ftp_mode = FTP_PASV;
      return 0;
    } else if (STREQ(buffer, "PORT")) {
      // watch out for an endian problems !!!
      char *start, *stop;
      for (start = buffer; !ParseRules::is_digit(*start); start++) {
        ;
      }
      for (stop = start; *stop != ','; stop++) {
        ;
      }
      for (i = 0; i < 4; i++) {
        ((unsigned char *)&(fd[sock].ftp_peer_addr))[i] = strtol(start, &stop, 10);
        for (start = ++stop; *stop != ','; stop++) {
          ;
        }
      }
      ((unsigned char *)&(fd[sock].ftp_peer_port))[0] = strtol(start, &stop, 10);
      start                                           = ++stop;
      ((unsigned char *)&(fd[sock].ftp_peer_port))[1] = strtol(start, nullptr, 10);
      fd[sock].length                                 = sprintf(fd[sock].req_header, "200 Okay\r\n");
      if (verbose) {
        puts(fd[sock].req_header);
      }
      make_long_response(sock);
      fd[sock].ftp_mode = FTP_PORT;
      return 0;
    } else if (STREQ(buffer, "RETR")) {
      if (fd[sock].ftp_mode == FTP_NULL) {
        // default to PORT ftp
        struct sockaddr_in ftp_peer;
        int ftp_peer_addr_len = sizeof(ftp_peer);
        if (getpeername(sock, (struct sockaddr *)&ftp_peer,
#if 0
                          &ftp_peer_addr_len
#else
                        (socklen_t *)&ftp_peer_addr_len
#endif
                        ) < 0) {
          perror("getsockname");
          exit(EXIT_FAILURE);
        }
        fd[sock].ftp_peer_addr = ftp_peer.sin_addr.s_addr;
        fd[sock].ftp_peer_port = ftp_peer.sin_port;
        fd[sock].ftp_mode      = FTP_PORT;
      }
      if (fd[sock].ftp_mode == FTP_PORT) {
        if ((fd[sock].ftp_data_fd = make_client(fd[sock].ftp_peer_addr, fd[sock].ftp_peer_port)) < 0) {
          panic("could not open ftp PORT data connection to client\n");
        }
        fd[fd[sock].ftp_data_fd].ftp_data_fd = sock;
        fd[fd[sock].ftp_data_fd].state       = STATE_FTP_DATA_READY;
        if (verbose) {
          printf("ftp PORT %d <-> %d\n", sock, fd[sock].ftp_data_fd);
        }
      }
      n = (char *)memchr(buffer, '\n', fd[sock].req_pos);
      if (!n) {
        return 0;
      }
      if (send_ftp_data(sock, buffer + 5 /*, n */) < 0) {
        errors++;
        *n = 0;
        if (verbose) {
          printf("badly formed ftp request: %s\n", buffer);
        }
        return 1;
      }
      fd[sock].response        = fd[sock].req_header;
      fd[sock].length          = sprintf(fd[sock].req_header, "150 %d bytes\r\n", fd[fd[sock].ftp_data_fd].length);
      fd[sock].req_pos         = 0;
      fd[sock].response_length = strlen(fd[sock].req_header);
      poll_set(sock, nullptr, write_ftp_response);
      return 0;
    } else {
      if (verbose || verbose_errors) {
        printf("ftp junk : %s\n", buffer);
      }
      fd[sock].req_pos = 0;
      return 0;
    }
  }
}

static int
accept_sock(int sock)
{
  struct sockaddr_in clientname;
  int size   = sizeof(clientname);
  int new_fd = 0;
  do {
    new_fd = accept(sock, (struct sockaddr *)&clientname,
#if 0
                    &size
#else
                    (socklen_t *)&size
#endif
    );
    if (new_fd < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      if (errno == EINTR || errno == ECONNABORTED) {
        continue;
      }
      printf("accept socket was %d\n", sock);
      panic_perror("accept");
    }
  } while (new_fd < 0);

  if (fcntl(new_fd, F_SETFL, O_NONBLOCK) < 0) {
    panic_perror("fcntl");
  }

#if 0
#ifdef BUFSIZE //  make default
  int bufsize = BUFSIZE;
  if (setsockopt(new_fd,SOL_SOCKET,SO_SNDBUF,
                 (const char *)&bufsize,sizeof(bufsize)) < 0) {
    perror("setsockopt");
  }
  if (setsockopt(new_fd,SOL_SOCKET,SO_SNDBUF,
                 (const char *)&bufsize,sizeof(bufsize)) < 0) {
    perror("setsockopt");
  }
#endif
#endif
  int enable = 1;
  if (setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&enable, sizeof(enable)) < 0) {
    perror("setsockopt");
  }
#ifdef PRINT_LOCAL_PORT
  struct sockaddr_in local_sa;
  size = sizeof(local_sa);
  getsockname(new_fd, (struct sockaddr *)&local_sa, &size);
  printf("local_sa.sin_port = %d\n", local_sa.sin_port);
#endif
  return new_fd;
}

static int
accept_compd(int sock)
{
  int new_fd = accept_sock(sock);
  servers++;
  new_servers++;
  poll_init_set(new_fd, nullptr, read_compd_request);
  fd[new_fd].count     = &servers;
  fd[new_fd].start     = now;
  fd[new_fd].ready     = now + server_delay * HRTIME_MSECOND;
  fd[new_fd].keepalive = server_keepalive ? server_keepalive : INT_MAX;

  return 0;
}

static int
accept_read(int sock)
{
  int new_fd = accept_sock(sock);
  servers++;
  new_servers++;
  if (ftp) {
    poll_init_set(new_fd, nullptr, write_ftp_response);
    make_response(new_fd, 220);
  } else {
    poll_init_set(new_fd, read_request);
  }
  fd[new_fd].count     = &servers;
  fd[new_fd].start     = now;
  fd[new_fd].ready     = now + server_delay * HRTIME_MSECOND;
  fd[new_fd].keepalive = server_keepalive ? server_keepalive : INT_MAX;

  return 0;
}

static int
accept_ftp_data(int sock)
{
  int new_fd = accept_sock(sock);
  servers++;
  new_servers++;
  poll_init(new_fd);
  fd[new_fd].ftp_data_fd               = fd[sock].ftp_data_fd;
  fd[fd[sock].ftp_data_fd].ftp_data_fd = new_fd;
  fd[new_fd].state                     = STATE_FTP_DATA_READY;
  fd[new_fd].count                     = &servers;
  fd[new_fd].start                     = now;
  fd[new_fd].ready                     = now + server_delay * HRTIME_MSECOND;
  fd[new_fd].keepalive                 = server_keepalive ? server_keepalive : INT_MAX;
  fd[new_fd].state                     = STATE_FTP_DATA_READY;
  fd[new_fd].doc                       = fd[sock].doc;
  fd[new_fd].doc_length                = fd[sock].doc_length;
  if (verbose) {
    printf("accept_ftp_data %d for %d\n", new_fd, sock);
  }
  send_ftp_data_when_ready(new_fd);
  return 1;
}

static int
open_server(unsigned short int port, accept_fn_t accept_fn)
{
  struct linger lngr;
  int sock;
  int one = 1;
  int err = 0;

  /* Create the socket. */
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  struct sockaddr_in &name = fd[sock].name;

  /* Give the socket a name. */
  name.sin_family      = AF_INET;
  name.sin_port        = htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) < 0) {
    perror((char *)"setsockopt");
    exit(EXIT_FAILURE);
  }
  if ((err = bind(sock, (struct sockaddr *)&name, sizeof(name))) < 0) {
    if (errno == EADDRINUSE) {
      close(sock);
      return -EADDRINUSE;
    }
    perror("bind");
    exit(EXIT_FAILURE);
  }

  int addrlen = sizeof(name);
  if ((err = getsockname(sock, (struct sockaddr *)&name,
#if 0
                         &addrlen
#else
                         (socklen_t *)&addrlen
#endif
                         )) < 0) {
    perror("getsockname");
    exit(EXIT_FAILURE);
  }
  ink_assert(addrlen);

  /* Tell the socket not to linger on exit */
  lngr.l_onoff  = 0;
  lngr.l_linger = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&lngr, sizeof(struct linger)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  if (listen(sock, 1024) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  /* put the socket in non-blocking mode */
  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl");
    exit(EXIT_FAILURE);
  }

  if (verbose) {
    printf("opening server on %d port %d\n", sock, name.sin_port);
  }

  poll_init_set(sock, accept_fn);

  return sock;
}

//   perform poll and invoke callbacks on active descriptors
static int
poll_loop()
{
  if (server_fd > 0) {
    while (read_ready(server_fd) > 0) {
      accept_read(server_fd);
    }
  }
  pollfd pfd[POLL_GROUP_SIZE];
  int ip = 0;
  now    = ink_get_hrtime_internal();
  for (int i = 0; i <= last_fd; i++) {
    if (fd[i].fd > 0 && (!fd[i].ready || now >= fd[i].ready)) {
      pfd[ip].fd      = i;
      pfd[ip].events  = 0;
      pfd[ip].revents = 0;
      if (fd[i].read_cb) {
        pfd[ip].events |= POLLIN;
      }
      if (fd[i].write_cb) {
        pfd[ip].events |= POLLOUT;
      }
      ip++;
    }
    if (ip >= POLL_GROUP_SIZE || i == last_fd) {
      int n = poll(pfd, ip, POLL_TIMEOUT);
      if (n > 0) {
        for (int j = 0; j < ip; j++) {
          if (pfd[j].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
            if (verbose) {
              printf("poll read %d %X\n", pfd[j].fd, pfd[j].revents);
            }
            if (fd[pfd[j].fd].read_cb && fd[pfd[j].fd].read_cb(pfd[j].fd)) {
              fd[pfd[j].fd].close();
              continue;
            }
          }
          if (pfd[j].revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL)) {
            if (verbose) {
              printf("poll write %d %X\n", pfd[j].fd, pfd[j].revents);
            }
            if (fd[pfd[j].fd].write_cb && fd[pfd[j].fd].write_cb(pfd[j].fd)) {
              fd[pfd[j].fd].close();
              continue;
            }
          }
        }
      }
      ip = 0;
    }
  }
  return 0;
}

static int
gen_bfc_dist(double f = 10.0)
{
  if (docsize >= 0) {
    return docsize;
  }

  double rand  = 0.0;
  double rand2 = 0.0;
  bool f_given = f < 9.0;
  if (!f_given) {
    // coverity[dont_call]
    rand = drand48();
    // coverity[dont_call]
    rand2 = drand48();
  } else {
    rand  = f;
    rand2 = (f * 13.0) - floor(f * 13.0);
  }

  int class_no;
  int file_no = 0;

  if (rand < 0.35) {
    class_no = 0;
  } else if (rand < 0.85) {
    class_no = 1;
  } else if (rand < 0.99) {
    class_no = 2;
  } else {
    class_no = 3;
    if (f_given) {
      rand2 = (f * 113.0) - floor(f * 113.0);
    }
  }

  if (rand2 < 0.018) {
    file_no = 0;
  } else if (rand2 < 0.091) {
    file_no = 1;
  } else if (rand2 < 0.237) {
    file_no = 2;
  } else if (rand2 < 0.432) {
    file_no = 3;
  } else if (rand2 < 0.627) {
    file_no = 4;
  } else if (rand2 < 0.783) {
    file_no = 5;
  } else if (rand2 < 0.887) {
    file_no = 6;
  } else if (rand2 < 0.945) {
    file_no = 7;
  } else if (rand2 < 1.000) {
    file_no = 8;
  }
  int size = 100;
  int i;
  for (i = 0; i < class_no; i++) {
    size = size * 10;
  }
  int increment = size;
  size          = size * (file_no + 1);
  // vary about the mean doc size for
  // that class/size
  if (!f_given) {
    // coverity[dont_call]
    size += (int)((-increment * 0.5) + (increment * drand48()));
  }
  if (verbose) {
    printf("gen_bfc_dist %d\n", size);
  }
  return size;
}

static void
build_response()
{
  int maxsize     = docsize > MAX_RESPONSE_LENGTH ? docsize : MAX_RESPONSE_LENGTH;
  response_buffer = (char *)malloc(maxsize + HEADER_SIZE);
  for (int i = 0; i < maxsize + HEADER_SIZE; i++) {
    response_buffer[i] = i % 256;
  }
}

static void
put_ka(int sock)
{
  int i = 0;
  for (; i < n_ka_cache; i++) {
    if (!ka_cache_head[i] || fd[ka_cache_head[i]].ip == fd[sock].ip) {
      goto Lpush;
    }
  }
  i = n_ka_cache++;
Lpush:
  if (ka_cache_tail[i]) {
    fd[ka_cache_tail[i]].next = sock;
  } else {
    ka_cache_head[i] = sock;
  }
  ka_cache_tail[i] = sock;
}

static int
get_ka(unsigned int ip)
{
  for (int i = 0; i < n_ka_cache; i++) {
    if (fd[ka_cache_head[i]].ip == ip) {
      int res          = ka_cache_head[i];
      ka_cache_head[i] = fd[ka_cache_head[i]].next;
      if (res == ka_cache_tail[i]) {
        ink_assert(!ka_cache_head[i]);
        ka_cache_tail[i] = 0;
      }
      return res;
    }
  }
  return -1;
}

static void
defer_url(char *url)
{
  if (n_defered_urls < MAX_DEFERED_URLS - 1) {
    defered_urls[n_defered_urls++] = strdup(url);
  } else {
    fprintf(stderr, "too many defered urls, dropping '%s'\n", url);
  }
}

static int
throttling_connections()
{
  return client_rate && keepalive_cons && current_clients >= keepalive_cons;
}

static void
done()
{
  interval_report();
  exit(0);
}

static int
is_done()
{
  return (urls_mode && !current_clients && !n_defered_urls) || (bandwidth_test && bandwidth_test_to_go <= 0 && !current_clients);
}

static void
undefer_url(bool unthrottled)
{
  if ((unthrottled || !throttling_connections()) && n_defered_urls) {
    --n_defered_urls;
    char *url = defered_urls[n_defered_urls];
    make_url_client(url, 0, true, unthrottled);
    free(url);
    if (verbose) {
      printf("undefer_url: made client %d clients\n", current_clients);
    }
  } else if (verbose) {
    printf("undefer_url: throttle\n");
  }
  if (is_done()) {
    done();
  }
}

static void
init_client(int sock)
{
  poll_init(sock);
  fd[sock].start = now;
  fd[sock].ready = now;
  fd[sock].count = &clients;
  poll_set(sock, nullptr, write_request);
}

static unsigned int
get_addr(const char *host)
{
  unsigned int addr         = inet_addr(host);
  struct hostent *host_info = nullptr;

  if (!addr || (-1 == (int)addr)) {
    host_info = gethostbyname(host);
    if (!host_info) {
      printf("gethostbyname(%s): %s\n", host, hstrerror(h_errno));
      return (unsigned int)-1;
    }
    addr = *((unsigned int *)host_info->h_addr);
  }

  return addr;
}

static char *
find_href_end(char *start, int len)
{
  char *end = start;
  if (!start) {
    return nullptr;
  }

  while (*end && len > 0) {
    if (*end == '\"') {
      break; /* " */
    }
    if (*end == '\'') {
      break;
    }
    if (*end == '>') {
      break;
    }
    if (*end == ' ') {
      break;
    }
    if (*end == '\t') {
      break;
    }
    if (*end == '\n') {
      break;
    }
    if (*end == '<') {
      break;
    }
    if (*end & 0x80) {
      break; /* hi order bit! */
    }
    len--;
    end++;
  }

  if (*end == 0 || len == 0) {
    return nullptr;
  } else {
    return end;
  }
} // find_href_end

static char *
find_href_start(const char *tag, char *base, int len)
{
  int taglen = strlen(tag);
  if (base == nullptr) {
    return nullptr;
  }

  char *start = base;
  char *end   = base + len;

Lagain : {
  start = strncasestr(start, tag, len);
  if ((start == nullptr) || (end - start < 6)) {
    return nullptr;
  }
  start += taglen;
  len -= taglen;
} // block

  while (ParseRules::is_ws(*start) && (end - start > 1)) {
    start++;
    len--;
  }
  if (*start == '=' && (end - start > 1)) {
    start++;
    len--;
  } else {
    goto Lagain;
  }
  while (ParseRules::is_ws(*start) && (end - start > 1)) {
    start++;
    len--;
  }
  //
  // Optional quotes:  href="x" or href='x' or href=x
  //
  if ((*start == '\"' || (*start == '\'')) && (end - start > 1)) { /*"'*/
    start++;
    len--;
  }
  while (ParseRules::is_ws(*start) && (end - start > 1)) {
    start++;
    len--;
  }

  return start;
} // find_href_start

static int
compose_url(char *new_url, char *base, char *input)
{
  char sche[8], host[512], port[10], path[512], frag[512], quer[512], para[512];
  char curl[512];
  int xsche, xhost, xport, xpath, xfrag, xquer, xpar, rel, slash;
  ink_web_decompose_url(base, sche, host, port, path, frag, quer, para, &xsche, &xhost, &xport, &xpath, &xfrag, &xquer, &xpar, &rel,
                        &slash);
  strcpy(curl, "http://");
  strcat(curl, host);
  if (xport) {
    strcat(curl, ":");
    strcat(curl, port);
  }
  strcat(curl, "/");
  strcat(curl, path);

  ink_web_canonicalize_url(curl, input, new_url, 512);
  return 0;
} // compose_urls

static void
compose_all_urls(const char *tag, char *buf, char *start, char *end, int buflen, char *base_url)
{
  char old;
  while ((start = find_href_start(tag, end, buflen - (end - buf)))) {
    char newurl[512];
    end = (char *)find_href_end(start, std::min(static_cast<int>(buflen - (start - buf)), 512 - 10));
    if (!end) {
      end = start + strlen(tag);
      continue;
    }
    old  = *end;
    *end = 0;
    compose_url(newurl, base_url, start);
    make_url_client(newurl, base_url);
    *end = old;
  } // while
}
//
// Input is a nullptr-terminated string (buf of buflen)
//       also, a read-write base_url
//
static void
extract_urls(char *buf, int buflen, char *base_url)
{
  // if (verbose) printf("EXTRACT<<%s\n>>", buf);
  char *start        = nullptr;
  char *end          = nullptr;
  char old_base[512] = {0};
  strncpy(old_base, base_url, sizeof(old_base) - 1);

  start = strncasestr(buf, "<base ", buflen);
  if (start) {
    end = (char *)memchr(start, '>', buflen - (start - buf));
    if (end) {
      char *rover = strncasestr(start, "href", end - start);
      if (rover) {
        rover += 4;
        while (rover < end && (ParseRules::is_ws(*rover) || *rover == '=' || *rover == '\'' || *rover == '\"')) { /* " */
          rover++;
        }
        start = rover;
        while (rover < end && !(ParseRules::is_ws(*rover) || *rover == '\'' || *rover == '\"')) {
          rover++;
        }
        *rover = 0;
        compose_url(base_url, old_base, start);
        // fixup unqualified hostnames (e.g. http://internal/foo)
        char *he = strchr(base_url + 8, '/');
        if (!memchr(base_url, '.', he - base_url)) {
          char t[512] = {0};
          strncpy(t, base_url, sizeof(t) - 1);
          char *old_he = strchr(old_base + 8, '.');
          if (old_he) {
            char *old_hee = strchr(old_he, '/');
            if (old_hee) {
              memcpy(base_url, t, (he - base_url));
              memcpy(base_url + (he - base_url), old_he, (old_hee - old_he));
              memcpy(base_url + (he - base_url) + (old_hee - old_he), t + (he - base_url), strlen(t + (he - base_url)));
              base_url[(he - base_url) + (old_hee - old_he) + strlen(t + (he - base_url))] = 0;
            }
          }
        }
      }
    }
  }

  end = buf;
  if (follow) {
    compose_all_urls("href", buf, start, end, buflen, base_url);
  }
  if (fullpage) {
    const char *tags[] = {
      "src", "image", "object", "archive", "background",
      // "location", "code"
    };
    for (unsigned i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
      compose_all_urls(tags[i], buf, start, end, buflen, base_url);
    }
  }
} // extract_urls

static void
follow_links(int sock)
{
  if (urls_mode) {
    if (fd[sock].binary) {
      return;
    }
    int l   = fd[sock].response_remaining;
    char *r = fd[sock].response, *p = r, *n = r;
    if (r) {
      extract_urls(r, l, fd[sock].base_url);
    }
    if (l < MAX_BUFSIZE) {
      while (n) {
        n = (char *)memchr(p, '\n', l - (p - r));
        if (!n) {
          n = (char *)memchr(p, '\r', l - (p - r));
        }
        if (n) {
          p = n + 1;
        }
      }
      int done = p - r, remaining = l - done;
      if (done) {
        memmove(r, p, remaining);
        fd[sock].response_remaining = remaining;
      }
    } else { // bail
      fd[sock].response_length = 0;
    }
  }
}

static int
verify_content(int sock, char *buf, int done)
{
  if ((urls_mode && !check_content) || range_mode) {
    return 1;
  }
  int l    = fd[sock].response_length;
  char *d  = response_buffer + (l % 256) + fd[sock].nalternate;
  int left = fd[sock].length;
  if (left > 0) {
    if (embed_url && !fd[sock].jg_compressed) {
      if (l == left && left > 64) {
        char *url_end = nullptr, *url_start = nullptr;
        get_path_from_req(fd[sock].base_url, &url_start, &url_end);
        if (url_end - url_start < done) {
          if (memcmp(url_start, buf, url_end - url_start)) {
            return 0;
          }
        }
      }
      // skip past the URL which is embedded in the document
      // to confound the fingerprinting code
      if (l - left < 64) {
        int skip = 64 - (l - left);
        left -= skip;
        done -= skip;
        buf += skip;
        if (done < 0) {
          done = 0;
        }
      }
    }
    if (!check_content) {
      return 1;
    }
    if (done > left) {
      done = left;
    }
    if (memcmp(buf, d + (fd[sock].response_length - left), done)) {
      return 0;
    }
  }
  return 1;
}

#define ZIPF_SIZE (1 << 20)
static double *zipf_table = nullptr;
static void
build_zipf()
{
  zipf_table = (double *)malloc(ZIPF_SIZE * sizeof(double));
  for (int i = 0; i < ZIPF_SIZE; i++) {
    zipf_table[i] = 1.0 / pow(i + 2, zipf);
  }
  for (int i = 1; i < ZIPF_SIZE; i++) {
    zipf_table[i] = zipf_table[i - 1] + zipf_table[i];
  }
  double x = zipf_table[ZIPF_SIZE - 1];
  for (int i = 0; i < ZIPF_SIZE; i++) {
    zipf_table[i] = zipf_table[i] / x;
  }
}

static int
get_zipf(double v)
{
  int l = 0, r = ZIPF_SIZE - 1, m;
  do {
    m = (r + l) / 2;
    if (v < zipf_table[m]) {
      r = m - 1;
    } else {
      l = m + 1;
    }
  } while (l < r);
  if (zipf_bucket_size == 1) {
    return m;
  }
  double x = zipf_table[m], y = zipf_table[m + 1];
  m += static_cast<int>((v - x) / (y - x));
  return m;
}

static int
read_response_error(int sock)
{
  errors++;
  fd[sock].close();
  if (!urls_mode) {
    make_bfc_client(proxy_addr, proxy_port);
  }
  return 0;
}

static int
read_response(int sock)
{
  int err = 0;

  if (fd[sock].req_pos >= 0) {
    if (!fd[sock].req_pos) {
      memset(fd[sock].req_header, 0, HEADER_SIZE);
    }
    do {
      int l = HEADER_SIZE - fd[sock].req_pos - 1;
      if (l <= 0) {
        if (verbose || verbose_errors) {
          // coverity[string_null_argument]
          printf("header too long '%s'", fd[sock].req_header);
        }
        return read_response_error(sock);
      }
      err = read(sock, fd[sock].req_header + fd[sock].req_pos, HEADER_SIZE - fd[sock].req_pos - 1);
    } while ((err == -1) && (errno == EINTR));
    if (err <= 0) {
      if (!err) {
        if (verbose_errors) {
          printf("read_response %d closed during header for '%s' after %d%s\n", sock, fd[sock].base_url, fd[sock].req_pos,
                 (keepalive && (fd[sock].keepalive != keepalive) && !fd[sock].req_pos) ? " -- keepalive timeout" : "");
        }
        return read_response_error(sock);
      }
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      if (errno == ECONNRESET) {
        if (!fd[sock].req_pos && keepalive > 0 && fd[sock].keepalive != keepalive) {
          fd[sock].close();
          if (!urls_mode) {
            make_bfc_client(proxy_addr, proxy_port);
          }
          return 0;
        }
        if (verbose || verbose_errors) {
          perror("read");
        }
        goto Ldone;
      }
      panic_perror("read");
    }
    if (verbose) {
      printf("read %d header %d [%s]\n", sock, err, fd[sock].req_header);
    }
    b1_ops++;

    strcpy(fd[sock].response_header, fd[sock].req_header);

    b1latency += (int)elapsed_from_start(sock);
    new_cbytes += err;
    new_tbytes += err;
    fd[sock].req_pos += err;
    fd[sock].bytes += err;
    fd[sock].active = ink_get_hrtime_internal();
    int total_read  = fd[sock].req_pos;
    char *p         = fd[sock].req_header;
    char *cl        = nullptr;
    int cli         = 0;
    while ((p = strchr(p, '\n'))) {
      if (verbose) {
        printf("read header end? [%s]\n", p);
      }
      if (p[1] == '\n' || (p[1] == '\r' && p[2] == '\n')) {
        int off = 1 + (p[1] == '\r' ? 2 : 1);
        p += off;
        strncpy(fd[sock].response_header, fd[sock].req_header, p - fd[sock].req_header);
        fd[sock].response_header[p - fd[sock].req_header] = '\0';
        int lbody                                         = fd[sock].req_pos - (p - fd[sock].req_header);
        cl = strncasestr(fd[sock].req_header, "Content-Length:", p - fd[sock].req_header);
        if (cl) {
          cli                 = atoi(cl + 16);
          int expected_length = fd[sock].response_length;
          if (compd_suite) {
            if (strstr(fd[sock].req_header, "x-jg")) {
              fd[sock].jg_compressed = 1;
              expected_length        = (fd[sock].response_length * 2) / 3;
            }
          }
          if (fd[sock].response_length && verbose_errors && expected_length != cli && !nocheck_length) {
            fprintf(stderr, "bad Content-Length expected %d got %d orig %d\n", expected_length, cli, fd[sock].response_length);
          }
          fd[sock].response_length = fd[sock].length = cli;
        }
        if (fd[sock].req_header[9] == '2') {
          if (!verify_content(sock, p, lbody)) {
            if (verbose || verbose_errors) {
              printf("content verification error '%s'\n", fd[sock].base_url);
            }
            return read_response_error(sock);
          }
        }
        total_proxy_response_body_bytes += lbody;
        total_proxy_response_header_bytes += p - fd[sock].req_header;
        fd[sock].length -= lbody;
        ink_assert(fd[sock].length >= 0);
        fd[sock].req_pos = -1;
        // coverity[dont_call]
        if (fd[sock].length && drand48() < client_abort_rate) {
          fd[sock].client_abort = 1;
          // coverity[dont_call]
          fd[sock].length        = (int)(drand48() * (fd[sock].length - 1));
          fd[sock].keepalive     = 0;
          fd[sock].drop_after_CL = 1;
        }
        if (verbose) {
          printf("read %d header done\n", sock);
        }
        break;
      }
      p++;
    }
    if (!p) {
      return 0;
    }
    int hlen = p - fd[sock].req_header;
    if (show_headers) {
      printf("Response From Proxy: {\n");
      for (char *c = fd[sock].req_header; c < p; c++) {
        putc(*c, stdout);
      }
      printf("}\n");
    }
    if (obey_redirects && urls_mode && fd[sock].req_header[9] == '3' && fd[sock].req_header[10] == '0' &&
        (fd[sock].req_header[11] == '1' || fd[sock].req_header[11] == '2')) {
      char *redirect = strstr(fd[sock].req_header, "http://");
      char *e        = redirect ? (char *)memchr(redirect, '\n', hlen) : 0;
      if (!redirect || !e) {
        fprintf(stderr, "bad redirect '%s'", fd[sock].req_header);
      } else {
        if (e[-1] == '\r') {
          e--;
        }
        *e = 0;
        make_url_client(redirect);
      }
      fd[sock].close();
      return 0;
    }
    if (fd[sock].req_header[9] != '2') {
      if (verbose_errors) {
        char *e = (char *)memchr(fd[sock].req_header, '\r', hlen);
        if (e) {
          *e = 0;
        } else {
          char *e = (char *)memchr(fd[sock].req_header, '\n', hlen);
          if (e) {
            *e = 0;
          } else {
            *p = 0;
          }
        }
        printf("error response %d after %dms: '%s':'%s' %lu-%lu\n", sock, (int)elapsed_from_start(sock), fd[sock].base_url,
               fd[sock].req_header, fd[sock].range_start, fd[sock].range_end);
      }
      return read_response_error(sock);
    }
    char *r    = fd[sock].req_header;
    int length = p - r;
    char *ka   = check_keepalive(r, length);
    if (urls_mode) {
      fd[sock].response_remaining = total_read - length;
      if (fd[sock].response_remaining) {
        memcpy(fd[sock].response, p, fd[sock].response_remaining);
      }
      if (check_content && !cl) {
        if (verbose || verbose_errors) {
          printf("missiing Content-Length '%s'\n", fd[sock].base_url);
        }
        return read_response_error(sock);
      }
    } else {
      fd[sock].response = 0;
    }
    if (!cl || !ka) {
      fd[sock].keepalive = -1;
    }
    if (!cl) {
      fd[sock].length = INT_MAX;
    }
  }

  if (fd[sock].length <= 0 && (fd[sock].keepalive > 0 || fd[sock].drop_after_CL)) {
    goto Ldone;
  }

  {
    char *r = nullptr;
    char buf[MAX_BUFSIZE];
    int toread = cbuffersize;
    if (urls_mode) {
      if (fd[sock].response_remaining + cbuffersize < MAX_BUFSIZE) {
        r = fd[sock].response + fd[sock].response_remaining;
      } else {
        toread = MAX_BUFSIZE - fd[sock].response_remaining;
        if (!toread) {
          if (verbose_errors || verbose) {
            fprintf(stderr, "line exceeds buffer, unable to follow links\n");
          }
          toread                      = cbuffersize;
          r                           = fd[sock].response;
          fd[sock].response_remaining = 0;
        } else {
          r = fd[sock].response + fd[sock].response_remaining;
        }
      }
    } else {
      r = buf;
    }
    if (fast(sock, client_speed, fd[sock].bytes)) {
      return 0;
    }
    if (fd[sock].bytes > abort_retry_bytes && (((now - fd[sock].start + 1) / HRTIME_SECOND) > abort_retry_secs) &&
        !faster_than(sock, abort_retry_speed, fd[sock].bytes)) {
      fd[sock].client_abort = 1;
      fd[sock].keepalive    = 0;
      if (!urls_mode && !client_rate) {
        make_bfc_client(proxy_addr, proxy_port);
      }
      goto Ldone;
    }
    do {
      err = read(sock, r, toread);
    } while ((err == -1) && (errno == EINTR));
    if (err < 0) {
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      if (errno == ECONNRESET) {
        if (verbose || verbose_errors) {
          perror("read");
        }
        goto Ldone;
      }
      panic_perror("read");
    }
    if (!err) {
      goto Ldone;
    }
    if (!verify_content(sock, buf, err)) {
      if (verbose || verbose_errors) {
        printf("content verification error '%s'\n", fd[sock].base_url);
      }
      return read_response_error(sock);
    }
    total_proxy_response_body_bytes += err;
    new_cbytes += err;
    new_tbytes += err;
    fd[sock].response_remaining += err;
    fd[sock].bytes += err;
    follow_links(sock);
    if (fd[sock].length != INT_MAX) {
      fd[sock].length -= err;
    }
    fd[sock].active = ink_get_hrtime_internal();
    if (verbose) {
      printf("read %d got %d togo %d %d %d\n", sock, err, fd[sock].length, fd[sock].keepalive, fd[sock].drop_after_CL);
    }
  }

  if (fd[sock].length <= 0 && (fd[sock].keepalive > 0 || fd[sock].drop_after_CL)) {
    goto Ldone;
  }

  return 0;

Ldone:
  if (!fd[sock].client_abort && !(server_abort_rate > 0) && fd[sock].length && fd[sock].length != INT_MAX) {
    if (verbose || verbose_errors) {
      printf("bad length %d wanted %d after %d ms: '%s'\n", fd[sock].response_length - fd[sock].length, fd[sock].response_length,
             (int)((ink_get_hrtime_internal() - fd[sock].active) / HRTIME_MSECOND), fd[sock].base_url);
    }
    return read_response_error(sock);
  }
  if (verbose) {
    printf("read %d done\n", sock);
  }
  new_ops++;
  double thislatency = elapsed_from_start(sock);
  latency += (int)thislatency;
  lat_ops++;
  if (fd[sock].keepalive > 0) {
    fd[sock].reset();
    put_ka(sock);
    current_clients--;
    if (urls_mode) {
      undefer_url();
      return 0;
    }
  } else {
    fd[sock].close();
  }
  if (!urls_mode && !client_rate) {
    make_bfc_client(proxy_addr, proxy_port);
  }
  return 0;
}

static int
write_request(int sock)
{
  int err = 0;

  // send request header
  if (!fd[sock].send_header) {
    do {
      err = write(sock, fd[sock].req_header + fd[sock].req_pos, fd[sock].length - fd[sock].req_pos);
    } while ((err == -1) && (errno == EINTR));
    if (err <= 0) {
      if (!err) {
        if (verbose_errors) {
          printf("write %d closed early\n", sock);
        }
        goto Lerror;
      }
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      perror("write");
      goto Lerror;
    }
    if (verbose) {
      printf("write %d %d\n", sock, err);
    }

    new_tbytes += err;
    total_client_request_bytes += err;
    fd[sock].req_pos += err;
    fd[sock].active = ink_get_hrtime_internal();

    if (fd[sock].req_pos >= fd[sock].length) {
      if (verbose) {
        printf("write request header complete %d %d\n", sock, fd[sock].length);
      }
      fd[sock].req_pos = 0;
      fd[sock].length  = fd[sock].response_length;
      if (!post_support || !fd[sock].post_size) {
        poll_set(sock, read_response);
        return 0;
      }
      fd[sock].send_header = 1;
    }
  }

  // send request body
  ink_assert(MAX_RESPONSE_LENGTH > fd[sock].post_size);

  if (fd[sock].send_header) {
    do {
      err = write(sock, response_buffer + fd[sock].req_pos, fd[sock].post_size - fd[sock].req_pos);
    } while ((err == -1) && (errno == EINTR));
    if (err <= 0) {
      if (!err) {
        if (verbose_errors) {
          printf("write %d closed early\n", sock);
        }
        goto Lerror;
      }
      if (errno == EAGAIN || errno == ENOTCONN) {
        return 0;
      }
      perror("write");
      goto Lerror;
    }
    if (verbose) {
      printf("write %d %d\n", sock, err);
    }

    new_tbytes += err;
    total_client_request_bytes += err;
    fd[sock].req_pos += err;
    fd[sock].active = ink_get_hrtime_internal();

    if (fd[sock].req_pos >= fd[sock].post_size) {
      if (verbose) {
        printf("write request body complete %d %d\n", sock, fd[sock].length);
      }
      fd[sock].send_header = 0;
      fd[sock].req_pos     = 0;
      fd[sock].length      = fd[sock].response_length;
      poll_set(sock, read_response);
    }
  }
  return 0;
Lerror:
  errors++;
#ifndef RETRY_CLIENT_WRITE_ERRORS
  if (!--nclients) {
    panic("no more clients\n");
  }
  return 1;
#else
  if (!urls_mode)
    make_bfc_client(proxy_host, proxy_port);
  fd[sock].close();
  return 0;
#endif
}

static int
write_ftp_response(int sock)
{
  int err = 0;

  do {
    err = write(sock, fd[sock].req_header + fd[sock].req_pos, fd[sock].length - fd[sock].req_pos);
  } while ((err == -1) && (errno == EINTR));

  if (err <= 0) {
    if (!err) {
      if (verbose_errors) {
        printf("write %d closed early\n", sock);
      }
      goto Lerror;
    }
    if (errno == EAGAIN || errno == ENOTCONN) {
      return 0;
    }
    perror("write");
    goto Lerror;
  }
  if (verbose) {
    printf("write %d %d\n", sock, err);
  }

  new_tbytes += err;
  fd[sock].req_pos += err;

  if (fd[sock].req_pos >= fd[sock].length) {
    if (verbose) {
      printf("write complete %d %d\n", sock, fd[sock].length);
    }
    fd[sock].req_pos = 0;
    fd[sock].length  = fd[sock].response_length;
    poll_set(sock, read_ftp_request);
  }
  return 0;
Lerror:
  errors++;
  return 1;
}

static int
make_client(unsigned int addr, int port)
{
  struct linger lngr;

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    panic_perror("socket");
  }

  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    panic_perror("fcntl");
  }

  /* tweak buffer size so that remote end can't close connection too fast */

#if 0
  int bufsize = cbuffersize;
  if (setsockopt(sock,SOL_SOCKET,SO_RCVBUF,
                 (const char *)&bufsize,sizeof(bufsize)) < 0)
    panic_perror("setsockopt");
  if (setsockopt(sock,SOL_SOCKET,SO_SNDBUF,
                 (const char *)&bufsize,sizeof(bufsize)) < 0)
    panic_perror("setsockopt");
#endif
  int enable = 1;
  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&enable, sizeof(enable)) < 0) {
    panic_perror("setsockopt");
  }

  /* Tell the socket not to linger on exit */
  lngr.l_onoff  = 1;
  lngr.l_linger = 0;
  if (!ftp) { // this causes problems for PORT ftp -- ewong
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&lngr, sizeof(struct linger)) < 0) {
      perror("setsockopt");
      exit(EXIT_FAILURE);
    }
  }

  /* Give the socket a name. */
  struct sockaddr_in name;
  memset(&name, 0, sizeof(sockaddr_in));
  name.sin_family      = AF_INET;
  name.sin_port        = htons(port);
  name.sin_addr.s_addr = addr;

  if (verbose) {
    printf("connecting to %u.%u.%u.%u:%d\n", ((unsigned char *)&addr)[0], ((unsigned char *)&addr)[1], ((unsigned char *)&addr)[2],
           ((unsigned char *)&addr)[3], port);
  }

  while (connect(sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
    if (errno == EINTR) {
      continue;
    }
    if (errno == EINPROGRESS) {
      break;
    }
    if (verbose_errors) {
      fprintf(stderr, "connect failed errno = %d\n", errno);
    }
    errors++;
    close(sock);
    return -1;
  }

  init_client(sock);
  fd[sock].ip = addr;
  clients++;
  current_clients++;
  new_clients++;
  return sock;
}

static void
make_range_header(int sock, double dr, char *rbuf, int size_limit)
{
  int tmp[3];

  if (!rbuf || !size_limit)
    return;

  tmp[0] = gen_bfc_dist(dr - 1.0);
  // coverity[dont_call]
  tmp[1] = ((int)(drand48() * 1000000)) % (tmp[0] - 1 - 0 + 1);
  // coverity[dont_call]
  tmp[2] = ((int)(drand48() * 1000000)) % (tmp[0] - 1 - 0 + 1) + tmp[1] + 100;

  if (tmp[0] > 100) {
    if (tmp[0] <= tmp[2]) {
      tmp[2] = tmp[0] - 1;
    }

    if (tmp[2] - tmp[1] < 100) {
      tmp[1] = tmp[2] - 100;
    }
  } else {
    tmp[1] = 0;
    tmp[2] = 99;
  }

  fd[sock].response_length = tmp[0];
  fd[sock].range_start     = tmp[1] > tmp[2] ? tmp[2] : tmp[1];
  fd[sock].range_end       = tmp[1] < tmp[2] ? tmp[2] : tmp[1];

  ink_assert((int)(fd[sock].range_end - fd[sock].range_start + 1) >= 100);
  snprintf(rbuf, size_limit, "Range: bytes=%lu-%lu\r\n", fd[sock].range_start, fd[sock].range_end);
}

static void
make_random_url(int sock, double *dr, double *h)
{
  // coverity[dont_call]
  *dr = drand48();
  // coverity[dont_call]
  *h = drand48();

  if (zipf == 0.0) {
    if (*h < hitrate) {
      *dr                      = 1.0 + (floor(*dr * hotset) / hotset);
      fd[sock].response_length = gen_bfc_dist(*dr - 1.0);
    } else
      fd[sock].response_length = gen_bfc_dist(*dr);
  } else {
    unsigned long long int doc = get_zipf(*dr);
    // Some large randomish number.
    unsigned long long int doc_len_int = doc * 0x14A4D0FB0E93E3A7LL;
    unsigned long int x                = doc_len_int;
    double y                           = (double)x;
    y /= 0x100000000LL; // deterministic random number between 0 and 1.0
    fd[sock].response_length = gen_bfc_dist(y);
    *dr                      = doc;
    range_mode               = 0;
  }
}

static int
make_nohost_request(int sock, double dr, const char *evo_str, const char *extension, const char *eheaders, const char *rbuf,
                    const char *cookie)
{
  int post_length = 0;

  switch (post_support) {
  case 0:
    if (range_mode) {
      sprintf(fd[sock].req_header,
              "GET http://%s:%d/%12.10f/%d%s%s HTTP/1.1\r\n"
              "%s"
              "%s"
              "%s"
              "%s"
              "%s"
              "%s"
              "\r\n",
              local_host, server_port, dr, fd[sock].response_length, evo_str, extension,
              fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "Connection: close\r\n",
              // coverity[dont_call]
              reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, "Host: localhost\r\n", rbuf, cookie);
    } else {
      sprintf(fd[sock].req_header,
              ftp ? "GET ftp://%s:%d/%12.10f/%d%s%s HTTP/1.0\r\n"
                    "%s"
                    "%s"
                    "%s"
                    "%s"
                    "\r\n" :
                    "GET http://%s:%d/%12.10f/%d%s%s HTTP/1.0\r\n"
                    "%s"
                    "%s"
                    "%s"
                    "%s"
                    "\r\n",
              local_host, server_port, dr, fd[sock].response_length, evo_str, extension,
              fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "",
              // coverity[dont_call]
              reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, cookie);
    }
    break;
  case 1:
    if (range_mode) {
      sprintf(fd[sock].req_header,
              "POST http://%s:%d/%12.10f/%d%s%s HTTP/1.1\r\n"
              "Content-Length: %d\r\n"
              "%s"
              "%s"
              "%s"
              "%s"
              "%s"
              "%s"
              "\r\n",
              local_host, server_port, dr, fd[sock].response_length, evo_str, extension, fd[sock].response_length,
              fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "Connection: close\r\n",
              // coverity[dont_call]
              reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, "Host: localhost\r\n", rbuf, cookie);
    } else {
      sprintf(fd[sock].req_header,
              "POST http://%s:%d/%12.10f/%d%s%s HTTP/1.0\r\n"
              "Content-Length: %d\r\n"
              "%s"
              "%s"
              "%s"
              "%s"
              "\r\n",
              local_host, server_port, dr, fd[sock].response_length, evo_str, extension, fd[sock].response_length,
              fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "",
              // coverity[dont_call]
              reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, cookie);
    }
    post_length = fd[sock].response_length;
    break;
  case 2:
    if (!post_size)
      ink_assert(!"post_size should never be zero!");

    if (range_mode) {
      sprintf(fd[sock].req_header,
              "POST http://%s:%d/%12.10f/%d%s%s HTTP/1.1\r\n"
              "Content-Length: %d\r\n"
              "%s"
              "%s"
              "%s"
              "%s"
              "%s"
              "%s"
              "\r\n",
              local_host, server_port, dr, fd[sock].response_length, evo_str, extension, post_size,
              fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "Connection: close\r\n",
              // coverity[dont_call]
              reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, "Host: localhost\r\n", rbuf, cookie);
    } else {
      sprintf(fd[sock].req_header,
              "POST http://%s:%d/%12.10f/%d%s%s HTTP/1.0\r\n"
              "Content-Length: %d\r\n"
              "%s"
              "%s"
              "%s"
              "%s"
              "\r\n",
              local_host, server_port, dr, fd[sock].response_length, evo_str, extension, post_size,
              fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "",
              // coverity[dont_call]
              reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, cookie);
    }
    post_length = post_size;
    break;
  }

  return post_length;
}

static int
make_host1_request(int sock, double dr, const char *evo_str, const char *extension, const char *eheaders, const char *cookie)
{
  sprintf(fd[sock].req_header,
          "GET /%12.10f/%d%s%s HTTP/1.0\r\n"
          "Host: %s:%d\r\n"
          "%s"
          "%s"
          "%s"
          "%s"
          "\r\n",
          dr, fd[sock].response_length, evo_str, extension, local_host, server_port,
          fd[sock].keepalive ? "Connection: Keep-Alive\r\n" : "",
          // coverity[dont_call]
          reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, cookie);
  return 0;
}

static int
make_host2_request(int sock, double dr, const char *evo_str, const char *extension, const char *eheaders, const char *cookie)
{
  /* Send a non-proxy client request i.e. for Transparency testing */
  sprintf(fd[sock].req_header,
          "GET /%12.10f/%d%s%s HTTP/1.0\r\n"
          "%s"
          "%s"
          "%s"
          "%s"
          "\r\n",
          dr, fd[sock].response_length, evo_str, extension, fd[sock].keepalive ? "Connection: Keep-Alive\r\n" : "",
          // coverity[dont_call]
          reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", eheaders, cookie);
  return 0;
}

static int
build_request(int sock)
{
  double dr, h;
  char rbuf[1024];

  make_random_url(sock, &dr, &h);

  if (verbose) {
    printf("gen_bfc_dist %d\n", fd[sock].response_length);
  }

  if (range_mode) {
    make_range_header(sock, dr, rbuf, 1024);
  }

  char eheaders[16384];
  *eheaders    = 0;
  int nheaders = extra_headers;
  if (nheaders > 0) {
    char *eh = eheaders;
    if (!vary_user_agent) {
      eh += sprintf(eh, "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.31 i586)\r\n");
      nheaders--;
    }
    if (nheaders > 0) {
      eh += sprintf(eh, "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\n");
    }
    while (--nheaders > 0) {
      eh += sprintf(eh, "Extra-Header%d: a lot of junk for header %d\r\n", nheaders, nheaders);
    }
  }
  char cookie[256];
  *cookie = 0;
  // coverity[dont_call]
  fd[sock].nalternate = (int)(alternates * drand48());
  if (alternates) {
    if (!vary_user_agent) {
      sprintf(cookie, "Cookie: jtest-cookie-%d\r\n", fd[sock].nalternate);
    } else {
      sprintf(cookie, "User-Agent: jtest-browser-%d\r\n", fd[sock].nalternate);
    }
  }
  const char *extension;
  switch (request_extension) {
  case 1:
    extension = ".html";
    break;
  case 2:
    extension = ".jpeg";
    break;
  case 3:
    extension = "/";
    break;
  default:
    extension = (compd_suite ? ".jpeg" : "");
  }

  char evo_str[20];
  evo_str[0] = '\0';
  if (evo_rate != 0.0) {
    double evo_index = dr + (((double)now) / HRTIME_HOUR) * evo_rate;
    sprintf(evo_str, ".%u", ((unsigned int)evo_index));
  }

  int post_body = 0;

  switch (hostrequest) {
  case 0:
    post_body = make_nohost_request(sock, dr, evo_str, extension, eheaders, rbuf, cookie);
    break;
  case 1:
    post_body = make_host1_request(sock, dr, evo_str, extension, eheaders, cookie);
    break;
  case 2:
    post_body = make_host2_request(sock, dr, evo_str, extension, eheaders, cookie);
    break;
  default:
    ink_release_assert(!"Unexpected hostrequest! Abort.");
    return 0;
  }

  if (range_mode) {
    fd[sock].response_length = fd[sock].range_end - fd[sock].range_start + 1;
    ink_assert(fd[sock].response_length > 0);
  }

  return post_body;
}

static void
make_bfc_client(unsigned int addr, int port)
{
  int sock = -1;
  char rbuf[1024];
  memset(rbuf, 0, 1024);

  if (bandwidth_test && bandwidth_test_to_go-- <= 0) {
    return;
  }
  if (keepalive) {
    sock = get_ka(addr);
  }
  if (sock < 0) {
    sock               = make_client(addr, port);
    fd[sock].keepalive = keepalive;
  } else {
    init_client(sock);
    current_clients++;
    fd[sock].keepalive--;
  }
  if (sock < 0) {
    panic("unable to open client connection\n");
  }

  fd[sock].post_size = build_request(sock);

  if (verbose) {
    printf("request %d [%s]\n", sock, fd[sock].req_header);
  }
  fd[sock].length = strlen(fd[sock].req_header);
  {
    char *s   = fd[sock].req_header;
    char *e   = (char *)memchr(s, '\r', 512);
    char *url = fd[sock].base_url;
    memcpy(url, s, e - s);
    url[e - s] = 0;
    if (show_before) {
      printf("%s\n", url);
    }
  }
  if (show_headers) {
    printf("Request to Proxy: {\n%s}\n", fd[sock].req_header);
  }
}

#define RUNNING(_n)                                                               \
  total_##_n   = (((total_##_n * (average_over - 1)) / average_over) + new_##_n); \
  running_##_n = total_##_n / average_over;                                       \
  new_##_n     = 0;

#define RUNNING_AVG(_t, _n, _o)                                        \
  _t = _o ? ((_t * (average_over - 1) + _n / _o) / average_over) : _t; \
  _n = 0;

void
interval_report()
{
  static int here = 0;
  now             = ink_get_hrtime_internal();
  if (!(here++ % 20)) {
    printf(" con  new     ops   1B  lat      bytes/per     svrs  new  ops      total   time  err\n");
  }
  RUNNING(clients);
  RUNNING_AVG(running_latency, latency, lat_ops);
  lat_ops = 0;
  RUNNING_AVG(running_b1latency, b1latency, b1_ops);
  b1_ops = 0;
  RUNNING(cbytes);
  RUNNING(ops);
  RUNNING(servers);
  RUNNING(sops);
  RUNNING(tbytes);
  float t      = (float)(now - start_time);
  uint64_t per = current_clients ? running_cbytes / current_clients : 0;
  printf("%4d %4d %7.1f %4d %4d %10" PRIu64 "/%-6" PRIu64 "  %4d %4d %4d  %9" PRIu64 " %6.1f %4d\n",
         current_clients, // clients, n_ka_cache,
         running_clients, running_ops, running_b1latency, running_latency, running_cbytes, per, running_servers, running_servers,
         running_sops, running_tbytes, t / ((float)HRTIME_SECOND), errors);
  if (is_done()) {
    printf("Total Client Request Bytes:\t\t%" PRIu64 "\n", total_client_request_bytes);
    printf("Total Server Response Header Bytes:\t%" PRIu64 "\n", total_server_response_header_bytes);
    printf("Total Server Response Body Bytes:\t%" PRIu64 "\n", total_server_response_body_bytes);
    printf("Total Proxy Request Bytes:\t\t%" PRIu64 "\n", total_proxy_request_bytes);
    printf("Total Proxy Response Header Bytes:\t%" PRIu64 "\n", total_proxy_response_header_bytes);
    printf("Total Proxy Response Body Bytes:\t%" PRIu64 "\n", total_proxy_response_body_bytes);
  }
}

#define URL_HASH_ENTRIES url_hash_entries
#define BYTES_PER_ENTRY 3
#define ENTRIES_PER_BUCKET 16
#define OVERFLOW_ENTRIES 1024 // many many

#define BUCKETS (URL_HASH_ENTRIES / ENTRIES_PER_BUCKET)
#define BYTES_PER_BUCKET (BYTES_PER_ENTRY * ENTRIES_PER_BUCKET)
#define URL_HASH_BYTES (BYTES_PER_ENTRY * (URL_HASH_ENTRIES + OVERFLOW_ENTRIES))

// NOTE: change to match BYTES_PER_ENTRY
#define ENTRY_TAG(_x) (((unsigned int)_x[0] << 16) + ((unsigned int)_x[1] << 8) + (unsigned int)_x[2])
#define SET_ENTRY_TAG(_x, _t) \
  _x[0] = _t >> 16;           \
  _x[1] = (_t >> 8) & 0xFF;   \
  _x[2] = _t & 0xFF;

#define MASK_TAG(_x) (_x & ((1U << (BYTES_PER_ENTRY * 8)) - 1))

#define BEGIN_HASH_LOOP                                                            \
  unsigned int bucket = (i % BUCKETS);                                             \
  unsigned int tag    = MASK_TAG((unsigned int)(i / BUCKETS));                     \
  if (!tag)                                                                        \
    tag++;                                                                         \
  unsigned char *base = bytes + bucket * BYTES_PER_BUCKET;                         \
  unsigned char *last = bytes + (bucket + 1) * BYTES_PER_BUCKET - BYTES_PER_ENTRY; \
  (void)last;                                                                      \
                                                                                   \
  for (unsigned int x = 0; x < ENTRIES_PER_BUCKET; x++) {                          \
    unsigned char *e = base + x * BYTES_PER_ENTRY;

#define BEGIN_OVERFLOW_HASH_LOOP                          \
  for (unsigned int j = 0; j < ENTRIES_PER_BUCKET; j++) { \
    unsigned char *e = base + (URL_HASH_ENTRIES + j) * BYTES_PER_ENTRY;

#define END_HASH_LOOP }

struct UrlHashTable {
  unsigned int numbytes;
  unsigned char *bytes;
  int fd;

  void
  zero()
  {
    memset(bytes, 0, numbytes);
  }

  void alloc(unsigned int want);

  void
  set(uint64_t i)
  {
    BEGIN_HASH_LOOP
    {
      if (!ENTRY_TAG(e)) {
        SET_ENTRY_TAG(e, tag);
        return;
      }
    }
    END_HASH_LOOP;

    fprintf(stderr, "url hash table overflow: %X, %X\n", (int)(base - bytes), tag);

    BEGIN_OVERFLOW_HASH_LOOP
    {
      if (!ENTRY_TAG(e)) {
        SET_ENTRY_TAG(e, tag);
        return;
      }
    }
    END_HASH_LOOP;

    ink_fatal("overview entries overflow");
  }

  void
  clear(uint64_t i)
  {
    BEGIN_HASH_LOOP
    {
      if (ENTRY_TAG(e) == tag) {
        if (e != last) {
          SET_ENTRY_TAG(e, ENTRY_TAG(last));
        }
        SET_ENTRY_TAG(last, 0);
        return;
      }
    }
    END_HASH_LOOP;

    fprintf(stderr, "url hash table entry to clear not found: %X, %X\n", (int)(base - bytes), tag);
  }

  int
  is_set(uint64_t i)
  {
    BEGIN_HASH_LOOP
    {
      if (ENTRY_TAG(e) == tag) {
        return 1;
      }
    }
    END_HASH_LOOP;

    if (ENTRY_TAG((last))) {
      BEGIN_OVERFLOW_HASH_LOOP
      {
        if (ENTRY_TAG(e) == tag) {
          return 1;
        }
      }
      END_HASH_LOOP;
    }
    return 0;
  }

  UrlHashTable();

  ~UrlHashTable();
};
UrlHashTable *uniq_urls = nullptr;

UrlHashTable::UrlHashTable() : numbytes(0), bytes(nullptr), fd(-1)
{
  off_t len = 0;

  if (!url_hash_entries) {
    return;
  }

  if (*url_hash_filename) {
    if ((fd = open(url_hash_filename, O_RDWR | O_CREAT, 0644)) == -1) {
      panic_perror("failed to open URL Hash file");
    }

    len = lseek(fd, 0, SEEK_END);
  }

  if (url_hash_entries > 0) {
    // if they specify the number of entries round it up
    url_hash_entries = (url_hash_entries + ENTRIES_PER_BUCKET - 1) & ~(ENTRIES_PER_BUCKET - 1);
    numbytes         = URL_HASH_BYTES;

    // ensure it is either a new file or the correct size
    if (len != 0 && len != numbytes) {
      panic("specified size != file size\n");
    }

  } else {
    // otherwise make sure the file is non-zero and then use its
    // size as the size
    if (!len) {
      panic("zero size URL Hash Table\n");
    }
    if (len != URL_HASH_BYTES) {
      fprintf(stderr, "FATAL: hash file length (%jd) != URL_HASH_BYTES (%jd)\n", (intmax_t)len, (intmax_t)URL_HASH_BYTES);
      exit(1);
    }
    numbytes = len;
  }

  if (*url_hash_filename) {
    if (ftruncate(fd, numbytes) == -1) {
      panic_perror("unable to truncate URL Hash file");
    }

    bytes = (unsigned char *)mmap(nullptr, numbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bytes == (unsigned char *)MAP_FAILED || !bytes) {
      panic("unable to map URL Hash file\n");
    }
  } else {
    bytes = (unsigned char *)malloc(numbytes);
    ink_assert(bytes);
    zero();
  }
} // UrlHashTable::UrlHashTable

UrlHashTable::~UrlHashTable()
{
  if (bytes) {
    munmap((char *)bytes, numbytes);
  }
  if (fd != -1) {
    close(fd);
  }
} // UrlHashTable::~UrlHashTable

static int
seen_it(char *url)
{
  if (!url_hash_entries) {
    return 0;
  }
  int l      = 0;
  char *para = strrchr(url, '#');
  if (para) {
    l = para - url;
  } else {
    l = strlen(url);
  }
  CryptoHash hash;
  CryptoContext().hash_immediate(hash, reinterpret_cast<void *>(url), l);
  uint64_t x = hash.fold();
  if (uniq_urls->is_set(x)) {
    if (verbose) {
      printf("YES: seen it '%s'\n", url);
    }
    return 1;
  }
  uniq_urls->set(x);
  if (verbose) {
    printf("NO: marked it '%s'\n", url);
  }
  return 0;
}

static int
make_url_client(const char *url, const char *base_url, bool seen, bool unthrottled)
{
  int iport       = 80;
  unsigned int ip = 0;
  char curl[512]  = {0};
  char sche[8], host[512], port[10], path[512], frag[512], quer[512], para[512];
  int xsche, xhost, xport, xpath, xfrag, xquer, xpar, rel, slash;

  if (base_url) {
    ink_web_canonicalize_url(base_url, url, curl, 512);
    // hack for our own web server!
    if (curl[strlen(curl) - 1] == 13) {
      curl[strlen(curl) - 1] = 0;
    }
    if (curl[strlen(curl) - 1] == 12) {
      curl[strlen(curl) - 1] = 0;
    }
  } else {
    strncpy(curl, url, sizeof(curl) - 1);
  }
  if (!seen && seen_it(curl)) {
    return -1;
  }
  ink_web_decompose_url(curl, sche, host, port, path, frag, quer, para, &xsche, &xhost, &xport, &xpath, &xfrag, &xquer, &xpar, &rel,
                        &slash);
  if (follow_same) {
    if (!xhost || strcasecmp(host, current_host)) {
      if (verbose) {
        printf("skipping %s\n", curl);
      }
      return -1;
    }
  }
  if (!unthrottled && throttling_connections()) {
    defer_url(curl);
    return -1;
  }
  if (proxy_port) {
    iport = proxy_port;
    ip    = proxy_addr;
  } else {
    if (xport) {
      iport = ts::svtoi(port);
    }
    if (!xhost) {
      if (verbose) {
        fprintf(stderr, "bad url '%s'\n", curl);
      }
      return -1;
    }
    ip = get_addr(host);
    if ((int)ip == -1) {
      if (verbose || verbose_errors) {
        fprintf(stderr, "bad host '%s'\n", host);
      }
      return -1;
    }
  }
  int sock = -1;
  if (keepalive) {
    sock = get_ka(ip);
  }
  if (sock < 0) {
    sock               = make_client(ip, iport);
    fd[sock].keepalive = keepalive;
  } else {
    init_client(sock);
    current_clients++;
    fd[sock].keepalive--;
  }
  if (sock < 0) {
    panic("cannot make client\n");
  }
  char eheaders[16384];
  *eheaders    = 0;
  int nheaders = extra_headers;
  memset(&eheaders, 0, 16384);
  if (nheaders > 0) {
    char *eh = eheaders;
    if (!vary_user_agent) {
      eh += sprintf(eh, "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.31 i586)\r\n");
      nheaders--;
    }
    if (nheaders > 0) {
      eh += sprintf(eh, "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\n");
    }
    while (--nheaders > 0) {
      eh += sprintf(eh, "Extra-Header%d: a lot of junk for header %d\r\n", nheaders, nheaders);
    }
  }
  if (proxy_port) {
    sprintf(fd[sock].req_header,
            "GET %s HTTP/1.0\r\n"
            "%s"
            "%s"
            "Accept: */*\r\n"
            "%s"
            "\r\n",
            curl,
            // coverity[dont_call]
            reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", fd[sock].keepalive ? "Proxy-Connection: Keep-Alive\r\n" : "",
            eheaders);
  } else {
    sprintf(fd[sock].req_header,
            "GET /%s%s%s%s%s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "%s"
            "%s"
            "Accept: */*\r\n"
            "%s"
            "\r\n",
            path, xquer ? "?" : "", quer, xpar ? ";" : "", para, host,
            // coverity[dont_call]
            reload_rate > drand48() ? "Pragma: no-cache\r\n" : "", fd[sock].keepalive ? "Connection: Keep-Alive\r\n" : "",
            eheaders);
  }

  if (verbose) {
    printf("curl = '%s'\n", curl);
  }
  if (show_before) {
    printf("%s\n", curl);
  }
  if (urlsdump_fp) {
    fprintf(urlsdump_fp, "%s\n", curl);
  }
  if (show_headers) {
    printf("Request to Proxy: {\n%s}\n", fd[sock].req_header);
  }

  {
    const char *ext = strrchr(path, '.');

    fd[sock].binary = 0;
    if (ext) {
      fd[sock].binary = !strncasecmp(ext, ".gif", 4) || !strncasecmp(ext, ".jpg", 4);
    }
  }

  fd[sock].response_length = 0;
  fd[sock].length          = strlen(fd[sock].req_header);
  if (!fd[sock].response) {
    fd[sock].response = (char *)malloc(MAX_BUFSIZE);
  }
  strcpy(fd[sock].base_url, curl);
  return sock;
}

static FILE *
get_defered_urls(FILE *fp)
{
  char url[512];
  while (fgets(url, 512, fp)) {
    if (n_defered_urls > MAX_DEFERED_URLS - 2) {
      return nullptr;
    }
    char *e = (char *)memchr(url, '\n', 512);
    if (e) {
      *e = 0;
    }
    make_url_client(url);
  }
  return fp;
}

int
main(int argc __attribute__((unused)), const char *argv[])
{
  appVersionInfo.setup(PACKAGE_NAME, "jtest", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  /* for QA -- we want to be able to tail an output file
   * during execution "nohup jtest -P pxy -p prt &"
   */
  setvbuf(stdout, (char *)nullptr, _IOLBF, 0);

  fd = (FD *)malloc(MAXFDS * sizeof(FD));
  memset(static_cast<void *>(fd), 0, MAXFDS * sizeof(FD));
  process_args(&appVersionInfo, argument_descriptions, n_argument_descriptions, argv);

  if (!drand_seed) {
    // coverity[dont_call]
    srand48((long)time(nullptr));
  } else {
    // coverity[dont_call]
    srand48((long)drand_seed);
  }
  if (zipf != 0.0) {
    build_zipf();
  }
  int max_fds = max_limit_fd();
  if (verbose) {
    printf("maximum of %d connections\n", max_fds);
  }
  signal(SIGPIPE, SIG_IGN);
  start_time = now = ink_get_hrtime_internal();

  urls_mode = n_file_arguments || *urls_file;
  nclients  = client_rate ? 0 : nclients;

  if (!local_host[0]) {
    if (gethostname(local_host, sizeof(local_host)) != 0) {
      panic_perror("gethostname failed");
    }
  }

  local_addr = get_addr(local_host);
  if (!proxy_host[0]) {
    strncpy(proxy_host, local_host, sizeof(proxy_host) - 1);
    proxy_host[sizeof(proxy_host) - 1] = 0;
  }
  if (proxy_port) {
    proxy_addr = get_addr(proxy_host);
  }

  if (!urls_mode) {
    if (compd_port) {
      build_response();
      open_server(compd_port, accept_compd);
    } else {
      if (!server_port) {
        server_port = proxy_port + 1000;
      }
      build_response();
      if (!only_clients) {
        for (int retry = 0; retry < 20; retry++) {
          server_fd = open_server(server_port + retry, accept_read);
          if (server_fd < 0) {
            if (server_fd == -EADDRINUSE) {
              continue;
            }
            panic_perror("open_server");
          }
          break;
        }
      }
      bandwidth_test_to_go = bandwidth_test;
      if (!only_server) {
        if (proxy_port) {
          for (int i = 0; i < nclients; i++) {
            make_bfc_client(proxy_addr, proxy_port);
          }
        }
      }
    }
  } else {
    if (check_content) {
      build_response();
    }
    follow       = follow_arg;
    follow_same  = follow_same_arg;
    uniq_urls    = new UrlHashTable;
    defered_urls = (char **)malloc(sizeof(char *) * MAX_DEFERED_URLS);
    average_over = 1;
    if (*urlsdump_file) {
      urlsdump_fp = fopen(urlsdump_file, "w");
      if (!urlsdump_fp) {
        panic_perror("fopen urlsdump file");
      }
    }
    if (*urls_file) {
      FILE *fp = fopen(urls_file, "r");
      if (!fp) {
        panic_perror("fopen urls file");
      }
      if (get_defered_urls(fp)) {
        fclose(fp);
      } else {
        urls_fp = fp;
      }
    }
    for (unsigned i = 0; i < n_file_arguments; i++) {
      char sche[8], host[512], port[10], path[512], frag[512], quer[512], para[512];
      int xsche, xhost, xport, xpath, xfrag, xquer, xpar, rel, slash;
      ink_web_decompose_url(file_arguments[i], sche, host, port, path, frag, quer, para, &xsche, &xhost, &xport, &xpath, &xfrag,
                            &xquer, &xpar, &rel, &slash);
      if (xhost) {
        strcpy(current_host, host);
      }
    }
    for (unsigned i = 0; i < n_file_arguments; i++) {
      make_url_client(file_arguments[i]);
    }
  }

  int t       = now / HRTIME_SECOND;
  int tclient = now / HRTIME_SECOND;
  int start   = now / HRTIME_SECOND;
  while (1) {
    if (poll_loop()) {
      break;
    }
    int t2 = now / HRTIME_SECOND;
    if (urls_fp && n_defered_urls < MAX_DEFERED_URLS - DEFERED_URLS_BLOCK - 2) {
      if (get_defered_urls(urls_fp)) {
        fclose(urls_fp);
        urls_fp = nullptr;
      }
    }
    if ((!urls_mode || client_rate) && interval && t + interval <= t2) {
      t = t2;
      interval_report();
    }
    if (t2 != tclient) {
      for (int i = 0; i < client_rate * (t2 - tclient); i++) {
        if (!urls_mode) {
          make_bfc_client(proxy_addr, proxy_port);
        } else {
          undefer_url(true);
        }
      }
      tclient = t2;
    }
    if (test_time) {
      if (t2 - start > test_time) {
        done();
      }
    }
    if (is_done()) {
      done();
    }
  }

  return 0;
}

/*---------------------------------------------------------------------------*

  int ink_web_decompose_url(...)

  This function takes an input URL in src_url and splits it into its
  component parts, including a scheme, host, port, path, fragment,
  query, and parameters. you must pass in buffers for each of these.
  If you pass in a nullptr pointer for any of these, it will not be
  returned.

  The flags "sche_exists", etc. tell you if that part of the URL was
  found. Each unfound part (with a non-nullptr buffer) will also contain
  the empty string '\0'.

  The flag "relative_url" indicates that the src_url did not start
  with a scheme. (This is kind of redundant with sche_exists but is
  the general way to do it.)

  The flag "leading_slash" indicates that the path began with a
  leading slash.

  mep - 4/15/96

  *---------------------------------------------------------------------------*/

static void
ink_web_decompose_url(const char *src_url, char *sche, char *host, char *port, char *path, char *frag, char *quer, char *para,
                      int *real_sche_exists, int *real_host_exists, int *real_port_exists, int *real_path_exists,
                      int *real_frag_exists, int *real_quer_exists, int *real_para_exists, int *real_relative_url,
                      int *real_leading_slash)
/*
 * Input: src_url
 * Outputs: every other argument
 *
 * You may pass in nullptr pointers for any of: sche, host, port, path,
 * frag, quer, or para, and they will not be returned.
 *
 *
 * According to the HTML Sourcebook, a URL consists:
 *
 *   http://www.address.edu:80/path/subdir/file.ext?query;params#fragment
 *   aaaa   bbbbbbbbbbbbbbb cc dddddddddddddddddddd eeeee ffffff gggggggg
 *
 * where
 *   a = scheme
 *   b = host
 *   c = port
 *   d = path
 *   e = query
 *   f = params
 *   g = fragment
 *
 * Order of parsing is: fragment, scheme, host, port, params, query, path
 *
 * Note that the hostname:port part may contain something like:
 *   user@pass:hostname:port
 *   bbbbbbbbbbbbbbbbbb cccc
 * i.e., the port is the thing after the _last_ colon in this part
 *
 */
{
  const char *start = src_url;
  int len           = strlen(src_url);
  const char *end   = start + len;
  const char *ptr   = start;
  const char *ptr2, *temp, *temp2;
  const char *sche1, *sche2;
  const char *host1, *host2;
  const char *port1, *port2;
  const char *path1, *path2;
  const char *frag1, *frag2;
  const char *quer1, *quer2;
  const char *para1, *para2;
  bool fail = false;
  int num;
  int sche_exists, host_exists, port_exists, path_exists, frag_exists, quer_exists, para_exists;
  int leading_slash;

  sche_exists = host_exists = port_exists = path_exists = 0;
  frag_exists = quer_exists = para_exists = 0;
  sche1 = sche2 = host1 = host2 = port1 = port2 = nullptr;
  path1 = path2 = frag1 = frag2 = quer1 = quer2 = para1 = para2 = nullptr;
  leading_slash                                                 = 0;

  temp2 = ptr;
  /* strip fragments "#" off the end */
  while (ptr < end) {
    if (*ptr == '#') {
      frag1       = ptr + 1;
      frag2       = end;
      frag_exists = 1;
      end         = ptr;
    }
    ptr++;
  }
  ptr = temp2;

  /* decide if there is a sche, i.e. if it's an absolute url */
  /* find end of sche */
  fail  = false;
  temp2 = ptr;
  while ((ptr < end) && !fail) {
    if (*ptr == ':') {
      sche1 = start;
      sche2 = ptr;
      ptr++; /* to continue to parse, skip the : */
      sche_exists = 1;
      fail        = true;
    } else if ((!ParseRules::is_alpha(*ptr) && (*ptr != '+') && (*ptr != '.') && (*ptr != '-')) || (ptr == end)) {
      sche_exists = 0;
      fail        = true;
    } else {
      ptr++;
    }
  }
  if (sche_exists == 0) {
    ptr = temp2;
  }

  /* find start of host */
  fail  = false;
  temp2 = ptr;
  while ((ptr < end - 1) && !fail) {
    if (*(ptr + 0) == '/') {
      if (*(ptr + 1) == '/') {
        host1 = ptr + 2;
        ptr += 2; /* skip "//" */
        host_exists = 1;
        fail        = true;
      } else {
        /* this is the start of a path, not a host */
        host_exists = 0;
        fail        = true;
      }
    } else {
      ptr++;
    }
  }

  /* find end of host */
  if (host_exists == 1) {
    while ((ptr < end) && (host2 == nullptr)) {
      if (*ptr == '/') {
        /* "/" marks the start of the path */
        host2 = ptr; /* just so we quit out of the loop */
      } else {
        ptr++;
      }
    }
    if (host2 == nullptr) {
      host2 = end;
    }

    if (host_exists == 1) {
      temp = host2 - 1;
      /* remove trailing dots from host */
      while ((temp > host1) && (*temp == '.')) {
        temp--;
        host2--;
      }

      /* find start & end of port */
      ptr2 = host1;
      temp = host2;
      while (ptr2 < temp) {
        if (*ptr2 == ':') {
          port1       = ptr2 + 1;
          port2       = temp;
          host2       = ptr2;
          port_exists = 1;
        }
        ptr2++;
      }
    }
  }
  if (host_exists == 0) {
    ptr = temp2;
  }

  temp2 = ptr;
  /* strip query "?" off the end */
  while (ptr < end) {
    if (*ptr == '?') {
      quer1       = ptr + 1;
      quer2       = end;
      quer_exists = 1;
      end         = ptr;
    }
    ptr++;
  }
  ptr = temp2;

  temp2 = ptr;
  /* strip parameters ";" off the end */
  while (ptr < end) {
    if (*ptr == ';') {
      para1       = ptr + 1;
      para2       = end;
      para_exists = 1;
      end         = ptr;
    }
    ptr++;
  }
  ptr = temp2;

  /* the path is the remainder of the string */
  /* don't include any leading slash */
  if (ptr < end) {
    if (*ptr == '/') {
      leading_slash = 1;
      path1         = ptr + 1;
      path2         = end;
      path_exists   = 1;
    } else {
      path1       = ptr;
      path2       = end;
      path_exists = 1;
    }
  } else {
    path1       = end;
    path2       = end;
    path_exists = 0;
  }

  if (sche_exists != 1) {
    *real_relative_url = 1;
  } else {
    *real_relative_url = 0;
  }

  /* extract strings for scheme, host, port, path, etc */

  if (sche != nullptr) {
    if (sche_exists) {
      num = sche2 - sche1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(sche, sche1, num + 1);
      *(sche + num) = '\0';

      /* make scheme lowercase */
      char *p = sche;
      while (*p) {
        *p = ParseRules::ink_tolower(*p);
        p++;
      }
    } else {
      *sche = 0;
    }
  }

  if (host != nullptr) {
    if (host_exists) {
      num = host2 - host1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(host, host1, num + 1);
      *(host + num) = '\0';

      /* make hostname lowercase */
      char *p = host;
      while (*p) {
        *p = ParseRules::ink_tolower(*p);
        p++;
      }
    } else {
      *host = 0;
    }
  }

  if (port != nullptr) {
    if (port_exists) {
      num = port2 - port1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(port, port1, num + 1);
      *(port + num) = '\0';
    } else {
      *port = 0;
    }
  }

  if (path != nullptr) {
    if (path_exists) {
      num = path2 - path1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(path, path1, num + 1);
      *(path + num) = '\0';
    } else {
      *path = 0;
    }
  }

  if (frag != nullptr) {
    if (frag_exists) {
      num = frag2 - frag1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(frag, frag1, num + 1);
      *(frag + num) = '\0';
    } else {
      *frag = 0;
    }
  }

  if (quer != nullptr) {
    if (quer_exists) {
      num = quer2 - quer1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(quer, quer1, num + 1);
      *(quer + num) = '\0';
    } else {
      *quer = 0;
    }
  }

  if (para != nullptr) {
    if (para_exists) {
      num = para2 - para1;
      if (num > MAX_URL_LEN - 1) {
        num = MAX_URL_LEN - 1;
      }
      strncpy(para, para1, num + 1);
      *(para + num) = '\0';
    } else {
      *para = 0;
    }
  }
  *real_sche_exists   = sche_exists;
  *real_host_exists   = host_exists;
  *real_port_exists   = port_exists;
  *real_path_exists   = path_exists;
  *real_frag_exists   = frag_exists;
  *real_quer_exists   = quer_exists;
  *real_para_exists   = para_exists;
  *real_leading_slash = leading_slash;
} /* End ink_web_decompose_url */

#if 0 /* debugging */
/*---------------------------------------------------------------------------*

  void ink_web_dump_url_components(FILE *fp, InkWebURLComponents *c)

  This routine writes a readable representation of the URL components
  pointed to by <c> on the file pointer <fp>.

  *---------------------------------------------------------------------------*/

static void ink_web_dump_url_components(FILE *fp, InkWebURLComponents *c)
{
  fprintf(fp,"sche:'%s', exists %d\n",c->sche,c->sche_exists);
  fprintf(fp,"host:'%s', exists %d\n",c->host,c->host_exists);
  fprintf(fp,"port:'%s', exists %d\n",c->port,c->port_exists);
  fprintf(fp,"path:'%s', exists %d\n",c->path,c->path_exists);
  fprintf(fp,"quer:'%s', exists %d\n",c->quer,c->quer_exists);
  fprintf(fp,"frag:'%s', exists %d\n",c->frag,c->frag_exists);
  fprintf(fp,"para:'%s', exists %d\n",c->para,c->para_exists);

  fprintf(fp,"rel_url:%d\n",c->rel_url);
  fprintf(fp,"leading_slash:%d\n",c->leading_slash);

  fprintf(fp,"\n");
} /* End ink_web_dump_url_components */

#endif

/*---------------------------------------------------------------------------*

  int ink_web_canonicalize_url(...)

  Inputs: base_url, emb_url, max_dest_url_len.
  Output: dest_url.

  This function takes a base url and an embedded url, and produces an
  absolute url as specified in RFC 1808, "Relative Uniform Resource
  Locators".

  A base url is often the url of a document and an embedded url is an
  incomplete reference to a secondary document, often in the same
  directory. Together they completely specify an absolute reference to
  the secondary document.

  For instance,
     base_url "http://inktomi.com/~mep"
     emb_url: "path1/path2/foo.html"

  becomes

     dest_url: "http://inktomi.com/~mep/path1/path2/foo.html"

  This function also applies "ink_web_escapify()" to the dest_url.

  You must supply the buffer dest_url and its size, max_dest_url_len.

  mep - 4/15/96

  *---------------------------------------------------------------------------*/

static void
ink_web_canonicalize_url(const char *base_url, const char *emb_url, char *dest_url, int max_dest_url_len)
{
  int doff;
  InkWebURLComponents base, emb;
  char temp[MAX_URL_LEN + 1], temp2[MAX_URL_LEN + 1];
  int leading_slash, use_base_sche, use_base_host, use_base_path, use_base_quer, use_base_para, use_base_frag;
  int host_last = 0;

  doff = 0;

  /* Initialize Component Values */

  leading_slash = 0;

  /* Decompose The Base And Embedded URLs */

  ink_web_decompose_url_into_structure(base_url, &base);
  ink_web_decompose_url_into_structure(emb_url, &emb);

  /* Print Out Components */

  /* Select Which Components To Use From Base & Embedded URL */

  dest_url[0] = '\0';

  use_base_path = 0;
  use_base_quer = 0;
  use_base_para = 0;
  use_base_frag = 0;

  if (!emb.sche_exists && !emb.path_exists && !emb.host_exists && !emb.quer_exists && !emb.frag_exists && !emb.para_exists) {
    /* 2a: if the embedded URL is empty, take everything from the base */

    use_base_sche = 1;
    use_base_host = 1;
    use_base_path = 1;
    use_base_quer = 1;
    use_base_para = 1;
    use_base_frag = 1;
  } else if (emb.sche_exists && ((strcasecmp(emb.sche, "telnet") == 0) || (strcasecmp(emb.sche, "mailto") == 0) ||
                                 (strcasecmp(emb.sche, "news") == 0))) {
    const char *p = emb_url;
    char *q       = dest_url;
    while (*p) {
      *q++ = ParseRules::ink_tolower(*p++);
    }
    return;
  } else if (emb.sche_exists && !(((strcasecmp(emb.sche, "http") == 0) && !emb.host_exists)))

  {
    /* 2b: not good enough, because things like 'http:overview.html' */

    use_base_sche = 0;
    use_base_host = 0;
    use_base_path = 0;
    use_base_quer = 0;
    use_base_para = 0;
    use_base_frag = 0;
  } else {
    use_base_sche = 1;

    /* step 3 - if emb_host non-empty, skip to 7 */

    if (emb.host_exists) {
      use_base_host = 0;
    } else {
      use_base_host = 1;

      /* step 4 - if emb_path preceeded by slash, skip to 7 */

      if (emb.leading_slash != 1) {
        /* step 5 */

        if (!emb.path_exists) {
          use_base_path = 1;

          if (emb.para_exists) {
            /* 5a - if emb_para non-empty, skip to 7 */

            use_base_para = 0;
          } else {
            /* otherwise use base_para */

            use_base_para = 1;

            if (emb.quer_exists) {
              /* 5b - if emb_quer non-empty, skip to 7 */

              use_base_quer = 0;
            } else {
              /* otherwise use base query */

              use_base_quer = 1;
            }
          }
        } else {
          use_base_path = 0;

          /* step 6 */
          /* create combined path */
          /* remove last segment of base_path */

          remove_last_seg(base.path, temp);
          remove_multiple_slash(temp, temp2);

          /* append emb_path */

          strcat(temp2, emb.path);

          /* remove "." and ".." */

          ink_web_remove_dots(temp2, emb.path, &leading_slash, MAX_URL_LEN);
          emb.path_exists   = 1;
          emb.leading_slash = base.leading_slash;
        } /* 5 */
      }   /* 4 */
    }     /* 3 */
  }

  /* step 7 - combine parts */

  if (use_base_sche) {
    if (base.sche_exists) {
      append_string(dest_url, base.sche, &doff, MAX_URL_LEN);
      append_string(dest_url, ":", &doff, MAX_URL_LEN);
      host_last = 0;
    }
  } else {
    if (emb.sche_exists) {
      append_string(dest_url, emb.sche, &doff, MAX_URL_LEN);
      append_string(dest_url, ":", &doff, MAX_URL_LEN);
      host_last = 0;
    }
  }

  if (use_base_host) {
    if (base.host_exists) {
      append_string(dest_url, "//", &doff, MAX_URL_LEN);
      append_string(dest_url, base.host, &doff, MAX_URL_LEN);
      if ((base.port_exists) && (strcmp(base.port, "80") != 0)) {
        append_string(dest_url, ":", &doff, MAX_URL_LEN);
        append_string(dest_url, base.port, &doff, MAX_URL_LEN);
      }
      host_last = 1;
    }
  } else {
    if (emb.host_exists) {
      append_string(dest_url, "//", &doff, MAX_URL_LEN);
      append_string(dest_url, emb.host, &doff, MAX_URL_LEN);
      if ((emb.port_exists) && (strcmp(emb.port, "80") != 0)) {
        append_string(dest_url, ":", &doff, MAX_URL_LEN);
        append_string(dest_url, emb.port, &doff, MAX_URL_LEN);
      }
      host_last = 1;
    }
  }

  if (use_base_path) {
    if (base.path_exists) {
      if (base.leading_slash) {
        append_string(dest_url, "/", &doff, MAX_URL_LEN);
      }

      ink_web_unescapify_string(temp, base.path, MAX_URL_LEN);
      ink_web_escapify_string(base.path, temp, max_dest_url_len);
      append_string(dest_url, base.path, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  } else {
    if (emb.path_exists) {
      if (emb.leading_slash) {
        append_string(dest_url, "/", &doff, MAX_URL_LEN);
      }
      ink_web_unescapify_string(temp, emb.path, MAX_URL_LEN);
      ink_web_escapify_string(emb.path, temp, max_dest_url_len);
      append_string(dest_url, emb.path, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  }

  if (use_base_para) {
    if (base.para_exists) {
      append_string(dest_url, ";", &doff, MAX_URL_LEN);
      append_string(dest_url, base.para, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  } else {
    if (emb.para_exists) {
      append_string(dest_url, ";", &doff, MAX_URL_LEN);
      append_string(dest_url, emb.para, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  }

  if (use_base_quer) {
    if (base.quer_exists) {
      append_string(dest_url, "?", &doff, MAX_URL_LEN);
      append_string(dest_url, base.quer, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  } else {
    if (emb.quer_exists) {
      append_string(dest_url, "?", &doff, MAX_URL_LEN);
      append_string(dest_url, emb.quer, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  }

  if (use_base_frag) {
    if (base.frag_exists) {
      append_string(dest_url, "#", &doff, MAX_URL_LEN);
      append_string(dest_url, base.frag, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  } else {
    if (emb.frag_exists) {
      append_string(dest_url, "#", &doff, MAX_URL_LEN);
      append_string(dest_url, emb.frag, &doff, MAX_URL_LEN);
      host_last = 0;
    }
  }

  if (host_last) {
    append_string(dest_url, "/", &doff, MAX_URL_LEN);
  }
}

/*---------------------------------------------------------------------------*

  int ink_web_decompose_url_into_structure(char *url, InkWebURLComponents *c)

  This routine takes a URL and violently tears apart its molecular structure,
  placing the URL components in the InkWebURLComponents structure pointed to
  by <c>.  Flags in the structure indicate whether individual fields are
  valid or not.

  *---------------------------------------------------------------------------*/

static void
ink_web_decompose_url_into_structure(const char *url, InkWebURLComponents *c)
{
  ink_web_decompose_url(url, c->sche, c->host, c->port, c->path, c->frag, c->quer, c->para, &(c->sche_exists), &(c->host_exists),
                        &(c->port_exists), &(c->path_exists), &(c->frag_exists), &(c->quer_exists), &(c->para_exists),
                        &(c->rel_url), &(c->leading_slash));

  c->is_path_name = 1;
  if (c->sche_exists &&
      ((strcasecmp(c->sche, "mailto") == 0) || (strcasecmp(c->sche, "telnet") == 0) || (strcasecmp(c->sche, "news") == 0))) {
    c->is_path_name = 0;
  }
} /* End ink_web_decompose_url_into_structure */

/*---------------------------------------------------------------------------*

  int ink_web_remove_dots(char *src, char *dest, int *leadingslash,
                          int max_dest_len)

  This routine takes a path and interprets "." and ".." segments, returning
  an appropriately parsed path. It is a warning to pass a path that resolves
  to a leading "..". Inputs are the src path and the length of the dest
  buffer. Return values are a string written into the dest buffer and
  the leadingslash flag, which indicates if the src (and the dest) have a
  leading slash, and are therefore not relative paths.

  Basically, these sequences: "<a><path-segment>..<b>" and "<a>.<b>" both
  turn into "<a><b>" where <a> is beginning-or-string or a complete segment,
  and <b> is end-of-string or a complete segment.

  e.g.
  path1/../path2  -> path2
  /path1/../path2 -> /path2
  /path1/path2/.. -> /path1
  path1/./path2   -> path1/path2
  path1/path2/.   -> path1/path2
  ./path1/path2   -> path1/path2
  ./path1         -> path1
  /./path1        -> /path1

  It is also a warning to pass a path whose returned value needs to be
  truncated to fit into max_dest_len characters.

  mep - 4/15/96

  *---------------------------------------------------------------------------*/

/* types of path segment */
#define NORMAL 0
#define DOT 1
#define DOTDOT 2
#define ZAP 3
#define ERROR 4

/* We statically allocate this many - if we need more, we dynamically */
/* allocate them. */
#define STATIC_PATH_LEVELS 256

static int
ink_web_remove_dots(char *src, char *dest, int *leadingslash, int max_dest_len)
{
  char *ptr, *end;
  int free_flag = 0;
  int scount, segstart, zapflag, doff, num;
  int temp, i;
  int error = 0;

  /* offsets to each path segment */
  char **seg, *segstatic[STATIC_PATH_LEVELS];

  /* type of each segment is a ".." */
  int *type, typestatic[STATIC_PATH_LEVELS];

  *leadingslash = 0;

  /* first quickly count the "/"s to get lower bound on # of path levels */
  ptr    = src;
  end    = src + strlen(src);
  scount = 0;
  while (ptr < end) {
    if (*ptr++ == '/') {
      scount++;
    }
  }
  scount++; /* adding one to this makes it a lower bound for any case */

  if (scount <= STATIC_PATH_LEVELS) {
    /* we can use the statically allocated ones */
    seg  = segstatic;
    type = typestatic;
  } else {
    /* too many levels of path - must dynamically allocate */
    seg       = (char **)malloc(scount * sizeof(char *));
    type      = (int *)malloc(scount * sizeof(int));
    free_flag = 1;
  }

  /* Determine starts of each path segment.
   * A segment is defined as:
   * "foo/" in the string "<a>foo/<b>", where:
   *    <a> is <start-of-string>, or a single "/"
   *    <b> is <end-of-string>, or another segment.
   * "foo" can be "." or ".."
   * Makes my head hurt just to think about it.
   *
   */
  ptr    = src;
  scount = 0;
  /* a segstart starts with start-of-string or a '/' */
  segstart = 1;
  while (ptr < end) {
    if (*ptr == '/') {
      /* include leading '/' in first segment */
      if (ptr == src) {
        *leadingslash = 1;
      }
      segstart = 1;
    } else if (segstart == 1) {
      seg[scount++] = ptr;
      segstart      = 0;
    } else {
      /* this is neither a "/" nor the first char of another segment */
    }
    ptr++;
  }
  /* Now scount is an accurate count of the segments we have found, */
  /* not just that lower bound we quickly got before */

  /* now figure out if segments are "..", ".", or normal */
  /* ZAP the "."s in place */
  for (i = 0; i < scount; i++) {
    ptr = seg[i];
    if (*ptr == '.') {
      if ((ptr == end - 1) || (*(ptr + 1) == '/')) {
        /* it's a "." */
        type[i] = DOT;
      } else if (((ptr == end - 2) && (*(ptr + 1) == '.')) || ((ptr < end - 2) && (*(ptr + 1) == '.') && (*(ptr + 2) == '/'))) {
        /* it's a ".." */
        type[i] = DOTDOT;
      } else {
        type[i] = NORMAL;
      }
    } else {
      /* it's not a special segment */
      type[i] = NORMAL;
    }
  }
  /* now ZAP each DOT, and each NORMAL following a DOTDOT */
  for (i = 0; i < scount; i++) {
    if (type[i] == DOT) {
      type[i] = ZAP;
    } else if (type[i] == DOTDOT) {
      /* got a DOTDOT, count back to find first NORMAL segment */
      temp    = i - 1;
      zapflag = 0;
      while ((temp >= 0) && (zapflag == 0)) {
        if (type[temp] == NORMAL) {
          /* found a NORMAL one, ZAP this pair */
          type[temp] = ZAP;
          type[i]    = ZAP;
          zapflag    = 1;
        } else {
          temp--;
        }
      }
      if (zapflag == 0) {
        type[i] = ERROR;
        error   = 1;
      }
    }
  }

  /* now write out the fixed path */
  doff  = 0;
  *dest = 0;
  if (*leadingslash) {
    strncpy(dest + doff, "/", 2);
    doff++;
  }
  for (i = 0; i < scount; i++) {
    if ((type[i] == NORMAL) || (type[i] == ERROR)) {
      if (i == scount - 1) {
        num = (int)(end - seg[i]);
      } else {
        num = (int)(seg[i + 1] - seg[i]);
      }

      /* truncate if nec. */
      if (doff + num > max_dest_len) {
        num = max_dest_len - doff;
      }

      strncpy(dest + doff, seg[i], num + 1);
      doff += num;
    } else if (type[i] == DOT) {
      /* if you get here, it indicates an algorithmic error in this routine */
      panic("ink_web_remove_dots - single dot remaining in string");
    } else if (type[i] == DOTDOT) {
      /* if you get here, it indicates an algorithmic error in this routine */
      panic("ink_web_remove_dots - double dot remaining in string");
    }
  }

  if (free_flag) {
    free(seg);
    free(type);
  }

  return (error);
}

/*---------------------------------------------------------------------------*

  int ink_web_unescapify_string(...)

  Takes a string that has has special characters turned to %AB format
  and converts them back to single special characters. See
  ink_web_escapify_string() above.

  mep - 4/15/96

  *---------------------------------------------------------------------------*/

static int
ink_web_unescapify_string(char *dest_in, char *src_in, int max_dest_len)
{
  char *src  = src_in;
  char *dest = dest_in;
  const char *c1;
  const char *c2;
  int quit   = 0;
  int dcount = 0;
  int num    = 0;
  int dig1   = 0;
  int dig2   = 0;

  while ((*src != 0) && !quit) {
    if (*src == '%') {
      /* found start of an escape sequence, unescape it */
      if ((*(src + 1) != 0) && (*(src + 2) != 0)) {
        c1 = strchr(hexdigits, *(src + 1));
        c2 = strchr(hexdigits, *(src + 2));
        if ((c1 == nullptr) || (c2 == nullptr)) {
          ink_warning("got escape sequence but no hex digits in:%s", src_in);
          if (dcount + 1 < max_dest_len) {
            *(dest++) = *src;
            dcount++;
          } else {
            ink_warning("ink_web_unescapify_string had to truncate:%s", src_in);
            quit = 1;
          }
        } else {
          /* check if hex digits lowercase */
          dig1 = (int)(c1 - hexdigits);
          dig2 = (int)(c2 - hexdigits);
          if (dig1 > 15) {
            dig1 -= 6;
          }
          if (dig2 > 15) {
            dig2 -= 6;
          }
          /* this is the ascii char */
          num = 16 * dig1 + dig2;

          if (!strchr(dontunescapify, num)) {
            /* unescapify the escape sequence you found */
            if (dcount + 1 < max_dest_len) {
              *(dest++) = num;
              dcount++;
              src += 2;
            } else {
              ink_warning("ink_web_escapify_string had to truncate:%s", src_in);
              quit = 1;
            }
          } else {
            /* don't unescapify these, just pass the escape sequence */
            if (dcount + 3 < max_dest_len) {
              *(dest++) = '%';
              *(dest++) = hexdigits[dig1];
              *(dest++) = hexdigits[dig2];
              dcount += 3;
              src += 2;
            } else {
              ink_warning("ink_web_unescapify_string had to truncate:%s", src_in);
              quit = 1;
            }
          }
        }
      } else {
        ink_warning("got escape sequence but no hex digits (too near end of string) in:%s", src_in);
        if (dcount + 1 < max_dest_len) {
          *dest++ = *src;
          dcount++;
        } else {
          ink_warning("ink_web_unescapify_string had to truncate:%s", src_in);
          quit = 1;
        }
      }
    } else {
      if (dcount + 1 < max_dest_len) {
        *dest++ = *src;
        dcount++;
      } else {
        ink_warning("ink_web_unescapify_string had to truncate:%s", src_in);
        quit = 1;
      }
    }
    src++;
  }
  /* terminate string */
  if (dcount < max_dest_len) {
    *dest = 0;
  } else {
    *(dest_in + max_dest_len) = 0;
  }

  return (quit);
}

/*---------------------------------------------------------------------------*

  int ink_web_escapify_string(...)

  This functions takes an input src_in and converts all special
  characters to %<hexdigit><hexdigit> form.

  Special characters are everything that is not:
  #$-_.+!*'(),;/?:@=&   or
  <alpha-char>  or
  <digit-char>

  e.g. "abcd fghi[klmn^" -> "abcd%20fghi%5Bklmn%5E"

  You must supply the buffer dest_in, with a size of max_dest_len. If
  the unescapified string grows larger than this, it will be truncated
  and you will get a warning.

  mep - 4/15/96

  *---------------------------------------------------------------------------*/

static int
ink_web_escapify_string(char *dest_in, char *src_in, int max_dest_len)
{
  int d1, d2;
  char *src  = src_in;
  char *dest = dest_in;
  int dcount = 0;
  int quit   = 0;

  while ((*src != 0) && (dcount < max_dest_len) && (quit == 0)) {
    if ((char *)memchr(dontescapify, *src, INT_MAX) || ParseRules::is_alpha(*src) || ParseRules::is_digit(*src)) {
      /* this is regular character, don't escapify it */
      if (dcount + 1 < max_dest_len) {
        *dest++ = *src;
        dcount++;
      } else {
        ink_warning("ink_web_escapify_string (1) had to truncate:'%s'", src_in);
        quit = 1;
      }
    } else {
      d1 = *src / 16;
      d2 = *src % 16;
      if (dcount + 3 < max_dest_len) {
        *dest++ = '%';
        *dest++ = hexdigits[d1];
        *dest++ = hexdigits[d2];
        /*      fprintf(stderr,"%d %d  %c %c\n",d1,d2,hexdigits[d1],hexdigits[d2]);*/
        dcount += 3;
      } else {
        ink_warning("ink_web_escapify_string (2) had to truncate:'%s'", src_in);
        quit = 1;
      }
    }
    src++;
  }
  /* terminate string */
  if (dcount < max_dest_len) {
    *dest = 0;
  } else {
    *(dest_in + max_dest_len - 1) = 0;
  }

  return (quit);
}
