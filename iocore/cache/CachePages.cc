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

#include "P_Cache.h"

#include "Show.h"
#include "I_Tasks.h"
#include "CacheControl.h"

struct ShowCache : public ShowCont {
  enum scan_type {
    scan_type_lookup,
    scan_type_delete,
    scan_type_invalidate,
  };

  int vol_index;
  int seg_index;
  scan_type scan_flag;
  int urlstrs_index;
  int linecount;
  char (*show_cache_urlstrs)[500];
  URL url;
  CacheKey show_cache_key;
  CacheVC *cache_vc;
  MIOBuffer *buffer;
  IOBufferReader *buffer_reader;
  int64_t content_length;
  VIO *cvio;
  int showMain(int event, Event *e);
  int lookup_url_form(int event, Event *e);
  int delete_url_form(int event, Event *e);
  int lookup_regex_form(int event, Event *e);
  int delete_regex_form(int event, Event *e);
  int invalidate_regex_form(int event, Event *e);

  int lookup_url(int event, Event *e);
  int delete_url(int event, Event *e);
  int lookup_regex(int event, Event *e);
  int delete_regex(int event, Event *e);
  int invalidate_regex(int event, Event *e);

  int handleCacheEvent(int event, Event *e);
  int handleCacheDeleteComplete(int event, Event *e);
  int handleCacheScanCallback(int event, Event *e);

  ShowCache(Continuation *c, HTTPHdr *h)
    : ShowCont(c, h),
      vol_index(0),
      seg_index(0),
      scan_flag(scan_type_lookup),
      cache_vc(nullptr),
      buffer(nullptr),
      buffer_reader(nullptr),
      content_length(0),
      cvio(nullptr)
  {
    urlstrs_index = 0;
    linecount     = 0;
    int query_len;
    char query[4096];
    char unescapedQuery[sizeof(query)];
    show_cache_urlstrs = nullptr;
    URL *u             = h->url_get();

    // process the query string
    if (u->query_get(&query_len) && query_len < (int)sizeof(query)) {
      strncpy(query, u->query_get(&query_len), query_len);
      strncpy(unescapedQuery, u->query_get(&query_len), query_len);

      query[query_len] = unescapedQuery[query_len] = '\0';

      query_len = unescapifyStr(query);

      Debug("cache_inspector", "query params: '%s' len %d [unescaped]", unescapedQuery, query_len);
      Debug("cache_inspector", "query params: '%s' len %d [escaped]", query, query_len);

      // remove 'C-m' s
      unsigned l, m;
      for (l = 0, m = 0; l < (unsigned)query_len; l++) {
        if (query[l] != '\015') {
          query[m++] = query[l];
        }
      }
      query[m] = '\0';

      unsigned nstrings = 1;
      char *p           = strstr(query, "url=");
      // count the no of urls
      if (p) {
        while ((p = strstr(p, "\n"))) {
          nstrings++;
          if ((size_t)(p - query) >= strlen(query) - 1) {
            break;
          } else {
            p++;
          }
        }
      }
      // initialize url array
      show_cache_urlstrs = new char[nstrings + 1][500];
      memset(show_cache_urlstrs, '\0', (nstrings + 1) * 500 * sizeof(char));

      char *q, *t;
      p = strstr(unescapedQuery, "url=");
      if (p) {
        p += 4; // 4 ==> strlen("url=")
        t = strchr(p, '&');
        if (!t) {
          t = (char *)unescapedQuery + strlen(unescapedQuery);
        }
        for (int s = 0; p < t; s++) {
          show_cache_urlstrs[s][0] = '\0';
          q                        = strstr(p, "%0D%0A" /* \r\n */); // we used this in the JS to separate urls
          if (!q) {
            q = t;
          }
          ink_strlcpy(show_cache_urlstrs[s], p, q - p + 1);
          p = q + 6; // +6 ==> strlen(%0D%0A)
        }
      }

      Debug("cache_inspector", "there were %d url(s) passed in", nstrings == 1 ? 1 : nstrings - 1);

      for (unsigned i = 0; i < nstrings; i++) {
        if (show_cache_urlstrs[i][0] == '\0') {
          continue;
        }
        Debug("cache_inspector", "URL %d: '%s'", i + 1, show_cache_urlstrs[i]);
        unescapifyStr(show_cache_urlstrs[i]);
        Debug("cache_inspector", "URL %d: '%s'", i + 1, show_cache_urlstrs[i]);
      }
    }

    SET_HANDLER(&ShowCache::showMain);
  }

