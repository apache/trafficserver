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

/***************************************************************************
 LogField.cc

 This file implements the LogField object, which is the central
 representation of a logging field.
 ***************************************************************************/
#include "ts/ink_platform.h"

#include "LogUtils.h"
#include "LogField.h"
#include "LogBuffer.h"
#include "LogAccess.h"
#include "Log.h"

// clang-format off
//
static const char *container_names[] = {
  "not-a-container",
  "cqh",
  "psh",
  "pqh",
  "ssh",
  "cssh",
  "ecqh",
  "epsh",
  "epqh",
  "essh",
  "ecssh",
  "icfg",
  "scfg",
  "record",
  "ms",
  "msdms",
};

static const char *aggregate_names[] = {
  "not-an-agg-op",
  "COUNT",
  "SUM",
  "AVG",
  "FIRST",
  "LAST",
};

// clang-format on

LogSlice::LogSlice(char *str)
{
  char *a, *b, *c;

  m_enable = false;
  m_start  = 0;
  m_end    = INT_MAX;

  if ((a = strchr(str, '[')) == nullptr) {
    return;
  }

  *a++ = '\0';
  if ((b = strchr(a, ':')) == nullptr) {
    return;
  }

  *b++ = '\0';
  if ((c = strchr(b, ']')) == nullptr) {
    return;
  }

  m_enable = true;

  // eat space
  while (a != b && *a == ' ') {
    a++;
  }

  if (a != b) {
    m_start = atoi(a);
  }

  // eat space
  while (b != c && *b == ' ') {
    b++;
  }

  if (b != c) {
    m_end = atoi(b);
  }
}

int
LogSlice::toStrOffset(int strlen, int *offset)
{
  int i, j, len;

  // letf index
  if (m_start >= 0) {
    i = m_start;
  } else {
    i = m_start + strlen;
  }

  if (i >= strlen) {
    return 0;
  }

  if (i < 0) {
    i = 0;
  }

  // right index
  if (m_end >= 0) {
    j = m_end;
  } else {
    j = m_end + strlen;
  }

  if (j <= 0) {
    return 0;
  }

  if (j > strlen) {
    j = strlen;
  }

  // available length
  len = j - i;

  if (len > 0) {
    *offset = i;
  }

  return len;
}

/*-------------------------------------------------------------------------
  LogField::LogField
  -------------------------------------------------------------------------*/

namespace
{
struct cmp_str {
  bool
  operator()(ts::ConstBuffer a, ts::ConstBuffer b) const
  {
    return ptr_len_casecmp(a._ptr, a._size, b._ptr, b._size) < 0;
  }
};
} // namespace

typedef std::map<ts::ConstBuffer, TSMilestonesType, cmp_str> milestone_map;
static milestone_map m_milestone_map;

struct milestone {
  const char *msname;
  TSMilestonesType mstype;
};

