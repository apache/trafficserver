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
 *  ControlBase.cc - Base class to process generic modifiers to
 *                         ControlMatcher Directives
 *
 *
 ****************************************************************************/


#include "ink_unused.h"        /* MAGIC_EDITING_TAG */

#include "Main.h"
#include "URL.h"
#include "Tokenizer.h"
#include "ControlBase.h"
#include "MatcherUtils.h"
#include "HTTP.h"
#include "ControlMatcher.h"
#include "InkTime.h"
#include "ink_platform.h"
#include "HdrUtils.h"

// iPort added
static const char *ModTypeStrings[] = {
  "Modifier Invalid",
  "Port",
  "Scheme",
  "Prefix",
  "Suffix",
  "Method",
  "TimeOfDay",
  "SrcIP",
  "iPort",
  "Tag"
};

const int secondsInDay = 24 * 60 * 60;

struct timeMod
{
  time_t start_time;
  time_t end_time;
};

struct ipMod
{
  ip_addr_t start_addr;
  ip_addr_t end_addr;
};

struct portMod
{
  int start_port;
  int end_port;
};

ControlBase::~ControlBase()
{
  int num_el;

  if (mod_elements != NULL) {

    // Free all Prefix/Postfix strings and
    //   SrcIP, Port, Time Of Day Structures
    num_el = (*mod_elements).length() + 1;
    for (intptr_t i = 0; i < num_el; i++) {
      modifier_el & cur_el = (*mod_elements)[i];
      if (cur_el.type == MOD_PREFIX ||
          cur_el.type == MOD_SUFFIX ||
          cur_el.type == MOD_TIME || cur_el.type == MOD_PORT || cur_el.type == MOD_SRC_IP || cur_el.type == MOD_TAG) {
        xfree(cur_el.opaque_data);
      }
    }

    delete mod_elements;
  }
}

static const modifier_el default_el = { MOD_INVALID, NULL };

void
ControlBase::Print()
{
  int num_el;
  struct in_addr a;
  int port;

  if (mod_elements == NULL) {
    return;
  }

  num_el = (*mod_elements).length();
  if (num_el <= 0) {
    return;
  }

  printf("\t\t\t");
  for (intptr_t i = 0; i < num_el; i++) {
    modifier_el & cur_el = (*mod_elements)[i];
    switch (cur_el.type) {
    case MOD_INVALID:
      printf("%s  ", ModTypeStrings[MOD_INVALID]);
      break;
    case MOD_PORT:
      port = ((portMod *) cur_el.opaque_data)->start_port;
      printf("%s=%d-", ModTypeStrings[MOD_PORT], port);
      port = ((portMod *) cur_el.opaque_data)->end_port;
      printf("%d  ", port);
      break;
    case MOD_IPORT:
      printf("%s=%d  ", ModTypeStrings[MOD_IPORT], (int) (long) cur_el.opaque_data);
      break;
    case MOD_SCHEME:
      printf("%s=%s  ", ModTypeStrings[MOD_SCHEME], (char *) cur_el.opaque_data);
      break;
    case MOD_PREFIX:
      printf("%s=%s  ", ModTypeStrings[MOD_PREFIX], (char *) cur_el.opaque_data);
      break;
    case MOD_SUFFIX:
      printf("%s=%s  ", ModTypeStrings[MOD_SUFFIX], (char *) cur_el.opaque_data);
      break;
    case MOD_METHOD:
      printf("%s=%s  ", ModTypeStrings[MOD_METHOD], (char *) cur_el.opaque_data);
      break;
    case MOD_TAG:
      printf("%s=%s  ", ModTypeStrings[MOD_TAG], (char *) cur_el.opaque_data);
      break;
    case MOD_SRC_IP:
      a.s_addr = ((ipMod *) cur_el.opaque_data)->start_addr;
      printf("%s=%s-", ModTypeStrings[MOD_SRC_IP], inet_ntoa(a));
      a.s_addr = ((ipMod *) cur_el.opaque_data)->end_addr;
      printf("%s  ", inet_ntoa(a));
      break;
    case MOD_TIME:
      printf("%s=%d-%d  ", ModTypeStrings[MOD_TIME],
             (int) ((timeMod *) cur_el.opaque_data)->start_time, (int) ((timeMod *) cur_el.opaque_data)->end_time);
      break;
    }
  }
  printf("\n");
}

