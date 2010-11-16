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

  DoTest.cc
******************************************************************/
#include "DoTest.h"

#define TRUE 1
#define FALSE 0

// Intitialize Stats variables
void
DoTest::initialize_stats()
{
  int i;
  char prefix[20];
  struct rlimit rlp;

  report_no = 0;                /* Each report is labeled with a number */

  if (getrlimit(RLIMIT_NOFILE, &rlp) == 0) {
    fd_limit = rlp.rlim_cur;
  } else {
    fd_limit = 0;
  }

  total_bytes_read = 0;         /* total bytes read; only from successful resp */
  total_bytes_requested = 0;    /* the bytes requested (excluding headers) */
  total_bytes_read_including_partial_docs = 0;
  finished_requests = 0;        /* number of successful requests */
  requests_made = 0;
  failed_requests = 0;
  last_finished = 0;            /* number of finished requests at last report */
  elapsed_time = 0;             /* Time since start */
  time_since_last_report = 0;
  total_round_trip_time = 0;    /* Sum of all latencies */
  total_first_byte_latency = 0;
  total_connect_time = 0;
  max_round_trip_time = 0;
  min_round_trip_time = MAXLONG;
  max_first_byte_latency = 0;
  min_first_byte_latency = MAXLONG;
  max_connect_time = 0;
  min_connect_time = MAXLONG;
  above_round_trip_time_cutoff = 0;     /* Reqs above latency cutoff */
  above_first_byte_latency_cutoff = 0;  /* Reqs above first-byte cutoff */
  above_connect_time_cutoff = 0;        /* Reqs above connect cutoff */
  generated_thinktime = 0;      /* Total number of reqs generated */

  for (i = 0; i < MAX_THTSTIMES; i++) {
    thinktime_generated[i] = 0; /* Number of reqs with each thinktime */
  }

  total_actual_thinktime = 0;
  generated_target_byterate = 0;        /* Total number of reqs generated */

  for (i = 0; i < MAX_TARGET_BYTERATES; i++) {
    target_byterate_generated[i] = 0;
  }

  // number of transactions that had limited target_byterate
  num_limited_byterate = 0;

  // Total error among all the limited byterate trans
  total_limited_byterate_error = 0;

  connections_open = 0;

  // Max number of connections simultaneously open over benchmark duration
  max_connections_open = 0;

  sprintf(prefix, "= r %d ", client_id);
  histogram_new(&round_trip_histogram,
                "sec", prefix,
                (int) ((histogram_max - 0.0) / (double) histogram_resolution), 0.0 /* min */ , histogram_max);

  sprintf(prefix, "= f %d", client_id);
  histogram_new(&first_byte_histogram,
                "sec", prefix,
                (int) ((histogram_max - 0.0) / (double) histogram_resolution), 0.0 /* min */ , histogram_max);

  sprintf(prefix, "= c %d", client_id);
  histogram_new(&connect_histogram,
                "sec", prefix,
                (int) ((histogram_max - 0.0) / (double) histogram_resolution), 0.0 /* min */ , histogram_max);
}


int
DoTest::create_new_connection_and_send_request(int user, struct timeval current_time)
{
  int s, fd, status, len_to_write, len_written;

  user_info[user].bytes_read = 0;
  user_info[user].status_line_info.status_code = 0;
  user_info[user].status_line_info.status_line_complete = READING_STATUS_LINE;
  user_info[user].status_line_info.buffer_index = 0;
  user_info[user].status_line_info.buffer[0] = '\0';
  user_info[user].transaction_start_time = current_time;
  user_info[user].last_read_time = current_time;

  if (debug) {
    printf("creating connection for %d user\n", user);
  }

  while ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 && errno == EINTR);

  if (fd < 0) {
    char err[200];

    if (errno == EMFILE) {
      sprintf(err, "socket creation failure (current fd limit = %d)", fd_limit);
    } else {
      sprintf(err, "socket creation failure");
    }

    perror(err);

#ifdef _PLUG_IN
    user_info[user].conn_status = TS_CONN_ERR;
#endif

    return 0;
  }
  // Set the socket options here
  user_info[user].fd = fd;
  fcntl(fd, F_SETFD, O_NONBLOCK);

  while ((status = connect(fd, (struct sockaddr *) user_info[user].target_addr,
                           sizeof(struct sockaddr))) < 0 && ((errno == EINTR)));

  gettimeofday(&current_time, NULL);
  user_info[user].connect_time = current_time;

  if ((status < 0) && (errno != EISCONN)) {
    perror("Error: connect");

#ifdef _PLUG_IN
    user_info[user].conn_status = TS_CONN_ERR;
#endif

    return 0;
  } else {

#ifdef _PLUG_IN
    user_info[user].internal_rid = 1;
#endif

    connections_open++;
    if (connections_open > max_connections_open) {
      max_connections_open = connections_open;
    }
    if (debug) {
      printf("connections_open = %d  max_connections_open = %d\n", connections_open, max_connections_open);
    }
  }

  if (debug) {
    struct timeval tv;
    tv = current_time;

    printf("Connection opened sec: %ld ms: %ld\n", tv.tv_sec, tv.tv_usec);
    fflush(stdout);
  }
  if (ssl) {
    RAND_seed((void *) randBuff, sizeof(randBuff));
    user_info[user].m_ssl = SSL_new(ctx);
    CHK_NULL(user_info[user].m_ssl);

    if (debug) {
      printf("DEBUG completed call to SSL_new\n");
    }
    SSL_set_fd(user_info[user].m_ssl, fd);

    do {
      err1 = SSL_connect(user_info[user].m_ssl);
      flag_connect = SSL_get_error(user_info[user].m_ssl, err1);
    } while (flag_connect != SSL_ERROR_NONE);

    /* Get server's certificate (note: beware of dynamic allocation) - opt */

    server_cert = SSL_get_peer_certificate(user_info[user].m_ssl);
    CHK_NULL(server_cert);
    if (debug) {
      printf("Server certificate:\n");
    }
    str = X509_NAME_oneline(X509_get_subject_name(server_cert), 0, 0);
    CHK_NULL(str);
    if (debug) {
      printf("\t subject: %s\n", str);
    }
    str = 0;

    str = X509_NAME_oneline(X509_get_issuer_name(server_cert), 0, 0);
    CHK_NULL(str);
    if (debug) {
      printf("\t issuer: %s\n", str);
    }
    str = 0;

    /* We could do all sorts of certificate verification stuff here before
       deallocating the certificate. */

    X509_free(server_cert);
  }

  len_to_write = strlen(user_info[user].request_sent);
  len_written = 0;

  if (debug) {
    printf("sending request [%s] on bucket %d (len_to_write %d) \n", user_info[user].request_sent, user, len_to_write);
  }

  while (len_to_write > len_written) {

    if (ssl) {
      s = SSL_write(user_info[user].m_ssl, user_info[user].request_sent + len_written, len_to_write - len_written);
      flag_write = SSL_get_error(user_info[user].m_ssl, s);
      CHK_SSL(s);
      if (debug) {
        if (SSL_ERROR_NONE == flag_write) {
          printf("ERR_NONE\n");
        } else if (SSL_ERROR_ZERO_RETURN == flag_write) {
          printf("ZERO_RET\n");
        } else if (SSL_ERROR_WANT_READ == flag_write) {
          printf("WANT_READ\n");
        } else {
          printf("OTHER\n");
        }
      }
    } else {
      while ((s = write(fd, (user_info[user].request_sent + len_written), (len_to_write -
                                                                           len_written))) < 0 && errno == EINTR);
    }
    if (s <= 0) {
      perror("Error: write");

#ifdef _PLUG_IN
      user_info[user].conn_status = TS_WRITE_ERR;
#endif

      return 0;
    }

    len_written += s;
  }

  if (debug) {
    printf("Sent request on bucket %d and set up poll stuct \n", user);
  }

  return 1;
}


