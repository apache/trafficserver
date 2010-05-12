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

/****************************************************************************

   test_results.cc

   Description:

   
 ****************************************************************************/

#include "test_exec.h"
#include "test_results.h"
#include "sio_buffer.h"
#include "sio_loop.h"
#include "test_utils.h"

#include "ink_platform.h"
#include "Diags.h"
#include "snprintf.h"

// From test_exec.cc
extern UserDirInfo *ud_info;

TestResult::TestResult():
test_case_name(NULL),
output_file(NULL), test_run_results(NULL), errors(0), warnings(0), time_start(0), time_stop(0), link()
{
}

TestResult::~TestResult()
{

  if (test_case_name) {
    free(test_case_name);
    test_case_name = NULL;
  }

  if (output_file) {
    free(output_file);
    output_file = NULL;
  }
}

void
TestResult::start(const char *name_arg)
{

  test_case_name = strdup(name_arg);
  time_start = time(NULL);
}

void
TestResult::build_output_file_name(const char *base, const char *ext)
{

  int len = strlen(save_results_dir) + 1 + strlen(test_run_results->run_id_str) +
    1 + strlen(base) + 1 + strlen(ext) + 1;

  if (output_file) {
    free(output_file);
  }

  output_file = (char *) malloc(len);
  sprintf(output_file, "%s/%s/%s.%s", save_results_dir, test_run_results->run_id_str, base, ext);
}

void
TestResult::finish()
{
  time_stop = time(NULL);
}

TestRunResults::TestRunResults():
run_id_str(NULL), test_name(NULL), username(NULL), build_id(NULL), start_time(0), cleanup_called(false)
{
}

TestRunResults::~TestRunResults()
{

  if (run_id_str) {
    free(run_id_str);
    run_id_str = NULL;
  }

  if (test_name) {
    free(test_name);
    test_name = NULL;
  }

  if (username) {
    free(username);
    username = NULL;
  }

  if (build_id) {
    free(build_id);
    build_id = NULL;
  }

  cleanup_results(false);
}

void
TestRunResults::start(const char *tname, const char *uname, const char *bid)
{
  start_time = time(NULL);
  test_name = strdup(tname);
  username = strdup(uname);
  build_id = strdup(bid);

  int len = strlen(tname) + 1 + strlen(uname) + 1 + 32 + 1;
  run_id_str = (char *) malloc(len);
  sprintf(run_id_str, "%s-%s-%u", tname, uname, start_time);

  if (post_to_tinderbox) {
    sio_buffer tinder_msg;
    build_tinderbox_message_hdr("building", start_time, &tinder_msg);

    char start_msg[] = "Tests starting";
    tinder_msg.fill(start_msg, sizeof(start_msg) - 1);

    int i = post_tinderbox_message(&tinder_msg, NULL);
  }

  if (save_results) {
    char tmp[1024];
    snprintf(tmp, 1023, "%s/%s", save_results_dir, run_id_str);
    tmp[1023] = '\0';

    int r;
    do {
      r = mkdir(tmp, 0755);
    } while (r < 0 && errno == EINTR);

    if (r) {
      TE_Error("Could not create save dir : %s : %s", tmp, strerror(errno));
      save_results = 0;
    }
  }
}