bool
ControlBase::CheckModifiers(HttpRequestData * request_data)
{
  if (!request_data->hdr) {
    //we use the same request_data for Socks as well (only IpMatcher)
    //we just return false here
    return true;
  }
  const char *data;
  const char *path;
  const char *method;
  ipMod *ipRange;
  portMod *portRange;
  timeMod *timeRange;
  time_t timeOfDay;
  URL *request_url = request_data->hdr->url_get();
  int data_len, path_len, scheme_len, method_len;
  char *tag, *request_tag;
  ip_addr_t src_ip;
  int port;

  // If the incoming request has no tag but the entry does, or both
  // have tags that do not match, then we do NOT have a match.
  request_tag = request_data->tag;
  if (!request_tag && mod_elements != NULL && getModElem(MOD_TAG) != NULL)
    return false;

  // If there are no modifiers, then of course we match them
  if (mod_elements == NULL) {
    return true;
  }

  for (intptr_t i = 0; i < mod_elements->length(); i++) {
    modifier_el & cur_el = (*mod_elements)[i];

    switch (cur_el.type) {
    case MOD_PORT:
      port = request_url->port_get();
      portRange = (portMod *) cur_el.opaque_data;
      if (port < portRange->start_port || port > portRange->end_port) {
        return false;
      }
      break;
    case MOD_IPORT:
      if (request_data->incoming_port != (long) cur_el.opaque_data) {
        return false;
      }
      break;
    case MOD_SCHEME:
      if (request_url->scheme_get(&scheme_len) != (char *) cur_el.opaque_data) {
        return false;
      }
      break;
    case MOD_PREFIX:
      // INKqa07820
      // The problem is that path_get() returns the URL's path
      // without the leading '/'.
      // E.g., If URL is http://inktomi/foo/bar,
      // then path_get() returns "foo/bar" but not "/foo/bar".
      // A simple solution is to skip the leading '/' in data.
      data = (char *) cur_el.opaque_data;
      if (*data == '/')
        data++;
      path = request_url->path_get(&path_len);
      if (ptr_len_ncmp(path, path_len, data, strlen(data)) != 0) {
        return false;
      }
      break;
    case MOD_SUFFIX:
      data = (char *) cur_el.opaque_data;
      data_len = strlen(data);
      ink_assert(data_len > 0);
      path = request_url->path_get(&path_len);
      // Suffix matching is case-insentive b/c it's
      //   mainly used for file type matching and
      //   jpeg, JPEG, Jpeg all mean the same thing
      //   (INKqa04363)
      if (path_len < data_len || strncasecmp(path + (path_len - data_len), data, data_len) != 0) {
        return false;
      }
      break;
    case MOD_METHOD:
      method = request_data->hdr->method_get(&method_len);
      if (ptr_len_casecmp(method, method_len, (char *) cur_el.opaque_data) != 0) {
        return false;
      }
      break;
    case MOD_TIME:
      timeRange = (timeMod *) cur_el.opaque_data;
      // INKqa11534
      // daylight saving time is not taken into consideration
      // so use ink_localtime_r() instead.
      // timeOfDay = (request_data->xact_start - ink_timezone()) % secondsInDay;
      {
        struct tm cur_tm;
        timeOfDay = request_data->xact_start;
        ink_localtime_r(&timeOfDay, &cur_tm);
        timeOfDay = cur_tm.tm_hour * (60 * 60)
          + cur_tm.tm_min * 60 + cur_tm.tm_sec;
      }
      if (timeOfDay < timeRange->start_time || timeOfDay > timeRange->end_time) {
        return false;
      }
      break;
    case MOD_SRC_IP:
      src_ip = htonl(request_data->src_ip);
      ipRange = (ipMod *) cur_el.opaque_data;
      if (src_ip < ipRange->start_addr || src_ip > ipRange->end_addr) {
        return false;
      }
      break;
    case MOD_TAG:
      // Check for a tag match.
      tag = (char *) cur_el.opaque_data;
      ink_assert(tag);
      if (request_tag && strcmp(request_tag, tag) != 0) {
        return false;
      }
      break;
    case MOD_INVALID:
      // Fall Through
    default:
      // Should never get here
      ink_assert(0);
      break;
    }
  }

  return true;
}

