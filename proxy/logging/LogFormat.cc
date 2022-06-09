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
 LogFormat.cc


 ***************************************************************************/
#include "tscore/ink_config.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "tscore/SimpleTokenizer.h"
#include "tscore/CryptoHash.h"

#include "LogUtils.h"
#include "LogFile.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogBuffer.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "Log.h"

// class variables
//
bool LogFormat::m_tagging_on = false;

/*-------------------------------------------------------------------------
  LogFormat::setup
  -------------------------------------------------------------------------*/

bool
LogFormat::setup(const char *name, const char *format_str, unsigned interval_sec)
{
  if (name == nullptr) {
    Note("missing log format name");
    return false;
  }

  if (format_str) {
    const char *tag                = " %<phn>";
    const size_t m_format_str_size = strlen(format_str) + (m_tagging_on ? strlen(tag) : 0) + 1;
    m_format_str                   = static_cast<char *>(ats_malloc(m_format_str_size));
    ink_strlcpy(m_format_str, format_str, m_format_str_size);
    if (m_tagging_on) {
      Note("Log tagging enabled, adding %%<phn> field at the end of "
           "format %s",
           name);
      ink_strlcat(m_format_str, tag, m_format_str_size);
    };

    char *printf_str    = nullptr;
    char *fieldlist_str = nullptr;
    int nfields         = parse_format_string(m_format_str, &printf_str, &fieldlist_str);
    if (nfields > (m_tagging_on ? 1 : 0)) {
      init_variables(name, fieldlist_str, printf_str, interval_sec);
    } else {
      Note("Format %s encountered an error parsing the symbol string "
           "\"%s\", symbol string contains no fields",
           ((name) ? name : "no-name"), format_str);
      m_valid = false;
    }

    ats_free(fieldlist_str);
    ats_free(printf_str);

    // We are only valid if init_variables() says we are.
    return true;
  }

  // We don't have a format string (ie. this will be a raw text log, so we are always valid.
  m_valid = true;
  return true;
}

/*-------------------------------------------------------------------------
  LogFormat::id_from_name
  -------------------------------------------------------------------------*/

int32_t
LogFormat::id_from_name(const char *name)
{
  int32_t id = 0;
  if (name) {
    CryptoHash hash;
    CryptoContext().hash_immediate(hash, name, static_cast<int>(strlen(name)));
#if defined(linux)
    /* Mask most significant bit so that return value of this function
     * is not sign extended to be a negative number.
     * This problem is only known to occur on Linux which
     * is a 32-bit OS.
     */
    id = static_cast<int32_t>(hash.fold()) & 0x7fffffff;
#else
    id = (int32_t)hash.fold();
#endif
  }
  return id;
}

/*-------------------------------------------------------------------------
  LogFormat::init_variables
  -------------------------------------------------------------------------*/

void
LogFormat::init_variables(const char *name, const char *fieldlist_str, const char *printf_str, unsigned interval_sec)
{
  m_field_count = parse_symbol_string(fieldlist_str, &m_field_list, &m_aggregate);

  if (m_field_count == 0) {
    m_valid = false;
  } else if (m_aggregate && !interval_sec) {
    Note("Format for aggregate operators but no interval "
         "was specified");
    m_valid = false;
  } else {
    if (m_aggregate) {
      m_agg_marshal_space = static_cast<char *>(ats_malloc(m_field_count * INK_MIN_ALIGN));
    }

    ats_free(m_name_str);
    m_name_str = nullptr;
    m_name_id  = 0;

    if (name) {
      m_name_str = ats_strdup(name);
      m_name_id  = id_from_name(m_name_str);
    }

    ats_free(m_fieldlist_str);
    m_fieldlist_str = nullptr;
    m_fieldlist_id  = 0;

    if (fieldlist_str) {
      m_fieldlist_str = ats_strdup(fieldlist_str);
      m_fieldlist_id  = id_from_name(m_fieldlist_str);
    }

    m_printf_str    = ats_strdup(printf_str);
    m_interval_sec  = interval_sec;
    m_interval_next = LogUtils::timestamp();

    m_valid = true;
  }
}

/*-------------------------------------------------------------------------
  LogFormat::LogFormat

  This is the general ctor that builds a LogFormat object from the data
  provided.  In this case, the "fields" character string is a printf-style
  string where the field symbols are represented within the string in the
  form %<symbol>.
  -------------------------------------------------------------------------*/

