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

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZEOF(t) (sizeof(t) / (sizeof((t)[0])))

typedef struct _info_t info_t;
typedef struct _state_t state_t;
typedef struct _transition_t transition_t;

struct _info_t {
  const char *name;
  const char *value;
  int namelen;
};

struct _state_t {
  int num;
  const char *value;
  transition_t *transitions;
};

struct _transition_t {
  int value;
  state_t *state;
  transition_t *next;
};

info_t fields[] = {
  {"Accept", "MIME_FIELD_ACCEPT", 0},
  {"Accept-Charset", "MIME_FIELD_ACCEPT_CHARSET", 0},
  {"Accept-Encoding", "MIME_FIELD_ACCEPT_ENCODING", 0},
  {"Accept-Language", "MIME_FIELD_ACCEPT_LANGUAGE", 0},
  {"Accept-Ranges", "MIME_FIELD_ACCEPT_RANGES", 0},
  {"Age", "MIME_FIELD_AGE", 0},
  {"Allow", "MIME_FIELD_ALLOW", 0},
  {"Approved", "MIME_FIELD_APPROVED", 0},
  {"Authorization", "MIME_FIELD_AUTHORIZATION", 0},
  {"Bytes", "MIME_FIELD_BYTES", 0},
  {"Cache-Control", "MIME_FIELD_CACHE_CONTROL", 0},
  {"Connection", "MIME_FIELD_CONNECTION", 0},
  {"Content-Base", "MIME_FIELD_CONTENT_BASE", 0},
  {"Content-Encoding", "MIME_FIELD_CONTENT_ENCODING", 0},
  {"Content-Language", "MIME_FIELD_CONTENT_LANGUAGE", 0},
  {"Content-Length", "MIME_FIELD_CONTENT_LENGTH", 0},
  {"Content-Location", "MIME_FIELD_CONTENT_LOCATION", 0},
  {"Content-Md5", "MIME_FIELD_CONTENT_MD5", 0},
  {"Content-Range", "MIME_FIELD_CONTENT_RANGE", 0},
  {"Content-Type", "MIME_FIELD_CONTENT_TYPE", 0},
  {"Control", "MIME_FIELD_CONTROL", 0},
  {"Cookie", "MIME_FIELD_COOKIE", 0},
  {"Date", "MIME_FIELD_DATE", 0},
  {"Distribution", "MIME_FIELD_DISTRIBUTION", 0},
  {"Etag", "MIME_FIELD_ETAG", 0},
  {"Expires", "MIME_FIELD_EXPIRES", 0},
  {"Followup-To", "MIME_FIELD_FOLLOWUP_TO", 0},
  {"From", "MIME_FIELD_FROM", 0},
  {"Host", "MIME_FIELD_HOST", 0},
  {"If-Match", "MIME_FIELD_IF_MATCH", 0},
  {"If-Modified-Since", "MIME_FIELD_IF_MODIFIED_SINCE", 0},
  {"If-None-Match", "MIME_FIELD_IF_NONE_MATCH", 0},
  {"If-Range", "MIME_FIELD_IF_RANGE", 0},
  {"If-Unmodified-Since", "MIME_FIELD_IF_UNMODIFIED_SINCE", 0},
  {"Keywords", "MIME_FIELD_KEYWORDS", 0},
  {"Last-Modified", "MIME_FIELD_LAST_MODIFIED", 0},
  {"Lines", "MIME_FIELD_LINES", 0},
  {"Location", "MIME_FIELD_LOCATION", 0},
  {"Max-Forwards", "MIME_FIELD_MAX_FORWARDS", 0},
  {"Message-ID", "MIME_FIELD_MESSAGE_ID", 0},
  {"Newsgroups", "MIME_FIELD_NEWSGROUPS", 0},
  {"Organization", "MIME_FIELD_ORGANIZATION", 0},
  {"Path", "MIME_FIELD_PATH", 0},
  {"Pragma", "MIME_FIELD_PRAGMA", 0},
  {"Proxy-Authenticate", "MIME_FIELD_PROXY_AUTHENTICATE", 0},
  {"Proxy-Authorization", "MIME_FIELD_PROXY_AUTHORIZATION", 0},
  {"Proxy-Connection", "MIME_FIELD_PROXY_CONNECTION", 0},
  {"Public", "MIME_FIELD_PUBLIC", 0},
  {"Range", "MIME_FIELD_RANGE", 0},
  {"References", "MIME_FIELD_REFERENCES", 0},
  {"Referer", "MIME_FIELD_REFERER", 0},
  {"Reply-To", "MIME_FIELD_REPLY_TO", 0},
  {"Retry-After", "MIME_FIELD_RETRY_AFTER", 0},
  {"Sender", "MIME_FIELD_SENDER", 0},
  {"Server", "MIME_FIELD_SERVER", 0},
  {"Set-Cookie", "MIME_FIELD_SET_COOKIE", 0},
  {"Subject", "MIME_FIELD_SUBJECT", 0},
  {"Summary", "MIME_FIELD_SUMMARY", 0},
  {"Transfer-Encoding", "MIME_FIELD_TRANSFER_ENCODING", 0},
  {"Upgrade", "MIME_FIELD_UPGRADE", 0},
  {"User-Agent", "MIME_FIELD_USER_AGENT", 0},
  {"Vary", "MIME_FIELD_VARY", 0},
  {"Via", "MIME_FIELD_VIA", 0},
  {"Warning", "MIME_FIELD_WARNING", 0},
  {"Www-Authenticate", "MIME_FIELD_WWW_AUTHENTICATE", 0},
  {"Xref", "MIME_FIELD_XREF", 0},
  {NULL, "MIME_FIELD_EXTENSION", 0},
};