// User i just finished, update stats
void
DoTest::update_completion_stats(int i)
{

  long rate_achieved;
  long round_trip_time, first_byte_latency, connect_time;
  total_bytes_read += user_info[i].bytes_read;
  total_bytes_requested += user_info[i].bytes_requested;

  round_trip_time = DIFF_TIME(user_info[i].transaction_start_time, user_info[i].transaction_end_time);
  connect_time = DIFF_TIME(user_info[i].transaction_start_time, user_info[i].connect_time);

  // Might want to redefine first_byte_latency to start after the connect
  first_byte_latency = DIFF_TIME(user_info[i].transaction_start_time, user_info[i].first_byte_time);

  total_round_trip_time += round_trip_time;
  total_first_byte_latency += first_byte_latency;
  total_connect_time += connect_time;

  if (debug) {
    printf("round_trip %ld first_byte %ld connect %ld\n", round_trip_time, first_byte_latency, connect_time);
  }

  if (round_trip_time > max_round_trip_time) {
    max_round_trip_time = round_trip_time;
  }

  if (round_trip_time < min_round_trip_time) {
    min_round_trip_time = round_trip_time;
  }

  if (first_byte_latency > max_first_byte_latency) {
    max_first_byte_latency = first_byte_latency;
  }

  if (first_byte_latency < min_first_byte_latency) {
    min_first_byte_latency = first_byte_latency;
  }

  if (connect_time > max_connect_time) {
    max_connect_time = connect_time;
  }

  if (connect_time < min_connect_time) {
    min_connect_time = connect_time;
  }

  if (round_trip_time > round_trip_time_cutoff) {
    above_round_trip_time_cutoff++;
  }

  if (first_byte_latency > first_byte_latency_cutoff) {
    above_first_byte_latency_cutoff++;
  }

  if (connect_time > connect_time_cutoff) {
    above_connect_time_cutoff++;
  }

  if ((QOS_docsize == 0) || (user_info[i].bytes_requested == QOS_docsize)) {

    histogram_point(&round_trip_histogram, (double) round_trip_time * 0.001);
    histogram_point(&first_byte_histogram, (double) first_byte_latency * 0.001);
    histogram_point(&connect_histogram, (double) connect_time * 0.001);

  } else {
    // Skipping histogram ...
  }

  assert(user_info[i].target_byterate != -2);

  if (user_info[i].target_byterate != -1) {

    num_limited_byterate++;

    // rate in bytes/sec
    rate_achieved = user_info[i].bytes_read * 1000 / round_trip_time;

    if (rate_achieved > user_info[i].target_byterate) {
      total_limited_byterate_error += rate_achieved - user_info[i].target_byterate;
    } else {
      total_limited_byterate_error += user_info[i].target_byterate - rate_achieved;
    }

  }

}


