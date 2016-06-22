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
 InkXml.cc


 ***************************************************************************/
#include "ts/ink_platform.h"
#include "ts/Diags.h"
#include "ts/ParseRules.h"

#include "InkXml.h"

/*-------------------------------------------------------------------------
  InkXmlAttr
  -------------------------------------------------------------------------*/

InkXmlAttr::InkXmlAttr(const char *tag, const char *value)
{
  m_tag   = ats_strdup(tag);
  m_value = ats_strdup(value);
}

InkXmlAttr::~InkXmlAttr()
{
  ats_free(m_tag);
  ats_free(m_value);
}

void
InkXmlAttr::display(FILE *fd)
{
  fprintf(fd, "    <%s,%s>\n", m_tag, m_value);
}

/*-------------------------------------------------------------------------
  InkXmlObject
  -------------------------------------------------------------------------*/

InkXmlObject::InkXmlObject(const char *object_name, bool dup_attrs_allowed)
{
  m_object_name       = ats_strdup(object_name);
  m_dup_attrs_allowed = dup_attrs_allowed;
}

InkXmlObject::~InkXmlObject()
{
  ats_free(m_object_name);
  clear_tags();
}

void
InkXmlObject::clear_tags()
{
  InkXmlAttr *attr;
  while ((attr = m_tags.dequeue())) {
    delete attr;
  }
}

int
InkXmlObject::add_tag(const char *tag, const char *value)
{
  ink_assert(tag != NULL);
  ink_assert(value != NULL);

  InkXmlAttr *attr = new InkXmlAttr(tag, value);
  return add_attr(attr);
}

int
InkXmlObject::add_attr(InkXmlAttr *attr)
{
  ink_assert(attr != NULL);

  if (!m_dup_attrs_allowed) {
    for (InkXmlAttr *a = first(); a; a = next(a)) {
      if (!strcmp(a->tag(), attr->tag())) {
        Debug("xml", "tag %s already exists & dups not allowed", attr->tag());
        return -1;
      }
    }
  }
  m_tags.enqueue(attr);
  return 0;
}

char *
InkXmlObject::tag_value(const char *tag_name)
{
  ink_assert(tag_name != NULL);

  for (InkXmlAttr *a = first(); a; a = next(a)) {
    if (!strcmp(a->tag(), tag_name)) {
      return a->value();
    }
  }
  return NULL;
}

void
InkXmlObject::display(FILE *fd)
{
  fprintf(fd, "<%s>\n", m_object_name);
  for (InkXmlAttr *a = first(); a; a = next(a)) {
    a->display(fd);
  }
}

/*-------------------------------------------------------------------------
  InkXmlConfigFile
  -------------------------------------------------------------------------*/

InkXmlConfigFile::InkXmlConfigFile(const char *config_file) : m_line(0), m_col(0)
{
  m_config_file = ats_strdup(config_file);
}

InkXmlConfigFile::~InkXmlConfigFile()
{
  ats_free(m_config_file);
  clear_objects();
}

void
InkXmlConfigFile::clear_objects()
{
  InkXmlObject *obj;
  while ((obj = m_objects.dequeue())) {
    delete obj;
  }
}

/*                                                                  */
int
InkXmlConfigFile::parse(int fd)
{
  ink_assert(fd >= 0);
  Debug("log", "Parsing XML config info from memory..");

  m_line = 1;
  m_col  = 0;

  InkXmlObject *obj;
  while ((obj = get_next_xml_object(fd)) != NULL) {
    Debug("log", "Adding XML object <%s>", obj->object_name());
    add_object(obj);
  }

  return 0;
}

/*                                                                  */

int
InkXmlConfigFile::parse()
{
  ink_assert(m_config_file != NULL);
  Debug("xml", "Parsing XML config file %s ...", m_config_file);

  int fd = ::open(m_config_file, O_RDONLY);
  if (fd < 0) {
    Debug("xml", "Error opening %s: %d, %s", m_config_file, fd, strerror(errno));
    return -1;
  }

  m_line = 1;
  m_col  = 0;

  InkXmlObject *obj;
  while ((obj = get_next_xml_object(fd)) != NULL) {
    Debug("xml", "Adding XML object <%s>", obj->object_name());
    add_object(obj);
  }

  ::close(fd);
  return 0;
}

InkXmlObject *
InkXmlConfigFile::find_object(const char *object_name)
{
  for (InkXmlObject *obj = first(); obj; obj = next(obj)) {
    if (!strcmp(object_name, obj->object_name())) {
      return obj;
    }
  }
  return NULL;
}

void
InkXmlConfigFile::display(FILE *fd)
{
  size_t i;

  fprintf(fd, "\n");
  for (i = 0; i < strlen(m_config_file) + 13; i++)
    fputc('-', fd);
  fprintf(fd, "\nConfig File: %s\n", m_config_file);
  for (i = 0; i < strlen(m_config_file) + 13; i++)
    fputc('-', fd);
  fprintf(fd, "\n");
  for (InkXmlObject *obj = first(); obj; obj = next(obj)) {
    obj->display(fd);
    fprintf(fd, "\n");
  }
}

void
InkXmlConfigFile::add_object(InkXmlObject *object)
{
  ink_assert(object != NULL);
  m_objects.enqueue(object);
}

/*-------------------------------------------------------------------------
  InkXmlConfigFile::get_next_xml_object()

  This routine (and its friends) does the real work of parsing the given
  open file for the next XML object.
  -------------------------------------------------------------------------*/

