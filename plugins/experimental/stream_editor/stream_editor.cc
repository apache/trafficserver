/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* stream-editor: apply string and/or regexp search-and-replace to
 * HTTP request and response bodies.
 *
 * Load from plugin.config, with one or more filenames as args.
 * These are config files, and all config files are equal.
 *
 * Each line in a config file and conforming to config syntax specifies a
 * rule for rewriting input or output.
 *
 * A line starting with [out] is an output rule.
 * One starting with [in] is an input rule.
 * Any other line is ignored, so blank lines and comments are fine.
 *
 * Each line must have a from: field and a to: field specifying what it
 * rewrites from and to.  Other fields are optional.  The full list:
 *    from:flags:value
 *    to:value
 *    scope:flags:value
 *    prio:value
 *    len:value
 *
 *   Fields are separated by whitespace.  from: and to: fields may contain
 *   whitespace if they are quoted.  Quoting may use any non-alphanumeric
 *   matched-pair delimiter, though the delimiter may not then appear
 *   (even escaped) within the value string.
 *
 *   Flags are:
 *      i - case-independent matching
 *      r - regexp match
 *      u (applies only to scope) - apply scope match to full URI
 *         starting with "http://" (the default is to match the path
 *         only, as in for example a <Location> in HTTPD).
 *
 *   A from: value is a string or a regexp, according to flags.
 *   A to: string is a replacement, and may reference regexp memory $1 - $9.
 *
 *   A scope: value is likewise a string or (memory-less) regexp and
 *   determines the scope of URLs over which the rule applies.
 *
 *   A prio: value is a single digit, and determines the priority of the
 *   rule.  That is to say, two or more rules generate overlapping matches,
 *   the priority value will determine which rule prevails.  A lower
 *   priority value prevails over a higher one.
 *
 *   A len: value is an integer, and applies only to a regexp from:
 *   It should be an estimate of the largest match size expected from
 *   the from: pattern.  It is used internally to determine the size of
 *   a continuity buffer, that avoids missing a match that spans more
 *   than one incoming data chunk arriving at the stream-editor filter.
 *   The default is 20.
 *
 *   Performance tips:
 *    - A high len: value on any rule can severely impact on performance,
 *      especially if mixed with short matches that match frequently.
 *    - Specify high-precedence rules (low prio: values) first in your
 *      configuration to avoid reshuffling edits while processing data.
 *
 *  Example: a trivial ruleset to escape HTML entities:
 *   [out] scope::/html-escape/ from::"&" to:"&amp;"
 *   [out] scope::/html-escape/ from::< to:&lt;
 *   [out] scope::/html-escape/ from::> to:&gt;
 *   [out] scope::/html-escape/ from::/"/ to:/&quot;/
 *   Note, the first & has to be quoted, as the two ampersands in the line
 *   would otherwise be mis-parsed as a matching pair of delimiters.
 *   Quoting the &amp;, and the " line with //, are optional (and quoting
 *   is not applicable to the scope: field).
 *   The double-colons delimit flags, of which none are used in this example.
 */
#define MAX_CONFIG_LINE 1024
#define MAX_RX_MATCH 10
#define WHITESPACE " \t\r\n"

#include <cstdint>

#include <vector>
#include <set>
#include <regex.h>
#include <cctype>
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
#include <stdexcept>
#include "ts/ts.h"

struct edit_t;
using editset_t = std::set<edit_t>;
using edit_p    = editset_t::const_iterator;

struct edit_t {
  const size_t start;
  const size_t bytes;
  const std::string repl;
  const int priority;
  edit_t(size_t s, size_t b, const std::string &r, int p) : start(s), bytes(b), repl(r), priority(p) { ; }
  bool
  operator!=(const edit_t &x) const
  {
    return start != x.start || bytes != x.bytes || repl != x.repl || priority != x.priority;
  }

  bool
  operator<(const edit_t &x) const
  {
    if ((start == x.start) || (start < x.start && start + bytes > x.start) || (x.start < start && x.start + x.bytes > start)) {
      /* conflicting edits.  Throw back to resolve conflict */
      /* Problem: we get called from erase() within conflict resolution,
       * and comparing to ourself then re-throws.
       * Need to exclude that case.
       */
      if (*this != x) {
        throw x;
      }
    }
    return start < x.start;
  }