enum mod_errors
{ ME_UNKNOWN, ME_PARSE_FAILED,
  ME_BAD_SCHEME, ME_BAD_METHOD, ME_BAD_MOD,
  ME_CALLEE_GENERATED, ME_BAD_IPORT
};

static const char *errorFormats[] = {
  "Unknown error parsing modifier",
  "Unable to parse modifier",
  "Unknown scheme",
  "Unknown method",
  "Unknown modifier",
  "Callee Generated",
  "Bad incoming port"
};

const void *
ControlBase::getModElem(ModifierTypes t)
{
  for (int i = 0; i < mod_elements->length(); i++) {
    modifier_el & cur_el = (*mod_elements) (i);
    if (cur_el.type == t) {
      return cur_el.opaque_data;
    }
  }
  return NULL;
}

const char *
ControlBase::ProcessModifiers(matcher_line * line_info)
{

  // Variables for error processing
  const char *errBuf = NULL;
  mod_errors err = ME_UNKNOWN;

  int num = 0;
  char *label;
  char *val;
  unsigned int tmp;
  int num_mod_elements;
  const char *tmp_scheme = NULL;

  // Set up the array to handle the modifier
  num_mod_elements = (line_info->num_el > 0) ? line_info->num_el : 1;
  mod_elements = new DynArray<modifier_el> (&default_el, num_mod_elements);


  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {

    label = line_info->line[0][i];
    val = line_info->line[1][i];

    // Skip NULL tags
    if (label == NULL) {
      continue;
    }

    modifier_el & cur_el = (*mod_elements) (num);
    num++;

    // Make sure we have a value
    if (val == NULL) {
      err = ME_PARSE_FAILED;
      goto error;
    }

    if (strcasecmp(label, "port") == 0) {
      cur_el.type = MOD_PORT;
      errBuf = ProcessPort(val, &cur_el.opaque_data);
      if (errBuf != NULL) {
        err = ME_CALLEE_GENERATED;
        goto error;
      }
    } else if (strcasecmp(label, "iport") == 0) {
      // coverity[secure_coding]
      if (sscanf(val, "%d", &tmp) == 1) {
        cur_el.type = MOD_IPORT;
        cur_el.opaque_data = (void *)(uintptr_t)tmp;
      } else {
        err = ME_BAD_IPORT;
        goto error;
      }

    } else if (strcasecmp(label, "scheme") == 0) {
      tmp_scheme = hdrtoken_string_to_wks(val);
      if (!tmp_scheme) {
        err = ME_BAD_SCHEME;
        goto error;
      }
      cur_el.type = MOD_SCHEME;
      cur_el.opaque_data = (void *) tmp_scheme;

    } else if (strcasecmp(label, "method") == 0) {
      cur_el.type = MOD_METHOD;
      cur_el.opaque_data = (void *) xstrdup(val);

    } else if (strcasecmp(label, "prefix") == 0) {
      cur_el.type = MOD_PREFIX;
      cur_el.opaque_data = (void *) xstrdup(val);

    } else if (strcasecmp(label, "suffix") == 0) {
      cur_el.type = MOD_SUFFIX;
      cur_el.opaque_data = (void *) xstrdup(val);
    } else if (strcasecmp(label, "src_ip") == 0) {
      cur_el.type = MOD_SRC_IP;
      errBuf = ProcessSrcIp(val, &cur_el.opaque_data);
      if (errBuf != NULL) {
        err = ME_CALLEE_GENERATED;
        goto error;
      }
    } else if (strcasecmp(label, "time") == 0) {
      cur_el.type = MOD_TIME;
      errBuf = ProcessTimeOfDay(val, &cur_el.opaque_data);
      if (errBuf != NULL) {
        err = ME_CALLEE_GENERATED;
        goto error;
      }
    } else if (strcasecmp(label, "tag") == 0) {
      cur_el.type = MOD_TAG;
      cur_el.opaque_data = (void *) xstrdup(val);
    } else {
      err = ME_BAD_MOD;
      goto error;

    }
  }

  return NULL;

error:
  delete mod_elements;
  mod_elements = NULL;
  if (err == ME_CALLEE_GENERATED) {
    return errBuf;
  } else {
    return errorFormats[err];
  }
}

