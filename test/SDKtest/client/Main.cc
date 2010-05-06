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

 Main.cc
******************************************************************/
#include "Config.h"
#include "LoadGenerator.h"
#include "DoTest.h"
#include "Plugin.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>


Config *config;
LoadGenerator *load_generator;
DoTest *do_test;
INKPlugin *plug_in;

#if defined(__linux__) || defined(__FreeBSD__)
static void
signal_handler(int sig)
{
#else
static void
signal_handler(int sig, siginfo_t * sinf, void *ucon)
{
#endif
  static int time_since_inception = 0;
  switch (sig) {
  case SIGALRM:{
      time_since_inception += config->execution_interval;
      /* report stats here */
#ifdef experiment
      if (time_since_inception < config->execution_interval) {
        do_test->print_stats(0);
        alarm(config->reporting_interval);
      } else {
#ifdef _PLUG_IN
        if (plug_in->connection_finish_fcn) {
          for (int i = 0; i < do_test->users; i++) {
            if (do_test->user_info[i].internal_rid) {
              (plug_in->connection_finish_fcn) (do_test->user_info[i].request_id, INK_TIME_EXPIRE);

            }
          }
        }
        if (plug_in->plugin_finish_fcn) {
          (plug_in->plugin_finish_fcn) ();
        }
        delete plug_in;
#endif
        load_generator->print_stats();
        do_test->print_stats(1);
        exit(0);
      }
#else

#ifdef _PLUG_IN
      if (plug_in->connection_finish_fcn) {
        for (int i = 0; i < do_test->users; i++) {
          if (do_test->user_info[i].internal_rid) {
            (plug_in->connection_finish_fcn) (do_test->user_info[i].request_id, INK_TIME_EXPIRE);
          }
        }
      }
      if (plug_in->plugin_finish_fcn) {
        (plug_in->plugin_finish_fcn) ();
      }
      delete plug_in;
#endif
      load_generator->print_stats();
      do_test->print_stats(0);  // print last incremental report before final report
      do_test->print_stats(1);
      if (time_since_inception < config->execution_interval) {
        alarm(config->reporting_interval);
      } else {
        exit(0);
      }
#endif
      break;
    }
  default:
    fprintf(stderr, "Error: don't know how to handle signal %d\n", sig);
    exit(1);
  }
}
int
main(int argc, char *argv[])
{
  char *config_file, *dir, *api;
  long warmup;
  int i, client_id = 0, exec_interval = 0, rd_tout = 1;
  int req_rate = 0;
  unsigned short seed16v[3];
  struct sigaction act;
  // struct sockaddr_in target_addr;
  struct timeval rand_time;
  signal(SIGPIPE, SIG_IGN);
  /*
     sigset(SIGALRM, signal_handler);
   */
#if defined(__linux__) || defined(__FreeBSD__)
  act.sa_handler = signal_handler;
  act.sa_flags = SA_RESTART;
#else
  act.sa_handler = NULL;
  act.sa_sigaction = signal_handler;
  act.sa_flags = SA_SIGINFO | SA_RESTART;
#endif
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGALRM, &act, (struct sigaction *) NULL) < 0) {
    fprintf(stderr, "Error in sigaction\n");
    perror("Error: sigaction");
    exit(1);
  }
  config_file = "SDKtest_client.config";
  warmup = 0;
  api = "";
  for (i = 1; i < argc && argv[i][0] == '-'; i++) {
    switch (argv[i][1]) {
    case '?':
    case 'h':
      printf("Usage:\n");
      printf("\t \"argv[0] -w\": To warmup the cache\n");
      printf("\t \"argv[0] -h\": To print this message\n");
      printf("\t \"argv[0] [-cconfig_file -iid]\" (no space): \n");
      printf("\t Default Config File: %s\n", config_file);
      printf("\t Default id: 0\n");
      exit(0);
      break;
    case 'd':
      dir = argv[i] + 2;
      if (chdir(dir) != 0) {
        perror("chdir");
        exit(1);
      }
      break;
    case 'c':
      config_file = argv[i] + 2;
      break;
    case 'i':
      client_id = atoi(argv[i] + 2);
      break;
    case 'w':
      warmup = 1;
      break;
    case 'p':
      api = argv[i] + 2;
      break;
    case 'x':
      exec_interval = atoi(argv[i] + 2);
      break;
    case 'r':
      req_rate = atoi(argv[i] + 2);
      rd_tout = 0;
      break;
    default:
      fprintf(stderr, "Error:  %s: unknown switch '%c', try -h for help\n", argv[0], argv[i][1]);
      exit(1);
    }
  }

  plug_in = new INKPlugin(client_id, api);
#ifdef _PLUG_IN
  plug_in->load_plugin();
#endif

  config = new Config(warmup, config_file, plug_in, rd_tout);

  if (exec_interval != 0) {
    config->execution_interval = exec_interval;
  }

  gettimeofday(&rand_time, NULL);
  seed16v[0] = rand_time.tv_usec;
  gettimeofday(&rand_time, NULL);
  seed16v[1] = rand_time.tv_usec;
  gettimeofday(&rand_time, NULL);
  seed16v[2] = rand_time.tv_usec;
  (void) seed48(seed16v);
  if (config->synthetic) {
    load_generator = new LoadGenerator(config->keepalive,
                                       warmup,
                                       config->debug,
                                       config->ssl,
                                       config->num_origin_servers,
                                       config->docset,
                                       config->hotset,
                                       config->hotset_access_ratio,
                                       config->origin_server_names,
                                       config->origin_server_ports,
                                       config->target_host,
                                       config->target_port,
                                       config->document_base,
                                       config->num_sizes,
                                       config->sizes, config->cumulative_size_prob, config->direct, plug_in);
  } else {
    load_generator = new LoadGenerator(config->log_file_p, config->keepalive, plug_in);
  }
  if (warmup) {
    warmup = (long) config->hotset;
    // if warmup is true, we pass the hotset to do_test
  }
  alarm(config->execution_interval);

  do_test = new DoTest(config->debug,
                       config->ssl,
                       client_id,
                       load_generator,
                       warmup,
                       config->users,
                       config->read_timeout,
                       config->keepalive,
                       config->num_thinktimes,
                       config->thinktimes,
                       config->cumulative_thinktime_prob,
                       config->num_target_byterates,
                       config->target_byterates,
                       config->cumulative_target_byterate_prob,
                       config->reporting_interval,
                       config->histogram_max,
                       config->histogram_resolution,
                       config->round_trip_time_cutoff,
                       config->first_byte_latency_cutoff,
                       config->connect_time_cutoff, config->QOS_docsize, plug_in, req_rate);
  do_test->actual_test(req_rate);
  delete plug_in;
  delete load_generator;
  delete do_test;
  exit(0);
}
