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

/*************************** -*- Mod: C++ -*- ******************************

   ParseRules.h --


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ParseRules.h"

const unsigned int parseRulesCType[256] = {
#include "ParseRulesCType"
};
const char parseRulesCTypeToUpper[256] = {
#include "ParseRulesCTypeToUpper"
};
const char parseRulesCTypeToLower[256] = {
#include "ParseRulesCTypeToLower"
};

// Implement our atol() / strtol() utility functions. Note that these will
// deal with two cases atol does not:
//
//   1. They will handle both base 10 and base 16, always. 0x indicates hex.
//   2. They all honor the SI multipliers (i.e. K, M, G and T.
//
int64_t
ink_atoi64(const char *str)
{
  int64_t num  = 0;
  int negative = 0;

  while (*str && ParseRules::is_wslfcr(*str)) {
    str += 1;
  }

  if (unlikely(str[0] == '0' && str[1] == 'x')) {
    str += 2;
    while (*str && ParseRules::is_hex(*str)) {
      num = (num << 4) + ink_get_hex(*str++);
    }
  } else {
    if (unlikely(*str == '-')) {
      negative = 1;
      str += 1;
    }

    /*
      NOTE: we first compute the value as negative then correct the
      sign back to positive. This enables us to correctly parse MININT.
    */
    while (*str && ParseRules::is_digit(*str)) {
      num = (num * 10) - (*str++ - '0');
    }
#if USE_SI_MULTILIERS
    if (*str) {
      if (*str == 'K') {
        num = num * (1LL << 10);
      } else if (*str == 'M') {
        num = num * (1LL << 20);
      } else if (*str == 'G') {
        num = num * (1LL << 30);
      } else if (*str == 'T') {
        num = num * (1LL << 40);
      }
    }
#endif
    if (!negative) {
      num = -num;
    }
  }
  return num;
}

uint64_t
ink_atoui64(const char *str)
{
  uint64_t num = 0;

  while (*str && ParseRules::is_wslfcr(*str)) {
    str += 1;
  }

  if (unlikely(str[0] == '0' && str[1] == 'x')) {
    str += 2;
    while (*str && ParseRules::is_hex(*str)) {
      num = (num << 4) + ink_get_hex(*str++);
    }
  } else {
    while (*str && ParseRules::is_digit(*str)) {
      num = (num * 10) + (*str++ - '0');
    }
#if USE_SI_MULTILIERS
    if (*str) {
      if (*str == 'K') {
        num = num * (1LL << 10);
      } else if (*str == 'M') {
        num = num * (1LL << 20);
      } else if (*str == 'G') {
        num = num * (1LL << 30);
      } else if (*str == 'T') {
        num = num * (1LL << 40);
      }
    }
#endif
  }
  return num;
}

int64_t
ink_atoi64(const char *str, int len)
{
  int64_t num  = 0;
  int negative = 0;

  while (len > 0 && *str && ParseRules::is_wslfcr(*str)) {
    str += 1;
    len--;
  }

  if (len < 1) {
    return 0;
  }

  if (unlikely(str[0] == '0' && len > 1 && str[1] == 'x')) {
    str += 2;
    while (len > 0 && *str && ParseRules::is_hex(*str)) {
      num = (num << 4) + ink_get_hex(*str++);
      len--;
    }
  } else {
    if (unlikely(*str == '-')) {
      negative = 1;
      str += 1;
    }

    /*
      NOTE: we first compute the value as negative then correct the
      sign back to positive. This enables us to correctly parse MININT.
    */
    while (len > 0 && *str && ParseRules::is_digit(*str)) {
      num = (num * 10) - (*str++ - '0');
      len--;
    }
#if USE_SI_MULTILIERS
    if (len > 0 && *str) {
      if (*str == 'K') {
        num = num * (1 << 10);
      } else if (*str == 'M') {
        num = num * (1 << 20);
      } else if (*str == 'G') {
        num = num * (1 << 30);
      }
    }
#endif

    if (!negative) {
      num = -num;
    }
  }
  return num;
}
