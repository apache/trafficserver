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

  Config.cc
******************************************************************/
#include <ctype.h>


//   ^[ ]*#                             --> return
//   ^[ ]*$                             --> return
//   ^[ ]*\([^ ]*\)[ ]*=[ ]*\([^ ]*\)[ ]*$   --> lhs=\1  rhs=\2

#include "Config.h"

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
Config::read_docsize_dist(long warmup)
{
  int end_of_file = 0;
  int n, i;
  long size;
  double prob, avg_doc_size = 0.0;
  int QOS_found = 0;
  num_sizes = 0;
  docsize_size_sum = 0;
  do {
    n = fscanf(docsize_dist_file_p, "%ld %lf", &size, &prob);
    if (n == EOF) {             /*fscanf will return number of matched items. If it
                                   returns EOF that means none were matched */
      end_of_file = 1;
    } else if (n == 2) {
      sizes[num_sizes] = size;
      if (size == QOS_docsize)
        QOS_found = 1;
      if (num_sizes == 0) {
        cumulative_size_prob[num_sizes] = prob;
      } else {
        cumulative_size_prob[num_sizes] = cumulative_size_prob[num_sizes - 1] + prob;
      }
      num_sizes++;
      avg_doc_size += prob * size;
      docsize_size_sum += size;
    } else {
      fprintf(stderr, "Error in docsize_dist_file\n");
      exit(1);
    }
  } while (!end_of_file);
  printf("Average Doc Size according to the specified distribution: %.2f\n", avg_doc_size);
  if (debug) {
    for (i = 0; i < num_sizes; i++) {
      printf("%7ld %.2f\n", sizes[i], cumulative_size_prob[i]);
    }
  }
  if ((cumulative_size_prob[num_sizes - 1]<0.99999) || (cumulative_size_prob[num_sizes - 1]> 1.00001)) {
    fprintf(stderr, "Error in docsize_dist_file: prob add up to %f\n", cumulative_size_prob[num_sizes - 1]);
    exit(1);
  }
  if (QOS_docsize != 0) {
    if (QOS_found) {
      if (!warmup) {
        fprintf(stderr, "Note: Only documents of QOS_docsize %d bytes will be included in histograms.\n", QOS_docsize);
      }
    } else {
      fprintf(stderr, "Error in QOS_docsize: %d is not in the document distribution\n", QOS_docsize);
      exit(1);
    }
  }
}

