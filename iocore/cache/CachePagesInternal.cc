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

#ifdef NON_MODULAR

struct ShowCacheInternal:ShowCont
{
  int part_index;
  int seg_index;
  CacheKey show_cache_key;
  CacheVC *cache_vc;


  int showMain(int event, Event * e);
  int showEvacuations(int event, Event * e);
  int showPartEvacuations(int event, Event * e);
  int showPartitions(int event, Event * e);
  int showPartPartitions(int event, Event * e);
  int showSegments(int event, Event * e);
  int showSegSegment(int event, Event * e);
#ifdef CACHE_STAT_PAGES
  int showConnections(int event, Event * e);
  int showPartConnections(int event, Event * e);
#endif

    ShowCacheInternal(Continuation * c, HTTPHdr * h):ShowCont(c, h), part_index(0), seg_index(0)
  {
    SET_HANDLER(&ShowCacheInternal::showMain);
  };

  ~ShowCacheInternal() {
  }

};
extern ShowCacheInternal *theshowcacheInternal;
Action *register_ShowCacheInternal(Continuation * c, HTTPHdr * h);




extern Part **gpart;
extern volatile int gnpart;


// Stat Pages
ShowCacheInternal *theshowcacheInternal = NULL;


#define STREQ_PREFIX(_x,_s) (!strncasecmp(_x,_s,sizeof(_s)-1))
#define STREQ_LEN_PREFIX(_x,_l,_s) (path_len < sizeof(_s) && !strncasecmp(_x,_s,sizeof(_s)-1))


Action *
register_ShowCacheInternal(Continuation * c, HTTPHdr * h)
{
  theshowcacheInternal = NEW(new ShowCacheInternal(c, h));
  URL *u = h->url_get();

  int path_len;
  const char *path = u->path_get(&path_len);

  if (!path) {
  }
#ifdef CACHE_STAT_PAGES
  else if (STREQ_LEN_PREFIX(path, path_len, "connections")) {
    SET_CONTINUATION_HANDLER(theshowcacheInternal, &ShowCacheInternal::showConnections);
  }
#endif
  else if (STREQ_PREFIX(path, "evacuations")) {
    SET_CONTINUATION_HANDLER(theshowcacheInternal, &ShowCacheInternal::showEvacuations);
  } else if (STREQ_PREFIX(path, "partitions")) {
    SET_CONTINUATION_HANDLER(theshowcacheInternal, &ShowCacheInternal::showPartitions);
  }

  if (theshowcacheInternal->mutex->thread_holding)
    CONT_SCHED_LOCK_RETRY(theshowcacheInternal);
  else
    eventProcessor.schedule_imm(theshowcacheInternal, ET_NET);
  return &theshowcacheInternal->action;
}


int
ShowCacheInternal::showMain(int event, Event * e)
{
  CHECK_SHOW(begin("Cache"));
#ifdef CACHE_STAT_PAGES
  CHECK_SHOW(show("<H3>Show <A HREF=\"/connections\">Connections</A></H3>\n"
                  "<H3>Show <A HREF=\"/evacuations\">Evacuations</A></H3>\n"
                  "<H3>Show <A HREF=\"/partitions\">Partitions</A></H3>\n"));
#else
  CHECK_SHOW(show("<H3>Show <A HREF=\"/evacuations\">Evacuations</A></H3>\n"
                  "<H3>Show <A HREF=\"/partitions\">Partitions</A></H3>\n"));
#endif
  return complete(event, e);
}

#ifdef CACHE_STAT_PAGES
int
ShowCacheInternal::showConnections(int event, Event * e)
{
  CHECK_SHOW(begin("Cache VConnections"));
  CHECK_SHOW(show("<H3>Cache Connections</H3>\n"
                  "<table border=1><tr>"
                  "<th>Operation</th>"
                  "<th>Partition</th>"
                  "<th>URL/Hash</th>" "<th>Bytes Done</th>" "<th>Total Bytes</th>" "<th>Bytes Todo</th>" "</tr>\n"));

  SET_HANDLER(&ShowCacheInternal::showPartConnections);
  CONT_SCHED_LOCK_RETRY_RET(this);
}