  ~ShowCache() override
  {
    if (show_cache_urlstrs) {
      delete[] show_cache_urlstrs;
    }
    url.destroy();
  }
};

#define STREQ_PREFIX(_x, _s) (!strncasecmp(_x, _s, sizeof(_s) - 1))
#define STREQ_LEN_PREFIX(_x, _l, _s) (path_len < sizeof(_s) && !strncasecmp(_x, _s, sizeof(_s) - 1))

Action *
register_ShowCache(Continuation *c, HTTPHdr *h)
{
  ShowCache *theshowcache = new ShowCache(c, h);
  URL *u                  = h->url_get();
  int path_len;
  const char *path = u->path_get(&path_len);

  if (!path) {
  } else if (STREQ_PREFIX(path, "lookup_url_form")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::lookup_url_form);
  } else if (STREQ_PREFIX(path, "delete_url_form")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::delete_url_form);
  } else if (STREQ_PREFIX(path, "lookup_regex_form")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::lookup_regex_form);
  } else if (STREQ_PREFIX(path, "delete_regex_form")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::delete_regex_form);
  } else if (STREQ_PREFIX(path, "invalidate_regex_form")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::invalidate_regex_form);
  }

  else if (STREQ_PREFIX(path, "lookup_url")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::lookup_url);
  } else if (STREQ_PREFIX(path, "delete_url")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::delete_url);
  } else if (STREQ_PREFIX(path, "lookup_regex")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::lookup_regex);
  } else if (STREQ_PREFIX(path, "delete_regex")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::delete_regex);
  } else if (STREQ_PREFIX(path, "invalidate_regex")) {
    SET_CONTINUATION_HANDLER(theshowcache, &ShowCache::invalidate_regex);
  }

  if (theshowcache->mutex->thread_holding) {
    CONT_SCHED_LOCK_RETRY(theshowcache);
  } else {
    eventProcessor.schedule_imm(theshowcache, ET_TASK);
  }

  return &theshowcache->action;
}

int
ShowCache::showMain(int event, Event *e)
{
  CHECK_SHOW(begin("Cache"));
  CHECK_SHOW(show("<H3><A HREF=\"./lookup_url_form\">Lookup url</A></H3>\n"
                  "<H3><A HREF=\"./delete_url_form\">Delete url</A></H3>\n"
                  "<H3><A HREF=\"./lookup_regex_form\">Regex lookup</A></H3>\n"
                  "<H3><A HREF=\"./delete_regex_form\">Regex delete</A></H3>\n"
                  "<H3><A HREF=\"./invalidate_regex_form\">Regex invalidate</A></H3>\n\n"));
  return complete(event, e);
}

int
ShowCache::lookup_url_form(int event, Event *e)
{
  CHECK_SHOW(begin("Cache Lookup"));
  CHECK_SHOW(show("<FORM METHOD=\"GET\" ACTION=\"./lookup_url\">\n"
                  "<H3>Lookup</H3>\n"
                  "<INPUT TYPE=\"TEXT\" NAME=\"url\" value=\"http://\">\n"
                  "<INPUT TYPE=\"SUBMIT\" value=\"Lookup\">\n"
                  "</FORM>\n\n"));
  return complete(event, e);
}