static const milestone milestones[] = {
  {"TS_MILESTONE_UA_BEGIN", TS_MILESTONE_UA_BEGIN},
  {"TS_MILESTONE_UA_FIRST_READ", TS_MILESTONE_UA_FIRST_READ},
  {"TS_MILESTONE_UA_READ_HEADER_DONE", TS_MILESTONE_UA_READ_HEADER_DONE},
  {"TS_MILESTONE_UA_BEGIN_WRITE", TS_MILESTONE_UA_BEGIN_WRITE},
  {"TS_MILESTONE_UA_CLOSE", TS_MILESTONE_UA_CLOSE},
  {"TS_MILESTONE_SERVER_FIRST_CONNECT", TS_MILESTONE_SERVER_FIRST_CONNECT},
  {"TS_MILESTONE_SERVER_CONNECT", TS_MILESTONE_SERVER_CONNECT},
  {"TS_MILESTONE_SERVER_CONNECT_END", TS_MILESTONE_SERVER_CONNECT_END},
  {"TS_MILESTONE_SERVER_BEGIN_WRITE", TS_MILESTONE_SERVER_BEGIN_WRITE},
  {"TS_MILESTONE_SERVER_FIRST_READ", TS_MILESTONE_SERVER_FIRST_READ},
  {"TS_MILESTONE_SERVER_READ_HEADER_DONE", TS_MILESTONE_SERVER_READ_HEADER_DONE},
  {"TS_MILESTONE_SERVER_CLOSE", TS_MILESTONE_SERVER_CLOSE},
  {"TS_MILESTONE_CACHE_OPEN_READ_BEGIN", TS_MILESTONE_CACHE_OPEN_READ_BEGIN},
  {"TS_MILESTONE_CACHE_OPEN_READ_END", TS_MILESTONE_CACHE_OPEN_READ_END},
  {"TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN", TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN},
  {"TS_MILESTONE_CACHE_OPEN_WRITE_END", TS_MILESTONE_CACHE_OPEN_WRITE_END},
  {"TS_MILESTONE_DNS_LOOKUP_BEGIN", TS_MILESTONE_DNS_LOOKUP_BEGIN},
  {"TS_MILESTONE_DNS_LOOKUP_END", TS_MILESTONE_DNS_LOOKUP_END},
  {"TS_MILESTONE_SM_START", TS_MILESTONE_SM_START},
  {"TS_MILESTONE_SM_FINISH", TS_MILESTONE_SM_FINISH},
  {"TS_MILESTONE_PLUGIN_ACTIVE", TS_MILESTONE_PLUGIN_ACTIVE},
  {"TS_MILESTONE_PLUGIN_TOTAL", TS_MILESTONE_PLUGIN_TOTAL},
  {"TS_MILESTONE_TLS_HANDSHAKE_START", TS_MILESTONE_TLS_HANDSHAKE_START},
  {"TS_MILESTONE_TLS_HANDSHAKE_END", TS_MILESTONE_TLS_HANDSHAKE_END},
};

void
LogField::init_milestone_container()
{
  if (m_milestone_map.empty()) {
    for (unsigned i = 0; i < countof(milestones); ++i) {
      m_milestone_map.insert(
        std::make_pair(ts::ConstBuffer(milestones[i].msname, strlen(milestones[i].msname)), milestones[i].mstype));
    }
  }
}

// Generic field ctor
LogField::LogField(const char *name, const char *symbol, Type type, MarshalFunc marshal, UnmarshalFunc unmarshal, SetFunc _setfunc)
  : m_name(ats_strdup(name)),
    m_symbol(ats_strdup(symbol)),
    m_type(type),
    m_container(NO_CONTAINER),
    m_marshal_func(marshal),
    m_unmarshal_func(unmarshal),
    m_unmarshal_func_map(nullptr),
    m_agg_op(NO_AGGREGATE),
    m_agg_cnt(0),
    m_agg_val(0),
    m_milestone1(TS_MILESTONE_LAST_ENTRY),
    m_milestone2(TS_MILESTONE_LAST_ENTRY),
    m_time_field(false),
    m_alias_map(nullptr),
    m_set_func(_setfunc)
{
  ink_assert(m_name != nullptr);
  ink_assert(m_symbol != nullptr);
  ink_assert(m_type >= 0 && m_type < N_TYPES);
  ink_assert(m_marshal_func != (MarshalFunc) nullptr);

  m_time_field = (strcmp(m_symbol, "cqts") == 0 || strcmp(m_symbol, "cqth") == 0 || strcmp(m_symbol, "cqtq") == 0 ||
                  strcmp(m_symbol, "cqtn") == 0 || strcmp(m_symbol, "cqtd") == 0 || strcmp(m_symbol, "cqtt") == 0);
}

