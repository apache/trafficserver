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



#ifndef LOG_FORMAT_H
#define LOG_FORMAT_H

#define LOG_FIELD_MARKER      	'\377'

#include "inktomi++.h"
#include "LogField.h"
#include "LogFormatType.h"
#include "InkXml.h"

/*-------------------------------------------------------------------------
  LogFormat

  Ok, as of 3.1, the role of the LogFormat object changes greatly.  Before
  it was the central object in the logging system that defined each "file"
  that would be created, and all other objects hung off of it.

  Now, this object will simply store the characteristics of a log format,
  which is defined as a set of fields.
  -------------------------------------------------------------------------*/

class LogFormat
{
public:
  LogFormat(LogFormatType type);
  LogFormat(const char *name, const char *format_str, unsigned interval_sec = 0);
  LogFormat(const char *name, const char *fieldlist_str, const char *printf_str, unsigned interval_sec = 0);
  LogFormat(const LogFormat & rhs);
  ~LogFormat();

  void display(FILE * fd = stdout);
  void displayAsXML(FILE * fd = stdout);

  bool valid()
  {
    return m_valid;
  }
  char *name()
  {
    return m_name_str;
  }
  char *fieldlist()
  {
    return m_fieldlist_str;
  }
  char *format_string()
  {
    return m_format_str;
  };
  ink32 name_id()
  {
    return m_name_id;
  }
  unsigned fieldlist_id()
  {
    return m_fieldlist_id;
  }
  LogFormatType type()
  {
    return m_format_type;
  }
  char *printf_str()
  {
    return m_printf_str;
  }
  bool is_aggregate()
  {
    return m_aggregate;
  }
  unsigned field_count()
  {
    return m_field_count;
  }
  long interval()
  {
    return m_interval_sec;
  };

public:
  static ink32 id_from_name(const char *name);
  static LogFormat *format_from_specification(char *spec,
                                              char **file_name, char **file_header, LogFileFormat * file_type);
  static int parse_symbol_string(const char *symbol_string, LogFieldList *field_list, bool *contains_aggregates);
  static int parse_format_string(const char *format_str, char **printf_str, char **fields_str);

  // these are static because m_tagging_on is a class variable
  //
  static void turn_tagging_on()
  {
    m_tagging_on = true;
  };
  static void turn_tagging_off()
  {
    m_tagging_on = false;
  };

private:
  void setup(const char *name, const char *format_str, unsigned interval_sec = 0);
  void init_variables(const char *name, const char *fieldlist_str, const char *printf_str, unsigned interval_sec);

public:
  LogFieldList m_field_list;
  long m_interval_sec;
  long m_interval_next;
  char *m_agg_marshal_space;

  static const char *const squid_format;        // pre defined formats
  static const char *const common_format;
  static const char *const extended_format;
  static const char *const extended2_format;

private:
  static bool m_tagging_on;     // flag to control tagging, class
  // variable
  bool m_valid;
  char *m_name_str;
  ink32 m_name_id;
  char *m_fieldlist_str;
  unsigned m_fieldlist_id;
  unsigned m_field_count;
  char *m_printf_str;
  bool m_aggregate;
  char *m_format_str;
  LogFormatType m_format_type;

public:
  LINK(LogFormat, link);

private:
  // -- member functions that are not allowed --
  LogFormat();
  LogFormat & operator=(LogFormat & rhs);
};

/*-------------------------------------------------------------------------
  LogFormatList
  -------------------------------------------------------------------------*/

class LogFormatList
{
public:
  LogFormatList();
  ~LogFormatList();

  void add(LogFormat * format, bool copy = true);
  LogFormat *find_by_name(const char *name) const;
  LogFormat *find_by_type(LogFormatType type, ink32 id) const;

  LogFormat *first() const
  {
    return m_format_list.head;
  }
  LogFormat *next(LogFormat * here) const
  {
    return (here->link).next;
  }
  void clear();
  unsigned count();
  void display(FILE * fd = stdout);
private:
  Queue<LogFormat> m_format_list;

  // -- member functions that are not allowed --
  LogFormatList(const LogFormatList & rhs);
  LogFormatList & operator=(const LogFormatList & rhs);
};

#endif