int
ShowCache::delete_url_form(int event, Event *e)
{
  CHECK_SHOW(begin("Cache Delete"));
  CHECK_SHOW(show("<FORM METHOD=\"GET\" ACTION=\"./delete_url\">\n"
                  "<P><B>Type the list urls that you want to delete\n"
                  "in the box below. The urls MUST be separated by\n"
                  "new lines</B></P>\n\n"
                  "<TEXTAREA NAME=\"url\" rows=10 cols=50>"
                  "http://"
                  "</TEXTAREA>\n"
                  "<INPUT TYPE=\"SUBMIT\" value=\"Delete\">\n"
                  "</FORM>\n\n"));
  return complete(event, e);
}

int
ShowCache::lookup_regex_form(int event, Event *e)
{
  CHECK_SHOW(begin("Cache Regex Lookup"));
  CHECK_SHOW(show("<FORM METHOD=\"GET\" ACTION=\"./lookup_regex\">\n"
                  "<P><B>Type the list of regular expressions that you want to lookup\n"
                  "in the box below. The regular expressions MUST be separated by\n"
                  "new lines</B></P>\n\n"
                  "<TEXTAREA NAME=\"url\" rows=10 cols=50>"
                  "http://"
                  "</TEXTAREA>\n"
                  "<INPUT TYPE=\"SUBMIT\" value=\"Lookup\">\n"
                  "</FORM>\n\n"));
  return complete(event, e);
}

int
ShowCache::delete_regex_form(int event, Event *e)
{
  CHECK_SHOW(begin("Cache Regex delete"));
  CHECK_SHOW(show("<FORM METHOD=\"GET\" ACTION=\"./delete_regex\">\n"
                  "<P><B>Type the list of regular expressions that you want to delete\n"
                  "in the box below. The regular expressions MUST be separated by\n"
                  "new lines</B></P>\n\n"
                  "<TEXTAREA NAME=\"url\" rows=10 cols=50>"
                  "http://"
                  "</TEXTAREA>\n"
                  "<INPUT TYPE=\"SUBMIT\" value=\"Delete\">\n"
                  "</FORM>\n\n"));
  return complete(event, e);
}

int
ShowCache::invalidate_regex_form(int event, Event *e)
{
  CHECK_SHOW(begin("Cache Regex Invalidate"));
  CHECK_SHOW(show("<FORM METHOD=\"GET\" ACTION=\"./invalidate_regex\">\n"
                  "<P><B>Type the list of regular expressions that you want to invalidate\n"
                  "in the box below. The regular expressions MUST be separated by\n"
                  "new lines</B></P>\n\n"
                  "<TEXTAREA NAME=\"url\" rows=10 cols=50>"
                  "http://"
                  "</TEXTAREA>\n"
                  "<INPUT TYPE=\"SUBMIT\" value=\"Invalidate\">\n"
                  "</FORM>\n"));
  return complete(event, e);
}