  bool
  saveto(editset_t &edits) const
  {
    /* loop to try until inserted or we lose a conflict */
    for (;;) {
      try {
        edits.insert(*this);
        return true;
      } catch (const edit_t &conflicted) {
        TSDebug("stream-editor", "Conflicting edits [%ld-%ld] vs [%ld-%ld]", start, start + bytes, conflicted.start,
                conflicted.start + conflicted.bytes);
        if (priority < conflicted.priority) {
          /* we win conflict and oust our enemy */
          edits.erase(conflicted);
        } else {
          /* we lose the conflict - give up */
          return false;
        }
      }
    }
  }
};

class scope_t
{
  virtual bool match(const char *) const = 0;
  const bool uri;

public:
  bool
  in_scope(TSHttpTxn tx) const
  {
    /* Get the URL from tx, and feed it to match() */
    bool ret = false;
    TSMBuffer bufp;
    TSMLoc offset;
    int length;
    TSReturnCode rc = TSHttpTxnPristineUrlGet(tx, &bufp, &offset);
    if (rc != TS_SUCCESS) {
      TSError("Error getting URL of current Txn");
      return ret;
    }
    char *url = TSUrlStringGet(bufp, offset, &length);

    if (!strncasecmp(url, "https://", 8)) {
      /* No use trying to edit https data */
      ret = false;
    } else {
      char *p = url;
      if (uri) {
        /* match against path component, discard earlier components */
        if (!strncasecmp(url, "http://", 7)) {
          p += 7;
          while (*p != '/') {
            assert(*p != '\0');
            ++p;
          }
        }
      }
      ret = match(p);
    }
    TSfree(url);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, offset);
    // TSMBufferDestroy(bufp);
    return ret;
  }

  scope_t(const bool u) : uri(u) { ; }
  virtual ~scope_t() = default;
};

class rxscope : public scope_t
{
private:
  regex_t rx;
  bool

  match(const char *str) const override
  {
    return (regexec(&rx, str, 0, nullptr, 0) == 0) ? true : false;
  }

public:
  rxscope(const bool u, const bool i, const char *pattern, int len) : scope_t(u)
  {
    int flags = REG_NOSUB | REG_EXTENDED | (i ? REG_ICASE : 0);
    char *str = TSstrndup(pattern, len);
    int error = regcomp(&rx, str, flags);
    if (error) {
      TSError("stream-editor: can't compile regexp [%s]", str);
      TSfree(str);
      throw;
    }
    TSfree(str);
  }

  ~rxscope() override { regfree(&rx); }
};

class strscope : public scope_t
{
private:
  const bool icase;
  char *str;
  bool

  match(const char *p) const override
  {
    return ((icase ? strncasecmp : strncmp)(str, p, strlen(str)) == 0) ? true : false;
  }

public:
  strscope(const bool u, const bool i, const char *pattern, int len) : scope_t(u), icase(i) { str = TSstrndup(pattern, len); }
  ~strscope() override
  {
    if (str) {
      TSfree(str);
    }
  }
};

class match_t
{
public:
  virtual bool find(const char *, size_t, size_t &, size_t &, const char *, std::string &) const = 0;
  virtual size_t cont_size() const                                                               = 0;
  virtual ~match_t()                                                                             = default;
};

class strmatch : public match_t
{
  const bool icase;
  char *str;
  const size_t slen;

public:
  bool
  find(const char *buf, size_t len, size_t &found, size_t &found_len, const char *to, std::string &repl) const override
  {
    const char *match = icase ? strcasestr(buf, str) : strstr(buf, str);
    if (match) {
      found     = match - buf;
      found_len = slen;
      repl      = to;
      return (found + slen > len) ? false : true;
    } else {
      return false;
    }
  }

  strmatch(const bool i, const char *pattern, int len) : icase(i), slen(len) { str = TSstrndup(pattern, len); }
  ~strmatch() override
  {
    if (str) {
      TSfree(str);
    }
  }