LogField::LogField(const char *name, const char *symbol, Type type, MarshalFunc marshal, UnmarshalFuncWithMap unmarshal,
                   const Ptr<LogFieldAliasMap> &map, SetFunc _setfunc)
  : m_name(ats_strdup(name)),
    m_symbol(ats_strdup(symbol)),
    m_type(type),
    m_container(NO_CONTAINER),
    m_marshal_func(marshal),
    m_unmarshal_func(nullptr),
    m_unmarshal_func_map(unmarshal),
    m_agg_op(NO_AGGREGATE),
    m_agg_cnt(0),
    m_agg_val(0),
    m_milestone1(TS_MILESTONE_LAST_ENTRY),
    m_milestone2(TS_MILESTONE_LAST_ENTRY),
    m_time_field(false),
    m_alias_map(map),
    m_set_func(_setfunc)
{
  ink_assert(m_name != nullptr);
  ink_assert(m_symbol != nullptr);
  ink_assert(m_type >= 0 && m_type < N_TYPES);
  ink_assert(m_marshal_func != (MarshalFunc) nullptr);
  ink_assert(m_alias_map);

  m_time_field = (strcmp(m_symbol, "cqts") == 0 || strcmp(m_symbol, "cqth") == 0 || strcmp(m_symbol, "cqtq") == 0 ||
                  strcmp(m_symbol, "cqtn") == 0 || strcmp(m_symbol, "cqtd") == 0 || strcmp(m_symbol, "cqtt") == 0);
}

TSMilestonesType
LogField::milestone_from_m_name()
{
  milestone_map::iterator it;
  TSMilestonesType result = TS_MILESTONE_LAST_ENTRY;

  it = m_milestone_map.find(ts::ConstBuffer(m_name, strlen(m_name)));
  if (it != m_milestone_map.end()) {
    result = it->second;
  }

  return result;
}

int
LogField::milestones_from_m_name(TSMilestonesType *ms1, TSMilestonesType *ms2)
{
  milestone_map::iterator it;
  ts::ConstBuffer ms1_name, ms2_name(m_name, strlen(m_name));

  ms1_name = ms2_name.splitOn('-');

  it = m_milestone_map.find(ms1_name);
  if (it != m_milestone_map.end()) {
    *ms1 = it->second;
  } else {
    return -1;
  }

  it = m_milestone_map.find(ms2_name);
  if (it != m_milestone_map.end()) {
    *ms2 = it->second;
  } else {
    return -1;
  }

  return 0;
}

// Container field ctor
LogField::LogField(const char *field, Container container, SetFunc _setfunc)
  : m_name(ats_strdup(field)),
    m_symbol(ats_strdup(container_names[container])),
    m_type(LogField::STRING),
    m_container(container),
    m_marshal_func(nullptr),
    m_unmarshal_func(nullptr),
    m_unmarshal_func_map(nullptr),
    m_agg_op(NO_AGGREGATE),
    m_agg_cnt(0),
    m_agg_val(0),
    m_milestone1(TS_MILESTONE_LAST_ENTRY),
    m_milestone2(TS_MILESTONE_LAST_ENTRY),
    m_time_field(false),
    m_alias_map(nullptr),
    m_set_func(_setfunc)
{
  ink_assert(m_name != nullptr);
  ink_assert(m_symbol != nullptr);
  ink_assert(m_type >= 0 && m_type < N_TYPES);

  m_time_field = (strcmp(m_symbol, "cqts") == 0 || strcmp(m_symbol, "cqth") == 0 || strcmp(m_symbol, "cqtq") == 0 ||
                  strcmp(m_symbol, "cqtn") == 0 || strcmp(m_symbol, "cqtd") == 0 || strcmp(m_symbol, "cqtt") == 0);

  switch (m_container) {
  case CQH:
  case PSH:
  case PQH:
  case SSH:
  case CSSH:
  case ECQH:
  case EPSH:
  case EPQH:
  case ESSH:
  case ECSSH:
  case SCFG:
    m_unmarshal_func = (UnmarshalFunc) & (LogAccess::unmarshal_str);
    break;

  case ICFG:
    m_unmarshal_func = &(LogAccess::unmarshal_int_to_str);
    break;

  case RECORD:
    m_unmarshal_func = &(LogAccess::unmarshal_record);
    break;

  case MS:
    m_milestone1 = milestone_from_m_name();
    if (TS_MILESTONE_LAST_ENTRY == m_milestone1) {
      Note("Invalid milestone name in LogField ctor: %s", m_name);
    }
    m_unmarshal_func = &(LogAccess::unmarshal_int_to_str);
    break;

  case MSDMS: {
    int rv = milestones_from_m_name(&m_milestone1, &m_milestone2);
    if (0 != rv) {
      Note("Invalid milestone range in LogField ctor: %s", m_name);
    }
    m_unmarshal_func = &(LogAccess::unmarshal_int_to_str);
    break;
  }

  default:
    Note("Invalid container type in LogField ctor: %d", container);
  }
}