// Compute how many bytes to read based on target_byterate
long
DoTest::compute_bytes_to_read(int user, struct timeval current_time)
{
  long should_have_read, to_read;
  long user_elapsed_time;

  // TSqa04029 tripped this
  assert(user_info[user].target_byterate != -2);        // Illegal value

  if (user_info[user].target_byterate == -1) {
    return MAXLONG;
  }
  // elapsed time in msec
  user_elapsed_time = DIFF_TIME(user_info[user].transaction_start_time, current_time);
  // Remember target is in bytes/sec
  should_have_read = user_info[user].target_byterate * user_elapsed_time / 1000;

  to_read = should_have_read - user_info[user].bytes_read;

  if (debug) {
    printf("Compute bytes to read: \n");
    printf("\t start time %ld sec %ld ms \n",
           user_info[user].transaction_start_time.tv_sec, user_info[user].transaction_start_time.tv_usec);
    printf("\t current time %ld sec %ld ms \n", current_time.tv_sec, current_time.tv_usec);
    printf("User %d Elapsed time %ld target %ld should have %ld actual %ld to_read %ld \n",
           user, user_elapsed_time, user_info[user].target_byterate,
           should_have_read, user_info[user].bytes_read, to_read);
  }

  return (to_read);
}


// Generate target byterate (in bytes/sec from give distribution )
long
DoTest::generate_target_byterate()
{

  double rand;
  int i;
  long target_byterate;

  rand = drand48();

  for (i = 0; i<num_target_byterates && rand> cumulative_target_byterate_prob[i]; i++);

  if (i == num_target_byterates) {

    fprintf(stderr, "Error: drand48() generated greater than 1.0 %f in generate_target_byterate\n", rand);

    for (i = 0; i < num_target_byterates; i++)
      printf("cumulative_target_byterate_prob[%d] = %f\n", i, cumulative_target_byterate_prob[i]);

    exit(1);
  }

  target_byterate_generated[i]++;
  generated_target_byterate++;
  target_byterate = target_byterates[i];

  if (debug) {
    printf("generated target byterate %ld bytes/sec (i %d num_target_byterates %d)\n", target_byterate, i,
           num_target_byterates);
  }

  return target_byterate;
}


// Generate thinktime (in msecs from give distribution )
long
DoTest::generate_think_time()
{

  double rand;
  int i;
  long thinktime;

  rand = drand48();

  for (i = 0; i<num_thinktimes && rand> cumulative_thinktime_prob[i]; i++);

  if (i == num_thinktimes) {

    fprintf(stderr, "Error: drand48() generated greater than 1.0 %g in generate_think_time\n", rand);

    for (i = 0; i < num_thinktimes; i++)
      printf("cumulative_thinktime_prob[%d] = %f\n", i, cumulative_thinktime_prob[i]);

    exit(1);
  }

  thinktime_generated[i]++;
  generated_thinktime++;
  thinktime = thinktimes[i];

  if (debug) {
    printf("generated thinktime %ld msec\n", thinktime);
  }

  return thinktime;
}

// Constructor
DoTest::DoTest(int adebug,
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
               double *acumulative_target_byterate_prob,
               int areporting_interval,
               double ahistogram_max,
               double ahistogram_resolution,
               long around_trip_time_cutoff,
               long afirst_byte_latency_cutoff,
               long aconnect_time_cutoff, int aQOS_docsize, TSPlugin * aplug_in, int arequest_rate)
{

  debug = adebug;
  ssl = assl;
  client_id = aclient_id;
  load_generator = aload_generator;
  warmup = awarmup;
  users = ausers;
  poll_timeout = apoll_timeout;
  keepalive = akeepalive;
  num_thinktimes = anum_thinktimes;
  thinktimes = athinktimes;
  cumulative_thinktime_prob = acumulative_thinktime_prob;
  num_target_byterates = anum_target_byterates;
  target_byterates = atarget_byterates;
  cumulative_target_byterate_prob = acumulative_target_byterate_prob;
  reporting_interval = areporting_interval;
  histogram_max = ahistogram_max;
  histogram_resolution = ahistogram_resolution;
  round_trip_time_cutoff = around_trip_time_cutoff;
  first_byte_latency_cutoff = afirst_byte_latency_cutoff;
  connect_time_cutoff = aconnect_time_cutoff;
  QOS_docsize = aQOS_docsize;

  request_rate = arequest_rate;

#ifdef _PLUG_IN
  plug_in = aplug_in;
#endif

  initialize_stats();
}


long
DoTest::get_request_rate(struct timeval last_poll_time)
{

  struct timeval now;
  long interval;
  long rate = 0;

  gettimeofday(&now, NULL);

  interval = DIFF_TIME(start_time, now);
  rate = (interval * request_rate / 1000 - requests_made);

  return rate;
}

