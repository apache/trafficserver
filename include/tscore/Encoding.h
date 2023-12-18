/** @file

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

#include <swoc/MemArena.h>
#include "tscore/Diags.h"
#include "tscore/ink_string.h"

/*-------------------------------------------------------------------------
  Encoding::escapify_url_common

  This routine will escapify a URL to remove spaces (and perhaps other ugly
  characters) from a URL and replace them with a hex escape sequence.
  Since the escapes are larger (multi-byte) than the characters being
  replaced, the string returned will be longer than the string passed.

  This is a worker function called by escapify_url and pure_escapify_url.  These
  functions differ on whether the function tries to detect and avoid
  double URL encoding (escapify_url) or not (pure_escapify_url)
  -------------------------------------------------------------------------*/

namespace Encoding
{
char *escapify_url(swoc::MemArena *arena, char *url, size_t len_in, int *len_out, char *dst = nullptr, size_t dst_size = 0,
                   const unsigned char *map = nullptr);
char *pure_escapify_url(swoc::MemArena *arena, char *url, size_t len_in, int *len_out, char *dst = nullptr, size_t dst_size = 0,
                        const unsigned char *map = nullptr);
}; // namespace Encoding