info_t schemes[] = {
  {"file", "URL_SCHEME_FILE", 0},     {"ftp", "URL_SCHEME_FTP", 0},     {"gopher", "URL_SCHEME_GOPHER", 0},
  {"http", "URL_SCHEME_HTTP", 0},     {"https", "URL_SCHEME_HTTPS", 0}, {"mailto", "URL_SCHEME_MAILTO", 0},
  {"news", "URL_SCHEME_NEWS", 0},     {"nntp", "URL_SCHEME_NNTP", 0},   {"prospero", "URL_SCHEME_PROSPERO", 0},
  {"telnet", "URL_SCHEME_TELNET", 0}, {"wais", "URL_SCHEME_WAIS", 0},   {NULL, "URL_SCHEME_NONE", 0},
};

info_t methods[] = {
  {"CONNECT", "HTTP_METHOD_CONNECT", -1}, {"DELETE", "HTTP_METHOD_DELETE", -1}, {"GET", "HTTP_METHOD_GET", -1},
  {"HEAD", "HTTP_METHOD_HEAD", -1},       {"HTTP/", "HTTP_METHOD_HTTP", -1},    {"OPTIONS", "HTTP_METHOD_OPTIONS", -1},
  {"POST", "HTTP_METHOD_POST", -1},       {"PURGE", "HTTP_METHOD_PURGE", -1},   {"PUT", "HTTP_METHOD_PUT", -1},
  {"TRACE", "HTTP_METHOD_TRACE", -1},     {NULL, "HTTP_METHOD_NONE", 0},
};

info_t statuses[] = {
  {"100", "HTTP_STATUS_CONTINUE", -1},
  {"101", "HTTP_STATUS_SWITCHING_PROTOCOL", -1},
  {"103", "HTTP_STATUS_EARLY_HINTS", -1},
  {"200", "HTTP_STATUS_OK", -1},
  {"201", "HTTP_STATUS_CREATED", -1},
  {"202", "HTTP_STATUS_ACCEPTED", -1},
  {"203", "HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION", -1},
  {"204", "HTTP_STATUS_NO_CONTENT", -1},
  {"205", "HTTP_STATUS_RESET_CONTENT", -1},
  {"206", "HTTP_STATUS_PARTIAL_CONTENT", -1},
  {"300", "HTTP_STATUS_MULTIPLE_CHOICES", -1},
  {"301", "HTTP_STATUS_MOVED_PERMANENTLY", -1},
  {"302", "HTTP_STATUS_MOVED_TEMPORARILY", -1},
  {"303", "HTTP_STATUS_SEE_OTHER", -1},
  {"304", "HTTP_STATUS_NOT_MODIFIED", -1},
  {"305", "HTTP_STATUS_USE_PROXY", -1},
  {"400", "HTTP_STATUS_BAD_REQUEST", -1},
  {"401", "HTTP_STATUS_UNAUTHORIZED", -1},
  {"402", "HTTP_STATUS_PAYMENT_REQUIRED", -1},
  {"403", "HTTP_STATUS_FORBIDDEN", -1},
  {"404", "HTTP_STATUS_NOT_FOUND", -1},
  {"405", "HTTP_STATUS_METHOD_NOT_ALLOWED", -1},
  {"406", "HTTP_STATUS_NOT_ACCEPTABLE", -1},
  {"407", "HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED", -1},
  {"408", "HTTP_STATUS_REQUEST_TIMEOUT", -1},
  {"409", "HTTP_STATUS_CONFLICT", -1},
  {"410", "HTTP_STATUS_GONE", -1},
  {"411", "HTTP_STATUS_LENGTH_REQUIRED", -1},
  {"412", "HTTP_STATUS_PRECONDITION_FAILED", -1},
  {"413", "HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE", -1},
  {"414", "HTTP_STATUS_REQUEST_URI_TOO_LONG", -1},
  {"415", "HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE", -1},
  {"500", "HTTP_STATUS_INTERNAL_SERVER_ERROR", -1},
  {"501", "HTTP_STATUS_NOT_IMPLEMENTED", -1},
  {"502", "HTTP_STATUS_BAD_GATEWAY", -1},
  {"503", "HTTP_STATUS_SERVICE_UNAVAILABLE", -1},
  {"504", "HTTP_STATUS_GATEWAY_TIMEOUT", -1},
  {"505", "HTTP_STATUS_HTTPVER_NOT_SUPPORTED", -1},
  {NULL, "HTTP_STATUS_NONE", 0},
};

