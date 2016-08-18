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

#ifndef INK_XML_H
#define INK_XML_H

#include "ts/List.h"

/*-------------------------------------------------------------------------
  InkXml.h

  This file defines the interface for parsing XML-style config files.  Such
  a file contains various XML objects, each with tags (or attibutes).  The
  parser's job is to build a database of these objects that can then be
  queried later to extract the desired information.
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  InkXmlAttr
  -------------------------------------------------------------------------*/

class InkXmlAttr
{
public:
  InkXmlAttr(const char *tag, const char *value);
  ~InkXmlAttr();

  char *
  tag() const
  {
    return m_tag;
  }
  char *
  value() const
  {
    return m_value;
  }
  void display(FILE *fd = stdout);

private:
  char *m_tag;
  char *m_value;

public:
  LINK(InkXmlAttr, link);

private:
  // -- member functions that are not allowed --
  InkXmlAttr();
  InkXmlAttr(const InkXmlAttr &);
  InkXmlAttr &operator=(InkXmlAttr &);
};

/*-------------------------------------------------------------------------
  InkXmlObject

  An XmlObject has a name and a set of <tag,value> attributes.
  -------------------------------------------------------------------------*/

class InkXmlObject
{
public:
  InkXmlObject(const char *object_name, bool dup_attrs_allowed = true);
  ~InkXmlObject();

  void clear_tags();
  int add_tag(const char *tag, const char *value);
  int add_attr(InkXmlAttr *attr);
  char *tag_value(const char *tag_name);
  void display(FILE *fd = stdout);

  char *
  object_name() const
  {
    return m_object_name;
  }
  InkXmlAttr *
  first() const
  {
    return m_tags.head;
  }
  InkXmlAttr *
  next(InkXmlAttr *here) const
  {
    return (here->link).next;
  }

private:
  char *m_object_name;
  bool m_dup_attrs_allowed;
  Queue<InkXmlAttr> m_tags;

public:
  LINK(InkXmlObject, link);

private:
  InkXmlObject *get_next_xml_object(int fd);

private:
  // -- member functions that are not allowed --
  InkXmlObject();
  InkXmlObject(const InkXmlObject &);
  InkXmlObject &operator=(InkXmlObject &);
};

/*-------------------------------------------------------------------------
  InkXmlConfigFile

  This object is used to parse an XML-style config file and create a
  database of resulting XML objects.
  -------------------------------------------------------------------------*/

class InkXmlConfigFile
{
public:
  InkXmlConfigFile(const char *config_file);
  ~InkXmlConfigFile();

  void clear_objects();
  /*                                                                     */
  int parse(int fd);
  /*                                                                     */
  int parse();
  void add_object(InkXmlObject *object);
  InkXmlObject *find_object(const char *object_name);
  void display(FILE *fd = stdout);

  InkXmlObject *
  first()
  {
    return m_objects.head;
  }
  InkXmlObject *
  next(InkXmlObject *here)
  {
    return (here->link).next;
  }

private:
  InkXmlObject *get_next_xml_object(int fd);
  InkXmlObject *parse_error();
  InkXmlObject *scan_object(int fd, char token);
  InkXmlAttr *scan_attr(int fd, const char *ident);
  char next_token(int fd, bool eat_whitespace = true);
  char scan_comment(int fd);

private:
  char *m_config_file;
  unsigned m_line;
  unsigned m_col;
  Queue<InkXmlObject> m_objects;

private:
  // -- member functions that are not allowed --
  InkXmlConfigFile();
  InkXmlConfigFile(const InkXmlConfigFile &);
  InkXmlConfigFile &operator=(InkXmlConfigFile &);
};

class NameList
{
private:
  struct ListElem {
    char *m_name;
    LINK(ListElem, link);

    ListElem(char *name) : m_name(name) {}
  };

public:
  NameList() : m_count(0) {}
  ~NameList() { clear(); }
  void
  enqueue(char *name)
  {
    ListElem *e = new ListElem(name);

    m_list.enqueue(e);
    m_count++;
  }

  char *
  dequeue()
  {
    char *ret   = NULL;
    ListElem *e = m_list.dequeue();

    if (e) {
      ret = e->m_name;
      delete e;
      m_count--;
    }
    return ret;
  }

  void
  clear()
  {
    ListElem *e;

    while ((e = m_list.dequeue()) != NULL) {
      delete e;
    }
    m_count = 0;
  }

  unsigned
  count()
  {
    return m_count;
  }

private:
  Queue<ListElem> m_list;
  unsigned m_count;
};

#endif /* INK_XML_H */
