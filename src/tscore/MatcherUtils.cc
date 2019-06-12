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

/*****************************************************************************
 *
 *  MatcherUtils.cc - Various helper routines used in ControlMatcher
 *                    and ReverseProxy
 *
 *
 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/Diags.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_assert.h"
#include "tscore/MatcherUtils.h"
#include "tscore/Tokenizer.h"

// char* readIntoBuffer(const char* file_path, const char* module_name,
//                          int* read_size_ptr)
//
//  Attempts to open and read arg file_path into a buffer allocated
//   off the heap (via ats_malloc() )  Returns a pointer to the buffer
//   is successful and nullptr otherwise.
//
//  CALLEE is responsible for deallocating the buffer via ats_free()
//
char *
readIntoBuffer(const char *file_path, const char *module_name, int *read_size_ptr)
{
  int fd;
  struct stat file_info;
  char *file_buf, *buf;
  int read_size = 0;
  int file_size;

  if (read_size_ptr != nullptr) {
    *read_size_ptr = 0;
  }
  // Open the file for Blocking IO.  We will be reading this
  //   at start up and infrequently afterward
  if ((fd = open(file_path, O_RDONLY)) < 0) {
    Error("%s Can not open %s file : %s", module_name, file_path, strerror(errno));
    return nullptr;
  }

  if (fstat(fd, &file_info) < 0) {
    Error("%s Can not stat %s file : %s", module_name, file_path, strerror(errno));
    close(fd);
    return nullptr;
  }

  file_size = file_info.st_size; // number of bytes in file

  if (file_size < 0) {
    Error("%s Can not get correct file size for %s file : %" PRId64 "", module_name, file_path, (int64_t)file_info.st_size);
    close(fd);
    return nullptr;
  }

  ink_assert(file_size >= 0);

  // Allocate a buffer large enough to hold the entire file
  //   File size should be small and this makes it easy to
  //   do two passes on the file
  file_buf = (char *)ats_malloc(file_size + 1);
  // Null terminate the buffer so that string operations will work
  file_buf[file_size] = '\0';

  int ret = 0;
  buf     = file_buf; // working pointer

  // loop over read, trying to read in as much as we can each time.
  while (file_size > read_size) {
    ret = read(fd, buf, file_size - read_size);
    if (ret <= 0) {
      break;
    }

    buf += ret;
    read_size += ret;
  }

  buf = nullptr; // done with. don't want to accidentally use this instead of file_buf.

  // Check to make sure that we got the whole file
  if (ret < 0) {
    Error("%s Read of %s file failed : %s", module_name, file_path, strerror(errno));
    ats_free(file_buf);
    file_buf = nullptr;
  } else if (read_size < file_size) {
    // Didn't get the whole file, drop everything. We don't want to return
    //   something partially read because, ie. with configs, the behaviour
    //   is undefined.
    Error("%s Only able to read %d bytes out %d for %s file", module_name, read_size, file_size, file_path);
    ats_free(file_buf);
    file_buf = nullptr;
  }

  if (file_buf && read_size_ptr) {
    *read_size_ptr = read_size;
  }

  close(fd);

  return file_buf;
}

// int unescapifyStr(char* buffer)
//
//   Unescapifies a URL without a making a copy.
//    The passed in string is modified
//
int
unescapifyStr(char *buffer)
{
  char *read  = buffer;
  char *write = buffer;
  char subStr[3];

  subStr[2] = '\0';
  while (*read != '\0') {
    if (*read == '%' && *(read + 1) != '\0' && *(read + 2) != '\0') {
      subStr[0] = *(++read);
      subStr[1] = *(++read);
      *write    = (char)strtol(subStr, (char **)nullptr, 16);
      read++;
      write++;
    } else if (*read == '+') {
      *write = ' ';
      write++;
      read++;
    } else {
      *write = *read;
      write++;
      read++;
    }
  }
  *write = '\0';

  return (write - buffer);
}

const char *
ExtractIpRange(char *match_str, in_addr_t *min, in_addr_t *max)
{
  IpEndpoint ip_min, ip_max;
  const char *zret = ExtractIpRange(match_str, &ip_min.sa, &ip_max.sa);
  if (nullptr == zret) { // success
    if (ats_is_ip4(&ip_min) && ats_is_ip4(&ip_max)) {
      if (min) {
        *min = ntohl(ats_ip4_addr_cast(&ip_min));
      }
      if (max) {
        *max = ntohl(ats_ip4_addr_cast(&ip_max));
      }
    } else {
      zret = "The addresses were not IPv4 addresses.";
    }
  }
  return zret;
}

//   char* ExtractIpRange(char* match_str, sockaddr* addr1,
//                         sockaddr* addr2)
//
//   Attempts to extract either an Ip Address or an IP Range
//     from match_str.  The range should be two addresses
//     separated by a hyphen and no spaces
//
//   If the extraction is successful, sets addr1 and addr2
//     to the extracted values (in the case of a single
//     address addr2 = addr1) and returns nullptr
//
//   If the extraction fails, returns a static string
//     that describes the reason for the error.
//
const char *
ExtractIpRange(char *match_str, sockaddr *addr1, sockaddr *addr2)
{
  Tokenizer rangeTok("-/");
  bool mask = strchr(match_str, '/') != nullptr;
  int mask_bits;
  int mask_val;
  int numToks;
  IpEndpoint la1, la2;

  // Extract the IP addresses from match data
  numToks = rangeTok.Initialize(match_str, SHARE_TOKS);

  if (numToks < 0) {
    return "no IP address given";
  } else if (numToks > 2) {
    return "malformed IP range";
  }

  if (0 != ats_ip_pton(rangeTok[0], &la1.sa)) {
    return "malformed IP address";
  }

  // Handle a IP range
  if (numToks == 2) {
    if (mask) {
      if (!ats_is_ip4(&la1)) {
        return "Masks supported only for IPv4";
      }
      // coverity[secure_coding]
      if (sscanf(rangeTok[1], "%d", &mask_bits) != 1) {
        return "bad mask specification";
      }

      if (!(mask_bits >= 0 && mask_bits <= 32)) {
        return "invalid mask specification";
      }

      if (mask_bits == 32) {
        mask_val = 0;
      } else {
        mask_val = htonl(0xffffffff >> mask_bits);
      }
      in_addr_t a = ats_ip4_addr_cast(&la1);
      ats_ip4_set(&la2, a | mask_val);
      ats_ip4_set(&la1, a & (mask_val ^ 0xffffffff));

    } else {
      if (0 != ats_ip_pton(rangeTok[1], &la2)) {
        return "malformed ip address at range end";
      }
    }

    if (1 == ats_ip_addr_cmp(&la1.sa, &la2.sa)) {
      return "range start greater than range end";
    }

    ats_ip_copy(addr2, &la2);
  } else {
    ats_ip_copy(addr2, &la1);
  }

  ats_ip_copy(addr1, &la1);
  return nullptr;
}

// char* tokLine(char* buf, char** last, char cont)
//
//  Similar to strtok_r but only tokenizes on '\n'
//   and will return tokens that are empty strings
//
char *
tokLine(char *buf, char **last, char cont)
{
  char *start;
  char *cur;
  char *prev = nullptr;

  if (buf != nullptr) {
    start = cur = buf;
    *last       = buf;
  } else {
    start = cur = (*last) + 1;
  }

  while (*cur != '\0') {
    if (*cur == '\n') {
      if (cont != '\0' && prev != nullptr && *prev == cont) {
        *prev = ' ';
        *cur  = ' ';
      } else {
        *cur  = '\0';
        *last = cur;
        return start;
      }
    }
    prev = cur++;
  }

  // Return the last line even if it does
  //  not end in a newline
  if (cur > (*last + 1)) {
    *last = cur - 1;
    return start;
  }

  return nullptr;
}

const char *matcher_type_str[] = {"invalid", "host", "domain", "ip", "url_regex", "url", "host_regex"};

// char* processDurationString(char* str, int* seconds)
//
//   Take a duration sting which is composed of
//      digits followed by a unit specifier
//         w - week
//         d - day
//         h - hour
//         m - min
//         s - sec
//
//   Trailing digits without a specifier are
//    assumed to be seconds
//
//   Returns nullptr on success and a static
//    error string on failure
//
const char *
processDurationString(char *str, int *seconds)
{
  char *s       = str;
  char *current = str;
  char unit;
  int tmp;
  int multiplier;
  int result = 0;
  int len;

  if (str == nullptr) {
    return "Missing time";
  }

  len = strlen(str);
  for (int i = 0; i < len; i++) {
    if (!ParseRules::is_digit(*current)) {
      // Make sure there is a time to proces
      if (current == s) {
        return "Malformed time";
      }

      unit = *current;

      switch (unit) {
      case 'w':
        multiplier = 7 * 24 * 60 * 60;
        break;
      case 'd':
        multiplier = 24 * 60 * 60;
        break;
      case 'h':
        multiplier = 60 * 60;
        break;
      case 'm':
        multiplier = 60;
        break;
      case 's':
        multiplier = 1;
        break;
      case '-':
        return "Negative time not permitted";
      default:
        return "Invalid time unit specified";
      }

      *current = '\0';

      // coverity[secure_coding]
      if (sscanf(s, "%d", &tmp) != 1) {
        // Really should not happen since everything
        //   in the string is digit
        ink_assert(0);
        return "Malformed time";
      }

      result += (multiplier * tmp);
      s = current + 1;
    }
    current++;
  }

  // Read any trailing seconds
  if (current != s) {
    // coverity[secure_coding]
    if (sscanf(s, "%d", &tmp) != 1) {
      // Really should not happen since everything
      //   in the string is digit
      ink_assert(0);
      return "Malformed time";
    } else {
      result += tmp;
    }
  }
  // We rolled over the int
  if (result < 0) {
    return "Time too big";
  }

  *seconds = result;
  return nullptr;
}

const matcher_tags http_dest_tags = {"dest_host", "dest_domain", "dest_ip", "url_regex", "url", "host_regex", true};

const matcher_tags ip_allow_src_tags = {nullptr, nullptr, "src_ip", nullptr, nullptr, nullptr, false};

const matcher_tags ip_allow_dest_tags = {nullptr, nullptr, "dest_ip", nullptr, nullptr, nullptr, true};

const matcher_tags socks_server_tags = {nullptr, nullptr, "dest_ip", nullptr, nullptr, nullptr, false};

// char* parseConfigLine(char* line, matcher_line* p_line,
//                       const matcher_tags* tags)
//
//   Parse out a config file line suitable for passing to
//    a ControlMatcher object
//
//   If successful, nullptr is returned.  If unsuccessful,
//     a static error string is returned
//
const char *
parseConfigLine(char *line, matcher_line *p_line, const matcher_tags *tags)
{
  enum pState {
    FIND_LABEL,
    PARSE_LABEL,
    PARSE_VAL,
    START_PARSE_VAL,
    CONSUME,
  };

  pState state      = FIND_LABEL;
  bool inQuote      = false;
  char *copyForward = nullptr;
  char *copyFrom    = nullptr;
  char *s           = line;
  char *label       = nullptr;
  char *val         = nullptr;
  int num_el        = 0;
  matcher_type type = MATCH_NONE;

  // Zero out the parsed line structure
  memset(p_line, 0, sizeof(matcher_line));

  if (*s == '\0') {
    return nullptr;
  }

  do {
    switch (state) {
    case FIND_LABEL:
      if (!isspace(*s)) {
        state = PARSE_LABEL;
        label = s;
      }
      s++;
      break;
    case PARSE_LABEL:
      if (*s == '=') {
        *s    = '\0';
        state = START_PARSE_VAL;
      }
      s++;
      break;
    case START_PARSE_VAL:
      // Init state needed for parsing values
      copyForward = nullptr;
      copyFrom    = nullptr;

      if (*s == '"') {
        inQuote = true;
        val     = s + 1;
      } else if (*s == '\\') {
        inQuote = false;
        val     = s + 1;
      } else {
        inQuote = false;
        val     = s;
      }

      if (inQuote == false && (isspace(*s) || *(s + 1) == '\0')) {
        state = CONSUME;
      } else {
        state = PARSE_VAL;
      }

      s++;
      break;
    case PARSE_VAL:
      if (inQuote == true) {
        if (*s == '\\') {
          // The next character is escaped
          //
          // To remove the escaped character
          // we need to copy
          //  the rest of the entry over it
          //  but since we do not know where the
          //  end is right now, defer the work
          //  into the future

          if (copyForward != nullptr) {
            // Perform the prior copy forward
            int bytesCopy = s - copyFrom;
            memcpy(copyForward, copyFrom, s - copyFrom);
            ink_assert(bytesCopy > 0);

            copyForward += bytesCopy;
            copyFrom = s + 1;
          } else {
            copyForward = s;
            copyFrom    = s + 1;
          }

          // Scroll past the escape character
          s++;

          // Handle the case that places us
          //  at the end of the file
          if (*s == '\0') {
            break;
          }
        } else if (*s == '"') {
          state = CONSUME;
          *s    = '\0';
        }
      } else if ((*s == '\\' && ParseRules::is_digit(*(s + 1))) || !ParseRules::is_char(*s)) {
        // INKqa10511
        // traffic server need to handle unicode characters
        // right now ignore the entry
        return "Unrecognized encoding scheme";
      } else if (isspace(*s)) {
        state = CONSUME;
        *s    = '\0';
      }

      s++;

      // If we are now at the end of the line,
      //   we need to consume final data
      if (*s == '\0') {
        state = CONSUME;
      }
      break;
    case CONSUME:
      break;
    }

    if (state == CONSUME) {
      // See if there are any quote copy overs
      //   we've pushed into the future
      if (copyForward != nullptr) {
        int toCopy = (s - 1) - copyFrom;
        memcpy(copyForward, copyFrom, toCopy);
        *(copyForward + toCopy) = '\0';
      }

      p_line->line[0][num_el] = label;
      p_line->line[1][num_el] = val;
      type                    = MATCH_NONE;

      // Check to see if this the primary specifier we are looking for
      if (tags->match_ip && strcasecmp(tags->match_ip, label) == 0) {
        type = MATCH_IP;
      } else if (tags->match_host && strcasecmp(tags->match_host, label) == 0) {
        type = MATCH_HOST;
      } else if (tags->match_domain && strcasecmp(tags->match_domain, label) == 0) {
        type = MATCH_DOMAIN;
      } else if (tags->match_regex && strcasecmp(tags->match_regex, label) == 0) {
        type = MATCH_REGEX;
      } else if (tags->match_url && strcasecmp(tags->match_url, label) == 0) {
        type = MATCH_URL;
      } else if (tags->match_host_regex && strcasecmp(tags->match_host_regex, label) == 0) {
        type = MATCH_HOST_REGEX;
      }
      // If this a destination tag, use it
      if (type != MATCH_NONE) {
        // Check to see if this second destination specifier
        if (p_line->type != MATCH_NONE) {
          if (tags->dest_error_msg == false) {
            return "Multiple Sources Specified";
          } else {
            return "Multiple Destinations Specified";
          }
        } else {
          p_line->dest_entry = num_el;
          p_line->type       = type;
        }
      }
      num_el++;

      if (num_el > MATCHER_MAX_TOKENS) {
        return "Malformed line: Too many tokens";
      }

      state = FIND_LABEL;
    }
  } while (*s != '\0');

  p_line->num_el = num_el;

  if (state != CONSUME && state != FIND_LABEL) {
    return "Malformed entry";
  }

  if (!tags->empty() && p_line->type == MATCH_NONE) {
    if (tags->dest_error_msg == false) {
      return "No source specifier";
    } else {
      return "No destination specifier";
    }
  }

  return nullptr;
}