InkXmlObject *
InkXmlConfigFile::get_next_xml_object(int fd)
{
  ink_assert(fd >= 0);

  char token;
  bool start_object = false;

  while ((token = next_token(fd)) != EOF) {
    switch (token) {
    case '<':
      start_object = true;
      break;

    case '!':
      if (!start_object)
        return parse_error();
      if ((token = scan_comment(fd)) == EOF) {
        return NULL;
      }
      Debug("xml", "comment scanned");
      start_object = false;
      break;

    default:
      if (!start_object)
        return parse_error();
      return scan_object(fd, token);
    }
  }
  return NULL;
}

InkXmlObject *
InkXmlConfigFile::parse_error()
{
  Debug("xml", "Invalid XML tag, line %u, col %u", m_line, m_col);
  return NULL;
}

#define BAD_ATTR ((InkXmlAttr *)1)

InkXmlObject *
InkXmlConfigFile::scan_object(int fd, char token)
{
  // this routine is called just after the first '<' is read for a new
  // object.

  const int max_ident_len = 2048;
  char ident[max_ident_len];
  int ident_len = 0;

  while (token != '>' && ident_len < max_ident_len) {
    ident[ident_len++] = token;
    token              = next_token(fd);
    if (token == EOF)
      return parse_error();
  }
  if (!ident_len || ident_len >= max_ident_len) {
    return parse_error();
  }

  ident[ident_len]  = 0;
  InkXmlObject *obj = new InkXmlObject(ident);
  ink_assert(obj != NULL);

  InkXmlAttr *attr;
  while ((attr = scan_attr(fd, ident)) != NULL) {
    if (attr == BAD_ATTR) {
      delete obj;
      return parse_error();
    }
    obj->add_attr(attr);
  }

  return obj;
}

InkXmlAttr *
InkXmlConfigFile::scan_attr(int fd, const char *id)
{
  // this routine is called after the object identifier has been scannedm
  // and should attempt to scan for the next attribute set.  When we see
  // the end of the object (closing identifier), we scan it and return
  // NULL for the attribute, signalling that there are no more
  // attributes.

  char token, prev, next;
  const int buf_size = 2048;
  char name[buf_size];
  char value[buf_size];
  char ident[buf_size];
  char *write_to   = NULL;
  int write_len    = 0;
  bool start_attr  = false;
  bool in_quotes   = false;
  InkXmlAttr *attr = NULL;

  prev = next = 0;
  while ((token = next_token(fd, !in_quotes)) != EOF) {
    switch (token) {
    case '<':
      if (in_quotes && write_to) {
        if (write_len >= buf_size)
          return BAD_ATTR;
        write_to[write_len++] = token;
        break;
      }
      start_attr = true;
      write_to   = name;
      write_len  = 0;
      break;

    case '=':
      if (in_quotes && write_to) {
        if (write_len >= buf_size)
          return BAD_ATTR;
        write_to[write_len++] = token;
        break;
      }
      if (!start_attr)
        return BAD_ATTR;
      write_to[write_len] = 0;
      write_to            = value;
      write_len           = 0;
      break;

    case '"':
      if (in_quotes) {
        if (prev == '\\') {
          // escape the quote, just replace the backslash
          // with it
          write_to[write_len - 1] = token;
          break;
        }
      }
      in_quotes = !in_quotes;
      break;

    case '/':
      if (in_quotes && write_to) {
        if (write_len >= buf_size)
          return BAD_ATTR;
        write_to[write_len++] = token;
        break;
      }
      if (!start_attr)
        return BAD_ATTR;
      if (prev == '<') {
        write_len = 0;
        token     = next_token(fd, !in_quotes);
        while (token != '>' && write_len < buf_size) {
          ident[write_len++] = token;
          token              = next_token(fd, !in_quotes);
          if (token == EOF)
            return BAD_ATTR;
        }
        if (!write_len || write_len >= buf_size) {
          return BAD_ATTR;
        }
        ident[write_len] = 0;
        if (strcmp(ident, id) != 0)
          return BAD_ATTR;
        return NULL;
      }

      next = next_token(fd, !in_quotes);
      if (next != '>')
        return BAD_ATTR;
      write_to[write_len] = 0;
      attr                = new InkXmlAttr(name, value);
      ink_assert(attr != NULL);
      return attr;

    case '>':
      if (in_quotes && write_to) {
        if (write_len >= buf_size)
          return BAD_ATTR;
        write_to[write_len++] = token;
        break;
      }
      // seen at this point, this is an error, probably becase
      // the person forgot the trailing '/'.
      return BAD_ATTR;

    default:
      if (!start_attr)
        return BAD_ATTR;
      if (write_len >= buf_size)
        return BAD_ATTR;
      write_to[write_len++] = token;
      break;
    }
    prev = token;
  }
  return BAD_ATTR;
}

char
InkXmlConfigFile::next_token(int fd, bool eat_whitespace)
{
  char ch;
  while (read(fd, &ch, 1) == 1) {
    if (ch == '\n') {
      m_line++;
      m_col = 0;
      continue;
    }
    m_col++;
    if (eat_whitespace && ParseRules::is_space(ch))
      continue;
    return ch;
  }
  return EOF;
}

char
InkXmlConfigFile::scan_comment(int fd)
{
  // this routine is called when we're just past a "<!" in the file.  we
  // need to skip until we find the matching '>'.

  int lt_stack = 1; // we've already seen one '<' character
  char token;
  while ((token = next_token(fd)) != EOF) {
    switch (token) {
    case '<':
      lt_stack++;
      break;
    case '>':
      lt_stack--;
      if (lt_stack == 0) {
        return token;
      }
      break;
    default:
      break;
    }
  }
  return EOF;
}
