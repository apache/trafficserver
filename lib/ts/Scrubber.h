/** @file

  The Scrubber class helps replace patterns of text inside a buffer with
  replacement text.

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

#pragma once
#include <vector>
#include "pcre.h"

#include "ts/ink_assert.h"
#include "ts/ink_memory.h"
#include "ts/MemView.h"

struct Scrub {
  ~Scrub()
  {
    if (compiled_re) {
      pcre_free(const_cast<pcre *>(compiled_re));
    }
  }
  static const int OVECCOUNT = 30;
  ts::StringView pattern;
  ts::StringView replacement;
  const pcre *compiled_re;
  int ovector[OVECCOUNT];
};

/*
 * Class that helps scrub specific strings from buffers
 */
class Scrubber
{
public:
  /*
   * Parses config & constructs Scrubber
   */
  Scrubber(const char *config);
  Scrubber(Scrubber &other) = delete;
  ~Scrubber();

  /*
   * Add another expression to scrub for
   *
   * @returns whether or not the addition was successful
   */
  bool scrub_add(const ts::StringView pattern, const ts::StringView replacement);

  /*
   * Scrubs the buffer in place with all the Scrub objects stored in this class.
   */
  void scrub_buffer(char *buffer) const;

  /*
   * Config getter. Caller should NOT free
   */
  char *
  get_config()
  {
    return config;
  };

private:
  /*
   * Scrubs the buffer in place with the passed in Scrub object
   */
  void scrub_buffer(char *buffer, Scrub *scrub) const;

  char *config = nullptr;
  std::vector<Scrub *> scrubs;
};
