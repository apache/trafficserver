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

#include "tscore/ink_platform.h"
#include "tscore/ink_args.h"
#include "tscore/I_Version.h"
#include "mgmtapi.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string_view>
#include "tscore/Regex.h"

/// XXX Use DFA or Regex wrappers?
#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#define SUBSTRING_VECTOR_COUNT 30 // Should be multiple of 3

static AppVersionInfo appVersionInfo;

struct VIA {
  VIA(const char *t) : title(t) {}
  ~VIA() { delete next; }
  const char *title;
  const char *viaData[128] = {}; // zero initialize
  VIA *next                = nullptr;
};

// Function to get via header table for every field/category in the via header
static VIA *
detailViaLookup(char flag)
{
  VIA *viaTable;

  // Detailed via codes after ":"
  switch (flag) {
  case 't':
    viaTable                                           = new VIA("Tunnel info");
    viaTable->viaData[static_cast<unsigned char>(' ')] = "no tunneling";
    viaTable->viaData[static_cast<unsigned char>('U')] = "tunneling because of url (url suggests dynamic content)";
    viaTable->viaData[static_cast<unsigned char>('M')] = "tunneling due to a method (e.g. CONNECT)";
    viaTable->viaData[static_cast<unsigned char>('O')] = "tunneling because cache is turned off";
    viaTable->viaData[static_cast<unsigned char>('F')] = "tunneling due to a header field (such as presence of If-Range header)";
    viaTable->viaData[static_cast<unsigned char>('N')] = "tunneling due to no forward";
    viaTable->viaData[static_cast<unsigned char>('A')] = "tunnel authorization";
    break;
  case 'c':
    // Cache type
    viaTable                                           = new VIA("Cache Type");
    viaTable->viaData[static_cast<unsigned char>('C')] = "cache";
    viaTable->viaData[static_cast<unsigned char>('L')] = "cluster, (not used)";
    viaTable->viaData[static_cast<unsigned char>('P')] = "parent";
    viaTable->viaData[static_cast<unsigned char>('S')] = "server";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "unknown";

    // Cache Lookup Result
    viaTable->next                                           = new VIA("Cache Lookup Result");
    viaTable->next->viaData[static_cast<unsigned char>('C')] = "cache hit but config forces revalidate";
    viaTable->next->viaData[static_cast<unsigned char>('I')] =
      "conditional miss (client sent conditional, fresh in cache, returned 412)";
    viaTable->next->viaData[static_cast<unsigned char>(' ')] = "cache miss or no cache lookup";
    viaTable->next->viaData[static_cast<unsigned char>('U')] = "cache hit, but client forces revalidate (e.g. Pragma: no-cache)";
    viaTable->next->viaData[static_cast<unsigned char>('D')] = "cache hit, but method forces revalidated (e.g. ftp, not anonymous)";
    viaTable->next->viaData[static_cast<unsigned char>('M')] = "cache miss (url not in cache)";
    viaTable->next->viaData[static_cast<unsigned char>('N')] =
      "conditional hit (client sent conditional, doc fresh in cache, returned 304)";
    viaTable->next->viaData[static_cast<unsigned char>('H')] = "cache hit";
    viaTable->next->viaData[static_cast<unsigned char>('S')] = "cache hit, but expired";
    viaTable->next->viaData[static_cast<unsigned char>('K')] = "cookie miss";
    break;
  case 'p':
    viaTable                                           = new VIA("Parent proxy connection status");
    viaTable->viaData[static_cast<unsigned char>(' ')] = "no parent proxy or unknown";
    viaTable->viaData[static_cast<unsigned char>('S')] = "connection opened successfully";
    viaTable->viaData[static_cast<unsigned char>('F')] = "connection open failed";
    break;
  case 's':
    viaTable                                           = new VIA("Origin server connection status");
    viaTable->viaData[static_cast<unsigned char>(' ')] = "no server connection needed";
    viaTable->viaData[static_cast<unsigned char>('S')] = "connection opened successfully";
    viaTable->viaData[static_cast<unsigned char>('F')] = "connection open failed";
    break;
  default:
    viaTable = nullptr;
    fprintf(stderr, "%s: %s: %c\n", appVersionInfo.AppStr, "Invalid VIA header character", flag);
    break;
  }

  return viaTable;
}