LogFormat::LogFormat(const char *name, const char *format_str, unsigned interval_sec, LogEscapeType escape_type)
  : m_interval_sec(0),
    m_interval_next(0),
    m_agg_marshal_space(nullptr),
    m_valid(false),
    m_name_str(nullptr),
    m_name_id(0),
    m_fieldlist_str(nullptr),
    m_fieldlist_id(0),
    m_field_count(0),
    m_printf_str(nullptr),
    m_aggregate(false),
    m_format_str(nullptr),
    m_escape_type(escape_type)
{
  setup(name, format_str, interval_sec);

  // A LOG_FORMAT_TEXT is a log without a format string, everything else is a LOG_FORMAT_CUSTOM. It's possible that we could get
  // rid of log types altogether, but LogFile currently tests whether a format is a LOG_FORMAT_TEXT format ...
  m_format_type = format_str ? LOG_FORMAT_CUSTOM : LOG_FORMAT_TEXT;
}

/*-------------------------------------------------------------------------
  LogFormat::LogFormat

  This is the copy ctor, needed for copying lists of Format objects.
  -------------------------------------------------------------------------*/

LogFormat::LogFormat(const LogFormat &rhs)
  : RefCountObj(rhs),
    m_interval_sec(0),
    m_interval_next(0),
    m_agg_marshal_space(nullptr),
    m_valid(rhs.m_valid),
    m_name_str(nullptr),
    m_name_id(0),
    m_fieldlist_str(nullptr),
    m_fieldlist_id(0),
    m_field_count(0),
    m_printf_str(nullptr),
    m_aggregate(false),
    m_format_str(nullptr),
    m_format_type(rhs.m_format_type),
    m_escape_type(rhs.m_escape_type)
{
  if (m_valid) {
    if (m_format_type == LOG_FORMAT_TEXT) {
      m_name_str = ats_strdup(rhs.m_name_str);
    } else {
      m_format_str = rhs.m_format_str ? ats_strdup(rhs.m_format_str) : nullptr;
      init_variables(rhs.m_name_str, rhs.m_fieldlist_str, rhs.m_printf_str, rhs.m_interval_sec);
    }
  }
}

/*-------------------------------------------------------------------------
  LogFormat::~LogFormat
  -------------------------------------------------------------------------*/

LogFormat::~LogFormat()
{
  ats_free(m_name_str);
  ats_free(m_fieldlist_str);
  ats_free(m_printf_str);
  ats_free(m_agg_marshal_space);
  ats_free(m_format_str);
  m_valid = false;
}

/*-------------------------------------------------------------------------
  LogFormat::format_from_specification

  This routine is obsolete as of 3.1, but will be kept around to preserve
  the old log config file option.

  This (static) function examines the given log format specification string
  and builds a new LogFormat object if the format specification is valid.
  On success, a pointer to a LogFormat object allocated from the heap (with
  new) will be returned.  On error, NULL is returned.
  -------------------------------------------------------------------------*/

LogFormat *
LogFormat::format_from_specification(char *spec, char **file_name, char **file_header, LogFileFormat *file_type)
{
  LogFormat *format;
  char *token;
  int format_id;
  char *format_name, *format_str;

  ink_assert(file_name != nullptr);
  ink_assert(file_header != nullptr);
  ink_assert(file_type != nullptr);

  SimpleTokenizer tok(spec, ':');

  //
  // Divide the specification string into tokens using the ':' as a
  // field separator.  There are currently eight (8) tokens that comprise
  // a format specification.  Verify each of the token values and if
  // everything looks ok, then build the LogFormat object.
  //
  // First should be the "format" keyword that says this is a format spec.
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  if (strcasecmp(token, "format") == 0) {
    Debug("log-format", "this is a format");
  } else {
    Debug("log-format", "should be 'format'");
    return nullptr;
  }

  //
  // Next should be the word "enabled" or "disabled", which indicates
  // whether we should care about this format or not.
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  if (!strcasecmp(token, "disabled")) {
    Debug("log-format", "format not enabled, skipping ...");
    return nullptr;
  } else if (!strcasecmp(token, "enabled")) {
    Debug("log-format", "enabled format");
  } else {
    Debug("log-format", "should be 'enabled' or 'disabled', not %s", token);
    return nullptr;
  }

  //
  // Next should be the numeric format identifier
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  format_id = atoi(token);
  // NOW UNUSED !!!

  //
  // Next should be the format name
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  format_name = token;

  //
  // Next should be the printf-stlye format symbol string
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  format_str = token;

  //
  // Next should be the file name for the log
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  *file_name = ats_strdup(token);

  //
  // Next should be the file type, either "ASCII" or "BINARY"
  //
  token = tok.getNext();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  if (!strcasecmp(token, "ASCII")) {
    *file_type = LOG_FILE_ASCII;
  } else if (!strcasecmp(token, "BINARY")) {
    *file_type = LOG_FILE_BINARY;
  } else {
    Debug("log-format", "%s is not a valid file format (ASCII or BINARY)", token);
    return nullptr;
  }

  //
  // the rest should be the file header
  //
  token = tok.getRest();
  if (token == nullptr) {
    Debug("log-format", "token expected");
    return nullptr;
  }
  // set header to NULL if "none" was specified (a NULL header means
  // "write no header" to the rest of the logging system)
  //
  *file_header = strcmp(token, "none") == 0 ? nullptr : ats_strdup(token);

  Debug("log-format", "custom:%d:%s:%s:%s:%d:%s", format_id, format_name, format_str, *file_name, *file_type, token);

  format = new LogFormat(format_name, format_str);
  ink_assert(format != nullptr);
  if (!format->valid()) {
    delete format;
    return nullptr;
  }

  return format;
}