int
ShowCache::handleCacheEvent(int event, Event *e)
{
  // we use VC_EVENT_xxx to finish the cluster read in cluster mode
  switch (event) {
  case VC_EVENT_EOS:
  case VC_EVENT_READ_COMPLETE: {
    // cluster read done, we just print hit in cluster
    CHECK_SHOW(show("<P><TABLE border=1 width=100%%>"));
    CHECK_SHOW(show("<TR><TH bgcolor=\"#FFF0E0\" colspan=2>Doc Hit from Cluster</TH></TR>\n"));
    CHECK_SHOW(show("<tr><td>Size</td><td>%" PRId64 "</td>\n", content_length));

    // delete button
    CHECK_SHOW(show("<tr><td>Action</td>\n"
                    "<td><FORM action=\"./delete_url\" method=get>\n"
                    "<Input type=HIDDEN name=url value=\"%s\">\n"
                    "<input type=submit value=\"Delete URL\">\n"
                    "</FORM></td></tr>\n",
                    show_cache_urlstrs[0]));
    CHECK_SHOW(show("</TABLE></P>"));

    if (buffer_reader) {
      buffer->dealloc_reader(buffer_reader);
      buffer_reader = nullptr;
    }
    if (buffer) {
      free_MIOBuffer(buffer);
      buffer = nullptr;
    }
    cvio = nullptr;
    cache_vc->do_io_close(-1);
    cache_vc = nullptr;
    return complete(event, e);
  }
  case CACHE_EVENT_OPEN_READ: {
    // get the vector
    cache_vc                 = (CacheVC *)e;
    CacheHTTPInfoVector *vec = &(cache_vc->vector);
    int alt_count            = vec->count();
    if (alt_count) {
      // check cache_vc->first_buf is NULL, response cache lookup busy.
      if (cache_vc->first_buf == nullptr) {
        cache_vc->do_io_close(-1);
        CHECK_SHOW(show("<H3>Cache Lookup Busy, please try again</H3>\n"));
        return complete(event, e);
      }

      Doc *d = (Doc *)(cache_vc->first_buf->data());
      time_t t;
      char tmpstr[4096];

      // print the Doc
      CHECK_SHOW(show("<P><TABLE border=1 width=100%%>"));
      CHECK_SHOW(show("<TR><TH bgcolor=\"#FFF0E0\" colspan=2>Doc</TH></TR>\n"));
      CHECK_SHOW(
        show("<TR><TD>Volume</td> <td>#%d - store='%s'</td></tr>\n", cache_vc->vol->cache_vol->vol_number, cache_vc->vol->path));
      CHECK_SHOW(show("<TR><TD>first key</td> <td>%s</td></tr>\n", d->first_key.toHexStr(tmpstr)));
      CHECK_SHOW(show("<TR><TD>key</td> <td>%s</td></tr>\n", d->key.toHexStr(tmpstr)));
      CHECK_SHOW(show("<tr><td>sync_serial</td><td>%lu</tr>\n", d->sync_serial));
      CHECK_SHOW(show("<tr><td>write_serial</td><td>%lu</tr>\n", d->write_serial));
      CHECK_SHOW(show("<tr><td>header length</td><td>%lu</tr>\n", d->hlen));
      CHECK_SHOW(show("<tr><td>fragment type</td><td>%lu</tr>\n", d->doc_type));
      CHECK_SHOW(show("<tr><td>No of Alternates</td><td>%d</td></tr>\n", alt_count));

      CHECK_SHOW(show("<tr><td>Action</td>\n"
                      "<td><FORM action=\"./delete_url\" method=get>\n"
                      "<Input type=HIDDEN name=url value=\"%s\">\n"
                      "<input type=submit value=\"Delete URL\">\n"
                      "</FORM></td></tr>\n",
                      show_cache_urlstrs[0]));
      CHECK_SHOW(show("</TABLE></P>"));

      for (int i = 0; i < alt_count; i++) {
        // unmarshal the alternate??
        CHECK_SHOW(show("<p><table border=1>\n"));
        CHECK_SHOW(show("<tr><th bgcolor=\"#FFF0E0\" colspan=2>Alternate %d</th></tr>\n", i + 1));
        CacheHTTPInfo *obj       = vec->get(i);
        CacheKey obj_key         = obj->object_key_get();
        HTTPHdr *cached_request  = obj->request_get();
        HTTPHdr *cached_response = obj->response_get();
        int64_t obj_size         = obj->object_size_get();
        int offset, tmp, used, done;
        char b[4096];

        // print request header
        CHECK_SHOW(show("<tr><td>Request Header</td><td><PRE>"));
        offset = 0;
        do {
          used = 0;
          tmp  = offset;
          done = cached_request->print(b, 4095, &used, &tmp);
          offset += used;
          b[used] = '\0';
          CHECK_SHOW(show("%s", b));
        } while (!done);
        CHECK_SHOW(show("</PRE></td><tr>\n"));

        // print response header
        CHECK_SHOW(show("<tr><td>Response Header</td><td><PRE>"));
        offset = 0;
        do {
          used = 0;
          tmp  = offset;
          done = cached_response->print(b, 4095, &used, &tmp);
          offset += used;
          b[used] = '\0';
          CHECK_SHOW(show("%s", b));
        } while (!done);
        CHECK_SHOW(show("</PRE></td></tr>\n"));
        CHECK_SHOW(show("<tr><td>Size</td><td>%" PRId64 "</td>\n", obj_size));
        CHECK_SHOW(show("<tr><td>Key</td><td>%s</td>\n", obj_key.toHexStr(tmpstr)));
        t = obj->request_sent_time_get();
        ink_ctime_r(&t, tmpstr);
        CHECK_SHOW(show("<tr><td>Request sent time</td><td>%s</td></tr>\n", tmpstr));
        t = obj->response_received_time_get();
        ink_ctime_r(&t, tmpstr);

        CHECK_SHOW(show("<tr><td>Response received time</td><td>%s</td></tr>\n", tmpstr));
        CHECK_SHOW(show("</TABLE></P>"));
      }

      cache_vc->do_io_close(-1);
      return complete(event, e);
    }
    // open success but no vector, that is the Cluster open read, pass through
  }
    // fallthrough

  case VC_EVENT_READ_READY:
    if (!cvio) {
      buffer         = new_empty_MIOBuffer();
      buffer_reader  = buffer->alloc_reader();
      content_length = cache_vc->get_object_size();
      cvio           = cache_vc->do_io_read(this, content_length, buffer);
    } else {
      buffer_reader->consume(buffer_reader->read_avail());
    }
    return EVENT_DONE;
  case CACHE_EVENT_OPEN_READ_FAILED:
    // something strange happen, or cache miss in cluster mode.
    CHECK_SHOW(show("<H3>Cache Lookup Failed, or missing in cluster</H3>\n"));
    return complete(event, e);
  default:
    CHECK_SHOW(show("<H3>Cache Miss</H3>\n"));
    return complete(event, e);
  }
}

