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

#include "inktomi++.h"
#include "GenericParser.h"

/***************************************************************************
 * Token
 *   a token is a name/value pairs of data. The data are of the type char *.
 *   The setName and setValue are not meant for data encapsulation, but
 *   free callers from allocating the memory.
 ***************************************************************************/

Token::Token():name(NULL), value(NULL)
{
}

Token::~Token()
{
  if (name) {
    xfree(name);
  }
  if (value) {
    xfree(value);
  }
}

void
Token::setName(const char *str)
{
  name = (char *) strtrim(str);
  //name = xstrdup(str_copy); DOESN't WORK
}

//
// need to allocate more memory than the actual str len in case
// more characters are appended to the value
//
void
Token::setValue(const char *str)
{
  char *str_copy = (char *) strtrim(str);
  // Can't use xstrdup after strtrim?
  //  value = xstrdup(str);
  ink_assert(value == NULL);
  if (str_copy) {
    size_t len = strlen(str_copy);
    value = (char *) xmalloc(sizeof(char) * (BUFSIZ));
    len = (len < BUFSIZ) ? len : BUFSIZ - 1;
    memcpy(value, str_copy, len);
    value[len] = '\0';
    xfree(str_copy);
  }
}

void
Token::appendValue(const char *str)
{
  char *str_copy = (char *) strtrim(str);
  static bool firstTime = true;
  if (value == NULL) {
    setValue(str_copy);
  } else {
    if (!firstTime) {
      strncat(value, " ", BUFSIZ - strlen(value) - 1);
    }
    strncat(value, str_copy, BUFSIZ - strlen(value) - 1);;
  }
  firstTime = false;
  if (str_copy)
    xfree(str_copy);
}

void
Token::Print()
{
  ink_assert(name != NULL);
  printf(" (%s", name);
  if (value != NULL) {
    printf(", %s", value);
  }
  printf("),");
}

/***************************************************************************
 * TokenList
 *   a token list is a list of token (obviously). It uses List.h to link
 *   the tokens together. This object includes some queue and stack manlipuation
 *   function calls in addition to the common length() and the debugging
 *   print() member functions.
 ***************************************************************************/
TokenList::TokenList():length(0)
{
}

TokenList::~TokenList()
{
  Token *token = NULL;
  while ((token = dequeue())) {
    delete(token);
  }
}

void
TokenList::Print()
{
  printf("\tRULE -->");
  for (Token * token = first(); token; token = next(token)) {
    token->Print();
  }
  printf("\n");
}

/***************************************************************************
 * Rule
 *   a rule is nothing more than just a token list. This object also
 *   contains a very important member function -- parse(). Depending on
 *   on the configuration file type, a specific parse function is invoked.
 *   Since the user of "Rule" are not expected to access the member data
 *   driectly, the get/set member functions are used for data encapsulation.
 ***************************************************************************/

Rule::Rule():
tokenList(NULL),
m_filetype(INK_FNAME_UNDEFINED), m_filename(NULL), m_ruleStr(NULL), m_comment(NULL), m_errorHint(NULL), m_lineNum(0)
{
}

void
Rule::setRuleStr(const char *str)
{
  ink_assert(m_comment == NULL);
  m_ruleStr = xstrdup(str);
}

void
Rule::setComment(const char *str)
{
  ink_assert(m_comment == NULL);
  m_comment = xstrdup(str);
}

void
Rule::setErrorHint(const char *str)
{
  ink_assert(m_errorHint == NULL);
  m_errorHint = xstrdup(str);
}

Rule::~Rule()
{
  if (tokenList) {
    delete(tokenList);
  }
  if (m_comment) {
    xfree(m_comment);
  }
  if (m_errorHint) {
    xfree(m_errorHint);
  }
  if (m_ruleStr) {
    xfree(m_ruleStr);
  }
  if (m_filename) {
    xfree(m_filename);
  }
}

void
Rule::Print()
{
  if (tokenList) {
    tokenList->Print();
  }
  if (m_errorHint) {
    printf("\treason: %s\n", m_errorHint);
  }
}

