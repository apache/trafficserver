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
 GenericParser.h


 ***************************************************************************/
#pragma once

#include <cstring>
#include "ts/ink_assert.h"
#include "ts/Tokenizer.h"
#include "ts/List.h"
#include "mgmtapi.h" // INKFileNameT

#define MIN_CONFIG_TOKEN 1
#define MAX_CONFIG_TOKEN 30

/***************************************************************************
 * Token
 *   a token is a name/value pairs of data. The data are of the type char *.
 *   The setName and setValue are not meant for data encapsulation, but
 *   free callers from allocating the memory.
 ***************************************************************************/
class Token
{
public:
  Token();
  Token(char *);
  ~Token();
  void setName(const char *);
  void setValue(const char *);
  void appendValue(const char *);
  void Print();

  char *name;
  char *value;
  LINK(Token, link);
};

/***************************************************************************
 * TokenList
 *   a token list is a list of token (obviously). It uses List.h to link
 *   the tokens together. This object includes some queue and stack manlipuation
 *   function calls in addition to the common length() and the debugging
 *   print() member functions.
 ***************************************************************************/
class TokenList
{
public:
  TokenList();
  ~TokenList();

  unsigned length;

  /**
   * Queue method
   */
  inline Token *
  first()
  {
    return m_nameList.head;
  };
  inline Token *
  last()
  {
    return m_nameList.tail;
  };
  inline void
  enqueue(Token *entry)
  {
    length++;
    m_nameList.enqueue(entry);
  };
  inline Token *
  dequeue()
  {
    length--;
    return (Token *)m_nameList.dequeue();
  };
  inline void
  remove(Token *entry)
  {
    length--;
    m_nameList.remove(entry);
  };
  /**
   * Stack method
   */
  inline Token *
  top()
  {
    return m_nameList.tail;
  };
  inline Token *
  bottom()
  {
    return m_nameList.head;
  };
  inline void
  push(Token *entry)
  {
    length++;
    m_nameList.push(entry);
  };
  inline Token *
  pop()
  {
    length--;
    return m_nameList.pop();
  };
  /**
   * General method
   */
  inline void
  insert(Token *newNode, Token *current)
  {
    m_nameList.insert(newNode, current);
  };
  inline Token *
  next(Token *current)
  {
    return (current->link).next;
  };

  void Print(); /* debugging use only */

private:
  Queue<Token> m_nameList;
};

/***************************************************************************
 * Rule
 *   a rule is nothing more than just a token list. This object also
 *   contains a very important member function -- parse(). Depending on
 *   on the configuration file type, a specific parse function is invoked.
 *   Since the user of "Rule" are not expected to access the member data
 *   driectly, the get/set member functions are used for data encapsulation.
 ***************************************************************************/
class Rule
{
public:
  TokenList *tokenList;

  inline TSFileNameT
  getFiletype()
  {
    return m_filetype;
  };
  void setRuleStr(const char *);
  inline char *
  getRuleStr()
  {
    return m_ruleStr;
  };
  void setComment(const char *);
  inline char *
  getComment()
  {
    return m_comment;
  };
  void setErrorHint(const char *);
  inline char *
  getErrorHint()
  {
    return m_errorHint;
  };

  Rule();
  ~Rule();
  void Print();
  TokenList *parse(const char *buf, TSFileNameT filetype);

  LINK(Rule, link);

private:
  TSFileNameT m_filetype;
  char *m_filename;
  char *m_ruleStr;
  char *m_comment;
  char *m_errorHint;

  TokenList *cacheParse(char *rule, unsigned short minNumToken = MIN_CONFIG_TOKEN, unsigned short maxNumToken = MAX_CONFIG_TOKEN);
  TokenList *congestionParse(char *rule, unsigned short minNumToken = MIN_CONFIG_TOKEN,
                             unsigned short maxNumToken = MAX_CONFIG_TOKEN);
  TokenList *parentParse(char *rule);
  TokenList *remapParse(char *rule);
  TokenList *socksParse(char *rule);
  TokenList *splitdnsParse(char *rule);
  TokenList *updateParse(char *rule);
  TokenList *arm_securityParse(char *rule);
  TokenList *hostingParse(char *rule);
  TokenList *ip_allowParse(char *rule);
  TokenList *volumeParse(char *rule);
  TokenList *logsParse(char *rule);
  TokenList *pluginParse(char *rule);
  TokenList *storageParse(char *rule);
  TokenList *log_hostsParse(char *rule);
  bool inQuote(const char *str);
};

/***************************************************************************
 * RuleList
 *   a rule list is a list of rule; which compose to a configuration file.
 *   Again some queue and stack manlipation member functions are included.
 *   The important function is this object is also parse() which breaks up
 *   the file into individual lines and passes on to the Rule object to
 *   continue parsing each rule.
 *   NOTE: a rule that spans more than one line would be a problem in here.
 ***************************************************************************/
class RuleList
{
public:
  RuleList();
  ~RuleList();

  unsigned length;

  /**
   * Queue method
   */
  inline Rule *
  first()
  {
    return m_lineList.head;
  }
  inline Rule *
  last()
  {
    return m_lineList.tail;
  }
  inline void
  enqueue(Rule *entry)
  {
    if (!entry->getComment()) {
      length++;
    }
    m_lineList.enqueue(entry);
  }
  inline Rule *
  dequeue()
  {
    length--;
    return (Rule *)m_lineList.dequeue();
  }
  /**
   * Stack method
   */
  inline Rule *
  top()
  {
    return m_lineList.tail;
  }
  inline Rule *
  bottom()
  {
    return m_lineList.head;
  }
  inline void
  push(Rule *entry)
  {
    if (!entry->getComment()) {
      length++;
    }
    m_lineList.push(entry);
  }
  inline Rule *
  pop()
  {
    length--;
    return m_lineList.pop();
  }
  /**
   * General method
   */
  inline Rule *
  next(Rule *current)
  {
    return (current->link).next;
  }
  inline void
  insert(Rule *newNode, Rule *current)
  {
    m_lineList.insert(newNode, current);
  }
  void Print(); /* debugging use only */
  void parse(char *buf, TSFileNameT filetype);
  void parse(char *buf, const char *filename);

private:
  TSFileNameT m_filetype;
  char *m_filename;
  Queue<Rule> m_lineList;
};

/*****************************************************************************************
 * General Routines
 *****************************************************************************************/
// char *strtrim(char *);
const char *strtrim(const char *, char chr = ' ');