// Copy ctor
LogField::LogField(const LogField &rhs)
  : m_name(ats_strdup(rhs.m_name)),
    m_symbol(ats_strdup(rhs.m_symbol)),
    m_type(rhs.m_type),
    m_container(rhs.m_container),
    m_marshal_func(rhs.m_marshal_func),
    m_unmarshal_func(rhs.m_unmarshal_func),
    m_unmarshal_func_map(rhs.m_unmarshal_func_map),
    m_agg_op(rhs.m_agg_op),
    m_agg_cnt(0),
    m_agg_val(0),
    m_milestone1(TS_MILESTONE_LAST_ENTRY),
    m_milestone2(TS_MILESTONE_LAST_ENTRY),
    m_time_field(rhs.m_time_field),
    m_alias_map(rhs.m_alias_map),
    m_set_func(rhs.m_set_func)
{
  ink_assert(m_name != nullptr);
  ink_assert(m_symbol != nullptr);
  ink_assert(m_type >= 0 && m_type < N_TYPES);
}

/*-------------------------------------------------------------------------
  LogField::~LogField
  -------------------------------------------------------------------------*/
LogField::~LogField()
{
  ats_free(m_name);
  ats_free(m_symbol);
}

/*-------------------------------------------------------------------------
  LogField::marshal_len

  This routine will find the marshalling length (in bytes) for this field,
  given a LogAccess object.  It does this by using the property of the
  marshalling routines that if the marshal buffer is NULL, only the size
  requirement is returned.
  -------------------------------------------------------------------------*/
unsigned
LogField::marshal_len(LogAccess *lad)
{
  if (m_container == NO_CONTAINER) {
    return (lad->*m_marshal_func)(nullptr);
  }

  switch (m_container) {
  case CQH:
  case PSH:
  case PQH:
  case SSH:
  case CSSH:
    return lad->marshal_http_header_field(m_container, m_name, nullptr);

  case ECQH:
  case EPSH:
  case EPQH:
  case ESSH:
  case ECSSH:
    return lad->marshal_http_header_field_escapify(m_container, m_name, nullptr);

  case ICFG:
    return lad->marshal_config_int_var(m_name, nullptr);

  case SCFG:
    return lad->marshal_config_str_var(m_name, nullptr);

  case RECORD:
    return lad->marshal_record(m_name, nullptr);

  case MS:
    return lad->marshal_milestone(m_milestone1, nullptr);

  case MSDMS:
    return lad->marshal_milestone_diff(m_milestone1, m_milestone2, nullptr);

  default:
    return 0;
  }
}

void
LogField::updateField(LogAccess *lad, char *buf, int len)
{
  if (m_container == NO_CONTAINER) {
    return (lad->*m_set_func)(buf, len);
  }
  // else...// future enhancement
}

/*-------------------------------------------------------------------------
  LogField::marshal

  This routine will marshsal the given field into the buffer provided.
  -------------------------------------------------------------------------*/
unsigned
LogField::marshal(LogAccess *lad, char *buf)
{
  if (m_container == NO_CONTAINER) {
    return (lad->*m_marshal_func)(buf);
  }

  switch (m_container) {
  case CQH:
  case PSH:
  case PQH:
  case SSH:
  case CSSH:
    return lad->marshal_http_header_field(m_container, m_name, buf);

  case ECQH:
  case EPSH:
  case EPQH:
  case ESSH:
  case ECSSH:
    return lad->marshal_http_header_field_escapify(m_container, m_name, buf);

  case ICFG:
    return lad->marshal_config_int_var(m_name, buf);

  case SCFG:
    return lad->marshal_config_str_var(m_name, buf);

  case RECORD:
    return lad->marshal_record(m_name, buf);

  case MS:
    return lad->marshal_milestone(m_milestone1, buf);

  case MSDMS:
    return lad->marshal_milestone_diff(m_milestone1, m_milestone2, buf);

  default:
    return 0;
  }
}