  size_t
  cont_size() const override
  {
    return slen;
  }
};

class rxmatch : public match_t
{
  size_t match_len;
  regex_t rx;

public:
  bool
  find(const char *buf, size_t len, size_t &found, size_t &found_len, const char *tmpl, std::string &repl) const override
  {
    regmatch_t pmatch[MAX_RX_MATCH];
    if (regexec(&rx, buf, MAX_RX_MATCH, pmatch, REG_NOTEOL) == 0) {
      char c;
      int n;
      found     = pmatch[0].rm_so;
      found_len = pmatch[0].rm_eo - found;
      while (c = *tmpl++, c != '\0') {
        switch (c) {
        case '\\':
          if (*tmpl != '\0') {
            repl.push_back(*tmpl++);
          }
          break;
        case '$':
          if (isdigit(*tmpl)) {
            n = *tmpl - '0';
          } else {
            n = MAX_RX_MATCH;
          }
          if (n > 0 && n < MAX_RX_MATCH) {
            repl.append(buf + pmatch[n].rm_so, pmatch[n].rm_eo - pmatch[n].rm_so);
            tmpl++; /* we've consumed one more character */
          } else {
            repl.push_back(c);
          }
          break;
        default:
          repl.push_back(c);
          break;
        }
      }
      return true;
    } else {
      return false;
    }
  }

  size_t
  cont_size() const override
  {
    return match_len;
  }

  rxmatch(bool i, const char *pattern, size_t sz, size_t match_max) : match_len(match_max)
  {
    char *str = TSstrndup(pattern, sz);
    int flags = REG_EXTENDED | (i ? REG_ICASE : 0);
    int error = regcomp(&rx, str, flags);
    if (error) {
      TSError("stream-editor: can't compile regexp [%s]", str);
      TSfree(str);
      throw;
    }
    TSfree(str);
  }

  ~rxmatch() override { regfree(&rx); }
};

#define PARSE_VERIFY(line, x, str) \
  while (x)                        \
    if (!isspace(*(x - 1)))        \
      x = strcasestr(x + 1, str);  \
    else                           \
      break

class rule_t
{
private:
  scope_t *scope;
  unsigned int priority;
  match_t *from;
  char *to;
  int *refcount;

public:
  rule_t(const char *line) : scope(nullptr), priority(5), from(nullptr), to(nullptr), refcount(nullptr)
  {
    const char *scope_spec = strcasestr(line, "scope:");
    const char *from_spec  = strcasestr(line, "from:");
    const char *to_spec    = strcasestr(line, "to:");
    const char *prio_spec  = strcasestr(line, "prio:");
    const char *len_spec   = strcasestr(line, "len:");
    bool icase             = false;
    bool rx                = false;
    bool uri;
    size_t len, match_len;
    char delim;

    PARSE_VERIFY(line, scope_spec, "scope:");
    PARSE_VERIFY(line, from_spec, "from:");
    PARSE_VERIFY(line, to_spec, "to:");
    PARSE_VERIFY(line, prio_spec, "prio:");
    PARSE_VERIFY(line, len_spec, "len:");

    if (!from_spec || !to_spec) {
      throw "Incomplete stream edit spec";
    }

    if (len_spec) {
      match_len = 0;
      len_spec += 4;
      while (isdigit(*len_spec)) {
        match_len = 10 * match_len + (*len_spec++ - '0');
      }
    } else {
      match_len = 20; // default
    }

    /* parse From: now, as failure could abort constructor */
    for (from_spec += 5; *from_spec != ':'; ++from_spec) {
      switch (*from_spec) {
      case 'i':
        icase = true;
        break;
      case 'r':
        rx = true;
        break;
      }
    }
    delim = *++from_spec;
    if (isalnum(delim)) {
      len = strcspn(from_spec, WHITESPACE);
    } else {
      const char *end = strchr(++from_spec, delim);
      if (end) {
        len = end - from_spec;
      } else {
        /* it wasn't a delimiter after all */
        len = strcspn(--from_spec, WHITESPACE);
      }
    }
    if (rx) {
      from = new rxmatch(icase, from_spec, len, match_len);
    } else {
      from = new strmatch(icase, from_spec, len);
    }

    if (scope_spec) {
      icase = false;
      rx    = false;
      uri   = true;
      for (scope_spec += 6; *scope_spec != ':'; ++scope_spec) {
        switch (*scope_spec) {
        case 'i':
          icase = true;
          break;
        case 'r':
          rx = true;
          break;
        case 'u':
          uri = false;
          break;
        }
      }
      ++scope_spec;
      len = strcspn(scope_spec, WHITESPACE);
      if (rx) {
        scope = new rxscope(uri, icase, scope_spec, len);
      } else {
        scope = new strscope(uri, icase, scope_spec, len);
      }
    }

    if (prio_spec) {
      prio_spec += 5;
      if (isdigit(*prio_spec)) {
        priority = *prio_spec - '0';
      }
    }

    to_spec += 3;
    delim = *to_spec;
    if (isalnum(delim)) {
      len = strcspn(to_spec, WHITESPACE);
    } else {
      const char *end = strchr(++to_spec, delim);
      if (end) {
        len = end - to_spec;
      } else {
        /* it wasn't a delimiter after all */
        len = strcspn(--to_spec, WHITESPACE);
      }
    }
    to = TSstrndup(to_spec, len);

    refcount = new int(1);
  }