int
ShowCacheInternal::showPartConnections(int event, Event * e)
{
  CACHE_TRY_LOCK(lock, gpart[part_index]->mutex, mutex->thread_holding);
  if (!lock) {
    CONT_SCHED_LOCK_RETRY_RET(this);
  }
  for (CacheVC * vc = (CacheVC *) gpart[part_index]->stat_cache_vcs.head; vc; vc = vc->stat_link.next) {

    char nbytes[60], todo[60], url[81092];
    int ib = 0, xd = 0;
    URL uu;

    MUTEX_LOCK(lock2, vc->mutex, mutex->thread_holding);
    // if vc is closed ignore - Ramki 08/30/2000
    if (vc->closed == 1)
      continue;
    sprintf(nbytes, "%d", vc->vio.nbytes);
    sprintf(todo, "%d", vc->vio.ntodo());

    if (vc->f.frag_type == CACHE_FRAG_TYPE_HTTP && vc->request.valid()) {
      URL *u = vc->request.url_get(&uu);
      u->print(url, 8000, &ib, &xd);
      url[ib] = 0;
    } else if (vc->alternate.valid()) {
      URL *u = vc->alternate.request_url_get(&uu);
      u->print(url, 8000, &ib, &xd);
      url[ib] = 0;
    } else
      vc->key.string(url);
    CHECK_SHOW(show("<tr>" "<td>%s</td>"        // operation
                    "<td>%s</td>"       // Part
                    "<td>%s</td>"       // URL/Hash
                    "<td>%d</td>"
                    "<td>%s</td>"
                    "<td>%s</td>"
                    "</tr>\n",
                    ((vc->vio.op == VIO::READ) ? "Read" : "Write"),
                    vc->part->hash_id,
                    url,
                    vc->vio.ndone,
                    vc->vio.nbytes == INK64_MAX ? "all" : nbytes, vc->vio.nbytes == INK64_MAX ? "all" : todo));
  }
  part_index++;
  if (part_index < gnpart)
    CONT_SCHED_LOCK_RETRY(this);
  else {
    CHECK_SHOW(show("</table>\n"));
    return complete(event, e);
  }
  return EVENT_CONT;
}

#endif


int
ShowCacheInternal::showEvacuations(int event, Event * e)
{
  CHECK_SHOW(begin("Cache Pending Evacuations"));
  CHECK_SHOW(show("<H3>Cache Evacuations</H3>\n"
                  "<table border=1><tr>"
                  "<th>Offset</th>" "<th>Estimated Size</th>" "<th>Reader Count</th>" "<th>Done</th>" "</tr>\n"));

  SET_HANDLER(&ShowCacheInternal::showPartEvacuations);
  CONT_SCHED_LOCK_RETRY_RET(this);
}


int
ShowCacheInternal::showPartEvacuations(int event, Event * e)
{
  Part *p = gpart[part_index];
  CACHE_TRY_LOCK(lock, p->mutex, mutex->thread_holding);
  if (!lock)
    CONT_SCHED_LOCK_RETRY_RET(this);

  EvacuationBlock *b;
  int last = (p->len - (p->start - p->skip)) / EVACUATION_BUCKET_SIZE;
  for (int i = 0; i < last; i++) {
    for (b = p->evacuate[i].head; b; b = b->link.next) {
      char offset[60];
      ink_sprintf(offset, "%lld", (inku64) part_offset(p, &b->dir));
      CHECK_SHOW(show("<tr>" "<td>%s</td>"      // offset
                      "<td>%d</td>"     // estimated size
                      "<td>%d</td>"     // reader count
                      "<td>%s</td>"     // done
                      "</tr>\n", offset, (int) dir_approx_size(&b->dir), b->readers, b->f.done ? "yes" : "no"));
    }
  }
  part_index++;
  if (part_index < gnpart)
    CONT_SCHED_LOCK_RETRY(this);
  else {
    CHECK_SHOW(show("</table>\n"));
    return complete(event, e);
  }
  return EVENT_CONT;
}

