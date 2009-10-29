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

   test_group.cc

   Description:

   
 ****************************************************************************/

#include "ink_hash_table.h"
#include "List.h"
#include "ParseRules.h"
#include "Diags.h"

#include "test_exec.h"
#include "sio_buffer.h"
#include "raf_cmd.h"
#include "test_utils.h"
#include "test_group.h"


const static int file_read_size = 32768;

static InkHashTable *test_case_hash = NULL;
static InkHashTable *test_group_hash = NULL;

enum build_line_status_t
{
  BUILD_LINE_COMPLETE,
  BUILD_LINE_CONTINUE,
  BUILD_LINE_FINISHED
};

test_case::test_case():
name(NULL), test_case_elements(NULL)
{
}

test_case::~test_case()
{
  if (name) {
    free(name);
    name = NULL;
  }
}

build_line_status_t
build_line(sio_buffer * input, sio_buffer * output, bool eof)
{

  while (1) {
    const char *newline = input->memchr('\n');

    if (newline) {
      const char *start = input->start();
      const char *copy_to;

      // Trim \r if it's there
      if (newline > start && *(newline - 1) == '\r') {
        copy_to = newline - 1;
      } else {
        copy_to = newline;
      }

      const char *tmp = copy_to;
      // Check to see if this a continuation line
      while (tmp > start && isspace(*tmp)) {
        tmp--;
      }

      bool cont_line = false;
      if (*tmp == '\\') {
        // It's a continuation line
        copy_to = tmp;
        cont_line = true;
      }

      output->fill(start, copy_to - start);
      input->consume((newline - start) + 1);

      if (cont_line == false) {
        return BUILD_LINE_COMPLETE;
      }
    } else {
      const char *start = input->start();
      int read_avail = input->read_avail();
      int to_copy = read_avail;

      if (read_avail > 0) {
        // Don't copy trailing \r since it might be part
        //  of \r\n sequence
        if (start[read_avail - 1] == '\r') {
          to_copy--;
        }
      } else if (read_avail == 0) {
        if (eof) {
          return BUILD_LINE_FINISHED;
        } else {
          return BUILD_LINE_CONTINUE;
        }
      }

      output->fill(start, to_copy);
      input->consume(to_copy);

      if (eof) {
        return BUILD_LINE_COMPLETE;
      } else {
        return BUILD_LINE_CONTINUE;
      }

    }
  }

  // We should never get here
  ink_release_assert(0);
  return BUILD_LINE_COMPLETE;
}

int
process_test_entry(InkHashTable * h, const char *tag, RafCmd * line_el, int line_num)
{

  if (line_el->length() < 3) {
    TE_Error("insufficent arguments to '%s' on line %d of test group file", tag, line_num);
    return -1;
  }

  int els = line_el->length() - 2;
  char **entry = (char **) malloc((els + 1) * sizeof(char **));

  for (int i = 0; i < els; i++) {
    entry[i] = strdup((*line_el)[i + 2]);
  }
  entry[els] = NULL;

  void *old_entry;
  int r = ink_hash_table_lookup(h, (*line_el)[1], &old_entry);

  if (r != 0) {
    destroy_argv((char **) old_entry);
  }

  Debug("test_group", "Adding %s %s - %s", tag, (*line_el)[1], entry[0]);
  ink_hash_table_insert(h, (*line_el)[1], entry);

  return 0;
}

int
process_group_data(sio_buffer * input, sio_buffer * line_buffer, int *line_num, bool eof)
{

  while (build_line(input, line_buffer, eof) == BUILD_LINE_COMPLETE) {

    (*line_num)++;

    // This isn't a RAF cmd but the quoting rules and
    //  the dynamic array is very conveient
    char *line_start = line_buffer->start();
    int line_length = line_buffer->read_avail();

    RafCmd comps;
    comps.process_cmd(line_start, line_length);

    if (comps.length() > 0) {
      if (*(comps[0]) != '#') {
        if (strcasecmp(comps[0], "test_case") == 0) {
          process_test_entry(test_case_hash, "test_case", &comps, *line_num);
        } else if (strcasecmp(comps[0], "test_group") == 0) {
          process_test_entry(test_group_hash, "test_group", &comps, *line_num);
        } else {
          TE_Error("unknown identifier '%s' on line %d of test group file", comps[0], *line_num);
        }
      }
    }

    line_buffer->reset();
  }

  return 0;
}


