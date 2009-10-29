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

/****************************************************************************
  Overview.cc

  Implements NNTP Overviews

  
 ****************************************************************************/
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "ink_string++.h"
#include "Overview.h"
#include "ink_unused.h"
#include "ParseRules.h"
#include "Allocator.h"  /* MAGIC_EDITING_TAG */

#define ORECORD_FAST_ALLOCATE   256

Allocator orecordAllocator("ORecord", ORECORD_FAST_ALLOCATE);
ORecord protoORecord;

ORecord *
ORecord::alloc(int len)
{
  ORecord *r = (len <= ORECORD_FAST_ALLOCATE) ? (ORecord *) orecordAllocator.alloc_void() : (ORecord *) xmalloc(len);
  memcpy(r, &protoORecord, REF_COUNT_OBJ_OFFSET);
  return r;
}

void
ORecord::free()
{
  ink_assert(from_offset < 600 && date_offset < 900);
  if (size() <= ORECORD_FAST_ALLOCATE)
    orecordAllocator.free_void(this);
  else
    xfree(this);
}

#define LEN2(_s,_h)                                           \
const char * _h##_string = "";                                \
int _h##_len = 0;                                             \
{                                                             \
  MIMEField *field = h->field_find (_s, sizeof(_s)-1);        \
  if (field) {                                                \
    _h##_string = field->value_get(&_h##_len);                \
  } else                                                      \
    _h##_string = "";                                         \
}
#define LEN(_h) LEN2(#_h,_h)