// Function to get via header table for every field/category in the via header
static VIA *
standardViaLookup(char flag)
{
  VIA *viaTable;

  // Via codes before ":"
  switch (flag) {
  case 'u':
    viaTable                                           = new VIA("Request headers received from client");
    viaTable->viaData[static_cast<unsigned char>('C')] = "cookie";
    viaTable->viaData[static_cast<unsigned char>('E')] = "error in request";
    viaTable->viaData[static_cast<unsigned char>('S')] = "simple request (not conditional)";
    viaTable->viaData[static_cast<unsigned char>('N')] = "no-cache";
    viaTable->viaData[static_cast<unsigned char>('I')] = "IMS";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "unknown";
    break;
  case 'c':
    viaTable                                           = new VIA("Result of Traffic Server cache lookup for URL");
    viaTable->viaData[static_cast<unsigned char>('A')] = "in cache, not acceptable (a cache \"MISS\")";
    viaTable->viaData[static_cast<unsigned char>('H')] = "in cache, fresh (a cache \"HIT\")";
    viaTable->viaData[static_cast<unsigned char>('S')] = "in cache, stale (a cache \"MISS\")";
    viaTable->viaData[static_cast<unsigned char>('R')] = "in cache, fresh Ram hit (a cache \"HIT\")";
    viaTable->viaData[static_cast<unsigned char>('M')] = "miss (a cache \"MISS\")";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "no cache lookup";
    break;
  case 's':
    viaTable                                           = new VIA("Response information received from origin server");
    viaTable->viaData[static_cast<unsigned char>('E')] = "error in response";
    viaTable->viaData[static_cast<unsigned char>('S')] = "connection opened successfully";
    viaTable->viaData[static_cast<unsigned char>('N')] = "not-modified";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "no server connection needed";
    break;
  case 'f':
    viaTable                                           = new VIA("Result of document write-to-cache:");
    viaTable->viaData[static_cast<unsigned char>('U')] = "updated old cache copy";
    viaTable->viaData[static_cast<unsigned char>('D')] = "cached copy deleted";
    viaTable->viaData[static_cast<unsigned char>('W')] = "written into cache (new copy)";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "no cache write performed";
    break;
  case 'p':
    viaTable                                           = new VIA("Proxy operation result");
    viaTable->viaData[static_cast<unsigned char>('R')] = "origin server revalidated";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "unknown";
    viaTable->viaData[static_cast<unsigned char>('S')] = "served or connection opened successfully";
    viaTable->viaData[static_cast<unsigned char>('N')] = "not-modified";
    break;
  case 'e':
    viaTable                                           = new VIA("Error codes (if any)");
    viaTable->viaData[static_cast<unsigned char>('A')] = "authorization failure";
    viaTable->viaData[static_cast<unsigned char>('H')] = "header syntax unacceptable";
    viaTable->viaData[static_cast<unsigned char>('C')] = "connection to server failed";
    viaTable->viaData[static_cast<unsigned char>('T')] = "connection timed out";
    viaTable->viaData[static_cast<unsigned char>('S')] = "server related error";
    viaTable->viaData[static_cast<unsigned char>('D')] = "dns failure";
    viaTable->viaData[static_cast<unsigned char>('N')] = "no error";
    viaTable->viaData[static_cast<unsigned char>('F')] = "request forbidden";
    viaTable->viaData[static_cast<unsigned char>('R')] = "cache read error";
    viaTable->viaData[static_cast<unsigned char>('M')] = "moved temporarily";
    viaTable->viaData[static_cast<unsigned char>('L')] = "looped detected";
    viaTable->viaData[static_cast<unsigned char>(' ')] = "unknown";
    break;
  default:
    viaTable = nullptr;
    fprintf(stderr, "%s: %s: %c\n", appVersionInfo.AppStr, "Invalid VIA header character", flag);
    break;
  }

  return viaTable;
}