TokenList *
Rule::parse(const char *const_rule, INKFileNameT filetype)
{
  char *rule = (char *) const_rule;
  m_filetype = filetype;

  switch (m_filetype) {
  case INK_FNAME_ADMIN_ACCESS: /* admin_access.config */
    return admin_accessParse(rule);
  case INK_FNAME_CACHE_OBJ:    /* cache.config */
    return cacheParse(rule);
  case INK_FNAME_CONGESTION:   /* congestion.config */
    return congestionParse(rule, 1, 15);
  case INK_FNAME_FILTER:       /* filter.config */
    return filterParse(rule);
  case INK_FNAME_HOSTING:      /* hosting.config */
    return hostingParse(rule);
  case INK_FNAME_ICP_PEER:     /* icp.config */
    return icpParse(rule, 8, 8);
  case INK_FNAME_IP_ALLOW:     /* ip_allow.config */
    return ip_allowParse(rule);
  case INK_FNAME_LOGS_XML:     /* logs_xml.config */
    return logs_xmlParse(rule);
  case INK_FNAME_MGMT_ALLOW:   /* mgmt_allow.config */
    return mgmt_allowParse(rule);
  case INK_FNAME_PARENT_PROXY: /* parent.config */
    return parentParse(rule);
  case INK_FNAME_PARTITION:    /* partition.config */
    return partitionParse(rule);
  case INK_FNAME_PLUGIN:       /* plugin.config */
    return pluginParse(rule);
  case INK_FNAME_REMAP:        /* remap.config */
    return remapParse(rule);
  case INK_FNAME_SOCKS:        /* socks.config */
    return socksParse(rule);
  case INK_FNAME_SPLIT_DNS:    /* splitdns.config */
    return splitdnsParse(rule);
  case INK_FNAME_STORAGE:      /* storage.config */
    return storageParse(rule);
  case INK_FNAME_UPDATE_URL:   /* update.config */
    return updateParse(rule);
  case INK_FNAME_VADDRS:       /* vaddrs.config */
    return vaddrsParse(rule);
  default:
    return NULL;
  }
}


/**
 * admin_accessParse
 **/
TokenList *
Rule::admin_accessParse(char *rule)
{
  return icpParse(rule, 3, 3);
}


/**
 * arm_securityParse
 **/
TokenList *
Rule::arm_securityParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token = (Token *) NULL;
  TokenList *m_tokenList = NEW(new TokenList());

  // ASSUMPTIONS:
  //   every token starts with a non-digit character is a "name"
  //   every token starts with a digit is a "value" or part of a "value"
  //   NO SPACE for port/ip range
  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {

    // If 1st element is non-digit, it is a value
    if (!ParseRules::is_digit(tokenStr[0])) {

      // it is a name
      if (token != (Token *) NULL) {
        // We have a token that hasn't been enqueue, enqueue it
        m_tokenList->enqueue(token);
      }

      token = NEW(new Token()); // Create a new token
      token->setName(tokenStr); // Set token Name
    } else if (token != (Token *) NULL) {
      // it is a value or part of a value
      token->appendValue(tokenStr);     // ISA port# or IP; append to value
    }
  }

  if (token != (Token *) NULL) {        // Enqueue the last token -- we haven't done it yet.
    m_tokenList->enqueue(token);
  }

  return m_tokenList;
}


/**
 * bypassSubParse
 **/
Token *
Rule::bypassSubParse(char *rule, unsigned short minNumToken, unsigned short maxNumToken)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);

  // Sanity Check -- must have more than 2 tokens, otherwise bail out
  if (numRuleTok < minNumToken) {
    this->setErrorHint("Extra token(s) expected!");
    return NULL;
  }

  Token *token = NEW(new Token());
  // First token -- either "src" or "dst"
  token->setName(tokenStr);

  // The rest is an IP address or a list of IP address
  while ((tokenStr = ruleTok.iterNext(&ruleTok_state))) {
    token->appendValue(tokenStr);
  }

  return (token);
}