int
TestRunResults::output_summary_html()
{

  char summary_file[1024];
  snprintf(summary_file, 1023, "%s/%s/index.html", save_results_dir, run_id_str);
  summary_file[1023] = '\0';

  int fd;
  do {
    fd = open(summary_file, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    TE_Error("Failed to create summary output file %s : %s", summary_file, strerror(errno));
    return -1;
  }

  sio_buffer output_buffer;
  build_summary_html(&output_buffer);

  int timeout_ms = 60000;
  const char *result_msg = write_buffer(fd, &output_buffer, &timeout_ms);

  int r = 0;
  if (result_msg != NULL) {
    TE_Error("Failed to write to summary file %s : %s", summary_file, strerror(errno));
    r = -1;
  }

  close(fd);
  return r;
}

void
TestRunResults::build_summary_html(sio_buffer * output)
{

  const char hdr1[] = "<html>\n<head>\n<title> DEFT Test Results: ";
  const char hdr2[] = "</title>\n<head>\n<body bgcolor=\"White\">\n"
    "<h2 align=\"center\"> DEFT Test results</h2>\n<h3> <table> ";
  const char hdr3[] = "</table>\n<p></p>\n<table>\n<tr><th>Test Case</th><th width=\"15%\"> Result </th>"
    "<th width=\"15%\"> Errors </th> <th width=\"15%\"> Warnings </th> <th width=\"15%\"> " "Duration </th></tr>\n";
  const char row_start[] = "<tr><td>";
  const char row_start_grey[] = "<tr bgcolor=\"F0F0F0\"><td>";

  const char next_el[] = "</td><td>";
  const char row_end[] = "</td></tr>\n";
  const char test_link_start[] = "<a href=\"";
  const char test_link_middle[] = ".html\">";
  const char test_link_end[] = "</a>";
  const char result_error[] = "<font color=\"red\">FAIL</font>";
  const char result_warn[] = "<font color=\"purple\">WARNINGS</font>";
  const char result_pass[] = "<font color=\"green\">PASS</font>";
  const char footer1[] = "</table>\n";
  const char footer2[] = "</body>\n</html>\n";

  output->fill(hdr1, sizeof(hdr1) - 1);
  output->fill(test_name, strlen(test_name));
  output->fill(hdr2, sizeof(hdr2) - 1);

  const char test_name_label[] = "<b>Test Name:</b> ";
  output->fill(row_start, sizeof(row_start) - 1);
  output->fill(test_name_label, sizeof(test_name_label) - 1);
  output->fill(test_name, strlen(test_name));
  output->fill(row_end, sizeof(row_end) - 1);

  const char user_name_label[] = "<b>User Name:</b> ";
  output->fill(row_start, sizeof(row_start) - 1);
  output->fill(user_name_label, sizeof(user_name_label) - 1);
  output->fill(ud_info->username, strlen(ud_info->username));
  output->fill(row_end, sizeof(row_end) - 1);

  const char host_name_label[] = "<b>Run From:</b> ";
  output->fill(row_start, sizeof(row_start) - 1);
  output->fill(host_name_label, sizeof(host_name_label) - 1);
  output->fill(ud_info->hostname, strlen(ud_info->hostname));
  output->fill(row_end, sizeof(row_end) - 1);


  const char start_time_label[] = "<b>Start Time:</b> ";
  output->fill(row_start, sizeof(row_start) - 1);
  output->fill(start_time_label, sizeof(start_time_label) - 1);
  {
    const char *tmp = ctime(&start_time);
    output->fill(tmp, strlen(tmp));
  }
  output->fill(row_end, sizeof(row_end) - 1);

  output->fill(row_start, sizeof(row_start) - 1);
  const char *end_label;
  if (cleanup_called) {
    end_label = "<b>End Time:</b> ";
  } else {
    end_label = "<b>Report Time:</b> ";
  }
  output->fill(end_label, strlen(end_label));
  {
    time_t now = time(NULL);
    const char *tmp = ctime(&now);
    output->fill(tmp, strlen(tmp));
  }
  output->fill(row_end, sizeof(row_end) - 1);

  if (build_id && *build_id != '\0') {
    const char build_id_label[] = "<b>Build Id:</b> ";
    output->fill(row_start, sizeof(row_start) - 1);
    output->fill(build_id_label, sizeof(build_id_label) - 1);
    output->fill(build_id, strlen(build_id));
    output->fill(row_end, sizeof(row_end) - 1);
  }

  output->fill(hdr3, sizeof(hdr3) - 1);

  int num_tests = 0;
  int total_errors = 0;
  int total_warnings = 0;
  TestResult *current = results.head;

  while (current) {
    output->fill(row_start_grey, sizeof(row_start_grey) - 1);
    output->fill(test_link_start, sizeof(test_link_start) - 1);
    output->fill(current->test_case_name, strlen(current->test_case_name));
    output->fill(test_link_middle, sizeof(test_link_middle) - 1);
    output->fill(current->test_case_name, strlen(current->test_case_name));
    output->fill(test_link_end, sizeof(test_link_end) - 1);
    output->fill(next_el, sizeof(next_el) - 1);

    if (current->errors > 0) {
      output->fill(result_error, sizeof(result_error) - 1);
    } else if (current->warnings > 0) {
      output->fill(result_warn, sizeof(result_warn) - 1);
    } else {
      output->fill(result_pass, sizeof(result_pass) - 1);
    }
    output->fill(next_el, sizeof(next_el) - 1);

    int r;
    char counts[2];
    counts[0] = current->errors;
    counts[1] = current->warnings;
    for (int i = 0; i < 2; i++) {
      char num_buf[64];
      r = sprintf(num_buf, "%d", counts[i]);
      output->fill(num_buf, r);
      output->fill(next_el, sizeof(next_el) - 1);
    }

    // Now compute the duration of the test
    time_t duration = current->time_stop - current->time_start;
    int hours = 0, minutes = 0, seconds = 0;

    if (duration > 0) {
      seconds = duration % 60;
      minutes = duration / 60;

      if (minutes > 0) {
        hours = minutes / 60;
        minutes = minutes % 60;
      }
    }

    char time_buf[256];
    r = sprintf(time_buf, "%d:%d:%d", hours, minutes, seconds);
    output->fill(time_buf, r);

    output->fill(row_end, sizeof(row_end) - 1);

    current = current->link.next;
  }

  output->fill(footer1, sizeof(footer1) - 1);
  output->fill(footer2, sizeof(footer2) - 1);
}


void
TestRunResults::send_final_tinderbox_message()
{

  sio_buffer tinder_hdr;
  sio_buffer body;
  char num_buf[32];

  int total_errors = 0;
  int total_warnings = 0;

  TestResult *current = results.head;

  while (current) {
    body.fill(current->test_case_name, strlen(current->test_case_name));
    total_errors += current->errors;
    total_warnings += current->warnings;
    body.fill("   ", 3);

    if (current->errors > 0) {
      body.fill(" errors: ", 9);
      int r = sprintf(num_buf, "%d", current->errors);
      body.fill(num_buf, r);
    }

    if (current->warnings > 0) {
      body.fill(" warnings: ", 10);
      int r = sprintf(num_buf, "%d", current->warnings);
      body.fill(num_buf, r);
    }

    if (current->errors == 0 && current->warnings == 0) {
      body.fill(" PASS", 5);
    }

    body.fill("\n", 1);

    current = current->link.next;

  }

  const char *status;
  if (total_errors == 0) {
    status = "success";
  } else {
    status = "test_failed_full";
  }

  time_t now = time(NULL);
  build_tinderbox_message_hdr(status, now, &tinder_hdr);
  post_tinderbox_message(&tinder_hdr, &body);
}


void
TestRunResults::cleanup_results(bool print)
{

  if (cleanup_called) {
    return;
  }
  cleanup_called = true;

  if (save_results) {
    output_summary_html();
  }

  if (post_to_tinderbox) {
    send_final_tinderbox_message();
  }

  if (results.head == NULL) {
    return;
  }

  if (print) {
    printf("\n------------- Final Results ------------------\n");
  }

  TestResult *t;
  while ((t = results.pop()) != NULL) {
    if (print) {
      printf("%s - Errors %d  Warnings %d - %s\n", t->test_case_name, t->errors, t->warnings, t->output_file);
    }
    delete t;
  }

  if (print) {
    printf("\n----------------------------------------------\n");
  }
}

TestResult *
TestRunResults::new_result()
{
  TestResult *res = new TestResult();
  res->test_run_results = this;
  results.push(res);
  return res;
}

void
TestRunResults::build_tinderbox_message_hdr(const char *status, time_t now, sio_buffer * output)
{

  Debug("tinderbox", "Build tinderbox msg with status %s", status);

  const char admin_hdr[] = "tinderbox: administrator : ";
  const char admin_hdr_end[] = "foo@inktomi.com\n";
  const char start_time_hdr[] = "tinderbox: starttime : ";
  const char build_name_hdr[] = "tinderbox: buildname : ";
  const char status_hdr[] = "tinderbox: status : ";
  const char now_hdr[] = "tinderbox: timenow : ";
  const char tree_hdr[] = "tinderbox: tree : ";
  const char ud_hdr[] = "tinderbox: ud_link : ";

  const char end_boiler_plate[] = "tinderbox: errorparser : unix\n"
    "tinderbox: supercolname : na\n" "tinderbox: buildno : 0\n" "tinderbox: messagetype : 0\n" "tinderbox: END\n\n";

  char time_buf[32];

  output->fill("\n", 1);
  output->fill(admin_hdr, sizeof(admin_hdr) - 1);
  output->fill(username, strlen(username));
  output->fill(admin_hdr_end, sizeof(admin_hdr_end) - 1);

  output->fill(start_time_hdr, sizeof(start_time_hdr) - 1);
  int r = sprintf(time_buf, "%u", start_time);
  output->fill(time_buf, r);
  output->fill("\n", 1);

  output->fill(build_name_hdr, sizeof(build_name_hdr) - 1);
  output->fill(test_name, strlen(test_name));
  output->fill("\n", 1);

  output->fill(status_hdr, sizeof(status_hdr) - 1);
  output->fill(status, strlen(status));
  output->fill("\n", 1);

  output->fill(now_hdr, sizeof(now_hdr) - 1);
  r = sprintf(time_buf, "%u", now);
  output->fill(time_buf, r);
  output->fill("\n", 1);

  output->fill(tree_hdr, sizeof(tree_hdr) - 1);
  output->fill(tinderbox_tree, strlen(tinderbox_tree));
  output->fill("\n", 1);

  if (save_results) {
    output->fill(ud_hdr, sizeof(ud_hdr) - 1);

    char tmp[1024];
    r = snprintf(tmp, 1023, "%s/%s/", save_results_url, run_id_str);
    tmp[1023] = '\0';
    output->fill(tmp, r);
    output->fill("\n", 1);
  }

  output->fill(end_boiler_plate, sizeof(end_boiler_plate) - 1);
}

int
TestRunResults::post_tinderbox_message(sio_buffer * hdr, sio_buffer * body)
{

  sio_buffer http_hdr;

  const char http_hdr_start[] = "PUT /cgi-bin/test_col_put.cgi HTTP/1.0\r\n"
    "User-Agent: DEFT Test Exec\r\n" "Content-Length: ";

  int len;
  char len_buf[32];
  len = hdr->read_avail();
  if (body) {
    len += body->read_avail();
  }
  int l = sprintf(len_buf, "%d", len);

  http_hdr.fill(http_hdr_start, sizeof(http_hdr_start) - 1);
  http_hdr.fill(len_buf, l);
  http_hdr.fill("\r\n\r\n", 4);

  // Resolve the name of the tinderbox machine
  struct hostent *he;
  struct in_addr in;
  he = gethostbyname(tinderbox_machine);

  if (he == NULL) {
    TE_Error("Tinderbox posting failed - could not resolve %s", tinderbox_machine);
    return 1;
  } else {
    memcpy(&in.s_addr, *he->h_addr_list, sizeof(in.s_addr));
  }

  int fd = SIO::make_client(in.s_addr, 80);

  if (fd < 0) {
    TE_Error("Tinderbox posting failed - connect failed - %s", strerror(errno));
    return 1;
  }
  // Send off the various buffers composing the http & encapsulated
  //   tinderbox messages
  sio_buffer *bufs_to_send[4] = { &http_hdr, hdr, body, NULL };
  sio_buffer **current = bufs_to_send;

  int timeout_ms = 60000;
  const char *result_msg;
  while (*current != NULL) {
    result_msg = write_buffer(fd, *current, &timeout_ms);

    if (result_msg != NULL) {
      TE_Error("Tinderbox posting failed - %s - %s", result_msg, strerror(errno));
      close(fd);
      return 1;
    }
    current++;
  }

  sio_buffer response_buffer;

  int hdr_count = 0;
  while (1) {
    result_msg = read_until(fd, &response_buffer, '\n', &timeout_ms);

    if (result_msg != NULL) {
      TE_Error("Tinderbox response error  - %s - %s", result_msg, strerror(errno));
      close(fd);
      return 1;
    }

    char *last = NULL;
    char *tmp;
    while ((tmp = response_buffer.memchr('\n')) != NULL) {
      hdr_count++;

      const char *start = response_buffer.start();
      if (tmp > start && *(tmp - 1) == '\r') {
        *(tmp - 1) = '\0';
      } else {
        *tmp = '\0';
      }

      Debug("tinderbox", "http response hdr: %s", start);
      if (hdr_count == 1) {
        int major_ver, minor_ver, status_code;
        int r = sscanf(start, "HTTP/%d.%d %d",
                       &major_ver, &minor_ver, &status_code);

        if (r != 3) {
          TE_Error("Tinderbox response malformed");
          close(fd);
          return 1;
        } else if (status_code != 201) {
          TE_Error("Tinderbox response bad status code %d", status_code);
          return 1;
        }
      }

      bool hdr_complete = false;
      if (last) {
        if ((tmp - last == 2 && *(last + 1) == '\0') || tmp - last == 1) {
          Debug("tinderbox", "Message posting complete");
          hdr_complete = true;
        }
      }

      response_buffer.consume((tmp - start) + 1);

      if (hdr_complete) {
        close(fd);
        return 0;
      }

      last = tmp;
    }
  }

  close(fd);
  TE_Error("Tinderbox response hdr truncated");
  return 1;
}