  rule_t(const rule_t &r) : scope(r.scope), priority(r.priority), from(r.from), to(r.to), refcount(r.refcount) { ++*refcount; }
  ~rule_t()
  {
    if (refcount) {
      if (!--*refcount) {
        if (scope) {
          delete scope;
        }
        if (from) {
          delete from;
        }
        if (to) {
          TSfree(to);
        }
        delete refcount;
      }
    }
  }

  bool
  in_scope(TSHttpTxn tx) const
  {
    /* if no scope was specified then everything is in-scope */
    return scope ? scope->in_scope(tx) : true;
  }

  size_t
  cont_size() const
  {
    return from->cont_size();
  }

  void
  apply(const char *buf, size_t len, editset_t &edits) const
  {
    /* find matches in the buf, and add match+replace to edits */

    size_t found;
    size_t found_len;
    size_t offs = 0;
    while (offs < len) {
      std::string repl;
      if (from->find(buf + offs, len - offs, found, found_len, to, repl)) {
        found += offs;
        edit_t(found, found_len, repl, priority).saveto(edits);
        offs = found + found_len;
      } else {
        break;
      }
    }
  }
};
using ruleset_t = std::vector<rule_t>;
using rule_p    = ruleset_t::const_iterator;

typedef struct contdata_t {
  TSCont cont             = nullptr;
  TSIOBuffer out_buf      = nullptr;
  TSIOBufferReader out_rd = nullptr;
  TSVIO out_vio           = nullptr;
  ruleset_t rules;
  std::string contbuf;
  size_t contbuf_sz = 0;
  int64_t bytes_in  = 0;
  int64_t bytes_out = 0;
  /* Use new/delete so destructor does cleanup for us */
  contdata_t() = default;
  ~contdata_t()
  {
    if (out_rd) {
      TSIOBufferReaderFree(out_rd);
    }
    if (out_buf) {
      TSIOBufferDestroy(out_buf);
    }
    if (cont) {
      TSContDestroy(cont);
    }
  }
  void
  set_cont_size(size_t sz)
  {
    if (contbuf_sz < 2 * sz) {
      contbuf_sz = 2 * sz - 1;
    }
  }
} contdata_t;

