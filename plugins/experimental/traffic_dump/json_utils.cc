/** @json_utils.cc
  Implementation of JSON formatting functions.
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

#include <iomanip>
#include <sstream>

#include "json_utils.h"

namespace
{
/** Write the content of buf to jsonfile between prevIdx (inclusive) and idx (not
 * inclusive).
 *
 * This function is used to help deal with escaped characters with a low number
 * of write calls. This is meant to be used like so: the caller inspects every
 * character in a buffer, doing one of two things with each character:
 *
 *  - The character does not need to be escaped. In this case, no call to
 *  write_buffered_content is made and the idx is advanced and prevIdx is not
 *  advanced. Eventually this character will be written once the contiguous set
 *  of non-escaped characters is collected.
 *
 *  - The character needs to be escaped. In this case, it will call this
 *  function. Anything between prevIdx and up to (but not including) idx is
 *  written. The caller then writes the escaped sequence for the character
 *  pointed to by idx ("\\t" instead of '\t', for instance). Then prevIdx is set
 *  past the escaped character.
 *
 * Finally, once all characters are inspected, a final write_buffered_content
 * call is made with idx set to one past the last character in buf. This
 * results in all characters between prevIdx and the last character in buf
 * (including that character) to be written.
 *
 * @param[in] buf The buffer containing characters that need to be written to jsonfile.
 *
 * @param[in,out] prevIdx The pointer to the beginning of the set of characters
 * in buf that have not been written yet. This is always updated before
 * function exit to one past idx.
 *
 * @param[in] idx The current character in buf being inspected.
 *
 * @param[out] jsonfile The stream to conditionally write buffer content to.
 */
inline void
write_buffered_context(char const *buf, int64_t &prevIdx, int64_t idx, std::ostream &jsonfile)
{
  if (prevIdx < idx) {
    jsonfile.write(buf + prevIdx, idx - prevIdx);
  }
  prevIdx = idx + 1;
}

int
esc_json_out(const char *buf, int64_t len, std::ostream &jsonfile)
{
  if (buf == nullptr) {
    return 0;
  }
  int64_t idx = 0, prevIdx = 0;
  // For an explanation of the algorithm here, see the doxygen comment for
  // write_buffered_content.
  for (idx = 0; idx < len; idx++) {
    char c = *(buf + idx);
    switch (c) {
    case '"':
    case '\\': {
      write_buffered_context(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\" << c;
      break;
    }
    case '\b': {
      write_buffered_context(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\b";
      break;
    }
    case '\f': {
      write_buffered_context(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\f";
      break;
    }
    case '\n': {
      write_buffered_context(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\n";
      break;
    }
    case '\r': {
      write_buffered_context(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\r";
      break;
    }
    case '\t': {
      write_buffered_context(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\t";
      break;
    }
    default: {
      if ('\x00' <= c && c <= '\x1f') {
        write_buffered_context(buf, prevIdx, idx, jsonfile);
        jsonfile << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
      }
      break;
      // else: The character does not need to be escaped. Do not call
      // write_buffered_content so nothing is written and prevIdx remains
      // pointing to the first character that needs to be written on the next
      // call to write_buffered_content.
    }
    }
  }
  // Finally, call write_buffered_content to write out any data that has not
  // been written yet.
  write_buffered_context(buf, prevIdx, idx, jsonfile);

  return len;
}
/** Escape characters in a string as needed and return the resultant escaped string.
 *
 * @param[in] s The characters that need to be escaped.
 */
std::string
escape_json(std::string_view s)
{
  std::ostringstream o;
  esc_json_out(s.data(), s.length(), o);
  return o.str();
}

/** An escape_json overload for a char buffer.
 *
 * @param[in] buf The char buffer pointer with characters that need to be escaped.
 *
 * @param[in] size The size of the buf char array.
 */
std::string
escape_json(char const *buf, int64_t size)
{
  std::ostringstream o;
  esc_json_out(buf, size, o);
  return o.str();
}

} // anonymous namespace

namespace traffic_dump
{
std::string
json_entry(std::string_view name, std::string_view value)
{
  return "\"" + escape_json(name) + "\":\"" + escape_json(value) + "\"";
}

std::string
json_entry(std::string_view name, char const *value, int64_t size)
{
  return "\"" + escape_json(name) + "\":\"" + escape_json(value, size) + "\"";
}

std::string
json_entry_array(std::string_view name, std::string_view value)
{
  return "[\"" + escape_json(name) + "\",\"" + escape_json(value) + "\"]";
}

} // namespace traffic_dump
