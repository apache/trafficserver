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

/*************************** -*- Mod: C++ -*- *********************

  DoTest.h
******************************************************************/

#ifndef _DoTest_h_
#define _DoTest_h_
#include "Defines.h"
#include "LoadGenerator.h"
#include "Plugin.h"
#include "Hist.h"
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/time.h>

#include <openssl/rsa.h>        /* SSLeay stuff */
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#define CHK_NULL(x) if ((x)==NULL) exit (1)
#define CHK_ERR(err,s) if ((err)==-1) { perror(s); exit(1); }
#define CHK_SSL(err) if ((err)==-1) { ERR_print_errors_fp(errFp); exit(2); }

#define DIFF_TIME(start_time,end_time) \
	     (long) (((double) (end_time).tv_sec*1000.0 + \
             (double)  (end_time).tv_usec / 1000.0) - \
             ((double) (start_time).tv_sec*1000.0 + \
             (double)  (start_time).tv_usec / 1000.0))

enum StatusStatus
{
  READING_STATUS_LINE,
  READING_HEADERS,
  GOT_ONE_NEWLINE_IN_HEADERS,
  GOT_TWO_NEWLINES_IN_HEADERS,
  DONE_READING_HEADERS
};

struct StatusLineInfo
{
  int status_code;
  StatusStatus status_line_complete;
  int buffer_index;
  char buffer[MAX_STATUS_LEN];
};

struct UserInfo
{
  struct timeval transaction_start_time;
  struct timeval connect_time;
  struct timeval first_byte_time;
  struct timeval last_read_time;
  struct timeval transaction_end_time;
  struct timeval think_time_start;

  long target_byterate;
  long think_time;
  int pollin_count;
  int read_count;
  long bytes_read;
  long bytes_requested;
  StatusLineInfo status_line_info;
  char request_sent[MAX_REQUEST_SIZE];
  struct sockaddr_in *target_addr;
  int fd;

  /* for differentiate a completed/non-completed connection */
  int internal_rid;
  void *request_id;
  SSL *m_ssl;

  /////////////////////
#ifdef _PLUG_IN
  struct sockaddr_in dynamic_target_addr;
  long content_count;
  TSRequestAction action;
  TSConnectionStatus conn_status;
#endif
  /////////////////////

  int blocked;
};

struct DoTest
{

  // Stats variables
  int report_no;                /* Each report is labeled with a number */
  int fd_limit;

  double total_bytes_read;      /* total bytes read; only from successful resp */
  double total_bytes_requested; /* the bytes requested (excluding headers) */
  double total_bytes_read_including_partial_docs;
  long finished_requests;       /* number of successful requests */
  long requests_made;
  long failed_requests;         /* number of failed requests */
  long last_finished;           /* number of finished requests at last report */

  struct timeval start_time;    /* start time */
  struct timeval stop_time;     /* stop time */
  struct timeval reporting_time;        /* current time */
  struct timeval last_reporting_time;   /* time of the last report */

  long elapsed_time;            /* Time since start */
  long time_since_last_report;  /* Time since last report */
  double total_round_trip_time; /* Sum of all latencies */
  double total_first_byte_latency;      /* Sum of all first-byte latencies */
  double total_connect_time;    /* Sum of all connect times */
  long max_round_trip_time;     /* Max of all latencies */
  long min_round_trip_time;     /* Min of all latencies */
  long max_first_byte_latency;  /* Max First Byte Latency */
  long min_first_byte_latency;  /* Min First Byte Latency */
  long max_connect_time;        /* Max of all connect times */
  long min_connect_time;        /* Min of all connect times */

  long round_trip_time_cutoff;
  /* It will report how many latencies were above this cutoff (in msec) */
  long above_round_trip_time_cutoff;    /* Req above latency cutoff */
  long first_byte_latency_cutoff;
  /* It will report how many first byte latencies were above
     this cutoff (in msec) */
  long above_first_byte_latency_cutoff; /* Req above first-byte cutoff */
  long connect_time_cutoff;
  /* It will report how many connect times were above
     this cutoff (in msec) */
  long above_connect_time_cutoff;       /* Req above connect cutoff */

  int QOS_docsize;

  histogram round_trip_histogram;
  histogram first_byte_histogram;
  histogram connect_histogram;

  long generated_thinktime;     /* Total number of reqs generated */
  long thinktime_generated[MAX_THTSTIMES];     /* Number of reqs with
                                                   each thinktime */
  double total_actual_thinktime;

  long generated_target_byterate;       /* Total number of reqs generated */
  long target_byterate_generated[MAX_TARGET_BYTERATES];
  /* Number of reqs with each target_byterate */
  long num_limited_byterate;
  // number of transactions that had limited target_byterate
  double total_limited_byterate_error;
  // Total error among all the limited byterate trans
  int reporting_interval;

  int connections_open;
  int max_connections_open;

  double histogram_max;
  double histogram_resolution;
  // Config
  int num_thinktimes;           /* Number of thinktimes in the distribution */
  long *thinktimes;             /* actual thinktimes */
  double *cumulative_thinktime_prob;
  /* Cumulative probability of selecting different thinktimes
     cumulative_size_prob[num_thinktimes-1] must be 1.0 */

  int num_target_byterates;     /* Number of thinktimes in the distribution */
  long *target_byterates;       /* actual thinktimes */
  double *cumulative_target_byterate_prob;
  /* Cumulative probability of selecting different
     target_byterates
     cumulative_target_byterate_prob[num_thinktimes-1]
     must be 1.0 */

  UserInfo *user_info;
  int debug;                    /* Debugging Messages */
  int ssl;                      /* ssl flad  */

  int client_id;                /* Among all the client processes running what
                                   is my Id */
  LoadGenerator *load_generator;
  TSPlugin *plug_in;
  long warmup;
  // =0: real test, > 0 : warmup; create only warmup number of
  // requests
  int users;
  int poll_timeout;
  int keepalive;

  int request_rate;
  struct timeval rr_time;
  int total_reqs_last_poll;

  struct pollfd *poll_vector;
  char read_buf[MAX_READBUF_SIZE];

  X509 *server_cert;
  SSL_CTX *ctx;
  SSL_METHOD *meth;

  FILE *errFp;
  char *str;
  int err1;
  int flag_connect;
  int flag_write;
  int flag_read;
  int tempint;
  int randBuff[64];

  // Generate thinktime (in msecs from give distribution )
  long generate_think_time();

  long generate_target_byterate();
  long compute_bytes_to_read(int user, struct timeval current_time);
  // User i just finished, update stats
  void update_completion_stats(int i);
  int create_new_connection_and_send_request(int user, struct timeval current_time);
  void initialize_stats();
  void print_stats(int all);
  long get_request_rate(struct timeval t);
  void actual_test(int rr_flag);
    DoTest(int adebug,
           int assl,
           int aclient_id,
           LoadGenerator * aload_generator,
           long awarmup,
           int ausers,
           int apoll_timeout,
           int akeepalive,
           int anum_thinktimes,
           long *athinktimes,
           double *acumulative_thinktime_prob,
           int anum_target_byterates,
           long *atarget_byterates,
           double *acumulative_target_byterates_prob,
           int areporting_interval,
           double ahistogram_max,
           double ahistogram_resolution,
           long around_trip_time_cutoff,
           long afirst_byte_latency_cutoff,
           long aconnect_time_cutoff, int aQOS_docsize, TSPlugin * aplug_in, int arequest_rate);
  void report(char *metric, char *units, char *combiner, double value);

   ~DoTest()
  {
  }
};

extern DoTest *do_test;

#endif // #ifndef DoTest_h_