/**
 * bypassParse
 *   ASSUMPTIONS:
 *     + each token is separated by either "\s", "\t" or ","
 *     + each rule has at least 2 tokens
 **/
TokenList *
Rule::bypassParse(char *rule, unsigned short minNumToken, unsigned short maxNumToken)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);

  // Sanity Check -- must have more than 3 tokens, otherwise bail out
  if (numRuleTok < minNumToken) {
    this->setErrorHint("Expecting more tokens");
    return NULL;
  }
  TokenList *m_tokenList = NEW(new TokenList());
  Token *token = NEW(new Token());

  // First token -- "bypass", no "VALUE"
  token->setName(tokenStr);
  m_tokenList->enqueue(token);

  // Skip first token
  rule += strlen(tokenStr);

  char *andToken = NULL;
  if (!(andToken = strstr(rule, "AND")) && !(andToken = strstr(rule, "and"))) {
    // Not an AND rule
    m_tokenList->enqueue(bypassSubParse(rule));
  } else {

    // Chop rule into two parts: rule & and
    rule[strlen(rule) - strlen(andToken)] = '\0';

    m_tokenList->enqueue(bypassSubParse(rule));
    m_tokenList->enqueue(bypassSubParse(andToken + strlen("AND")));
  }

  return m_tokenList;
}


/**
 * cacheParse
 * CAUTION: This function is used for number of similar formatted
 *          configureation file parsing. Modification to this function
 *          will manifest changes to other parsing function.
 **/
TokenList *
Rule::cacheParse(char *rule, unsigned short minNumToken, unsigned short maxNumToken)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token = NULL;
  bool insideQuote = false;
  const char *newStr;

  // Sanity Check -- number of token
  if (numRuleTok < minNumToken) {
    setErrorHint("Expecting more space delimited tokens!");
    return NULL;
  }
  if (numRuleTok > maxNumToken) {
    setErrorHint("Expecting less space delimited tokens!");
    return NULL;
  }
  // Sanity Check -- no space before or after '='
  if (strstr(rule, " =")) {
    setErrorHint("Expected space before '='");
    return NULL;
  }
  if (strstr(rule, "= ")) {
    setErrorHint("Expected space after '='");
    return NULL;
  }

  TokenList *m_tokenList = NEW(new TokenList());

  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {

    if (!insideQuote) {
      Tokenizer subruleTok("=");
      int numSubRuleTok = subruleTok.Initialize(tokenStr);
      tok_iter_state subruleTok_state;
      const char *subtoken = subruleTok.iterFirst(&subruleTok_state);

      // Every token must have a '=' sign
      if (numSubRuleTok < 2) {
        setErrorHint("'=' is expected in space-delimited token");
        return NULL;
      }

      token = NEW(new Token());
      /* set the name */
      token->setName(subtoken);

      /* set the value */
      if (numSubRuleTok == 2) {
        subtoken = subruleTok.iterNext(&subruleTok_state);
      } else {
        /* Ignore the first "=" sign and treat the rest as one token
           for INKqa09488 */
        const char *secondEqual = strstr(strstr(tokenStr, "="), "=");
        secondEqual++;
        subtoken = xstrdup(secondEqual);
      }
      insideQuote = inQuote(subtoken);

      newStr = strtrim(subtoken, '\"');
      if (!insideQuote) {
        //          printf("!insideQuote: %s\n", subtoken);
        token->setValue(newStr);
        m_tokenList->enqueue(token);
      } else {
        //          printf("insideQuote: %s\n", subtoken);
        //          printf("%s 1\n", subtoken);
        token->appendValue(newStr);
      }
      xfree((char *) newStr);

    } else {
      //      printf("%s 2\n", tokenStr);
      newStr = strtrim(tokenStr, '\"');
      token->appendValue(newStr);
      xfree((char *) newStr);
      insideQuote = inQuote(tokenStr);
      if (insideQuote) {
        //              printf("enqueue\n");
        m_tokenList->enqueue(token);
        insideQuote = false;
      } else {
        insideQuote = true;
      }
    }
  }
  return m_tokenList;
}



/**
 * congestionParse
 **/