void
DoTest::actual_test(int rr_flag)
{

  int i, poll_rv, s;
  long current_think_time, bytes_to_read;
  struct timeval current_time;
  struct timeval last_time;
  double warmup_status;

  if (ssl) {
    if (debug) {
      printf("Initializing SSL algorithms and methods ...\n");
    }
    /* SSL initialization stuff */
    SSLeay_add_ssl_algorithms();
    SSL_load_error_strings();
    meth = SSLv23_client_method();
    ctx = SSL_CTX_new(meth);
    CHK_NULL(ctx);
  }

  int more_request = TRUE;

  if (((user_info = (UserInfo *) malloc((users + 1) * sizeof(UserInfo))) == NULL) ||
      ((poll_vector = (struct pollfd *) malloc((users + 1) * sizeof(struct pollfd))) == NULL)) {

    fprintf(stderr, "Error: Can't allocate memory for %d users\n", users);
    exit(1);
  }

  fprintf(stderr, "Client %2d started.\n", client_id);

  // Initialize the start time for the report
  gettimeofday(&start_time, NULL);
  gettimeofday(&last_time, NULL);
  memcpy((&last_reporting_time), (&start_time), sizeof(struct timeval));

  // initialize
  for (i = 0; i < users; i++) {

    user_info[i].bytes_read = 0;
    user_info[i].bytes_requested = -1;
    user_info[i].fd = -1;
    poll_vector[i].fd = -1;
    poll_vector[i].revents = 0;
    user_info[i].think_time = 0;
    user_info[i].target_byterate = -2;
    user_info[i].blocked = 0;

#ifdef _PLUG_IN
    user_info[i].internal_rid = 0;
    user_info[i].request_id = NULL;
    user_info[i].target_addr = &(user_info[i].dynamic_target_addr);
    user_info[i].content_count = 0;
    user_info[i].action = TS_KEEP_GOING;
    user_info[i].conn_status = TS_CONN_COMPLETE;
#endif
  }

  gettimeofday(&rr_time, NULL);

  for (i = 0; i < users; i++) {

    gettimeofday(&current_time, NULL);

    if (!warmup || (warmup && requests_made < warmup)) {
      load_generator->generate_new_request(user_info[i].request_sent,
                                           &(user_info[i].request_id),
                                           &(user_info[i].bytes_requested), &(user_info[i].target_addr));

#ifdef _PLUG_IN
      // if no more request is generated, print stats and return
      if (strcmp(user_info[i].request_sent, "") == 0) {
        more_request = FALSE;
      }
#endif

      if (more_request) {
        if (create_new_connection_and_send_request(i, current_time)) {
          user_info[i].target_byterate = generate_target_byterate();
          poll_vector[i].fd = user_info[i].fd;
          poll_vector[i].events = POLLIN;
          poll_vector[i].revents = 0;
        } else {
          user_info[i].target_byterate = generate_target_byterate();
          poll_vector[i].fd = -1;
          poll_vector[i].events = POLLIN;
          poll_vector[i].revents = 0;
        }
        requests_made++;
      }

    }

  }

  if (connections_open == 0) {
    fprintf(stderr, "Error: unable to make any connections.  Aborting.\n");
    exit(1);
  }

  if (debug) {
    printf("Starting do_test for %d users \n", users);
  }

  warmup_status = 0;
  while ((!warmup && more_request) || (warmup && ((finished_requests + failed_requests) < warmup) && more_request)) {

    if (warmup && (total_bytes_read_including_partial_docs >= warmup_status)) {
      fprintf(stderr, "Warmup: %5.0f Mbyte (%7ld of %7ld documents) finished\n",
              total_bytes_read_including_partial_docs / (1024.0 * 1024.0), finished_requests, warmup);
      warmup_status += 10 * 1024 * 1024;
    }

    if (debug) {
      printf("Going into poll with %d users, timeout %d\n", users, poll_timeout);
    }

    while ((poll_rv = poll(poll_vector, users, poll_timeout)) < 0 && errno == EINTR);

    if (rr_flag) {
      gettimeofday(&rr_time, NULL);
      total_reqs_last_poll = requests_made;
    }

    if (debug) {
      printf("Came out of poll with %d return value\n", poll_rv);
    }

    if (poll_rv < 0) {
      perror("Error: Poll Error");
      exit(1);
    }

    gettimeofday(&current_time, NULL);

    if (DIFF_TIME(last_time, current_time) > (1000 * reporting_interval)) {
      print_stats(0);
      gettimeofday(&last_time, NULL);
    }

    for (i = 0; i < users; i++) {

      if (poll_vector[i].revents & (POLLOUT | POLLERR | POLLNVAL | POLLHUP)) {
        if (poll_vector[i].revents & POLLHUP) {
          // Quietly close connection
        } else {
          fprintf(stderr, "Error: file descriptor %d ", i);

          if (poll_vector[i].revents & POLLOUT) {
            fprintf(stderr, "got event POLLOUT\n");
          }
          if (poll_vector[i].revents & POLLERR) {
            fprintf(stderr, "got event POLLERR\n");
          }
          if (poll_vector[i].revents & POLLNVAL) {
            fprintf(stderr, "got event POLLNVAL\n");
          }
        }
        goto CONN_FINISH;
      }

      gettimeofday(&current_time, NULL);

      if (poll_vector[i].revents & POLLIN) {    /* i is ready */

        user_info[i].pollin_count++;
        bytes_to_read = compute_bytes_to_read(i, current_time);

        if (debug) {
          printf("bucket %d ready for read.. reading %ld bytes\n", i, bytes_to_read);
        }

        if (bytes_to_read == 0)
          continue;

        if (bytes_to_read > MAX_READBUF_SIZE)
          bytes_to_read = MAX_READBUF_SIZE;

        if (ssl) {
          tempint = 0;
          do {
            s = SSL_read(user_info[i].m_ssl, read_buf, bytes_to_read);
            flag_read = SSL_get_error(user_info[i].m_ssl, s);
            if (debug) {
              if (s <= 0) {     //didn't get in here
                if (SSL_ERROR_NONE == flag_read) {
                  printf("ERR_NONE\n");
                } else if (SSL_ERROR_ZERO_RETURN == flag_read) {
                  printf("ZERO_RET\n");
                } else if (SSL_ERROR_WANT_READ == flag_read) {
                  printf("WANT_READ\n");
                } else if (SSL_ERROR_WANT_X509_LOOKUP == flag_read) {
                  printf("SSL_ERROR_WANT_X509_LOOKUP\n");
                } else if (SSL_ERROR_SYSCALL == flag_read) {
                  printf("SSL_ERROR_SYSCALL\n");
                } else if (SSL_ERROR_SSL == flag_read) {
                  printf("SSL_ERROR_SSL\n");
                }
              }
            }
            if (-1 != s)
              tempint += s;
            if (debug) {
              printf("s: %d tempint: %d \n", s, tempint);
            }
          } while (flag_read == SSL_ERROR_NONE);
          s = tempint;
        } else {
          while ((s = read(poll_vector[i].fd, read_buf, bytes_to_read)) < 0 && errno == EINTR);
        }

        if (s < 0) {
          perror("Error: read");

#ifdef _PLUG_IN
          user_info[i].conn_status = TS_READ_ERR;
#endif

          // Equivalent to closing the connection, as far as we r concerned
          s = 0;
        }

        gettimeofday(&current_time, NULL);

        if (s == 0) {           /* connection finished */

        CONN_FINISH:
          if (debug) {
            printf("User %d closed (total requests finished %ld). Requested %ld Read %ld bytes \n",
                   i, finished_requests, user_info[i].bytes_requested, user_info[i].bytes_read);
          }

          while (close(poll_vector[i].fd) < 0 && errno == EINTR);
          connections_open--;

          if (debug) {
            printf("connections_open = %d  max_connections_open = %d\n", connections_open, max_connections_open);
          }

          assert(connections_open >= 0);
          user_info[i].transaction_end_time = current_time;

#ifdef _PLUG_IN
          // for indicating to plugin that no more content is returned
          if ((user_info[i].status_line_info.status_code == 200 ||
               user_info[i].status_line_info.status_code == 0) && user_info[i].action == TS_KEEP_GOING) {
            if (plug_in->partial_body_process_fcn) {
              (plug_in->partial_body_process_fcn) (user_info[i].request_id, (void *) "", 0, user_info[i].content_count);
            }
          }
          if (plug_in->connection_finish_fcn) {
            (plug_in->connection_finish_fcn) (user_info[i].request_id, user_info[i].conn_status);
          }
          user_info[i].conn_status = TS_CONN_COMPLETE;
          user_info[i].request_id = NULL;
          user_info[i].internal_rid = 0;
#endif

          if (debug) {
            struct timeval tv;
            tv = current_time;
            printf("Connection closed sec: %ld ms: %ld\n", tv.tv_sec, tv.tv_usec);
            fflush(stdout);
          }
#ifdef ERROR_TEST
          if ((random() % 1000) == 0) {
            printf("Artificial fault injected to test status code\n");
            /* user_info[i].status_line_info.status_code = 404; */
            user_info[i].bytes_requested = 999999;
          }
#endif

#ifdef _PLUG_IN
          if (user_info[i].action == TS_STOP_SUCCESS) {
            finished_requests += keepalive;
            update_completion_stats(i);
            // doesn't support keepalive yet... => keepalive == 1
            // 200 response but not an error: close connection requested from plugins
          } else if (user_info[i].action == TS_STOP_FAIL) {
            failed_requests += keepalive;
          } else if (user_info[i].status_line_info.status_code != 200 && user_info[i].status_line_info.status_code != 0) {
            failed_requests += keepalive;
          }
#else
          /* Check for errors */
          if (user_info[i].status_line_info.status_code != 200 && user_info[i].status_line_info.status_code != 0) {
            fprintf(stderr, "Error: user %d got non-200 response: %s; Request sent [%s]",
                    i, user_info[i].status_line_info.buffer, user_info[i].request_sent);
            failed_requests += keepalive;
          }
#endif
          else if (user_info[i].bytes_read >= user_info[i].bytes_requested) {
            /* Transaction has successfully completed. Record the
               stats */
            if (debug) {
              printf("user_info[%d].bytes_read = %d   user_info[%d].bytes_requested = %d\n",
                     i, (int) user_info[i].bytes_read, i, (int) user_info[i].bytes_requested);
            }
            finished_requests += keepalive;
            update_completion_stats(i);
          } else {              // 200 response
            fprintf(stderr,
                    "Error: user %d got 200 response:\n%s but got only %ld bytes when %ld bytes were requested;\nRequest sent [%s]\n%ld msec since connection opened; %ld msec since last read\nPOLLIN count = %d;  number of read calls returning data = %d\n\n",
                    i, user_info[i].status_line_info.buffer, user_info[i].bytes_read, user_info[i].bytes_requested,
                    user_info[i].request_sent, (long) (DIFF_TIME(user_info[i].transaction_start_time, current_time)),
                    (long) (DIFF_TIME(user_info[i].last_read_time, current_time)), user_info[i].pollin_count,
                    user_info[i].read_count);
            failed_requests += keepalive;
          }
          poll_vector[i].fd = -1;
          poll_vector[i].revents = 0;
          user_info[i].fd = -1;
          user_info[i].bytes_requested = -1;
          user_info[i].target_byterate = -2;
          user_info[i].bytes_read = 0;
          user_info[i].pollin_count = 0;
          user_info[i].read_count = 0;
          strcpy(user_info[i].request_sent, "");
          user_info[i].think_time = generate_think_time();

          if (ssl) {
            SSL_free(user_info[i].m_ssl);
          }

#ifdef _PLUG_IN
          user_info[i].target_addr = &(user_info[i].dynamic_target_addr);
          user_info[i].content_count = 0;
          user_info[i].action = TS_KEEP_GOING;
#endif

          if (user_info[i].think_time > 0) {
            user_info[i].think_time_start = current_time;
            //gettimeofday(&(user_info[i].think_time_start), NULL);
            assert(poll_vector[i].fd == -1);
            assert(user_info[i].fd == -1);
          } else {

            // Now start a new transaction for that user
            if (!warmup || (warmup && requests_made < warmup)) {
              if (!rr_flag || (get_request_rate(rr_time) > 0)) {
                load_generator->generate_new_request(user_info[i].request_sent,
                                                     &(user_info[i].request_id),
                                                     &(user_info[i].bytes_requested), &(user_info[i].target_addr));
#ifdef _PLUG_IN
                // if no more request is generated, print stats and return
                if (strcmp(user_info[i].request_sent, "") == 0) {
                  more_request = FALSE;
                }
#endif

                if (more_request) {
                  if (create_new_connection_and_send_request(i, current_time)) {
                    user_info[i].target_byterate = generate_target_byterate();
                    poll_vector[i].fd = user_info[i].fd;
                    poll_vector[i].events = POLLIN;
                    poll_vector[i].revents = 0;
                  } else {
                    user_info[i].target_byterate = generate_target_byterate();
                    poll_vector[i].fd = -1;
                    poll_vector[i].events = POLLIN;
                    poll_vector[i].revents = 0;
                  }
                  requests_made++;
                }
              } else {
                user_info[i].blocked = 1;
              }

            }

          }

        } else {                // s > 0
          /* Transaction is not complete yet. You just read some
             bytes so update stats.. */

          int j;

#ifdef _PLUG_IN
          char *partial_body = NULL;
          int partial_length = 0;
#endif

          StatusLineInfo *sli = &(user_info[i].status_line_info);

          if (user_info[i].bytes_read == 0) {
            user_info[i].first_byte_time = current_time;
          }

          user_info[i].bytes_read += s;
          total_bytes_read_including_partial_docs += s;
          user_info[i].read_count++;
          user_info[i].last_read_time = current_time;

          if (sli->status_line_complete != DONE_READING_HEADERS) {

            for (j = 0; j < s; j++) {

              sli->buffer[sli->buffer_index++] = read_buf[j];

              if (sli->buffer_index >= MAX_STATUS_LEN - 1) {
                sli->buffer[MAX_STATUS_LEN - 1] = '\0';
                sli->status_line_complete = DONE_READING_HEADERS;
                break;
              } else if (sli->status_line_complete == READING_STATUS_LINE) {
                if (read_buf[j] == '\n') {
                  sli->buffer[sli->buffer_index] = '\0';
                  sli->status_line_complete = READING_HEADERS;
                  if (debug)
                    printf("user %d got: %s", i, sli->buffer);
                  sscanf(sli->buffer, "%*s %d", &(sli->status_code));
                }
              } else if (sli->status_line_complete == READING_HEADERS) {
                if ((read_buf[j] == '\n') || (read_buf[j] == '\r')) {
                  sli->buffer[sli->buffer_index] = '\0';
                  sli->status_line_complete = GOT_ONE_NEWLINE_IN_HEADERS;
                }
              } else if (sli->status_line_complete == GOT_ONE_NEWLINE_IN_HEADERS) {
                if ((read_buf[j] == '\n') || (read_buf[j] == '\r')) {
                  sli->status_line_complete = GOT_TWO_NEWLINES_IN_HEADERS;
                } else {
                  sli->status_line_complete = READING_HEADERS;
                }
              } else if (sli->status_line_complete == GOT_TWO_NEWLINES_IN_HEADERS) {
                if ((read_buf[j] == '\n') || (read_buf[j] == '\r')) {

                  // just finish reading the header
#ifdef _PLUG_IN
                  if (sli->status_line_complete != DONE_READING_HEADERS) {

                    if (j + 2 < s) {    // j not the last byte read
                      partial_body = &read_buf[j + 2];
                      partial_length = s - (j + 2);
                    } else {
                      partial_length = 0;
                      partial_body = &read_buf[j + 2];
                    }

                    if (plug_in->header_process_fcn) {
                      user_info[i].action =
                        (plug_in->header_process_fcn) (user_info[i].request_id,
                                                       sli->buffer, sli->buffer_index + 1, user_info[i].request_sent);
                    }

                    sli->status_line_complete = DONE_READING_HEADERS;
                    if (user_info[i].action == TS_STOP_SUCCESS || user_info[i].action == TS_STOP_FAIL) {
                      goto CONN_FINISH;
                    }
                    break;
                  }
#else
                  sli->status_line_complete = DONE_READING_HEADERS;
                  break;
#endif
                } else {
                  sli->status_line_complete = READING_HEADERS;
                }
              }
            }
            if (debug && (sli->status_line_complete == DONE_READING_HEADERS)) {
              printf("user %d full headers: %s\n", i, sli->buffer);
            }
          }
#ifdef _PLUG_IN
          else {
            partial_body = read_buf;
            partial_length = s;
          }

          if (sli->status_line_complete == DONE_READING_HEADERS && user_info[i].action == TS_KEEP_GOING) {
            if ((sli->status_code == 200 || sli->status_code == 0)
                && partial_length > 0) {
              user_info[i].content_count += partial_length;
              if (plug_in->partial_body_process_fcn) {
                user_info[i].action =
                  (plug_in->partial_body_process_fcn) (user_info[i].request_id,
                                                       (void *) partial_body,
                                                       partial_length, user_info[i].content_count);
              }
              if (user_info[i].action == TS_STOP_SUCCESS || user_info[i].action == TS_STOP_FAIL) {
                goto CONN_FINISH;
              }
            }
          }
#endif

          if (debug) {
            printf("Bucket %d  read %d bytes (total %ld) \n", i, s, user_info[i].bytes_read);
          }

        }

      } else {                  // Not ready
        if (user_info[i].think_time > 0) {      // See if its time to start

          assert(user_info[i].fd == -1);
          assert(poll_vector[i].fd == -1);
          assert(user_info[i].bytes_read == 0);
          assert(user_info[i].bytes_requested == -1);
          assert(user_info[i].target_byterate == -2);

          current_think_time = DIFF_TIME(user_info[i].think_time_start, current_time);
          assert(current_think_time >= 0);

          if (debug) {
            printf("User %d current_think_time %ld target think_time %ld\n",
                   i, current_think_time, user_info[i].think_time);
          }

          if (current_think_time > user_info[i].think_time) {
            // Start the request
            total_actual_thinktime += current_think_time;
            user_info[i].think_time = 0;
            load_generator->generate_new_request(user_info[i].request_sent,
                                                 &(user_info[i].request_id),
                                                 &(user_info[i].bytes_requested), &(user_info[i].target_addr));

#ifdef _PLUG_IN
            // if no more request is generated, print stats and return
            if (strcmp(user_info[i].request_sent, "") == 0) {
              more_request = FALSE;
            }
#endif

            if (more_request) {
              if (create_new_connection_and_send_request(i, current_time)) {
                user_info[i].target_byterate = generate_target_byterate();
                poll_vector[i].fd = user_info[i].fd;
                poll_vector[i].events = POLLIN;
                poll_vector[i].revents = 0;
              } else {
                user_info[i].target_byterate = generate_target_byterate();
                poll_vector[i].fd = -1;
                poll_vector[i].events = POLLIN;
                poll_vector[i].revents = 0;
              }
              requests_made++;
            }
          }
        } else if (user_info[i].blocked) {
          assert(rr_flag);
          assert(user_info[i].fd == -1);
          assert(poll_vector[i].fd == -1);
          assert(user_info[i].bytes_read == 0);
          assert(user_info[i].bytes_requested == -1);
          assert(user_info[i].target_byterate == -2);
          struct timeval temp_now;
          gettimeofday(&temp_now, NULL);
          if (get_request_rate(rr_time) > 0) {
            user_info[i].blocked = 0;
            load_generator->generate_new_request(user_info[i].request_sent,
                                                 &(user_info[i].request_id),
                                                 &(user_info[i].bytes_requested), &(user_info[i].target_addr));
#ifdef _PLUG_IN
            // if no more request is generated, print stats and return
            if (strcmp(user_info[i].request_sent, "") == 0) {
              more_request = FALSE;
            }
#endif
            if (more_request) {
              if (create_new_connection_and_send_request(i, current_time)) {
                user_info[i].target_byterate = generate_target_byterate();
                poll_vector[i].fd = user_info[i].fd;
                poll_vector[i].events = POLLIN;
                poll_vector[i].revents = 0;
              } else {
                user_info[i].target_byterate = generate_target_byterate();
                poll_vector[i].fd = -1;
                poll_vector[i].events = POLLIN;
                poll_vector[i].revents = 0;
              }
              requests_made++;
            }
          }
        }

      }
    }
  }

  if (warmup) {
    fprintf(stderr, "Warmup: %5.0f Mbyte (%7ld of %7ld documents) finished\n",
            total_bytes_read_including_partial_docs / (1024.0 * 1024.0), finished_requests, warmup);
    warmup_status += 10 * 1024 * 1024;
  }
#ifdef _PLUG_IN
  for (i = 0; i < do_test->users; i++) {
    if (do_test->user_info[i].internal_rid) {
      if (plug_in->connection_finish_fcn) {
        (plug_in->connection_finish_fcn) (do_test->user_info[i].request_id, TS_TIME_EXPIRE);
      }
    }
  }
  if (plug_in->plugin_finish_fcn) {
    (plug_in->plugin_finish_fcn) ();
  }
  delete plug_in;
#endif

  print_stats(1);
}