int
ShowCache::lookup_url(int event, Event *e)
{
  char header_str[300];
  HttpCacheKey key;
  cache_generation_t generation = -1;

  snprintf(header_str, sizeof(header_str), "<font color=red>%s</font>", show_cache_urlstrs[0]);
  CHECK_SHOW(begin(header_str));
  url.create(nullptr);
  const char *s;
  s = show_cache_urlstrs[0];
  url.parse(&s, s + strlen(s));

  RecGetRecordInt("proxy.config.http.cache.generation", &generation);
  Cache::generate_key(&key, &url, generation);

  SET_HANDLER(&ShowCache::handleCacheEvent);
  Action *lookup_result = cacheProcessor.open_read(this, &key.hash, CACHE_FRAG_TYPE_HTTP, key.hostname, key.hostlen);
  if (!lookup_result) {
    lookup_result = ACTION_IO_ERROR;
  }
  if (lookup_result == ACTION_RESULT_DONE) {
    return EVENT_DONE; // callback complete
  } else if (lookup_result == ACTION_IO_ERROR) {
    handleEvent(CACHE_EVENT_OPEN_READ_FAILED, nullptr);
    return EVENT_DONE; // callback complete
  } else {
    return EVENT_CONT; // callback pending, will be a cluster read.
  }
}