TokenList *
Rule::congestionParse(char *rule, unsigned short minNumToken, unsigned short maxNumToken)
{
  return cacheParse(rule, minNumToken, maxNumToken);
}

/**
 * filterParse
 **/
TokenList *
Rule::filterParse(char *rule)
{
  return cacheParse(rule, 2);
}


/**
 * hostingParse
 **/
TokenList *
Rule::hostingParse(char *rule)
{
  // ASSUMPTIONS:
  //   NO SPACE around "=" sign
  //   NO SPACE around ","
  return cacheParse(rule, 2, 2);
}


/**
 * icpParse
 *   - mimic proxy/ICPConfig/icp_config_change_callback
 **/
TokenList *
Rule::icpParse(char *rule, unsigned short minNumToken, unsigned short maxNumToken)
{
  Tokenizer ruleTok(":");
  int numRuleTok = ruleTok.Initialize(rule, ALLOW_EMPTY_TOKS);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token;
  TokenList *m_tokenList = NEW(new TokenList());

  // Sanity Check -- number of token
  if (numRuleTok < minNumToken) {
    setErrorHint("Expecting more ':' delimited tokens!");
    return NULL;
  }
  if (numRuleTok > maxNumToken + 1 ||
      (numRuleTok == maxNumToken + 1 && strspn(ruleTok[maxNumToken], " ") != strlen(ruleTok[maxNumToken]))) {
    setErrorHint("Expecting less ':' delimited tokens!");
    return NULL;
  }


  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {
    token = NEW(new Token());
    token->setName(tokenStr);
    m_tokenList->enqueue(token);
  }

  return m_tokenList;
}


/**
 * ip_allowParse
 **/
TokenList *
Rule::ip_allowParse(char *rule)
{
  // ASSUMPTIONS:
  //   NO SPACE around "=" sign
  //   NO SPACE around "-"
  return cacheParse(rule, 2, 2);
}

TokenList *
Rule::ipnatParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token;

  if (numRuleTok < 10) {
    setErrorHint("Expecting at least 10 tokens!");
    return NULL;
  }

  TokenList *m_tokenList = NEW(new TokenList());

  while (tokenStr) {
    token = NEW(new Token());
    token->setName(tokenStr);
    tokenStr = ruleTok.iterNext(&ruleTok_state);
    m_tokenList->enqueue(token);
  }

  // Just to make sure that there are no left overs
  // coverity[returned_pointer]
  tokenStr = ruleTok.iterNext(&ruleTok_state);
  ink_debug_assert(tokenStr == NULL);

  return m_tokenList;
}



/**
 * logsParse
 **/
TokenList *
Rule::logsParse(char *rule)
{
  return NULL;
}


/**
 * log_hostsParse
 **/
TokenList *
Rule::log_hostsParse(char *rule)
{
  if (strstr(rule, " ")) {
    return NULL;
  }

  Token *token = NEW(new Token());
  TokenList *m_tokenList = NEW(new TokenList());
  token->setName(rule);
  m_tokenList->enqueue(token);

  return m_tokenList;
}


/**
 * logs_xmlParse
 **/
TokenList *
Rule::logs_xmlParse(char *rule)
{
  return NULL;
}


/**
 * mgmt_allowParse
 **/
TokenList *
Rule::mgmt_allowParse(char *rule)
{
  return cacheParse(rule, 2, 2);
}


/**
 * parentParse
 **/
TokenList *
Rule::parentParse(char *rule)
{
  return cacheParse(rule, 2);
}


/**
 * partitionParse
 **/
TokenList *
Rule::partitionParse(char *rule)
{
  return cacheParse(rule, 3, 3);
}


/**
 * pluginParse
 **/
TokenList *
Rule::pluginParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token;
  TokenList *m_tokenList = NEW(new TokenList());

  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {
    token = NEW(new Token());
    token->setName(tokenStr);
    m_tokenList->enqueue(token);
  }

  return m_tokenList;
}


/**
 * remapParse
 **/