// const char* ControlBase::ProcessSrcIp(char* val, void** opaque_ptr)
//
//   Wrapper to Parse out the src ip range
//
//   On success, sets *opaque_ptr to a malloc allocated ipMod 
//      structure and returns NULL
//   On failure, returns a static error string
//
const char *
ControlBase::ProcessSrcIp(char *val, void **opaque_ptr)
{
  ipMod *range = (ipMod *) xmalloc(sizeof(ipMod));
  const char *errBuf = ExtractIpRange(val, &range->start_addr,
                                      &range->end_addr);

  if (errBuf == NULL) {
    *opaque_ptr = range;
    return NULL;
  } else {
    *opaque_ptr = NULL;
    xfree(range);
    return errBuf;
  }
}

// const char* TODtoSeconds(const char* time_str, time_t* seconds) {
//
//   Convents a TimeOfDay (TOD) to a second value
//
//   On success, sets *seconds to number of seconds since midnight
//      represented by time_str and returns NULL
//      
//   On failure, returns a static error string
//
const char *
TODtoSeconds(const char *time_str, time_t * seconds)
{
  int hour = 0;
  int min = 0;
  int sec = 0;
  time_t tmp = 0;

  // coverity[secure_coding]
  if (sscanf(time_str, "%d:%d:%d", &hour, &min, &sec) != 3) {
    // coverity[secure_coding]
    if (sscanf(time_str, "%d:%d", &hour, &min) != 2) {
      return "Malformed time specified";
    }
  }

  if (!(hour >= 0 && hour <= 23)) {
    return "Illegal hour specification";
  }
  tmp = hour * 60;

  if (!(min >= 0 && min <= 59)) {
    return "Illegal minute specification";
  }
  tmp = (tmp + min) * 60;

  if (!(sec >= 0 && sec <= 59)) {
    return "Illegal second specification";
  }
  tmp += sec;

  *seconds = tmp;
  return NULL;
}

// const char* ControlBase::ProcessTimeOfDay(char* val, void** opaque_ptr)
//
//   Parse out a time of day range.
//
//   On success, sets *opaque_ptr to a malloc allocated timeMod 
//      structure and returns NULL
//   On failure, returns a static error string
//
const char *
ControlBase::ProcessTimeOfDay(char *val, void **opaque_ptr)
{
  Tokenizer rangeTok("-");
  timeMod *t_mod = NULL;
  int num_tok;
  const char *errBuf;

  *opaque_ptr = NULL;

  num_tok = rangeTok.Initialize(val, SHARE_TOKS);
  if (num_tok == 1) {
    return "End time not specified";
  } else if (num_tok > 2) {
    return "Malformed Range";
  }

  t_mod = (timeMod *) xmalloc(sizeof(timeMod));

  errBuf = TODtoSeconds(rangeTok[0], &t_mod->start_time);

  if (errBuf != NULL) {
    xfree(t_mod);
    return errBuf;
  }

  errBuf = TODtoSeconds(rangeTok[1], &t_mod->end_time);

  if (errBuf != NULL) {
    xfree(t_mod);
    return errBuf;
  }

  *opaque_ptr = t_mod;
  return NULL;
}


// const char* ControlBase::ProcessPort(char* val, void** opaque_ptr)
//
//   Parse out a port range.
//
//   On success, sets *opaque_ptr to a malloc allocated portMod
//      structure and returns NULL
//   On failure, returns a static error string
//
const char *
ControlBase::ProcessPort(char *val, void **opaque_ptr)
{
  Tokenizer rangeTok("-");
  portMod *p_mod = NULL;
  int num_tok;

  *opaque_ptr = NULL;

  num_tok = rangeTok.Initialize(val, SHARE_TOKS);
  if (num_tok > 2) {
    return "Malformed Range";
  }

  p_mod = (portMod *) xmalloc(sizeof(portMod));

  // coverity[secure_coding]
  if (sscanf(rangeTok[0], "%d", &p_mod->start_port) != 1) {
    xfree(p_mod);
    return "Invalid start port";
  }

  if (num_tok == 2) {
    // coverity[secure_coding]
    if (sscanf(rangeTok[1], "%d", &p_mod->end_port) != 1) {
      xfree(p_mod);
      return "Invalid end port";
    }
    if (p_mod->end_port < p_mod->start_port) {
      xfree(p_mod);
      return "Malformed Range: end port < start port";
    }
  } else {
    p_mod->end_port = p_mod->start_port;
  }

  *opaque_ptr = p_mod;
  return NULL;
}