// Function to print via header
static void
printViaHeader(std::string_view header)
{
  VIA *viaTable = nullptr;
  VIA *viaEntry = nullptr;
  bool isDetail = false;

  printf("Via Header Details:\n");

  // Loop through input via header flags
  for (char c : header) {
    if ((c == ':') || (c == ';')) {
      isDetail = true;
      continue;
    }

    if (islower(c)) {
      // Get the via header table
      delete viaTable;
      viaEntry = viaTable = isDetail ? detailViaLookup(c) : standardViaLookup(c);
    } else {
      // This is a one of the sequence of (uppercase) VIA codes.
      if (viaEntry) {
        unsigned char idx = c;
        printf("%-55s:", viaEntry->title);
        printf("%s\n", viaEntry->viaData[idx] ? viaEntry->viaData[idx] : "Invalid sequence");
        viaEntry = viaEntry->next;
      }
    }
  }
  delete viaTable;
}

// Check validity of via header and then decode it
static TSMgmtError
decodeViaHeader(std::string_view text)
{
  // Via header inside square brackets
  if (!text.empty() && text.front() == '[' && text.back() == ']') {
    text.remove_prefix(1);
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return TS_ERR_FAIL;
  }

  printf("Via header is [%.*s], Length is %zu\n", int(text.size()), text.data(), text.size());

  char extender[6];
  if (text.size() == 5) { // add a trailing space in this case.
    memcpy(extender, text.data(), text.size());
    extender[5] = ' ';
    text        = std::string_view{extender, 6};
  }

  if (text.size() == 22 || text.size() == 6) {
    // Decode via header
    printViaHeader(text);
    return TS_ERR_OKAY;
  }
  // Invalid header size, come out.
  printf("\nInvalid VIA header. VIA header length should be 6 or 22 characters\n");
  printf("Valid via header format is "
         "[u<client-stuff>c<cache-lookup-stuff>s<server-stuff>f<cache-fill-stuff>p<proxy-stuff>e<error-codes>:t<tunneling-info>c<"
         "cache type><cache-lookup-result>p<parent-proxy-conn-info>s<server-conn-info>]\n");
  return TS_ERR_FAIL;
}

// Read user input from stdin
static TSMgmtError
filterViaHeader()
{
  const pcre *compiledReg;
  const pcre_extra *extraReg = nullptr;
  int subStringVector[SUBSTRING_VECTOR_COUNT];
  const char *err;
  int errOffset;
  int pcreExecCode;
  int i;
  const char *viaPattern =
    R"(\[([ucsfpe]+[^\]]+)\])"; // Regex to match via header with in [] which can start with character class ucsfpe
  std::string line;

  // Compile PCRE via header pattern
  compiledReg = pcre_compile(viaPattern, 0, &err, &errOffset, nullptr);

  if (compiledReg == nullptr) {
    printf("PCRE regex compilation failed with error %s at offset %d\n", err, errOffset);
    return TS_ERR_FAIL;
  }

  // Read all lines from stdin
  while (std::getline(std::cin, line)) {
    // Match for via header pattern
    pcreExecCode = pcre_exec(compiledReg, extraReg, line.data(), line.size(), 0, 0, subStringVector, SUBSTRING_VECTOR_COUNT);

    // Match failed, don't worry. Continue to next line.
    if (pcreExecCode < 0) {
      continue;
    }

    // Match successful, but too many substrings
    if (pcreExecCode == 0) {
      pcreExecCode = SUBSTRING_VECTOR_COUNT / 3;
      printf("Too many substrings were found. %d substrings couldn't fit into subStringVector\n", pcreExecCode - 1);
    }

    // Loop based on number of matches found
    for (i = 1; i < pcreExecCode; i++) {
      std::string_view match{line.data() + subStringVector[2 * i], size_t(subStringVector[2 * i + 1] - subStringVector[2 * i])};
      // Decode matched substring
      decodeViaHeader(match);
    }
  }
  return TS_ERR_OKAY;
}

int
main(int /* argc ATS_UNUSED */, const char **argv)
{
  TSMgmtError status;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME, "traffic_via", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  /* see 'ink_args.h' for meanings of the various fields */
  ArgumentDescription argument_descriptions[] = {
    VERSION_ARGUMENT_DESCRIPTION(),
    HELP_ARGUMENT_DESCRIPTION(),
  };

  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  for (unsigned i = 0; i < n_file_arguments; ++i) {
    if (strcmp(file_arguments[i], "-") == 0) {
      // Filter arguments provided from stdin
      status = filterViaHeader();
    } else {
      status = decodeViaHeader(std::string_view{file_arguments[i], strlen(file_arguments[i])});
    }

    if (status != TS_ERR_OKAY) {
      return 1;
    }
  }

  return 0;
}