TokenList *
Rule::remapParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);

  if ((numRuleTok != 3) && (numRuleTok != 4)) {
    setErrorHint("Expecting exactly 4 space delimited tokens");
    return NULL;
  }

  Token *token;
  TokenList *m_tokenList = NEW(new TokenList());

  token = NEW(new Token());
  token->setName(tokenStr);
  m_tokenList->enqueue(token);
  tokenStr = ruleTok.iterNext(&ruleTok_state);

  token = NEW(new Token());
  token->setName(tokenStr);
  tokenStr = ruleTok.iterNext(&ruleTok_state);
  token->setValue(tokenStr);
  m_tokenList->enqueue(token);

  // Just to make sure that there are no left overs
  tokenStr = ruleTok.iterNext(&ruleTok_state);
  if (tokenStr) {
    token = NEW(new Token());
    token->setName(tokenStr);
    m_tokenList->enqueue(token);
    ruleTok.iterNext(&ruleTok_state);
  }
  return m_tokenList;
}


/**
 * socksParse
 **/
TokenList *
Rule::socksParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token = NULL;
  bool insideQuote = false;
  const char *newStr;


  if (numRuleTok < 2) {
    setErrorHint("Expecting at least 2 space delimited tokens");
    return NULL;
  }

  TokenList *m_tokenList = NEW(new TokenList());

  /* check which rule type it is */
  if (strcmp(tokenStr, "no_socks") == 0) {      /* INK_SOCKS_BYPASS rule type */
    /* the token name = "no socks", the value = "list of ip addresses" */
    token = NEW(new Token());
    token->setName(tokenStr);

    for (; (tokenStr = ruleTok.iterNext(&ruleTok_state));) {
      token->appendValue(tokenStr);
    }
    m_tokenList->enqueue(token);
  } else if (strcmp(tokenStr, "auth") == 0) {   /* INK_SOCKS_AUTH rule type */
    /* first token:  name = "auth", value = "u"
       second token: name = <username>
       third token:  name = <password> */
    token = NEW(new Token());
    token->setName(tokenStr);
    tokenStr = ruleTok.iterNext(&ruleTok_state);
    token->setValue(tokenStr);  /* should be "u" authoriziation type */
    m_tokenList->enqueue(token);

    /* create tokens for username and password */
    for (; (tokenStr = ruleTok.iterNext(&ruleTok_state));) {
      token = NEW(new Token());
      token->setName(tokenStr);
      m_tokenList->enqueue(token);
    }

  } else {                      /* INK_SOCKS_MULTIPLE rule type */
    for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {
      /* token is a name-value pair separated with = sign */
      if (!insideQuote) {
        Tokenizer subruleTok("=");
        int numSubRuleTok = subruleTok.Initialize(tokenStr);
        tok_iter_state subruleTok_state;
        const char *subtoken = subruleTok.iterFirst(&subruleTok_state);

        // Every token must have a '=' sign
        if (numSubRuleTok < 2) {
          setErrorHint("'=' is expected in space-delimited token");
          return NULL;
        }

        token = NEW(new Token());
        /* set the name */
        token->setName(subtoken);

        /* set the value */
        if (numSubRuleTok == 2) {
          subtoken = subruleTok.iterNext(&subruleTok_state);
        } else {
          /* Ignore the first "=" sign and treat the rest as one token
             for INKqa09488 */
          const char *secondEqual = strstr(strstr(tokenStr, "="), "=");
          secondEqual++;
          subtoken = xstrdup(secondEqual);
        }
        insideQuote = inQuote(subtoken);

        newStr = strtrim(subtoken, '\"');
        if (!insideQuote) {
          //          printf("!insideQuote: %s\n", subtoken);
          token->setValue(newStr);
          m_tokenList->enqueue(token);
        } else {
          //          printf("insideQuote: %s\n", subtoken);
          //          printf("%s 1\n", subtoken);
          token->appendValue(newStr);
        }
        xfree((char *) newStr);

      } else {
        //      printf("%s 2\n", tokenStr);
        newStr = strtrim(tokenStr, '\"');
        token->appendValue(newStr);
        xfree((char *) newStr);
        insideQuote = inQuote(tokenStr);
        if (insideQuote) {
          //              printf("enqueue\n");
          m_tokenList->enqueue(token);
          insideQuote = false;
        } else {
          insideQuote = true;
        }
      }
    }                           /* end for loop */

  }

  return m_tokenList;
}