#define MOVE(_h)                        \
  r->_h##_offset = b - &r->buf[0];      \
  memcpy(b, _h##_string, _h##_len); \
  b[_h##_len] = 0;                      \
  b += _h##_len + 1;

ORecord *
ORecord::create(MIMEHdr * h)
{
  LEN(subject);
  LEN(from);
  const char *date_string = "";
  int date_len = 0;
  {
    MIMEField *field = h->field_find("Date", sizeof("Date") - 1);
    if (field) {
      date_string = field->value_get(&date_len);
    }
  }
  LEN2("message-id", message_id);
  LEN(references);
  LEN(bytes);
  LEN(lines);
  LEN(Xref);

  int len = SIZEOF_ORECORD +
    subject_len + 1 +
    from_len + 1 +
    date_len + 1 + message_id_len + 1 + references_len + 1 + bytes_len + 1 + lines_len + 1 + Xref_len + 1;
  ORecord *r = alloc(len);
  char *b = &r->buf[0];

  memcpy(b, subject_string, subject_len + 1);
  b += subject_len + 1;
  MOVE(from);
  MOVE(date);
  MOVE(message_id);
  MOVE(references);
  MOVE(bytes);
  MOVE(lines);
  MOVE(Xref);
  ink_assert((int) (b - &r->buf[0] + SIZEOF_ORECORD) == len);
  r->next_offset = -(int) (b - &r->buf[0]);
  return r;
}

#undef LEN
#undef MOVE

#define MOVE(_h)                                                     \
r->_h##_offset = b - &r->buf[0];                                     \
if (_h) { memcpy(b,_h,_h##_len); b += _h##_len; }                    \
*b++ = 0;

ORecord *
ORecord::create(char *subject, int subject_len,
                char *from, int from_len,
                char *date, int date_len,
                char *message_id, int message_id_len,
                char *references, int references_len,
                char *bytes, int bytes_len, char *lines, int lines_len, char *Xref, int Xref_len)
{
  int len =
    SIZEOF_ORECORD +
    subject_len + 1 +
    from_len + 1 +
    date_len + 1 + message_id_len + 1 + references_len + 1 + bytes_len + 1 + lines_len + 1 + Xref_len + 1;
  ORecord *r = alloc(len);
  char *b = &r->buf[0];

  if (subject) {
    memcpy(b, subject, subject_len);
    b += subject_len;
  }
  *b++ = 0;
  MOVE(from);
  MOVE(date);
  MOVE(message_id);
  MOVE(references);
  MOVE(bytes);
  MOVE(lines);
  MOVE(Xref);
  ink_assert((int) (b - &r->buf[0] + SIZEOF_ORECORD) == len);
  r->next_offset = -(int) (b - &r->buf[0]);
  return r;
}

#undef MOVE

int
ORecord::marshal(char *b, int len)
{
  if (len < (int) (sizeof(short) + size() - REF_COUNT_OBJ_OFFSET))
    return 0;
  short t = (short) (size() - REF_COUNT_OBJ_OFFSET);
  _memcpy(b, (char *) &t, sizeof(t));
  b += sizeof(t);
  memcpy(b, ((char *) this) + REF_COUNT_OBJ_OFFSET, t);
  return sizeof(short) + t;
}

int
ORecord::unmarshal(char *b, int len, Ptr<ORecord> *result)
{
  if (len < (int) (sizeof(short) + SIZEOF_ORECORD - REF_COUNT_OBJ_OFFSET))
    return 0;
  short t = 0;
  _memcpy((char *) &t, b, sizeof(t));
  b += sizeof(t);
  if (len < (int) sizeof(short) + t)
    return 0;
  ORecord *r = alloc(t + REF_COUNT_OBJ_OFFSET);
  memcpy(((char *) r) + REF_COUNT_OBJ_OFFSET, b, t);
  *result = r;
  return (int) sizeof(short) + t;
}

#define S(_h) if (len == sizeof(#_h)-1 && !strncasecmp(#_h,s,len))    \
                return get_##_h();                                    \
              else return NULL;
char *
ORecord::get_raw(const char *s, int len)
{
  if (s[len - 1] == ':')
    len--;
  switch (ParseRules::ink_tolower(*s)) {
  case 's':
    S(subject);
  case 'f':
    S(from);
  case 'd':
    S(date);
  case 'm':
    if (len == (int) sizeof("message-id") - 1 && !strncasecmp("message-id", s, len))
      return get_message_id();
    else
      return NULL;
  case 'r':
    S(subject);
  case 'b':
    S(subject);
  case 'l':
    S(subject);
  case 'x':
    S(subject);
  default:
    return NULL;
  }
}

#undef S

Overview *
Overview::shallow_copy(unsigned int off)
{
  Overview *dup = NEW(new Overview);
  if (xoffset > off)
    off = xoffset;
  dup->set_offset(off);
  dup->xlastoffset = xlastoffset;
  dup->xlastfulloffset = xlastfulloffset;
  for (unsigned int i = off; i <= xlastoffset; i++) {
    ORecord *h = get(i);
    if (h)
      dup->add_internal(h, i, false);
  }
  return dup;
}

Overview::~Overview()
{
  if (vector) {
    for (int i = 0; i < vector_size; i++)
      vector[i] = 0;
    xfree(vector);
  }
}

void
Overview::set_offset(unsigned int new_offset)
{
  Ptr<ORecord> *new_vector = NULL;
  bool changed = false;
  int new_vector_size = 0;
  int delta = 0;
  int i = 0;

  if (new_offset > xoffset) {
    delta = new_offset - xoffset;
    new_vector_size = vector_size - delta;
    if (new_vector_size > 0) {
      int s = new_vector_size * sizeof(Ptr<ORecord>);
      new_vector = (Ptr<ORecord> *)xmalloc(s);
      memset(new_vector, 0, s);
      for (i = delta; i < vector_size; i++) {
        new_vector[i - delta] = vector[i];
        vector[i] = NULL;
      }
    }
    changed = true;
  } else if (new_offset < xoffset) {
    delta = xoffset - new_offset;
    new_vector_size = vector_size + delta;
    if (new_vector_size > 0) {
      int s = new_vector_size * sizeof(Ptr<ORecord>);
      new_vector = (Ptr<ORecord> *)xmalloc(s);
      memset(new_vector, 0, s);
      for (i = 0; i < vector_size; i++) {
        new_vector[i + delta] = vector[i];
        vector[i] = NULL;
      }
    }
    changed = true;
  }

  if (changed) {
    for (i = 0; i < vector_size; i++)
      vector[i] = 0;
    if (vector)
      xfree(vector);

    vector = new_vector;
    vector_size = new_vector_size;
    xoffset = new_offset;

    update_lastoffset(new_offset);
  }
}

void
Overview::add_internal(ORecord * header, unsigned int idx, bool copy_header)
{
  if (!vector)
    xoffset = idx;
  else if (idx < xoffset)
    set_offset(idx);

  idx -= xoffset;
  if ((int) idx >= vector_size) {
    Ptr<ORecord> *new_vector;
    int new_vector_size;
    int i;

    new_vector_size = 2 * vector_size;
    if (new_vector_size < (int) idx + 1)
      new_vector_size = (int) idx + 1;
    if (new_vector_size < OVERVIEW_MIN_SIZE)
      new_vector_size = OVERVIEW_MIN_SIZE;
    int s = new_vector_size * sizeof(Ptr<ORecord>);
    new_vector = (Ptr<ORecord> *)xmalloc(s);
    memset(new_vector, 0, s);

    if (vector) {
      for (i = 0; i < vector_size; i++) {
        new_vector[i] = vector[i];
        vector[i] = NULL;
      }
      xfree(vector);
    }
    vector = new_vector;
    vector_size = new_vector_size;
  }

  ink_assert(vector[idx] == NULL);
  if ((idx + xoffset - 1 == xlastfulloffset) || (!idx && !xlastfulloffset))
    update_lastfulloffset(idx + xoffset);
  else
    update_lastoffset(idx + xoffset);

  if (copy_header)
    vector[idx] = ORecord::copy(header);
  else
    vector[idx] = header;
#if defined(VERIFY_NTEST_ORECORD)
  verify_ORecord(this, idx + xoffset);
#endif
}

void
Overview::remove(unsigned int idx)
{
  if (idx<xoffset || idx>= xoffset + vector_size)
    return;
  vector[idx - xoffset] = NULL;
}

int
Overview::marshal_length(unsigned int start, unsigned int alast)
{
  int length = (int) (5 * sizeof(int));
  unsigned int i;

  if (!vector_size)
    return length;

  unsigned int first = !start ? 0 : start - xoffset;
  unsigned int last = !alast ? vector_size - 1 : alast - xoffset;
  if ((int) last > vector_size - 1)
    last = vector_size - 1;
  if (vector_size) {
    ink_assert((int) last <= vector_size - 1);
    for (i = first; i <= last; i++) {
      length += (int) sizeof(char);
      if (vector[i])
        length += vector[i]->marshal_length();
    }
  }

  return length;
}

static int
verify_overview(int vector_size,
                unsigned int xoffset, unsigned int xlastoffset, unsigned int version, unsigned int xlastfulloffset)
{
  if (vector_size > 131072)     // MAX_ARTICLES_PER_GROUP
    return -0x1001;
  if (version != CURRENT_VERSION)
    return -0x1002;
  if (xlastfulloffset > xlastoffset)
    return -0x1003;
  if (xoffset && xlastoffset < xoffset - 1)
    return -0x1004;
  return 0;
}

static int
verify_overview(Overview * o)
{
  return verify_overview(o->vector_size, o->xoffset, o->xlastoffset, o->version, o->xlastfulloffset);
}

int
Overview::marshal(char *buf, int length, unsigned int *begin, unsigned int alast, unsigned int skip_till)
{
  int start_length = length;
  const char *end = buf + length;

  unsigned int first = (!begin || !*begin) ? 0 : *begin - xoffset;
  bool header = !first;
  ink_assert(!skip_till || skip_till >= xoffset);
  unsigned int last = !alast ? vector_size - 1 : alast - xoffset;
  if ((int) last > vector_size - 1)
    last = vector_size - 1;
  int n = 0;
  if (skip_till && first < skip_till - xoffset)
    first = skip_till - xoffset;
  if (vector_size && first <= last)
    n = last - first + 1;
  unsigned int new_offset = skip_till ? skip_till : xoffset;

  // FIXME: this needs to be cleaned up
  //        I don't think that this conditional is necessary

  unsigned int new_xlastoffset = n >= (int) (xlastoffset - new_offset + 1) ? xlastoffset : new_offset + n - 1;
  unsigned int new_xlastfulloffset = xlastfulloffset > new_xlastoffset ? new_xlastoffset : xlastfulloffset;
  int new_vector_size = vector_size > n ? n : vector_size;
  if (!n) {
    new_xlastoffset = new_offset;
    new_xlastfulloffset = new_offset;
  }
  ink_assert(new_xlastfulloffset >= new_offset);
  ink_assert(new_xlastoffset >= new_offset);

  if (header) {
    // need to ensure we can make progress and write down one element
    if (length < (int) (5 * sizeof(int) + (n ? 1 : 0)))
      return 0;

    int res = 0;
    if ((res = verify_overview(this)) < 0) {
      ink_assert(!"verify_overview");
      return res;
    }
    if ((res = verify_overview(new_vector_size, new_offset, new_xlastoffset, version, new_xlastfulloffset)) < 0) {
      ink_assert(!"new verify_overview");
      return res;
    }

    _memcpy(buf, (char *) &version, sizeof(int));
    buf += sizeof(int);
    // convert to current version on save
    ink_assert(version == CURRENT_VERSION);
    _memcpy(buf, (char *) &new_vector_size, sizeof(int));
    buf += sizeof(int);
    _memcpy(buf, (char *) &new_offset, sizeof(int));
    buf += sizeof(int);
    _memcpy(buf, (char *) &new_xlastoffset, sizeof(int));
    buf += sizeof(int);
    _memcpy(buf, (char *) &new_xlastfulloffset, sizeof(int));
    buf += sizeof(int);
    length -= 5 * sizeof(int);
  }

  int before_length = length;
  unsigned int i = first;

  if (vector_size) {
    for (; i <= last; i++) {
      before_length = length;

      if (length < 1)
        goto Labort;

      *buf++ = (vector[i] ? 1 : 0);
      length--;

      if (vector[i]) {
#if defined(VERIFY_NTEST_ORECORD)
        verify_ORecord(this, i + xoffset);
#endif
        int err = vector[i]->marshal(buf, (int) (end - buf));
        if (err <= 0) {
          if (!err)
            goto Labort;
          return err;
        }

        buf += err;
        length -= err;
      }
    }
  }

  length = start_length - length;
  if (begin)
    *begin = i + xoffset;
  return length;
Labort:
  length = start_length - before_length;
  if (begin)
    *begin = i + xoffset;
  return length;
}

int
Overview::unmarshal(const char *buf, int length, unsigned int *begin, unsigned int alast)
{
  const char *start = buf;
  const char *end = buf + length;
  unsigned int first = (!begin || !*begin) ? 0 : *begin - xoffset;

  if (!first) {

    if (length < (int) (5 * sizeof(int)))
      return 0;

    _memcpy((char *) &version, buf, sizeof(int));
    buf += sizeof(int);
    _memcpy((char *) &vector_size, buf, sizeof(int));
    buf += sizeof(int);
    _memcpy((char *) &xoffset, buf, sizeof(int));
    buf += sizeof(int);
    _memcpy((char *) &xlastoffset, buf, sizeof(int));
    buf += sizeof(int);
    _memcpy((char *) &xlastfulloffset, buf, sizeof(int));
    buf += sizeof(int);
    length -= 5 * sizeof(int);

    if (vector_size && vector_size < OVERVIEW_MIN_SIZE)
      vector_size = OVERVIEW_MIN_SIZE;

    int res = 0;
    if ((res = verify_overview(this)) < 0) {
      ink_assert(!"verify_overview");
      return res;
    }

    if (vector_size) {
      ink_assert(!vector);
      int s = vector_size * sizeof(ORecord *);
      vector = (Ptr<ORecord> *)xmalloc(s);
      memset(vector, 0, s);
    } else
      vector = NULL;
  }

  unsigned int last = !alast ? vector_size - 1 : alast - xoffset;
  const char *before_buf = buf;
  unsigned int i = first;

  if (vector_size) {
    for (; i <= last; i++) {
      ink_assert(buf <= end);

      before_buf = buf;
      vector[i] = NULL;

      if (length < 1)
        goto Labort;

      char present = *buf++;
      length--;

      if (present > 1)
        return -1;
      if (present) {
        int err = ORecord::unmarshal((char *) buf,
                                     (int) (end - buf), &vector[i]);
        if (err <= 0) {
          if (!err)
            goto Labort;
          return err;
        }
#if defined(VERIFY_NTEST_ORECORD)
        verify_ORecord(this, i + xoffset);
#endif

        ink_assert(buf + err <= end);
        buf += err;
        length -= err;
      }
    }
  }

  if (begin)
    *begin = i + xoffset;
  return (int) ((caddr_t) buf - (caddr_t) start);
Labort:
  if (begin)
    *begin = i + xoffset;
  return (int) ((caddr_t) before_buf - (caddr_t) start);
}

void
Overview::free()
{
  for (int i = 0; i < vector_size; i++)
    vector[i] = 0;
  delete this;
}

#if defined(VERIFY_NTEST_ORECORD)
void
verify_ORecord(Overview * o, unsigned int i)
{
  ORecord *r = NULL;
  if ((r = o->vector[i - o->xoffset])) {
    ink_assert(r->from_offset >= 0 && r->from_offset < 100);
    char *n = strchr(r->get_message_id(), ':');
    ink_assert(ink_atoui(n + 1) == i);
  }
}
#endif