static int64_t
process_block(contdata_t *contdata, TSIOBufferReader reader)
{
  int64_t nbytes, start;
  size_t n = 0;
  size_t buflen;
  size_t keep;
  const char *buf;
  TSIOBufferBlock block;

  if (reader == nullptr) { // We're just flushing anything we have buffered
    keep   = 0;
    buf    = contdata->contbuf.c_str();
    buflen = contdata->contbuf.length();
    nbytes = 0;
  } else {
    block = TSIOBufferReaderStart(reader);
    buf   = TSIOBufferBlockReadStart(block, reader, &nbytes);

    if (contdata->contbuf.empty()) {
      /* Use the data as-is */
      buflen = nbytes;
    } else {
      contdata->contbuf.append(buf, nbytes);
      buf    = contdata->contbuf.c_str();
      buflen = contdata->contbuf.length();
    }
    keep = contdata->contbuf_sz;
  }
  size_t bytes_read = 0;

  editset_t edits;

  for (const auto &rule : contdata->rules) {
    rule.apply(buf, buflen, edits);
  }

  for (edit_p p = edits.begin(); p != edits.end(); ++p) {
    /* Preserve continuity buffer */
    if (p->start >= buflen - keep) {
      break;
    }

    /* pass through bytes before edit */
    start = p->start - bytes_read;

    while (start > 0) {
      // FIXME: would this be quicker if we managed a TSIOBuffer
      //        so we could use TSIOBufferCopy ?
      n = TSIOBufferWrite(contdata->out_buf, buf + bytes_read, start);
      assert(n > 0); // FIXME - handle error
      bytes_read += n;
      contdata->bytes_out += n;
      start -= n;
    }

    /* omit deleted bytes */
    bytes_read += p->bytes;

    /* insert replacement bytes */
    n = TSIOBufferWrite(contdata->out_buf, p->repl.c_str(), p->repl.length());
    assert(n == p->repl.length()); // FIXME (if this ever happens)!
    contdata->bytes_out += n;

    /* increment counts  - done */
  }
  contdata->bytes_in += bytes_read;

  /* data after the last edit */
  if (bytes_read < buflen - keep) {
    n = TSIOBufferWrite(contdata->out_buf, buf + bytes_read, buflen - bytes_read - keep);
    contdata->bytes_in += n;
    contdata->bytes_out += n;
    bytes_read += n;
  }
  /* reset buf to what we've not processed */
  contdata->contbuf = buf + bytes_read;

  return nbytes;
}
static void
streamedit_process(TSCont contp)
{
  // Read the data available to us
  // Concatenate with anything we have buffered
  // Loop over rules, and apply them to build our edit set
  // Loop over edits, and apply them to the stream
  // Retain buffered data at the end
  int64_t ntodo, nbytes;
  contdata_t *contdata      = static_cast<contdata_t *>(TSContDataGet(contp));
  TSVIO input_vio           = TSVConnWriteVIOGet(contp);
  TSIOBufferReader input_rd = TSVIOReaderGet(input_vio);

  if (contdata->out_buf == nullptr) {
    contdata->out_buf = TSIOBufferCreate();
    contdata->out_rd  = TSIOBufferReaderAlloc(contdata->out_buf);
    contdata->out_vio = TSVConnWrite(TSTransformOutputVConnGet(contp), contp, contdata->out_rd, INT64_MAX);
  }

  TSIOBuffer in_buf = TSVIOBufferGet(input_vio);
  /* Test for EOS */
  if (in_buf == nullptr) {
    process_block(contdata, nullptr); // flush any buffered data
    TSVIONBytesSet(contdata->out_vio, contdata->bytes_out);
    TSVIOReenable(contdata->out_vio);
    return;
  }

  /* Test for EOS */
  ntodo = TSVIONTodoGet(input_vio);
  if (ntodo == 0) {
    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation.
     */
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
    TSVIOReenable(contdata->out_vio);
    return;
  }

  /* now parse & process buffered data.  We can set some aside
   * as a continuity buffer to deal with the problem of matches
   * that span input chunks.
   */
  while (ntodo = TSIOBufferReaderAvail(input_rd), ntodo > 0) {
    nbytes = process_block(contdata, input_rd);
    TSIOBufferReaderConsume(input_rd, nbytes);
    TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + nbytes);
  }

  ntodo = TSVIONTodoGet(input_vio);
  if (ntodo == 0) {
    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation.
     */
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  } else {
    /* Call back the input VIO continuation to let it know that we
     * are ready for more data.
     */
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
  }
  TSVIOReenable(contdata->out_vio);
}
static int
streamedit_filter(TSCont contp, TSEvent event, void *edata)
{
  /* Our main function that does the work.
   * Called as a continuation for filtering.
   * *** if necessary, add call at TXN_CLOSE for cleanup.
   */
  TSVIO input_vio;

  if (TSVConnClosedGet(contp)) {
    contdata_t *contdata = static_cast<contdata_t *>(TSContDataGet(contp));
    delete contdata;
    return TS_SUCCESS;
  }

  switch (event) {
  case TS_EVENT_ERROR:
    input_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;
  default:
    streamedit_process(contp);
    break;
  }
  return TS_SUCCESS;
}