/**
 * splitdnsParse
 **/
TokenList *
Rule::splitdnsParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token = NULL;
  bool insideQuote = false;
  const char *newStr;

  // Sanity Check -- number of token
  if (numRuleTok < 0) {
    setErrorHint("Expecting more space delimited tokens!");
    return NULL;
  }
  if (numRuleTok > 10) {
    setErrorHint("Expecting less space delimited tokens!");
    return NULL;
  }
  // Sanity Check -- no space before or after '='
  if (strstr(rule, " =")) {
    setErrorHint("Expected space before '='");
    return NULL;
  }
  if (strstr(rule, "= ")) {
    setErrorHint("Expected space after '='");
    return NULL;
  }

  TokenList *m_tokenList = NEW(new TokenList());

  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {

    if (!insideQuote) {
      Tokenizer subruleTok("=");
      int numSubRuleTok = subruleTok.Initialize(tokenStr);
      tok_iter_state subruleTok_state;
      const char *subtoken = subruleTok.iterFirst(&subruleTok_state);

      // Every token must have a '=' sign
      if (numSubRuleTok < 2) {
        setErrorHint("'=' is expected in space-delimited token");
        return NULL;
      }

      token = NEW(new Token());
      token->setName(subtoken);
      subtoken = subruleTok.iterNext(&subruleTok_state);

      insideQuote = inQuote(subtoken);

      newStr = strtrim(subtoken, '\"');
      if (!insideQuote) {
        token->setValue(newStr);
        m_tokenList->enqueue(token);
      } else {
        //          printf("%s 1\n", subtoken);
        token->appendValue(newStr);
      }
      xfree((char *) newStr);

    } else {
      //            printf("%s 2\n", tokenStr);
      newStr = strtrim(tokenStr, '\"');
      token->appendValue(newStr);
      xfree((char *) newStr);
      insideQuote = inQuote(tokenStr);
      if (insideQuote) {
        //              printf("enqueue\n");
        m_tokenList->enqueue(token);
        insideQuote = false;
      } else {
        insideQuote = true;
      }
    }
  }

  return m_tokenList;
  //  return cacheParse(rule, 2);
}


/**
 * updateParse
 **/
TokenList *
Rule::updateParse(char *rule)
{
  Tokenizer ruleTok("\\");
  int numRuleTok = ruleTok.Initialize(rule, ALLOW_EMPTY_TOKS);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);

  // NOTE: ignore white spaces before/after the '\'
  // There should only be 5 tokens; if there are 6 tokens, the
  // sixth token must be all white spaces
  if (numRuleTok<5 || numRuleTok> 6 || (numRuleTok == 6 && strspn(ruleTok[5], " ") != strlen(ruleTok[5]))) {
    setErrorHint("Expecting exactly 5 '\' delimited tokens");
    return NULL;
  }

  Token *token;
  TokenList *m_tokenList = NEW(new TokenList());

  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {
    token = NEW(new Token());
    token->setName(tokenStr);
    m_tokenList->enqueue(token);
  }

  return m_tokenList;
}


/**
 * vaddrsParse
 **/
TokenList *
Rule::vaddrsParse(char *rule)
{
  // ASSUMPTIONS:
  //   UNIX: IP_address device subinterface
  //   Win:  IP_address interface
  Tokenizer ruleTok(" \t");
  ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);
  Token *token;
  TokenList *m_tokenList = NEW(new TokenList());

  for (; tokenStr; tokenStr = ruleTok.iterNext(&ruleTok_state)) {
    token = NEW(new Token());
    token->setName(tokenStr);
    m_tokenList->enqueue(token);
  }

  return m_tokenList;
}


/**
 * storageParse
 * ------------
 * the token value is pathname; if a size is specified, that is stored as
 * the token's value
 **/