void
Config::read_thinktime_dist()
{
  int end_of_file = 0;
  int n, i;
  long thinktime;
  double prob, avg_thinktime = 0.0;
  num_thinktimes = 0;
  do {
    n = fscanf(thinktime_dist_file_p, "%ld %lf", &thinktime, &prob);
    if (n == EOF) {             /*fscanf will return number of matched items. If it
                                   returns EOF that means none were matched */
      end_of_file = 1;
    } else if (n == 2) {
      thinktimes[num_thinktimes] = thinktime;
      if (num_thinktimes == 0) {
        cumulative_thinktime_prob[num_thinktimes] = prob;
      } else {
        cumulative_thinktime_prob[num_thinktimes] = cumulative_thinktime_prob[num_thinktimes - 1] + prob;
      }
      num_thinktimes++;
      avg_thinktime += prob * thinktime;
    } else {
      fprintf(stderr, "Error in thinktime_dist_file\n");
      exit(1);
    }
  } while (!end_of_file);
  printf("\n");
  printf("Average Think Time according to the specified distribution: %.2f\n", avg_thinktime);
  printf("Thinktime probabilities:\n");
  for (i = 0; i < num_thinktimes; i++) {
    printf("%3ld %.2f\n", thinktimes[i], cumulative_thinktime_prob[i]);
  }
  printf("\n");
  if ((cumulative_thinktime_prob[num_thinktimes - 1] < 0.99999) ||
      (cumulative_thinktime_prob[num_thinktimes - 1] > 1.00001)) {
    fprintf(stderr, "Error in thinktime_dist_file: prob add up to %f\n", cumulative_thinktime_prob[num_thinktimes - 1]);
    exit(1);
  }
}
void
Config::read_target_byterate_dist()
{
  int end_of_file = 0;
  int n, i;
  long target_byterate;
  double prob;
  num_target_byterates = 0;
  do {
    n = fscanf(target_byterate_dist_file_p, "%ld %lf", &target_byterate, &prob);
    if (n == EOF) {             /*fscanf will return number of matched items. If it
                                   returns EOF that means none were matched */
      end_of_file = 1;
    } else if (n == 2) {
      target_byterates[num_target_byterates] = target_byterate;
      if (num_target_byterates == 0) {
        cumulative_target_byterate_prob[num_target_byterates] = prob;
      } else {
        cumulative_target_byterate_prob[num_target_byterates] =
          cumulative_target_byterate_prob[num_target_byterates - 1] + prob;
      }
      num_target_byterates++;
    } else {
      fprintf(stderr, "Error in target_byterate_dist file\n");
      exit(1);
    }
  } while (!end_of_file);
  printf("Byterate probabilities:\n");
  for (i = 0; i < num_target_byterates; i++) {
    printf("%3ld %.2f\n", target_byterates[i], cumulative_target_byterate_prob[i]);
  }
  if ((cumulative_target_byterate_prob[num_target_byterates - 1]
       < 0.99999) || (cumulative_target_byterate_prob[num_target_byterates - 1]
                      > 1.00001)) {
    fprintf(stderr, "Error in target_byterate_dist_file: prob add up to %f\n",
            cumulative_target_byterate_prob[num_target_byterates - 1]);
    exit(1);
  }
  printf("\n");
}
Config::Config(long warmup, char *config_file, INKPlugin * aplug_in, int aread_timeout)
{
  char line[MAX_LINE_SIZE], lhs[MAX_LINE_SIZE], rhs[MAX_LINE_SIZE];
  int c;
  int end_of_file = 0;
  int line_no, i;

#ifdef _PLUG_IN
  plug_in = aplug_in;
#endif

  /* Default Values */
#ifdef OLD_DEFAULT
  strcpy(target_host, "localhost");
  strcpy(target_port, "8090");

#else
  strcpy(target_host, "");
  strcpy(target_port, "");

#endif
  strcpy(document_base, "");
  synthetic = 1;
  strcpy(log_file, "sample.log");
  users = 1;
  execution_interval = 10;
  reporting_interval = 1000000;
  histogram_max = 30.0;
  histogram_resolution = 0.5;
  round_trip_time_cutoff = 2000;        /* msec */
  connect_time_cutoff = 500;    /* msec */
  first_byte_latency_cutoff = 1000;     /* msec */
  debug = 0;
  ssl = 0;
  read_timeout = aread_timeout;
  hotset = 1;
  docset = 1;
  hitrate = 100;
  keepalive = 1;
  QOS_docsize = 0;
  num_origin_servers = 0;
  strcpy(docsize_dist_file, "docsize.specweb");
  strcpy(thinktime_dist_file, "thinktime.0");
  strcpy(target_byterate_dist_file, "byterate.fast");

  if (!(conf_file = fopen(config_file, "r"))) {
    fprintf(stderr, "Error: could not open the config file %s\n", config_file);
    perror("Error: Config File Open");
    exit(1);
  }
  line_no = 1;
  end_of_file = 0;
  do {
    i = 0;
    do {
      line[i++] = (c = getc(conf_file));
    } while (c != '\n' && c != EOF && i < MAX_LINE_SIZE);
    if (i == MAX_LINE_SIZE) {
      fprintf(stderr, "Error in Config File: Lines can only be %d chars long\n", MAX_LINE_SIZE);
      exit(1);
    }
    if (c == EOF) {
      end_of_file = 1;
    }
    /* last char is either newline or EOF, so skip it */
    i--;
    line[i] = '\0';
    if (i > 0) {
      process_line(line_no, line, i, lhs, rhs);

#ifdef _PLUG_IN
      // target_host, target_port, and document_base 
      // will be passed to the INKOptionProcess() later, and
      // comments are skipped.
      if ((strcmp(lhs, "target_host") || strcmp(lhs, "target_port") || strcmp(lhs, "document_base")) && strcmp(lhs, "")) {
        if (plug_in->options_process_fcn) {
          (plug_in->options_process_fcn) (lhs, rhs);
        }
      }
#endif

      if (!strcmp(lhs, "target_host")) {
        strcpy(target_host, rhs);
      } else if (!strcmp(lhs, "target_port")) {
        strcpy(target_port, rhs);
      } else if (!strcmp(lhs, "document_base")) {
        strcpy(document_base, rhs);
      } else if (!strcmp(lhs, "synthetic")) {
        synthetic = atoi(rhs);
      } else if (!strcmp(lhs, "log_file")) {
        strcpy(log_file, rhs);
      } else if (!strcmp(lhs, "users")) {
        users = atoi(rhs);
#if 0
        assert((users) > 0 && (users) < MAX_USERS);
#endif
        assert((users) > 0);
      } else if (!strcmp(lhs, "execution_interval")) {
        execution_interval = atoi(rhs);
        assert((execution_interval) > 0);
      } else if (!strcmp(lhs, "reporting_interval")) {
        reporting_interval = atoi(rhs);
        assert((reporting_interval) > 0);
      } else if (!strcmp(lhs, "histogram_max")) {
        histogram_max = atof(rhs);
        assert((histogram_max) > 0.0);
        if (histogram_max > 1000.0) {
          fprintf(stderr, "Error: histogram times are (now) specified in seconds.  %f sec is too big.\n",
                  histogram_max);
          exit(1);
        }
      } else if (!strcmp(lhs, "histogram_resolution")) {
        histogram_resolution = atof(rhs);
        assert((histogram_resolution) > 0.0);
      } else if (!strcmp(lhs, "round_trip_cutoff")) {
        round_trip_time_cutoff = atol(rhs);
        assert(round_trip_time_cutoff > 0);
      } else if (!strcmp(lhs, "first_byte_cutoff")) {
        first_byte_latency_cutoff = atol(rhs);
        assert(first_byte_latency_cutoff > 0);
      } else if (!strcmp(lhs, "connect_cutoff")) {
        connect_time_cutoff = atol(rhs);
        assert(connect_time_cutoff > 0);
      } else if (!strcmp(lhs, "debug")) {
        debug = atoi(rhs);
        assert(((debug) == 0) || ((debug) == 1));
      } else if (!strcmp(lhs, "ssl")) {
        ssl = atoi(rhs);
        assert(((ssl) == 0) || ((ssl) == 1));
      } else if (!strcmp(lhs, "read_timeout")) {
        read_timeout = atoi(rhs);
        assert((read_timeout) > 0);
      } else if (!strcmp(lhs, "hotset")) {
        hotset = atof(rhs);
        assert((hotset) > 0);
      } else if (!strcmp(lhs, "docset")) {
        docset = atof(rhs);
        assert((docset) > 0);
      } else if (!strcmp(lhs, "hitrate")) {
        hitrate = atoi(rhs);
        assert((hitrate) >= 0 && (hitrate) <= 100);
      } else if (!strcmp(lhs, "keepalive")) {
        keepalive = atoi(rhs);
        assert((keepalive) > 0);
      } else if (!strcmp(lhs, "origin_servers")) {
        int j = 0, k;
        num_origin_servers = 0;
        while ((line[j] != '\0') && (line[j] != '='))
          j++;
        if (line[j] == '=')
          j++;
        while (line[j] != '\0') {
          assert((num_origin_servers) < MAX_ORIGIN_SERVERS);
          while ((line[j] != '\0') && isspace(line[j]))
            j++;
          if (line[j] != '\0') {
            for (k = 0; ((line[j] != '\0') && !isspace(line[j])); k++) {
              origin_server_names[num_origin_servers][k] = line[j++];
            }
            origin_server_names[num_origin_servers++][k] = '\0';
          }
        }
        char *p;
        for (j = 0; j < num_origin_servers; j++) {
          p = strrchr(origin_server_names[j], ':');
          if ((p == NULL) || (p == origin_server_names[j])) {
            fprintf(stderr, "No port supplied for origin server %d: '%s'\n", j, origin_server_names[j]);
            exit(1);
          }
          origin_server_ports[j] = p + 1;
          *p = '\0';
#if 0
          fprintf(stderr, "Origin server %d is host '%s' port '%s'\n",
                  j, origin_server_names[j], origin_server_ports[j]);
#endif
        }
      } else if (!strcmp(lhs, "docsize_dist_file")) {
        strcpy(docsize_dist_file, rhs);
      } else if (!strcmp(lhs, "thinktime_dist_file")) {
        strcpy(thinktime_dist_file, rhs);
      } else if (!strcmp(lhs, "byterate_dist_file")) {
        strcpy(target_byterate_dist_file, rhs);
      } else if (!strcmp(lhs, "QOS_docsize")) {
        QOS_docsize = atoi(rhs);
      } else if (line[0] == '#') {
        /* ignore comment */
      } else {
        /* printf("Unrecognized input in config file line %d\n", line_no); exit(1); */
      }
    }
    line_no++;
  } while (!end_of_file);

  if (num_origin_servers == 0) {
    fprintf(stderr, "No origin servers specified.\n");
    exit(1);
  }

  if ((strlen(target_host) == 0) || (strlen(target_port) == 0)) {
    direct = 1;
    fprintf(stderr, "target_host and/or target_port not specified -- will connect directly to origin servers\n");
  } else {
    direct = 0;
  }


  hotset_access_ratio = hitrate / 100.0;
  read_timeout *= 1000;         /* convert into msec */
  if (warmup) {
    reporting_interval = 9999999;       /* Some large value */
    execution_interval = 9999999;       /* Some large value */
    synthetic = 1;
    keepalive = 1;
  }
  if (fclose(conf_file)) {
    fprintf(stderr, "Error: could not close the config file %s\n", config_file);
    perror("Error: Config File Close");
    exit(1);
  }
  if (!synthetic) {
    docsize_dist_file_p = NULL;
    if (!(log_file_p = fopen(log_file, "r"))) {
      fprintf(stderr, "Error: could not open the log file %s\n", log_file);
      perror("Error: Log File Open");
      exit(1);
    }
  } else {
    log_file_p = NULL;
    /* Round-off hotset and docset */
    if (!(docsize_dist_file_p = fopen(docsize_dist_file, "r"))) {
      fprintf(stderr, "Error: could not open the docsize_dist_file %s\n", docsize_dist_file);
      perror("Error: DocSize Dist File Open");
      exit(1);
    }
    read_docsize_dist(warmup);
    if (fclose(docsize_dist_file_p)) {
      fprintf(stderr, "Error: could not close the docsize_dist file %s\n", docsize_dist_file);
      perror("Error: Docsize File Close");
      exit(1);
    }
    if (debug) {
      printf("Hotset %f num_origin_servers %d num_sizes %d\n", hotset, num_origin_servers, num_sizes);
      printf("Docset %f num_origin_servers %d num_sizes %d\n", docset, num_origin_servers, num_sizes);
    }
    hotset = (double) (ceil(hotset * 1.0 / (num_origin_servers * num_sizes)) * (num_origin_servers * num_sizes));

    docset = (double) (ceil(docset * 1.0 / (num_origin_servers * num_sizes)) * (num_origin_servers * num_sizes));
  }
  if (warmup) {                 // Don't need thinktimes or target_byterate
    fprintf(stderr, "Total size of hotset: %.1f MByte\n",
            (((double) hotset / (double) num_sizes) * (double) docsize_size_sum) / (1024.0 * 1024.0));
    num_thinktimes = 1;
    thinktimes[0] = 0;
    cumulative_thinktime_prob[0] = 1.0;
    num_target_byterates = 1;
    target_byterates[0] = -1;
    cumulative_target_byterate_prob[0] = 1.0;
    if (users > MAX_WARMUP_USERS) {
      fprintf(stderr, "Reducing number of users for warmup from %d to %d\n", users, MAX_WARMUP_USERS);
      users = MAX_WARMUP_USERS;
    }
  } else {
    if (!(thinktime_dist_file_p = fopen(thinktime_dist_file, "r"))) {
      fprintf(stderr, "Error: could not open the thinktime_dist_file %s\n", thinktime_dist_file);
      perror("Error: Thinktime Dist File Open");
      exit(1);
    }
    read_thinktime_dist();
    if (fclose(thinktime_dist_file_p)) {
      fprintf(stderr, "Error: could not close the thinktime_dist file %s\n", thinktime_dist_file);
      perror("Error: Thinktime File Close");
      exit(1);
    }
    if (!(target_byterate_dist_file_p = fopen(target_byterate_dist_file, "r"))) {
      fprintf(stderr, "Error: could not open the target_byterate_dist_file %s\n", target_byterate_dist_file);
      perror("Error: Target ByteRate Dist File Open");
      exit(1);
    }
    read_target_byterate_dist();
    if (fclose(target_byterate_dist_file_p)) {
      fprintf(stderr, "Error: could not close the target_byterate_dist file %s\n", target_byterate_dist_file);
      perror("Error: Target ByteRate File Close");
      exit(1);
    }
  }


#ifdef _PLUG_IN
  if (plug_in->options_process_fcn) {
    (plug_in->options_process_fcn) ("target_host", target_host);
    (plug_in->options_process_fcn) ("target_port", target_port);
    (plug_in->options_process_fcn) ("document_base", document_base);
  }
#endif


  printf("target_host '%s'\n", target_host);
  printf("target_port '%s'\n", target_port);
  // printf("synthetic %d\n",synthetic);
  // printf("log_file '%s'\n", log_file);
  printf("users %d\n", users);
  printf("execution_interval %d\n", execution_interval);
  printf("reporting_interval %d\n", reporting_interval);
  printf("debug %d\n", debug);
  printf("ssl %d\n", ssl);
  printf("read_timeout %d\n", read_timeout);
  printf("adjusted hotset %.0f\n", hotset);
  printf("adjusted docset %.0f \n", docset);
  printf("hitrate %d \n", hitrate);
  printf("keepalive %d\n", keepalive);
  printf("num_origin_servers %d\n", num_origin_servers);

  /////////////////////////////
#ifdef _PLUG_IN
  if (plug_in->options_process_finish_fcn) {
    (plug_in->options_process_finish_fcn) ();
  }
#endif
  /////////////////////////////    
}