/*-------------------------------------------------------------------------
  LogFormat::parse_symbol_string

  This function does the work of parsing a comma-separated symbol list and
  adding them to the LogFieldList that is provided.  The total number of
  fields added to the list is returned.
  -------------------------------------------------------------------------*/

int
LogFormat::parse_symbol_string(const char *symbol_string, LogFieldList *field_list, bool *contains_aggregates)
{
  char *sym_str;
  int field_count = 0;
  LogField *f;
  char *symbol, *name, *sym, *saveptr;
  LogField::Container container;
  LogField::Aggregate aggregate;

  if (symbol_string == nullptr) {
    return 0;
  }
  ink_assert(field_list != nullptr);
  ink_assert(contains_aggregates != nullptr);

  *contains_aggregates = false; // we'll change if it does

  //
  // strtok_r will mangle the input string; we'll make a copy for that.
  //
  sym_str = ats_strdup(symbol_string);
  symbol  = strtok_r(sym_str, ",", &saveptr);

  while (symbol != nullptr) {
    //
    // See if there is an aggregate operator, which will contain "()"
    //
    char *begin_paren = strchr(symbol, '(');
    if (begin_paren) {
      char *end_paren = strchr(symbol, ')');
      if (end_paren) {
        Debug("log-agg", "Aggregate symbol: %s", symbol);
        *begin_paren = '\0';
        *end_paren   = '\0';
        name         = begin_paren + 1;
        sym          = symbol;
        Debug("log-agg", "Aggregate = %s, field = %s", sym, name);
        aggregate = LogField::valid_aggregate_name(sym);
        if (aggregate == LogField::NO_AGGREGATE) {
          Note("Invalid aggregate specification: %s", sym);
        } else {
          if (aggregate == LogField::eCOUNT && strcmp(name, "*") == 0) {
            f = Log::global_field_list.find_by_symbol("psql");
          } else {
            f = Log::global_field_list.find_by_symbol(name);
          }
          if (!f) {
            Note("Invalid field symbol %s used in aggregate "
                 "operation",
                 name);
          } else if (f->type() != LogField::sINT) {
            Note("Only single integer field types may be aggregated");
          } else {
            LogField *new_f = new LogField(*f);
            new_f->set_aggregate_op(aggregate);
            field_list->add(new_f, false);
            field_count++;
            *contains_aggregates = true;
            Debug("log-agg", "Aggregate field %s(%s) added", sym, name);
          }
        }
      } else {
        Note("Invalid aggregate field specification: no trailing "
             "')' in %s",
             symbol);
      }
    }
    //
    // Now check for a container field, which starts with '{'
    //
    else if (*symbol == '{') {
      Debug("log-format", "Container symbol: %s", symbol);
      f              = nullptr;
      char *name_end = strchr(symbol, '}');
      if (name_end != nullptr) {
        name      = symbol + 1;
        *name_end = 0;            // changes '}' to '\0'
        sym       = name_end + 1; // start of container symbol
        LogSlice slice(sym);
        Debug("log-format", "Name = %s, symbol = %s", name, sym);
        container = LogField::valid_container_name(sym);
        if (container == LogField::NO_CONTAINER) {
          Note("Invalid container specification: %s", sym);
        } else {
          f = new LogField(name, container);
          ink_assert(f != nullptr);
          if (slice.m_enable) {
            f->m_slice = slice;
            Debug("log-slice", "symbol = %s, [%d:%d]", sym, f->m_slice.m_start, f->m_slice.m_end);
          }
          field_list->add(f, false);
          field_count++;
          Debug("log-format", "Container field {%s}%s added", name, sym);
        }
      } else {
        Note("Invalid container field specification: no trailing "
             "'}' in %s",
             symbol);
      }
    }
    //
    // treat this like a regular field symbol
    //
    else {
      LogSlice slice(symbol);
      Debug("log-format", "Regular field symbol: %s", symbol);
      f = Log::global_field_list.find_by_symbol(symbol);
      if (f != nullptr) {
        LogField *cpy = new LogField(*f);
        if (slice.m_enable) {
          cpy->m_slice = slice;
          Debug("log-slice", "symbol = %s, [%d:%d]", symbol, cpy->m_slice.m_start, cpy->m_slice.m_end);
        }
        field_list->add(cpy, false);
        field_count++;
        Debug("log-format", "Regular field %s added", symbol);
      } else {
        Note("The log format symbol %s was not found in the "
             "list of known symbols.",
             symbol);
        field_list->addBadSymbol(symbol);
      }
    }

    //
    // Get the next symbol
    //
    symbol = strtok_r(nullptr, ",", &saveptr);
  }

  ats_free(sym_str);
  return field_count;
}