TokenList *
Rule::storageParse(char *rule)
{
  Tokenizer ruleTok(" \t");
  int numRuleTok = ruleTok.Initialize(rule);
  tok_iter_state ruleTok_state;
  const char *tokenStr = ruleTok.iterFirst(&ruleTok_state);

  if ((numRuleTok != 1) && (numRuleTok != 2)) {
    setErrorHint("Expecting one or two tokens");
    return NULL;
  }

  Token *token;
  TokenList *m_tokenList = NEW(new TokenList());

  // at least one token, anyways
  token = NEW(new Token());
  token->setName(tokenStr);
  if (numRuleTok > 1) {         // numRulTok == 2
    tokenStr = ruleTok.iterNext(&ruleTok_state);
    token->setValue(tokenStr);
  }
  m_tokenList->enqueue(token);

  return m_tokenList;
}


/*
 * bool Rule::inQuote(char *str)
 *   Counts the number of quote found in "str"
 *   RETURN true  if "str" contains odd  number of quotes (")
 *          false if "str" contains even number of quotes (including zero)
 */
bool Rule::inQuote(const char *str)
{
  unsigned
    numQuote = 0;
  for (const char *ptr = str; *ptr != '\0'; ptr++) {
    if (*ptr == '\"') {
      numQuote++;
    }
  }
  return (numQuote & 1);
}


/***************************************************************************
 * RuleList
 *   a rule list is a list of rule; which compose to a configuration file.
 *   Again some queue and stack manlipation member functions are included.
 *   The important function is this object is also parse() which breaks up
 *   the file into individual lines and passes on to the Rule object to
 *   continue parsing each rule.
 *   NOTE: a rule that spans more than one line would be a problem in here.
 ***************************************************************************/
RuleList::RuleList():length(0), m_filename(NULL)
{
  m_filetype = INK_FNAME_UNDEFINED;
}

RuleList::~RuleList()
{
  if (m_filename) {
    xfree(m_filename);
  }
  Rule *rule = NULL;
  while ((rule = dequeue())) {
    delete rule;
  }
}

void
RuleList::Print()
{
  printf("RULELIST-->\n");
  for (Rule * rule = first(); rule; rule = next(rule)) {
    rule->Print();
  }
  printf("length: %d\n", length);
}

/*
 * void RuleList::parse(char *fileBuf, const char *filename)
 *   Takes configuration file buffer, tokenize the buffer according carriage
 *   return. For each line, pasre it.
 *
 */
void
RuleList::parse(char *fileBuf, const char *filename)
{
  m_filename = xstrdup(filename);

  if (strstr(filename, "admin_access.config")) {
    m_filetype = INK_FNAME_ADMIN_ACCESS;        /* admin_access.config */
  } else if (strstr(filename, "cache.config")) {
    m_filetype = INK_FNAME_CACHE_OBJ;   /* cache.config */
  } else if (strstr(filename, "congestion.config")) {
    m_filetype = INK_FNAME_CONGESTION;  /* congestion.config */
  } else if (strstr(filename, "filter.config")) {
    m_filetype = INK_FNAME_FILTER;      /* filter.config */
  } else if (strstr(filename, "hosting.config")) {
    m_filetype = INK_FNAME_HOSTING;     /* hosting.config */
  } else if (strstr(filename, "icp.config")) {
    m_filetype = INK_FNAME_ICP_PEER;    /* icp.config */
  } else if (strstr(filename, "ip_allow.config")) {
    m_filetype = INK_FNAME_IP_ALLOW;    /* ip_allow.config */
  } else if (strstr(filename, "logs.config")) {
    m_filetype = INK_FNAME_UNDEFINED;   /* logs.config */
  } else if (strstr(filename, "logs_xml.config")) {
    m_filetype = INK_FNAME_LOGS_XML;    /* logs_xml.config */
  } else if (strstr(filename, "mgmt_allow.config")) {
    m_filetype = INK_FNAME_MGMT_ALLOW;  /* mgmt_allow.config */
  } else if (strstr(filename, "parent.config")) {
    m_filetype = INK_FNAME_PARENT_PROXY;        /* parent.config */
  } else if (strstr(filename, "partition.config")) {
    m_filetype = INK_FNAME_PARTITION;   /* partition.config */
  } else if (strstr(filename, "plugin.config")) {
    m_filetype = INK_FNAME_PLUGIN;      /* plugin.config */
  } else if (strstr(filename, "remap.config")) {
    m_filetype = INK_FNAME_REMAP;       /* remap.config */
  } else if (strstr(filename, "socks.config")) {
    m_filetype = INK_FNAME_SOCKS;       /* socks.config */
  } else if (strstr(filename, "splitdns.config")) {
    m_filetype = INK_FNAME_SPLIT_DNS;   /* splitdns.config */
  } else if (strstr(filename, "update.config")) {
    m_filetype = INK_FNAME_UPDATE_URL;  /* update.config */
  } else if (strstr(filename, "vaddrs.config")) {
    m_filetype = INK_FNAME_VADDRS;      /* vaddrs.config */
  } else if (strstr(filename, "plugin.config")) {
    m_filetype = INK_FNAME_UNDEFINED;   /* plugin.config */
  } else if (strstr(filename, "storage.config")) {
    m_filetype = INK_FNAME_STORAGE;     /* storage.config */
  }
  else {
    m_filetype = INK_FNAME_UNDEFINED;
  }

  // Call the proper function
  this->parse(fileBuf, m_filetype);
}