void
DoTest::report(char *metric, char *units, char *combiner, double value)
{
  printf("Client %2d ", client_id);
  printf("%30s = %9.0f %8s %10s\n", metric, value, units, combiner);
}


void
DoTest::print_stats(int all)
{
  int i;
  double total_in_progress_time;
  long transactions_in_progress;
  double avg_requested_think_time = 0.0;
  char s[200];

  if (all) {
    fprintf(stderr, "Client %2d done.\n", client_id);
    printf("Finished %ld requests Failed %ld requests\n", finished_requests, failed_requests);
  }

  if (warmup)
    return;

  if (all) {
    for (i = 0; i < num_thinktimes; i++) {
      printf("\t ThinkTime %d (%ld msec): %ld (%.2f%%)\n", i,
             thinktimes[i], thinktime_generated[i],
             generated_thinktime ? thinktime_generated[i] * 100.0 / generated_thinktime : 0);
      avg_requested_think_time +=
        generated_thinktime ? ((thinktimes[i] * thinktime_generated[i]) * 1.0 / generated_thinktime) : 0;
    }
    printf("Average Requested Think Time %.2f, Actual Average %.2f\n",
           avg_requested_think_time, requests_made ? total_actual_thinktime * 1.0 / requests_made : 0);
    for (i = 0; i < num_target_byterates; i++) {
      printf("\t ByteRate %d (%ld bytes/s): %ld (%.2f%%)\n", i,
             target_byterates[i], target_byterate_generated[i],
             generated_target_byterate ? target_byterate_generated[i] * 100.0 / generated_target_byterate : 0);
    }
    printf("Average Byte Rate difference(target %% achieved) for limited byte rates %.2f\n",
           num_limited_byterate ? total_limited_byterate_error * 1.0 / num_limited_byterate : 0);
  }

  gettimeofday(&reporting_time, NULL);
  elapsed_time = DIFF_TIME(start_time, reporting_time);
  time_since_last_report = DIFF_TIME(last_reporting_time, reporting_time);
  memcpy(&last_reporting_time, &reporting_time, sizeof(struct timeval));

  transactions_in_progress = 0;
  total_in_progress_time = 0;

  for (i = 0; i < users; i++) {
    if (user_info[i].bytes_requested != -1) {
      transactions_in_progress++;
      total_in_progress_time += DIFF_TIME((user_info[i].transaction_start_time), reporting_time);
    }
  }

  if (all) {
    printf("Connect time distribution:\n");
    histogram_display(&connect_histogram);
    printf("First byte time distribution:\n");
    histogram_display(&first_byte_histogram);
    printf("Round trip time distribution:\n");
    histogram_display(&round_trip_histogram);

#define safediv(top,bottom) ((bottom) ? (((double)(top))/((double)(bottom))) : (bottom))


    report("Elapsed time", "msec", "max", (double) elapsed_time);
    report("Requests", "count", "sum", (double) finished_requests);
    report("Cumulative rate", "op/sec", "sum", (double) safediv(finished_requests * 1000.0, elapsed_time));
    report("Cumulative throughput", "byte/sec", "sum", (double) safediv(total_bytes_read * 1000.0, elapsed_time));
    report("Cumulative Mbit throughput", "Mbit/sec", "sum",
           (double) safediv(total_bytes_read * 8.0, elapsed_time * 1000.0));
    report("Bytes requested per request", "byte", "ave Requests",
           (double) safediv(total_bytes_requested, finished_requests));
    report("Bytes received per request", "byte", "ave Requests", (double) safediv(total_bytes_read, finished_requests));

    report("%time in blocking connect", "percent", "ave Elapsed time",
           (double) safediv(total_connect_time * 100.0, elapsed_time));
    report("Average connect time", "msec", "ave Requests", (double) safediv(total_connect_time, finished_requests));
    if (min_connect_time == MAXLONG)
      min_connect_time = 0;
    report("Minimum connect time", "msec", "min", (double) min_connect_time);
    report("Maximum connect time", "msec", "max", (double) max_connect_time);
    sprintf(s, "Connect time > %ld msec", connect_time_cutoff);
    report(s, "count", "sum", (double) above_connect_time_cutoff);

    report("Average first-byte latency", "msec", "ave Requests",
           (double) safediv(total_first_byte_latency, finished_requests));
    if (min_first_byte_latency == MAXLONG)
      min_first_byte_latency = 0;
    report("Minimum first-byte latency", "msec", "min", (double) min_first_byte_latency);
    report("Maximum first-byte latency", "msec", "max", (double) max_first_byte_latency);
    sprintf(s, "First-byte latency > %ld msec", first_byte_latency_cutoff);
    report(s, "count", "sum", (double) above_first_byte_latency_cutoff);

    report("Average round trip", "msec", "ave Requests", (double) safediv(total_round_trip_time, finished_requests));
    if (min_round_trip_time == MAXLONG)
      min_round_trip_time = 0;
    report("Minimum round trip", "msec", "min", (double) min_round_trip_time);
    report("Maximum round trip", "msec", "max", (double) max_round_trip_time);
    sprintf(s, "Round-trip time > %ld msec", round_trip_time_cutoff);
    report(s, "count", "sum", (double) above_round_trip_time_cutoff);

    report("Transactions in progress", "count", "sum", (double) transactions_in_progress);
    report("Average time in progress", "msec", "ave Requests",
           (double) safediv(total_in_progress_time, transactions_in_progress));
    report("Simulated users", "count", "sum", (double) users);
    report("Users opening a connection", "count", "sum", (double) max_connections_open);
    report("Failed requests", "count", "sum", (double) failed_requests);

#ifdef _PLUG_IN
    if (plug_in->report_fcn) {
      (plug_in->report_fcn) ();
    }
#endif

  } else {
    sprintf(s,                  // don't use any '=' in the output below, or awk script will break
            "Client %d  Report %3d  Elapsed %6.1f sec   Cumulative rate %4.0f op/sec   Last %5.1f sec %4.0f op/sec\n",
            client_id, report_no,
            0.001 * (double) elapsed_time,
            safediv(finished_requests * 1000.0, elapsed_time),
            0.001 * time_since_last_report,
            safediv((finished_requests - last_finished) * 1000.0, time_since_last_report));
    fprintf(stdout, "%s", s);
    fprintf(stderr, "%s", s);
    fflush(stderr);
  }
  last_finished = finished_requests;

  report_no++;
}


extern "C"
{
  void TSReportSingleData(char *metric, char *unit, TSReportCombiner combiner, double value)
  {
    switch (combiner) {
    case TS_SUM:
      do_test->report(metric, unit, "sum", value);
      break;
      case TS_MAX:do_test->report(metric, unit, "max", value);
      break;
      case TS_MIN:do_test->report(metric, unit, "min", value);
      break;
      case TS_AVE:do_test->report(metric, unit, "ave Requests", value);
      break;
      default:fprintf(stderr, "Error: Illegal combiner in report");
    };
  }
}