//
// Parse escape string, supports two forms:
//
// 1) Octal representation: '\abc', for example: '\060'
//    0 < (a*8^2 + b*8 + c) < 255
//
// 2) Hex representation: '\xab', for example: '\x3A'
//    0 < (a*16 + b) < 255
//
// Return -1 if the beginning four characters are not valid
// escape sequence, otherwise return unsigned char value of the
// escape sequence in the string.
//
// NOTE: The value of escape sequence should be greater than 0
//       and less than 255, since:
//       - 0 is terminator of string, and
//       - 255('\377') has been used as LOG_FIELD_MARKER.
//
int
LogFormat::parse_escape_string(const char *str, int len)
{
  int sum, start = 0;
  unsigned char a, b, c;

  if (str[start] != '\\' || len < 2) {
    return -1;
  }

  if (str[start + 1] == '\\') {
    return '\\';
  }
  if (len < 4) {
    return -1;
  }

  a = static_cast<unsigned char>(str[start + 1]);
  b = static_cast<unsigned char>(str[start + 2]);
  c = static_cast<unsigned char>(str[start + 3]);

  if (isdigit(a) && isdigit(b)) {
    sum = (a - '0') * 64 + (b - '0') * 8 + (c - '0');

    if (sum == 0 || sum >= 255) {
      Warning("Octal escape sequence out of range: \\%c%c%c, treat it as normal string\n", a, b, c);
      return -1;
    } else {
      return sum;
    }

  } else if (tolower(a) == 'x' && isxdigit(b) && isxdigit(c)) {
    int i, j;
    if (isdigit(b)) {
      i = b - '0';
    } else {
      i = toupper(b) - 'A' + 10;
    }

    if (isdigit(c)) {
      j = c - '0';
    } else {
      j = toupper(c) - 'A' + 10;
    }

    sum = i * 16 + j;

    if (sum == 0 || sum >= 255) {
      Warning("Hex escape sequence out of range: \\%c%c%c, treat it as normal string\n", a, b, c);
      return -1;
    } else {
      return sum;
    }
  }

  return -1;
}

/*-------------------------------------------------------------------------
  LogFormat::parse_format_string

  This function will parse a custom log format string, which is a
  combination of printf characters and logging field names, separating this
  combined format string into a normal printf string and a fieldlist.  The
  number of logging fields parsed will be returned.  The two strings
  returned are allocated with ats_malloc, and should be released by the
  caller.  The function returns -1 on error.

  For 3.1, I've added the ability to log summary information using
  aggregate operators SUM, COUNT, AVG, ...
  -------------------------------------------------------------------------*/