/*
 * void RuleList::parse(char *fileBuf, INKFileNameT filetype)
 *   Takes configuration file buffer, tokenize the buffer according carriage
 *   return. For each line, pasre it.
 *   NOTE: (1) comment line must start with '#' as the first character without
 *             leading spaces
 *         (2) a line must
 *
 */
void
RuleList::parse(char *fileBuf, INKFileNameT filetype)
{
#ifdef _WIN32
  Tokenizer lineTok("\r\n");
#else
  Tokenizer lineTok("\n");
#endif
  tok_iter_state lineTok_state;
  const char *line;

  if (filetype == INK_FNAME_LOGS_XML) {
    printf("Yes Yes! XML!\n");
    //      InkXmlConfigFile(NULL);
    return;
  }

  lineTok.Initialize(fileBuf);
  line = lineTok.iterFirst(&lineTok_state);
  while (line) {
    Rule *rule = NEW(new Rule());

    if (line[0] == '#') {       // is this comment
      rule->setComment(line);
    } else {
      TokenList *m_tokenList = rule->parse(line, filetype);
      if (m_tokenList) {
        rule->setRuleStr(line);
        rule->tokenList = m_tokenList;
      } else {
        //rule->setComment("## WARNING: The following configuration rule is invalid!");
        char *error_rule;
        size_t error_rule_size = sizeof(char) * (strlen(line) + strlen("#ERROR: ") + 1);
        error_rule = (char *) xmalloc(error_rule_size);
        if (error_rule) {
          snprintf(error_rule, error_rule_size, "#ERROR: %s", line);
          rule->setComment(error_rule);
          xfree(error_rule);
        }
      }
    }

    //rule->Print();
    this->enqueue(rule);

    // Get next line
    line = lineTok.iterNext(&lineTok_state);
  }
  //this->Print();
};


/***************************************************************************
 * General Routines
 ***************************************************************************/
// Concatenate two strings together and return the resulting string

/*
char *
strtrim(char *str) {
  while(isspace(*str)) {
	str++;
  }
  while(isspace(str[strlen(str)-1])) {
	str[strlen(str)-1] = '\0';
  }
  return str;
}
*/

//
// This function ALLOCATES MEMORY FOR A NEW STRING!!
//
const char *
strtrim(const char *str_in, char chr)
{
  char *str = xstrdup(str_in);

  char *str_ptr = str;          // so we can free str later if it changes
  while (*str == chr) {
    str++;
  }
  while (str[strlen(str) - 1] == chr) {
    str[strlen(str) - 1] = '\0';
  }

  char *newStr = xstrdup(str);
  xfree(str_ptr);
  return newStr;
}