info_t days[] = {
  {"Fri", "FRIDAY", -1},    {"Friday", "FRIDAY", -1},       {"Mon", "MONDAY", -1},     {"Monday", "MONDAY", -1},
  {"Sat", "SATURDAY", -1},  {"Saturday", "SATURDAY", -1},   {"Sun", "SUNDAY", -1},     {"Sunday", "SUNDAY", -1},
  {"Thu", "THURSDAY", -1},  {"Thursday", "THURSDAY", -1},   {"Tue", "TUESDAY", -1},    {"Tuesday", "TUESDAY", -1},
  {"Wed", "WEDNESDAY", -1}, {"Wednesday", "WEDNESDAY", -1}, {NULL, "UNKNOWN_DAY", -1},
};

info_t months[] = {
  {"Apr", "APRIL", -1},   {"Aug", "AUGUST", -1},    {"Dec", "DECEMBER", -1},     {"Feb", "FEBRUARY", -1}, {"Jan", "JANUARY", -1},
  {"Jul", "JULY", -1},    {"Jun", "JUNE", -1},      {"Mar", "MARCH", -1},        {"May", "MAY", -1},      {"Nov", "NOVEMBER", -1},
  {"Oct", "OCTOBER", -1}, {"Sep", "SEPTEMBER", -1}, {NULL, "UNKNOWN_MONTH", -1},
};

info_t connections[] = {
  {"CLOSE", "HTTP_CONNECTION_CLOSE", -1},
  {"KEEP-ALIVE", "HTTP_CONNECTION_KEEP_ALIVE", -1},
  {NULL, "HTTP_CONNECTION_NONE", -1},
};

info_t cache_controls[] = {
  {"max-age", "HTTP_CACHE_DIRECTIVE_MAX_AGE", -1},
  {"max-stale", "HTTP_CACHE_DIRECTIVE_MAX_STALE", -1},
  {"min-fresh", "HTTP_CACHE_DIRECTIVE_MIN_FRESH", -1},
  {"must-revalidate", "HTTP_CACHE_DIRECTIVE_MUST_REVALIDATE", -1},
  {"no-cache", "HTTP_CACHE_DIRECTIVE_NO_CACHE", -1},
  {"no-store", "HTTP_CACHE_DIRECTIVE_NO_STORE", -1},
  {"no-transform", "HTTP_CACHE_DIRECTIVE_NO_TRANSFORM", -1},
  {"only-if-cached", "HTTP_CACHE_DIRECTIVE_ONLY_IF_CACHED", -1},
  {"private", "HTTP_CACHE_DIRECTIVE_PRIVATE", -1},
  {"proxy-revalidate", "HTTP_CACHE_DIRECTIVE_PROXY_REVALIDATE", -1},
  {"public", "HTTP_CACHE_DIRECTIVE_PUBLIC", -1},
  {"s-maxage", "HTTP_CACHE_DIRECTIVE_S_MAX_AGE", -1},
  {NULL, "HTTP_CACHE_DIRECTIVE_CACHE_EXTENSION", -1},
};

state_t *start  = NULL;
int state_count = 0;

int *map               = NULL;
int *basetbl           = NULL;
int *nexttbl           = NULL;
int *checktbl          = NULL;
const char **accepttbl = NULL;
char **prefixtbl       = NULL;

state_t *
mkstate()
{
  state_t *state;

  state              = (state_t *)malloc(sizeof(state_t));
  state->num         = state_count++;
  state->value       = NULL;
  state->transitions = NULL;

  return state;
}