int
LogFormat::parse_format_string(const char *format_str, char **printf_str, char **fields_str)
{
  ink_assert(printf_str != nullptr);
  ink_assert(fields_str != nullptr);

  if (format_str == nullptr) {
    *printf_str = *fields_str = nullptr;
    return 0;
  }
  //
  // Since the given format string is a combination of the printf-string
  // and the field symbols, when we break it up into these two components
  // each is guaranteed to be smaller (or the same size) as the format
  // string.
  //
  unsigned len = static_cast<unsigned>(::strlen(format_str));
  *printf_str  = static_cast<char *>(ats_malloc(len + 1));
  *fields_str  = static_cast<char *>(ats_malloc(len + 1));

  unsigned printf_pos  = 0;
  unsigned fields_pos  = 0;
  unsigned field_count = 0;
  unsigned field_len;
  unsigned start, stop;
  int escape_char;

  for (start = 0; start < len; start++) {
    //
    // Look for logging fields: %<field>
    //
    if ((format_str[start] == '%') && (start + 1 < len) && (format_str[start + 1] == '<')) {
      //
      // this is a field symbol designation; look for the
      // trailing '>'.
      //
      if (fields_pos > 0) {
        (*fields_str)[fields_pos++] = ',';
      }
      for (stop = start + 2; stop < len; stop++) {
        if (format_str[stop] == '>') {
          break;
        }
      }
      if (format_str[stop] == '>') {
        //
        // We found the termination for this field spec;
        // copy the field symbol to the symbol string and place a
        // LOG_FIELD_MARKER in the printf string.
        //
        field_len = stop - start - 2;
        memcpy(&(*fields_str)[fields_pos], &format_str[start + 2], field_len);
        fields_pos += field_len;
        (*printf_str)[printf_pos++] = LOG_FIELD_MARKER;
        ++field_count;
        start = stop;
      } else {
        //
        // This was not a logging field spec after all,
        // then try to detect and parse escape string.
        //
        escape_char = parse_escape_string(&format_str[start], (len - start));

        if (escape_char == '\\') {
          start += 1;
          (*printf_str)[printf_pos++] = static_cast<char>(escape_char);
        } else if (escape_char >= 0) {
          start += 3;
          (*printf_str)[printf_pos++] = static_cast<char>(escape_char);
        } else {
          memcpy(&(*printf_str)[printf_pos], &format_str[start], stop - start + 1);
          printf_pos += stop - start + 1;
        }
      }
    } else {
      //
      // This was not the start of a logging field spec,
      // then try to detect and parse escape string.
      //
      escape_char = parse_escape_string(&format_str[start], (len - start));

      if (escape_char == '\\') {
        start += 1;
        (*printf_str)[printf_pos++] = static_cast<char>(escape_char);
      } else if (escape_char >= 0) {
        start += 3;
        (*printf_str)[printf_pos++] = static_cast<char>(escape_char);
      } else {
        (*printf_str)[printf_pos++] = format_str[start];
      }
    }
  }

  //
  // Ok, now NULL terminate the strings and return the number of fields
  // actually found.
  //
  (*fields_str)[fields_pos] = '\0';
  (*printf_str)[printf_pos] = '\0';

  Debug("log-format", "LogFormat::parse_format_string: field_count=%d, \"%s\", \"%s\"", field_count, *fields_str, *printf_str);
  return field_count;
}

/*-------------------------------------------------------------------------
  LogFormat::display

  Print out some info about this object.
  -------------------------------------------------------------------------*/

void
LogFormat::display(FILE *fd)
{
  static const char *types[] = {"SQUID_LOG", "COMMON_LOG", "EXTENDED_LOG", "EXTENDED2_LOG", "LOG_FORMAT_CUSTOM", "LOG_FORMAT_TEXT"};

  fprintf(fd, "--------------------------------------------------------\n");
  fprintf(fd, "Format : %s (%s) (%p), %u fields.\n", m_name_str, types[m_format_type], this, m_field_count);
  if (m_fieldlist_str) {
    fprintf(fd, "Symbols: %s\n", m_fieldlist_str);
    fprintf(fd, "Fields :\n");
    m_field_list.display(fd);
  } else {
    fprintf(fd, "Fields : None\n");
  }
  fprintf(fd, "--------------------------------------------------------\n");
}

/*-------------------------------------------------------------------------
  LogFormatList
  -------------------------------------------------------------------------*/

LogFormatList::LogFormatList() = default;

LogFormatList::~LogFormatList()
{
  clear();
}

void
LogFormatList::clear()
{
  LogFormat *f;
  while ((f = m_format_list.dequeue())) {
    delete f;
  }
}

void
LogFormatList::add(LogFormat *format, bool copy)
{
  ink_assert(format != nullptr);

  if (copy) {
    m_format_list.enqueue(new LogFormat(*format));
  } else {
    m_format_list.enqueue(format);
  }
}

LogFormat *
LogFormatList::find_by_name(const char *name) const
{
  for (LogFormat *f = first(); f; f = next(f)) {
    if (!strcmp(f->name(), name)) {
      return f;
    }
  }
  return nullptr;
}

unsigned
LogFormatList::count()
{
  unsigned cnt = 0;
  for (LogFormat *f = first(); f; f = next(f)) {
    cnt++;
  }
  return cnt;
}

void
LogFormatList::display(FILE *fd)
{
  for (LogFormat *f = first(); f; f = next(f)) {
    f->display(fd);
  }
}