int
load_group_file(const char *filename)
{

  test_case_hash = ink_hash_table_create(InkHashTableKeyType_String);
  test_group_hash = ink_hash_table_create(InkHashTableKeyType_String);

  int fd;

  do {
    fd = open(filename, O_RDONLY);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    TE_Error("Failed to open group file %s : %s", filename, strerror(errno));
    return -1;
  }

  sio_buffer file_read_buf;
  sio_buffer line_buf;
  int line_num = 0;
  bool read_complete = false;

  do {
    int avail = file_read_buf.expand_to(file_read_size);

    int r;
    do {
      r = read(fd, file_read_buf.end(), avail);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
      TE_Error("Read from test group file failed : %s", strerror(errno));
      close(fd);
      return -1;
    } else if (r == 0) {
      read_complete = true;
      close(fd);
    } else {
      file_read_buf.fill(r);
      process_group_data(&file_read_buf, &line_buf, &line_num, false);
    }
  } while (read_complete == false);

  process_group_data(&file_read_buf, &line_buf, &line_num, true);

  return 0;
}

struct test_group_place
{
  test_group_place();
  ~test_group_place();
  char *test_group_name;
  const char **group_els;
  int current_el;
    Link<test_group_place> link;
};

test_group_place::test_group_place():
test_group_name(NULL), group_els(NULL), current_el(0), link()
{
}

test_group_place::~test_group_place()
{

  if (test_group_name) {
    free(test_group_name);
    test_group_name = NULL;
  }
}


struct test_group_iter
{
  test_case current_case;
    DLL<test_group_place> test_group_list;
};

test_group_iter *
test_group_start(const char *tg_name)
{

  void *tg_entry;
  int r = ink_hash_table_lookup(test_group_hash,
                                (char *) tg_name, &tg_entry);

  if (r == 0) {
    return NULL;
  } else {
    test_group_place *tgp = new test_group_place();
    tgp->test_group_name = strdup(tg_name);
    tgp->group_els = (const char **) tg_entry;

    test_group_iter *iter = new test_group_iter;
    iter->current_case.name = NULL;
    iter->test_group_list.push(tgp);

    return iter;
  }

  return NULL;
}

const test_case *
test_group_next(test_group_iter * tg_iter)
{

  while (tg_iter->test_group_list.head) {
    test_group_place *cur = tg_iter->test_group_list.head;

    // Check to see if we've exhausted the current group
    if (cur->group_els[cur->current_el] == NULL) {
      tg_iter->test_group_list.remove(cur);
      delete cur;
    } else {
      const char *el_name = cur->group_els[cur->current_el];
      cur->current_el++;

      // Check to see if the group entry is really another
      //    group
      void *tentry;
      int r = ink_hash_table_lookup(test_group_hash, (char *) el_name, &tentry);

      // The entry is really a group itself
      if (r != 0) {
        test_group_place *next_group = new test_group_place();
        next_group->test_group_name = strdup(el_name);
        next_group->group_els = (const char **) tentry;
        tg_iter->test_group_list.push(next_group);
        continue;
      }
      // Check to see if the group entry is really testcase
      r = ink_hash_table_lookup(test_case_hash, (char *) el_name, &tentry);
      if (r != 0) {
        if (tg_iter->current_case.name) {
          free(tg_iter->current_case.name);
        }
        tg_iter->current_case.name = strdup(el_name);
        tg_iter->current_case.test_case_elements = (const char **) tentry;
        return &tg_iter->current_case;
      } else {
        TE_Error("Unknown entry '%s' in test_group '%s' - skipping",
                 el_name, tg_iter->test_group_list.head->test_group_name);
      }
    }
  }

  return NULL;
}

void
test_group_finish(test_group_iter * tg_iter)
{
  test_group_place *tmp;

  while ((tmp = tg_iter->test_group_list.pop()) != NULL) {
    delete tmp;
  }

  delete tg_iter;
}

int
lookup_test_case(const char *name, test_case * tcase)
{

  int r = 0;

  void *entry;
  r = ink_hash_table_lookup(test_case_hash, (char *) name, &entry);
  if (r != 0) {
    if (tcase->name) {
      free(tcase->name);
    }
    tcase->name = strdup(name);
    tcase->test_case_elements = (const char **) entry;
    return 1;
  } else {
    return 0;
  }
}