int
ShowCache::delete_url(int event, Event *e)
{
  if (urlstrs_index == 0) {
    // print the header the first time delete_url is called
    CHECK_SHOW(begin("Delete URL"));
    CHECK_SHOW(show("<B><TABLE border=1>\n"));
  }

  if (strcmp(show_cache_urlstrs[urlstrs_index], "") == 0) {
    // close the page when you reach the end of the
    // url list
    CHECK_SHOW(show("</TABLE></B>\n"));
    return complete(event, e);
  }
  url.create(nullptr);
  const char *s;
  s = show_cache_urlstrs[urlstrs_index];
  CHECK_SHOW(show("<TR><TD>%s</TD>", s));
  url.parse(&s, s + strlen(s));
  SET_HANDLER(&ShowCache::handleCacheDeleteComplete);
  // increment the index so that the next time
  // delete_url is called you delete the next url
  urlstrs_index++;

  HttpCacheKey key;
  Cache::generate_key(&key, &url); // XXX choose a cache generation number ...

  cacheProcessor.remove(this, &key, CACHE_FRAG_TYPE_HTTP);
  return EVENT_DONE;
}

int
ShowCache::handleCacheDeleteComplete(int event, Event *e)
{
  if (event == CACHE_EVENT_REMOVE) {
    CHECK_SHOW(show("<td>Delete <font color=green>succeeded</font></td></tr>\n"));
  } else {
    CHECK_SHOW(show("<td>Delete <font color=red>failed</font></td></tr>\n"));
  }
  return delete_url(event, e);
}

int
ShowCache::lookup_regex(int event, Event *e)
{
  CHECK_SHOW(begin("Regex Lookup"));
  CHECK_SHOW(show("<SCRIPT LANGIAGE=\"Javascript1.2\">\n"
                  "urllist = new Array(100);\n"
                  "index = 0;\n"
                  "function addToUrlList(input) {\n"
                  "	for (c=0; c < index; c++) {\n"
                  "		if (urllist[c] == encodeURIComponent(input.name)) {\n"
                  "			urllist.splice(c,1);\n"
                  "			index--;\n"
                  "			return true;\n"
                  "		}\n"
                  "	}\n"
                  "	urllist[index++] = encodeURIComponent(input.name);\n"
                  "	return true;\n"
                  "}\n"
                  "function setUrls(form) {\n"
                  "	form.elements[0].value=\"\";\n"
                  "   if (index > 10) {\n"
                  "           alert(\"Can't choose more than 10 urls for deleting\");\n"
                  "           return true;\n"
                  "}\n"
                  "	for (c=0; c < index; c++){\n"
                  "		form.elements[0].value += urllist[c]+ \"%%0D%%0A\";\n"
                  "	}\n"
                  "   if (form.elements[0].value == \"\"){\n"
                  "	    alert(\"Please select at least one url before clicking delete\");\n"
                  "       return true;\n"
                  "}\n"
                  "   srcfile=\"./delete_url?url=\" + form.elements[0].value;\n"
                  "   document.location=srcfile;\n "
                  "	return true;\n"
                  "}\n"
                  "</SCRIPT>\n"));

  CHECK_SHOW(show("<FORM NAME=\"f\" ACTION=\"./delete_url\" METHOD=GET> \n"
                  "<INPUT TYPE=HIDDEN NAME=\"url\">\n"
                  "<B><TABLE border=1>\n"));

  scan_flag = scan_type_lookup; // lookup
  SET_HANDLER(&ShowCache::handleCacheScanCallback);
  cacheProcessor.scan(this);
  return EVENT_DONE;
}

int
ShowCache::delete_regex(int event, Event *e)
{
  CHECK_SHOW(begin("Regex Delete"));
  CHECK_SHOW(show("<B><TABLE border=1>\n"));
  scan_flag = scan_type_delete; // delete
  SET_HANDLER(&ShowCache::handleCacheScanCallback);
  cacheProcessor.scan(this);
  return EVENT_DONE;
}

int
ShowCache::invalidate_regex(int event, Event *e)
{
  CHECK_SHOW(begin("Regex Invalidate"));
  CHECK_SHOW(show("<B><TABLE border=1>\n"));
  scan_flag = scan_type_invalidate; // invalidate
  SET_HANDLER(&ShowCache::handleCacheScanCallback);
  cacheProcessor.scan(this);
  return EVENT_DONE;
}