transition_t *
mktransition()
{
  transition_t *transition;

  transition        = (transition_t *)malloc(sizeof(transition_t));
  transition->value = 0;
  transition->state = NULL;
  transition->next  = NULL;

  return transition;
}

void
prstate(state_t *state)
{
  transition_t *transitions;

  printf("%3d:", state->num);

  if (state->value)
    printf(" %s", state->value);
  printf("\n");

  transitions = state->transitions;
  while (transitions) {
    printf("     %c --> %d\n", tolower(transitions->value), transitions->state->num);
    transitions = transitions->next;
  }

  transitions = state->transitions;
  while (transitions) {
    prstate(transitions->state);
    transitions = transitions->next;
  }
}

void
add_states(state_t *state, info_t *info, int pos)
{
  transition_t *transitions;

  if (info->namelen == pos) {
    state->value = info->value;
    return;
  }

  transitions = state->transitions;
  while (transitions) {
    if (tolower(transitions->value) == tolower(info->name[pos])) {
      if ((transitions->state->value && (info->namelen == (pos + 1))) || (info->namelen != (pos + 1))) {
        add_states(transitions->state, info, pos + 1);
        return;
      }
    }

    transitions = transitions->next;
  }

  if (state->transitions) {
    transitions = state->transitions;
    while (transitions->next)
      transitions = transitions->next;

    transitions->next = mktransition();
    transitions       = transitions->next;
  } else {
    transitions        = mktransition();
    state->transitions = transitions;
  }

  transitions->value = info->name[pos];
  transitions->state = mkstate();

  add_states(transitions->state, info, pos + 1);
}

void
prtable(const char *type, const char *name, int *table, int size)
{
  int i;

  printf("  static %s %s[%d] =\n", type, name, size);
  printf("  {\n");

  for (i = 0; i < size; i++) {
    if ((i % 12) == 0)
      printf("    %3d,", table[i]);
    else if ((i % 12) == 11)
      printf(" %3d,\n", table[i]);
    else
      printf(" %3d,", table[i]);
  }

  if ((i % 12) != 0)
    printf("\n");

  printf("  };\n");
}

int
mkmap(state_t *state)
{
  static int count = 1;

  transition_t *transitions;

  transitions = state->transitions;
  while (transitions) {
    if (map[tolower(transitions->value)] == 0) {
      map[tolower(transitions->value)] = count;
      map[toupper(transitions->value)] = count;
      count += 1;
    }

    mkmap(transitions->state);

    transitions = transitions->next;
  }

  return count;
}

void
mkaccept(state_t *state, const char *defvalue)
{
  transition_t *transitions;

  if (state->value)
    accepttbl[state->num] = state->value;
  else
    accepttbl[state->num] = defvalue;

  transitions = state->transitions;
  while (transitions) {
    mkaccept(transitions->state, defvalue);
    transitions = transitions->next;
  }
}

void
mkprefix(state_t *state, char *prefix, int length)
{
  transition_t *transitions;

  prefixtbl[state->num] = (char *)malloc(sizeof(char) * (length + 1));
  if (length > 0) {
    strncpy(prefixtbl[state->num], prefix, length);
  }
  prefixtbl[state->num][length] = '\0';

  transitions = state->transitions;
  while (transitions) {
    prefix[length] = transitions->value;
    mkprefix(transitions->state, prefix, length + 1);
    transitions = transitions->next;
  }
}

int
checkbase(state_t *state, int base)
{
  transition_t *transitions;

  transitions = state->transitions;
  while (transitions) {
    if (checktbl[base + map[transitions->value]] != -1)
      return 0;
    transitions = transitions->next;
  }

  return 1;
}

void
mktranstables(state_t *state)
{
  transition_t *transitions;
  int base;

  base = 0;
  while (base < state_count) {
    if (checkbase(state, base))
      break;
    base += 1;
  }

  assert(base < state_count);

  basetbl[state->num] = base;

  transitions = state->transitions;
  while (transitions) {
    assert(checktbl[basetbl[state->num] + map[transitions->value]] == -1);

    checktbl[basetbl[state->num] + map[transitions->value]] = state->num;
    nexttbl[basetbl[state->num] + map[transitions->value]]  = transitions->state->num;
    transitions                                             = transitions->next;
  }

  transitions = state->transitions;
  while (transitions) {
    mktranstables(transitions->state);
    transitions = transitions->next;
  }
}