int
ShowCacheInternal::showPartitions(int event, Event * e)
{
  CHECK_SHOW(begin("Cache Partitions"));
  CHECK_SHOW(show("<H3>Cache Partitions</H3>\n"
                  "<table border=1><tr>"
                  "<th>ID</th>"
                  "<th>Blocks</th>"
                  "<th>Directory Entries</th>"
                  "<th>Write Position</th>"
                  "<th>Write Agg Todo</th>"
                  "<th>Write Agg Todo Size</th>"
                  "<th>Write Agg Done</th>"
                  "<th>Phase</th>" "<th>Create Time</th>" "<th>Sync Serial</th>" "<th>Write Serial</th>" "</tr>\n"));

  SET_HANDLER(&ShowCacheInternal::showPartPartitions);
  CONT_SCHED_LOCK_RETRY_RET(this);
}


int
ShowCacheInternal::showPartPartitions(int event, Event * e)
{
  Part *p = gpart[part_index];
  CACHE_TRY_LOCK(lock, p->mutex, mutex->thread_holding);
  if (!lock)
    CONT_SCHED_LOCK_RETRY_RET(this);

  char ctime[256];
  ink_ctime_r(&p->header->create_time, ctime);
  ctime[strlen(ctime) - 1] = 0;
  int agg_todo = 0;
  int agg_done = p->agg_buf_pos;
  CacheVC *c = 0;
  for (c = p->agg.head; c; c = (CacheVC *) c->link.next)
    agg_todo++;
  CHECK_SHOW(show("<tr>" "<td>%s</td>"  // ID
                  "<td>%d</td>" // blocks
                  "<td>%lld</td>" // directory entries
                  "<td>%d</td>" // write position
                  "<td>%d</td>" // write agg to do
                  "<td>%d</td>" // write agg to do size
                  "<td>%d</td>" // write agg done
                  "<td>%d</td>" // phase
                  "<td>%s</td>" // create time
                  "<td>%u</td>" // sync serial
                  "<td>%u</td>" // write serial
                  "</tr>\n",
                  p->hash_id,
                  (int) ((p->len - (p->start - p->skip)) / INK_BLOCK_SIZE),
                  (inku64)(p->buckets * DIR_DEPTH * p->segments),
                  (int) ((p->header->write_pos - p->start) / INK_BLOCK_SIZE),
                  agg_todo,
                  p->agg_todo_size,
                  agg_done, p->header->phase, ctime, p->header->sync_serial, p->header->write_serial));
  CHECK_SHOW(show("</table>\n"));
  SET_HANDLER(&ShowCacheInternal::showSegments);
  return showSegments(event, e);
}

int
ShowCacheInternal::showSegments(int event, Event * e)
{
  CHECK_SHOW(show("<H3>Cache Partition Segments</H3>\n"
                  "<table border=1><tr>"
                  "<th>Free</th>"
                  "<th>Used</th>"
                  "<th>Empty</th>" "<th>Valid</th>" "<th>Agg Valid</th>" "<th>Avg Size</th>" "</tr>\n"));

  SET_HANDLER(&ShowCacheInternal::showSegSegment);
  seg_index = 0;
  CONT_SCHED_LOCK_RETRY_RET(this);
}

int
ShowCacheInternal::showSegSegment(int event, Event * e)
{
  Part *p = gpart[part_index];
  CACHE_TRY_LOCK(lock, p->mutex, mutex->thread_holding);
  if (!lock)
    CONT_SCHED_LOCK_RETRY_RET(this);
  int free = 0, used = 0, empty = 0, valid = 0, agg_valid = 0, avg_size = 0;
  dir_segment_accounted(seg_index, p, 0, &free, &used, &empty, &valid, &agg_valid, &avg_size);
  CHECK_SHOW(show("<tr>"
                  "<td>%d</td>"
                  "<td>%d</td>"
                  "<td>%d</td>"
                  "<td>%d</td>" "<td>%d</td>" "<td>%d</td>" "</tr>\n", free, used, empty, valid, agg_valid, avg_size));
  seg_index++;
  if (seg_index < p->segments)
    CONT_SCHED_LOCK_RETRY(this);
  else {
    CHECK_SHOW(show("</table>\n"));
    seg_index = 0;
    part_index++;
    if (part_index < gnpart)
      CONT_SCHED_LOCK_RETRY(this);
    else
      return complete(event, e);
  }
  return EVENT_CONT;
}



#endif // NON_MODULAR
