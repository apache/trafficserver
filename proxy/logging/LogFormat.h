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

#pragma once

#define LOG_FIELD_MARKER '\377'

#include "tscore/ink_platform.h"
#include "LogField.h"

enum LogFormatType {
  // We start the numbering at 4 to compatibility with Traffic Server 4.x, which used
  // to have the predefined log formats enumerated above ...
  LOG_FORMAT_CUSTOM = 4,
  LOG_FORMAT_TEXT   = 5
};

enum LogFileFormat {
  LOG_FILE_BINARY,
  LOG_FILE_ASCII,
  LOG_FILE_PIPE, // ie. ASCII pipe
  N_LOGFILE_TYPES
};

/*-------------------------------------------------------------------------
  LogFormat

  Now, this object will simply store the characteristics of a log format,
  which is defined as a set of fields.
  -------------------------------------------------------------------------*/

class LogFormat : public RefCountObj
{
public:
  LogFormat(const char *name, const char *format_str, unsigned interval_sec = 0);
  LogFormat(const char *name, const char *fieldlist_str, const char *printf_str, unsigned interval_sec = 0);
  LogFormat(const LogFormat &rhs);

  ~LogFormat() override;

  void display(FILE *fd = stdout);

  bool
  valid() const
  {
    return m_valid;
  }
  char *
  name() const
  {
    return m_name_str;
  }
  char *
  fieldlist() const
  {
    return m_fieldlist_str;
  }
  char *
  format_string() const
  {
    return m_format_str;
  }
  int32_t
  name_id() const
  {
    return m_name_id;
  }
  unsigned
  fieldlist_id() const
  {
    return m_fieldlist_id;
  }
  LogFormatType
  type() const
  {
    return m_format_type;
  }
  char *
  printf_str() const
  {
    return m_printf_str;
  }
  bool
  is_aggregate() const
  {
    return m_aggregate;
  }
  unsigned
  field_count() const
  {
    return m_field_count;
  }
  long
  interval() const
  {
    return m_interval_sec;
  }

public:
  static int32_t id_from_name(const char *name);
  static LogFormat *format_from_specification(char *spec, char **file_name, char **file_header, LogFileFormat *file_type);
  static int parse_symbol_string(const char *symbol_string, LogFieldList *field_list, bool *contains_aggregates);
  static int parse_format_string(const char *format_str, char **printf_str, char **fields_str);
  static int parse_escape_string(const char *str, int len);

  // these are static because m_tagging_on is a class variable
  static void
  turn_tagging_on()
  {
    m_tagging_on = true;
  }
  static void
  turn_tagging_off()
  {
    m_tagging_on = false;
  }

private:
  bool setup(const char *name, const char *format_str, unsigned interval_sec = 0);
  void init_variables(const char *name, const char *fieldlist_str, const char *printf_str, unsigned interval_sec);

public:
  LogFieldList m_field_list;
  long m_interval_sec;
  long m_interval_next;
  char *m_agg_marshal_space;

private:
  static bool m_tagging_on; // flag to control tagging, class
  // variable
  bool m_valid;
  char *m_name_str;
  int32_t m_name_id;
  char *m_fieldlist_str;
  unsigned m_fieldlist_id;
  unsigned m_field_count;
  char *m_printf_str;
  bool m_aggregate;
  char *m_format_str;
  LogFormatType m_format_type;

public:
  LINK(LogFormat, link);

  // noncopyable
  LogFormat &operator=(LogFormat &rhs) = delete;

private:
  // -- member functions that are not allowed --
  LogFormat();
};

// For text logs, there is no format string; we'll simply log the
// entire entry as a string without any field substitutions.  To
// indicate this, the format_str will be NULL.
static inline LogFormat *
MakeTextLogFormat(const char *name = "text")
{
  return new LogFormat(name, nullptr /* format_str */);
}

/*-------------------------------------------------------------------------
  LogFormatList
  -------------------------------------------------------------------------*/

class LogFormatList
{
public:
  LogFormatList();
  ~LogFormatList();

  void add(LogFormat *format, bool copy = true);
  LogFormat *find_by_name(const char *name) const;

  LogFormat *
  first() const
  {
    return m_format_list.head;
  }
  LogFormat *
  next(LogFormat *here) const
  {
    return (here->link).next;
  }

  void clear();
  unsigned count();
  void display(FILE *fd = stdout);

  // noncopyable
  // -- member functions that are not allowed --
  LogFormatList(const LogFormatList &rhs) = delete;
  LogFormatList &operator=(const LogFormatList &rhs) = delete;

private:
  Queue<LogFormat> m_format_list;
};