/*-------------------------------------------------------------------------
  LogField::marshal_agg
  -------------------------------------------------------------------------*/
unsigned
LogField::marshal_agg(char *buf)
{
  ink_assert(buf != nullptr);
  int64_t avg = 0;

  switch (m_agg_op) {
  case eCOUNT:
    LogAccess::marshal_int(buf, m_agg_cnt);
    break;

  case eSUM:
  case eFIRST:
  case eLAST:
    LogAccess::marshal_int(buf, m_agg_val);
    break;

  case eAVG:
    if (m_agg_cnt) {
      avg = m_agg_val / m_agg_cnt;
    }
    LogAccess::marshal_int(buf, avg);
    break;

  default:
    Note("Cannot marshal aggregate field %s; "
         "invalid aggregate operator: %d",
         m_symbol, m_agg_op);
    return 0;
  }

  m_agg_val = 0;
  m_agg_cnt = 0;

  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  LogField::unmarshal

  This routine will invoke the proper unmarshalling routine to return a
  string that represents the ASCII value of the field.
  -------------------------------------------------------------------------*/
unsigned
LogField::unmarshal(char **buf, char *dest, int len)
{
  if (!m_alias_map) {
    if (m_unmarshal_func == (UnmarshalFunc)LogAccess::unmarshal_str ||
        m_unmarshal_func == (UnmarshalFunc)LogAccess::unmarshal_http_text) {
      UnmarshalFuncWithSlice func = (UnmarshalFuncWithSlice)m_unmarshal_func;
      return (*func)(buf, dest, len, &m_slice);
    }
    return (*m_unmarshal_func)(buf, dest, len);
  } else {
    return (*m_unmarshal_func_map)(buf, dest, len, m_alias_map);
  }
}

/*-------------------------------------------------------------------------
  LogField::display
  -------------------------------------------------------------------------*/
void
LogField::display(FILE *fd)
{
  static const char *names[LogField::N_TYPES] = {"sINT", "dINT", "STR", "IP"};

  fprintf(fd, "    %30s %10s %5s\n", m_name, m_symbol, names[m_type]);
}

/*-------------------------------------------------------------------------
  LogField::operator==

  This operator does only care of the name and m_symbol, may need
  do check on others layter.
  -------------------------------------------------------------------------*/
bool
LogField::operator==(LogField &rhs)
{
  if (strcmp(name(), rhs.name()) || strcmp(symbol(), rhs.symbol())) {
    return false;
  } else {
    return true;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
void
LogField::set_aggregate_op(LogField::Aggregate agg_op)
{
  if (agg_op > 0 && agg_op < LogField::N_AGGREGATES) {
    m_agg_op = agg_op;
  } else {
    Note("Invalid aggregate operator identifier: %d", agg_op);
    m_agg_op = NO_AGGREGATE;
  }
}

void
LogField::update_aggregate(int64_t val)
{
  switch (m_agg_op) {
  case eCOUNT:
  case eSUM:
  case eAVG:
    m_agg_val += val;
    m_agg_cnt++;
    break;

  case eFIRST:
    if (m_agg_cnt == 0) {
      m_agg_val = val;
      m_agg_cnt++;
    }
    break;

  case eLAST:
    m_agg_val = val;
    m_agg_cnt++;
    break;

  default:
    Note("Cannot update aggregate field; invalid operator %d", m_agg_op);
    return;
  }

  Debug("log-agg",
        "Aggregate field %s updated with val %" PRId64 ", "
        "new val = %" PRId64 ", cnt = %" PRId64 "",
        m_symbol, val, m_agg_val, m_agg_cnt);
}

LogField::Container
LogField::valid_container_name(char *name)
{
  for (unsigned i = 1; i < countof(container_names); i++) {
    if (strcmp(name, container_names[i]) == 0) {
      return (LogField::Container)i;
    }
  }

  return LogField::NO_CONTAINER;
}

LogField::Aggregate
LogField::valid_aggregate_name(char *name)
{
  for (unsigned i = 1; i < countof(aggregate_names); i++) {
    if (strcmp(name, aggregate_names[i]) == 0) {
      return (LogField::Aggregate)i;
    }
  }

  return LogField::NO_AGGREGATE;
}

bool
LogField::fieldlist_contains_aggregates(const char *fieldlist)
{
  const char *match;

  for (unsigned i = 1; i < countof(aggregate_names); i++) {
    if ((match = strstr(fieldlist, aggregate_names[i])) != nullptr) {
      // verify that the aggregate string is not part of a container field name.
      if ((strchr(fieldlist, '{') == nullptr) && (strchr(match, '}') == nullptr)) {
        return true;
      }
    }
  }

  return false;
}

/*-------------------------------------------------------------------------
  LogFieldList

  It is ASSUMED that each element on this list has been allocated from the
  heap with "new" and that each element is on at most ONE list.  To enforce
  this, items are copied by default, using the copy ctor.
  -------------------------------------------------------------------------*/
LogFieldList::LogFieldList() : m_marshal_len(0) {}

LogFieldList::~LogFieldList()
{
  clear();
}

void
LogFieldList::clear()
{
  LogField *f;
  while ((f = m_field_list.dequeue())) {
    delete f; // safe given the semantics stated above
  }
  m_marshal_len = 0;
}

void
LogFieldList::add(LogField *field, bool copy)
{
  ink_assert(field != nullptr);

  if (copy) {
    m_field_list.enqueue(new LogField(*field));
  } else {
    m_field_list.enqueue(field);
  }

  if (field->type() == LogField::sINT) {
    m_marshal_len += INK_MIN_ALIGN;
  }
}

LogField *
LogFieldList::find_by_name(const char *name) const
{
  for (LogField *f = first(); f; f = next(f)) {
    if (!strcmp(f->name(), name)) {
      return f;
    }
  }
  return nullptr;
}

LogField *
LogFieldList::find_by_symbol(const char *symbol) const
{
  LogField *field = nullptr;

  if (Log::field_symbol_hash) {
    if (ink_hash_table_lookup(Log::field_symbol_hash, (char *)symbol, (InkHashTableValue *)&field) && field) {
      Debug("log-field-hash", "Field %s found", field->symbol());
      return field;
    }
  }
  // trusty old method

  for (field = first(); field; field = next(field)) {
    if (!strcmp(field->symbol(), symbol)) {
      return field;
    }
  }

  return nullptr;
}

unsigned
LogFieldList::marshal_len(LogAccess *lad)
{
  int bytes = 0;
  for (LogField *f = first(); f; f = next(f)) {
    if (f->type() != LogField::sINT) {
      const int len = f->marshal_len(lad);
      ink_release_assert(len >= INK_MIN_ALIGN);
      bytes += len;
    }
  }
  return m_marshal_len + bytes;
}

unsigned
LogFieldList::marshal(LogAccess *lad, char *buf)
{
  char *ptr;
  int bytes = 0;
  for (LogField *f = first(); f; f = next(f)) {
    ptr = &buf[bytes];
    bytes += f->marshal(lad, ptr);
    ink_assert(bytes % INK_MIN_ALIGN == 0);
  }
  return bytes;
}

unsigned
LogFieldList::marshal_agg(char *buf)
{
  char *ptr;
  int bytes = 0;
  for (LogField *f = first(); f; f = next(f)) {
    ptr = &buf[bytes];
    bytes += f->marshal_agg(ptr);
  }
  return bytes;
}

unsigned
LogFieldList::count()
{
  unsigned cnt = 0;
  for (LogField *f = first(); f; f = next(f)) {
    cnt++;
  }
  return cnt;
}

void
LogFieldList::display(FILE *fd)
{
  for (LogField *f = first(); f; f = next(f)) {
    f->display(fd);
  }
}