void
mktables(state_t *state, const char *defvalue, int useprefix)
{
  char prefix[1024];
  int char_count;
  int i;

  /* make the character map */
  map = (int *)malloc(sizeof(int) * 256);
  for (i = 0; i < 256; i++)
    map[i] = 0;

  char_count = mkmap(state);

  prtable("int", "map", map, 256);
  printf("\n");

  /* make the accept state table */
  accepttbl = (const char **)malloc(sizeof(const char *) * state_count);
  for (i = 0; i < state_count; i++)
    accepttbl[i] = NULL;

  mkaccept(state, defvalue);

  /* print the accept state table */
  printf("  static int accepttbl[%d] =\n", state_count);
  printf("  {\n");

  for (i = 0; i < state_count; i++)
    printf("    %s,\n", accepttbl[i]);

  printf("  };\n\n");

  /* make the prefix table */
  if (useprefix) {
    prefixtbl = (char **)malloc(sizeof(char *) * state_count);
    for (i = 0; i < state_count; i++)
      prefixtbl[i] = NULL;

    mkprefix(state, prefix, 0);

    /* print the prefix table */
    printf("  static const char *prefixtbl[%d] =\n", state_count);
    printf("  {\n");

    for (i = 0; i < state_count; i++)
      printf("    \"%s\",\n", prefixtbl[i]);

    printf("  };\n\n");
  }

  /* make the state transition tables */

  basetbl  = (int *)malloc(sizeof(int) * state_count);
  nexttbl  = (int *)malloc(sizeof(int) * (state_count + char_count));
  checktbl = (int *)malloc(sizeof(int) * (state_count + char_count));

  for (i = 0; i < state_count; i++) {
    basetbl[i] = -1;
  }

  for (i = 0; i < (state_count + char_count); i++) {
    nexttbl[i]  = 0;
    checktbl[i] = -1;
  }

  mktranstables(state);

  prtable("int", "basetbl", basetbl, state_count);
  printf("\n");
  prtable("int", "nexttbl", nexttbl, state_count + char_count);
  printf("\n");
  prtable("int", "checktbl", checktbl, state_count + char_count);
}

const char *
rundfa(const char *buf, int length)
{
  const char *end;
  int state;
  int ch, tmp;

  state = 0;
  end   = buf + length;

  while (buf != end) {
    ch = map[(int)*buf++];

    tmp = basetbl[state] + ch;
    if (checktbl[tmp] != state)
      return NULL;
    state = nexttbl[tmp];
  }

  return accepttbl[state];
}

void
mkdfa(info_t *infos, int ninfos, int useprefix, int debug)
{
  /*
     static const char *names[] =
     {
     "foo",
     "bar",
     "foobar",
     "argh",
     "filep",
     };
     static int nnames = SIZEOF (names);
     const char *accept;
   */

  int i;

  start = mkstate();

  for (i = 0; i < (ninfos - 1); i++)
    infos[i].namelen = strlen(infos[i].name);

  for (i = 0; i < (ninfos - 1); i++)
    add_states(start, &infos[i], 0);

  mktables(start, infos[ninfos - 1].value, useprefix);

  if (debug) {
    printf("\n/*\n");
    prstate(start);
    printf("*/\n");

    /*
       for (i = 0; i < ninfos; i++)
       {
       accept = rundfa (infos[i].name, infos[i].namelen);
       if (accept)
       printf ("%s\n", accept);
       else
       printf ("%s not accepted\n", infos[i].name);
       }

       for (i = 0; i < nnames; i++)
       {
       accept = rundfa (names[i], strlen (names[i]));
       if (accept)
       printf ("%s\n", accept);
       else
       printf ("%s not accepted\n", names[i]);
       }
     */
  }
}

int
main(int argc, char *argv[])
{
  if (argc < 2)
    return 1;

  if (strcmp(argv[1], "fields") == 0)
    mkdfa(fields, SIZEOF(fields), 1, (argc == 3));
  else if (strcmp(argv[1], "methods") == 0)
    mkdfa(methods, SIZEOF(methods), 0, (argc == 3));
  else if (strcmp(argv[1], "statuses") == 0)
    mkdfa(statuses, SIZEOF(statuses), 0, (argc == 3));
  else if (strcmp(argv[1], "schemes") == 0)
    mkdfa(schemes, SIZEOF(schemes), 0, (argc == 3));
  else if (strcmp(argv[1], "days") == 0)
    mkdfa(days, SIZEOF(days), 0, (argc == 3));
  else if (strcmp(argv[1], "months") == 0)
    mkdfa(months, SIZEOF(months), 0, (argc == 3));
  else if (strcmp(argv[1], "connections") == 0)
    mkdfa(connections, SIZEOF(connections), 0, (argc == 3));
  else if (strcmp(argv[1], "cache-controls") == 0)
    mkdfa(cache_controls, SIZEOF(cache_controls), 0, (argc == 3));

  return 0;
}