int
ShowCache::handleCacheScanCallback(int event, Event *e)
{
  switch (event) {
  case CACHE_EVENT_SCAN: {
    cache_vc = (CacheVC *)e;
    return EVENT_CONT;
  }
  case CACHE_EVENT_SCAN_OBJECT: {
    HTTPInfo *alt = (HTTPInfo *)e;
    char xx[501], m[501];
    int ib = 0, xd = 0, ml = 0;

    alt->request_get()->url_print(xx, 500, &ib, &xd);
    xx[ib] = '\0';

    const char *mm = alt->request_get()->method_get(&ml);

    memcpy(m, mm, ml);
    m[ml] = 0;

    int res = CACHE_SCAN_RESULT_CONTINUE;

    for (unsigned s = 0; show_cache_urlstrs[s][0] != '\0'; s++) {
      const char *error;
      int erroffset;
      pcre *preq = pcre_compile(show_cache_urlstrs[s], 0, &error, &erroffset, nullptr);

      Debug("cache_inspector", "matching url '%s' '%s' with regex '%s'", m, xx, show_cache_urlstrs[s]);

      if (preq) {
        int r = pcre_exec(preq, nullptr, xx, ib, 0, 0, nullptr, 0);

        pcre_free(preq);
        if (r != -1) {
          linecount++;
          if ((linecount % 5) == 0) {
            CHECK_SHOW(show("<TR bgcolor=\"#FFF0E0\">"));
          } else {
            CHECK_SHOW(show("<TR>"));
          }

          switch (scan_flag) {
          case scan_type_lookup:
            /*Y! Bug: 2249781: using onClick() because i need encodeURIComponent() and YTS doesn't have something like that */
            CHECK_SHOW(show("<TD><INPUT TYPE=CHECKBOX NAME=\"%s\" "
                            "onClick=\"addToUrlList(this)\"></TD>"
                            "<TD><A onClick='window.location.href=\"./lookup_url?url=\"+ encodeURIComponent(\"%s\");' HREF=\"#\">"
                            "<B>%s</B></A></br></TD></TR>\n",
                            xx, xx, xx));
            break;
          case scan_type_delete:
            CHECK_SHOW(show("<TD><B>%s</B></TD>"
                            "<TD><font color=red>deleted</font></TD></TR>\n",
                            xx));
            res = CACHE_SCAN_RESULT_DELETE;
            break;
          case scan_type_invalidate:
            HTTPInfo new_info;
            res = CACHE_SCAN_RESULT_UPDATE;
            new_info.copy(alt);
            new_info.response_get()->set_cooked_cc_need_revalidate_once();
            CHECK_SHOW(show("<TD><B>%s</B></TD>"
                            "<TD><font color=red>Invalidate</font></TD>"
                            "</TR>\n",
                            xx));
            cache_vc->set_http_info(&new_info);
          }

          break;
        }
      } else {
        // TODO: Regex didn't compile, show errors ?
        Debug("cache_inspector", "regex '%s' didn't compile", show_cache_urlstrs[s]);
      }
    }
    return res;
  }
  case CACHE_EVENT_SCAN_DONE:
    CHECK_SHOW(show("</TABLE></B>\n"));
    if (scan_flag == 0) {
      if (linecount) {
        CHECK_SHOW(show("<P><INPUT TYPE=button value=\"Delete\" "
                        "onClick=\"setUrls(window.document.f)\"></P>"
                        "</FORM>\n"));
      }
    }
    CHECK_SHOW(show("<H3>Done</H3>\n"));
    Debug("cache_inspector", "scan done");
    complete(event, e);
    return EVENT_DONE;
  case CACHE_EVENT_SCAN_FAILED:
  default:
    CHECK_SHOW(show("<H3>Error while scanning disk</H3>\n"));
    return EVENT_DONE;
  }
}