static int
streamedit_setup(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txn        = static_cast<TSHttpTxn>(edata);
  ruleset_t *rules_in  = static_cast<ruleset_t *>(TSContDataGet(contp));
  contdata_t *contdata = nullptr;

  assert((event == TS_EVENT_HTTP_READ_RESPONSE_HDR) || (event == TS_EVENT_HTTP_READ_REQUEST_HDR));

  /* make a new list comprising those rules that are in scope */
  for (const auto &r : *rules_in) {
    if (r.in_scope(txn)) {
      if (contdata == nullptr) {
        contdata = new contdata_t();
      }
      contdata->rules.push_back(r);
      contdata->set_cont_size(r.cont_size());
    }
  }

  if (contdata == nullptr) {
    /* Nothing to do */
    return TS_SUCCESS;
  }

  /* we have a job to do, so insert filter */
  contdata->cont = TSTransformCreate(streamedit_filter, txn);
  TSContDataSet(contdata->cont, contdata);

  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSHttpTxnHookAdd(txn, TS_HTTP_REQUEST_TRANSFORM_HOOK, contdata->cont);
  } else {
    TSHttpTxnHookAdd(txn, TS_HTTP_RESPONSE_TRANSFORM_HOOK, contdata->cont);
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);

  return TS_SUCCESS;
}

static void
read_conf(const char *filename, ruleset_t *&in, ruleset_t *&out)
{
  char buf[MAX_CONFIG_LINE];
  FILE *file = fopen(filename, "r");

  if (file == nullptr) {
    TSError("[stream-editor] Failed to open %s", filename);
    return;
  }
  while (fgets(buf, MAX_CONFIG_LINE, file) != nullptr) {
    try {
      if (!strncasecmp(buf, "[in]", 4)) {
        if (in == nullptr) {
          in = new ruleset_t();
        }
        in->push_back(rule_t(buf));
      } else if (!strncasecmp(buf, "[out]", 5)) {
        if (out == nullptr) {
          out = new ruleset_t();
        }
        out->push_back(rule_t(buf));
      }
    } catch (...) {
      TSError("stream-editor: failed to parse rule %s", buf);
    }
  }
  fclose(file);
}

extern "C" void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont inputcont, outputcont;
  ruleset_t *rewrites_in  = nullptr;
  ruleset_t *rewrites_out = nullptr;

  info.plugin_name   = (char *)"stream-editor";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"users@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[stream-editor] Plugin registration failed");
    return;
  }

  /* Allow different config files */
  while (--argc) {
    read_conf(*++argv, rewrites_in, rewrites_out);
  }

  if (rewrites_in != nullptr) {
    TSDebug("[stream-editor]", "initializing input filtering");
    inputcont = TSContCreate(streamedit_setup, nullptr);
    if (inputcont == nullptr) {
      TSError("[stream-editor] failed to initialize input filtering!");
    } else {
      TSContDataSet(inputcont, rewrites_in);
      TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, inputcont);
    }
  } else {
    TSDebug("[stream-editor]", "no input filter rules, skipping filter");
  }

  if (rewrites_out != nullptr) {
    TSDebug("[stream-editor]", "initializing output filtering");
    outputcont = TSContCreate(streamedit_setup, nullptr);
    if (outputcont == nullptr) {
      TSError("[stream-editor] failed to initialize output filtering!");
    } else {
      TSContDataSet(outputcont, rewrites_out);
      TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, outputcont);
    }
  } else {
    TSDebug("[stream-editor]", "no output filter rules, skipping filter");
  }
}
