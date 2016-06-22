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

#include "ts/ink_platform.h"
#include "ts/ParseRules.h"
#include "ts/ink_string.h"
#include "CfgContextUtils.h"
#include "ts/Tokenizer.h"
/***************************************************************************
 * Conversion Functions
 ***************************************************************************/
/* ---------------------------------------------------------------------------
 * string_to_ip_addr_ele
 * ---------------------------------------------------------------------------
 * Converts ip address string format to an TSIpAddrEle.
 * Determines if single/range, cidr/not-cidr based on format of string.
 *
 * if SINGLE =  ip_a/cidr_a
 * if RANGE =   ip_a/cidr_a-ip_b/cidr_b (possible to have spaces next to dash)
 * Returns NULL if invalid ele (eg. if ip's are invalid)
 */
TSIpAddrEle *
string_to_ip_addr_ele(const char *str)
{
  Tokenizer range_tokens(RANGE_DELIMITER_STR);
  Tokenizer cidr_tokens(CIDR_DELIMITER_STR);
  Tokenizer cidr_tokens2(CIDR_DELIMITER_STR);
  TSIpAddrEle *ele;
  const char *const_ip_a, *const_ip_b;
  char *ip_a = NULL, *ip_b = NULL;
  int numTokens = 0;
  char buf[MAX_BUF_SIZE];

  // ink_assert(str);
  if (!str)
    return NULL;

  ele = TSIpAddrEleCreate();
  if (!ele)
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);
  snprintf(buf, sizeof(buf), "%s", str);

  // determine if range or single type
  range_tokens.Initialize(buf, COPY_TOKS);
  numTokens = range_tokens.count();
  if (numTokens == 1) { // SINGLE TYPE
    ele->type = TS_IP_SINGLE;
    // determine if cidr type
    cidr_tokens.Initialize(buf, COPY_TOKS);
    numTokens = cidr_tokens.count();
    if (numTokens == 1) { // Single, NON-CIDR TYPE
      ele->ip_a = string_to_ip_addr(str);
    } else { // Single, CIDR TYPE
      if (!isNumber(cidr_tokens[1]))
        goto Lerror;
      ele->ip_a   = string_to_ip_addr(cidr_tokens[0]);
      ele->cidr_a = ink_atoi(cidr_tokens[1]);
    }
    if (!ele->ip_a) // ERROR: Invalid ip
      goto Lerror;
  } else { // RANGE TYPE
    ele->type  = TS_IP_RANGE;
    const_ip_a = range_tokens[0];
    const_ip_b = range_tokens[1];
    ip_a       = ats_strdup(const_ip_a);
    ip_b       = ats_strdup(const_ip_b);

    // determine if ip's are cidr type; only test if ip_a is cidr, assume both are same
    cidr_tokens.Initialize(ip_a, COPY_TOKS);
    numTokens = cidr_tokens.count();
    if (numTokens == 1) { // Range, NON-CIDR TYPE
      ele->ip_a = string_to_ip_addr(ip_a);
      ele->ip_b = string_to_ip_addr(ip_b);
    } else { // Range, CIDR TYPE */
      ele->ip_a   = string_to_ip_addr(cidr_tokens[0]);
      ele->cidr_a = ink_atoi(cidr_tokens[1]);
      cidr_tokens2.Initialize(ip_b, COPY_TOKS);
      ele->ip_b   = string_to_ip_addr(cidr_tokens2[0]);
      ele->cidr_b = ink_atoi(cidr_tokens2[1]);
      if (!isNumber(cidr_tokens[1]) || !isNumber(cidr_tokens2[1]))
        goto Lerror;
    }
    if (!ele->ip_a || !ele->ip_b) // ERROR: invalid IP
      goto Lerror;
  }

  ats_free(ip_a);
  ats_free(ip_b);
  return ele;

Lerror:
  ats_free(ip_a);
  ats_free(ip_b);
  TSIpAddrEleDestroy(ele);

  return NULL;
}

/* ----------------------------------------------------------------------------
 * ip_addr_ele_to_string
 * ---------------------------------------------------------------------------
 * Converts an  TSIpAddrEle to following string format:
 * Output:
 * if SINGLE =             ip_a/cidr_a
 * if RANGE =              ip_a/cidr_a-ip_b/cidr_b
 * If there is no cidr =   ip_a-ip_b
 * Returns NULL if invalid ele (needs to check that the ip's are valid too)
 */
char *
ip_addr_ele_to_string(TSIpAddrEle *ele)
{
  char buf[MAX_BUF_SIZE];
  char *str, *ip_a_str = NULL, *ip_b_str = NULL;

  // ink_assert(ele);
  if (!ele)
    goto Lerror;

  memset(buf, 0, MAX_BUF_SIZE);

  if (ele->ip_a == TS_INVALID_IP_ADDR)
    goto Lerror; // invalid ip_addr

  if (ele->type == TS_IP_SINGLE) { // SINGLE TYPE
    ip_a_str = ip_addr_to_string(ele->ip_a);
    if (!ip_a_str) // ERROR: invalid IP address
      goto Lerror;
    if (ele->cidr_a != TS_INVALID_IP_CIDR) { // a cidr type
      snprintf(buf, sizeof(buf), "%s%c%d", ip_a_str, CIDR_DELIMITER, ele->cidr_a);
    } else { // not cidr type
      snprintf(buf, sizeof(buf), "%s", ip_a_str);
    }

    ats_free(ip_a_str);
    str = ats_strdup(buf);

    return str;
  } else if (ele->type == TS_IP_RANGE) { // RANGE TYPE
    ip_a_str = ip_addr_to_string(ele->ip_a);
    ip_b_str = ip_addr_to_string(ele->ip_b);

    if (!ip_a_str || !ip_b_str)
      goto Lerror;

    if (ele->cidr_a != TS_INVALID_IP_CIDR && ele->cidr_b != TS_INVALID_IP_CIDR) {
      // a cidr type
      snprintf(buf, sizeof(buf), "%s%c%d%c%s%c%d", ip_a_str, CIDR_DELIMITER, ele->cidr_a, RANGE_DELIMITER, ip_b_str, CIDR_DELIMITER,
               ele->cidr_b);
    } else { // not cidr type
      snprintf(buf, sizeof(buf), "%s%c%s", ip_a_str, RANGE_DELIMITER, ip_b_str);
    }
    ats_free(ip_a_str);
    ats_free(ip_b_str);
    str = ats_strdup(buf);

    return str;
  }

Lerror:
  ats_free(ip_a_str);
  ats_free(ip_b_str);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * ip_addr_to_string
 * ---------------------------------------------------------------------------
 * Converts an  TSIpAddr (char*) to dotted decimal string notation. Allocates
 * memory for the new TSIpAddr.
 * Returns NULL if invalid TSIpAddr
 */
char *
ip_addr_to_string(TSIpAddr ip)
{
  // ink_assert(ip != TS_INVALID_IP_ADDR);
  if (ip == TS_INVALID_IP_ADDR) {
    return NULL;
  }
  if (!ccu_checkIpAddr(ip)) {
    return NULL;
  }
  return ats_strdup((char *)ip);
}

/* ----------------------------------------------------------------------------
 * string_to_ip_addr
 * ---------------------------------------------------------------------------
 * Converts an ip address in dotted-decimal string format into a string;
 * allocates memory for string. If IP is invalid, then returns TS_INVALID_IP_ADDR.
 */
TSIpAddr
string_to_ip_addr(const char *str)
{
  // ink_assert(str);
  if (!ccu_checkIpAddr(str))
    return TS_INVALID_IP_ADDR;

  char *copy;
  copy = ats_strdup(str);
  return (TSIpAddr)copy;
}

/* ---------------------------------------------------------------
 * ip_addr_list_to_string
 * ---------------------------------------------------------------
 * converts TSIpAddrList <==> ip_addr1<delim>ip_addr2<delim>ip_addr3, ...
 * Returns TSIpAddrList with original elements.
 * If encounters invalid TSIpAddrEle, returns NULL.
 */
char *
ip_addr_list_to_string(IpAddrList *list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  int buf_pos = 0;
  TSIpAddrEle *ip_ele;
  char *ip_str, *new_str;
  int num, i;

  // ink_assert(list && delimiter);
  if (!list || !delimiter)
    return NULL;

  num = queue_len((LLQ *)list);

  for (i = 0; i < num; i++) {
    ip_ele = (TSIpAddrEle *)dequeue((LLQ *)list); // read next ip
    ip_str = ip_addr_ele_to_string(ip_ele);

    if (!ip_str) {
      enqueue((LLQ *)list, ip_ele);
      return NULL;
    }
    if (i == num - 1)
      snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", ip_str);
    else
      snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s%s", ip_str, delimiter);
    buf_pos = strlen(buf);
    ats_free(ip_str);

    enqueue((LLQ *)list, ip_ele); // return ip to list
  }

  new_str = ats_strdup(buf);

  return new_str;
}

/* ---------------------------------------------------------------
 * string_to_ip_addr_list
 * ---------------------------------------------------------------
 * Converts ip_addr1<delim>ip_addr2<delim>ip_addr3, ...==> TSIpAddrList
 * Does checking to make sure that each ip_addr is valid; if encounter
 * an invalid ip addr, then returns TS_INVALID_LIST
 */
TSIpAddrList
string_to_ip_addr_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  TSIpAddrList ip_list;
  TSIpAddrEle *ip_ele;

  // ink_assert(str_list && delimiter);
  if (!str_list || !delimiter)
    return TS_INVALID_LIST;

  tokens.Initialize(str_list);
  numToks = tokens.count();

  ip_list = TSIpAddrListCreate();

  for (i = 0; i < numToks; i++) {
    ip_ele = string_to_ip_addr_ele(tokens[i]);
    if (ip_ele) {
      TSIpAddrListEnqueue(ip_list, ip_ele);
    } else { // Error: invalid IP
      TSIpAddrListDestroy(ip_list);
      return TS_INVALID_LIST;
    }
  }
  return ip_list;
}

/* ---------------------------------------------------------------
 * port_list_to_string (REPLACE BY sprintf_ports)
 * ---------------------------------------------------------------
 * Purpose: prints a list of ports in a PortList into string delimited format
 * Input:  ports - the queue of TSPortEle *'s.
 * Output: port_0<delim>port_1<delim>...<delim>port_n
 *         (each port can refer to a port range, eg 80-90)
 *         Return NULL if encounter invalid port or empty port list
 */
char *
port_list_to_string(PortList *ports, const char *delimiter)
{
  int num_ports;
  size_t pos = 0;
  int i, psize;
  TSPortEle *port_ele;
  char buf[MAX_BUF_SIZE];
  char *str;

  // ink_assert(ports && delimiter);
  if (!ports || !delimiter)
    goto Lerror;

  num_ports = queue_len((LLQ *)ports);
  if (num_ports <= 0) { // no ports specified
    goto Lerror;
  }
  // now list all the ports, including ranges
  for (i = 0; i < num_ports; i++) {
    port_ele = (TSPortEle *)dequeue((LLQ *)ports);
    if (!ccu_checkPortEle(port_ele)) {
      enqueue((LLQ *)ports, port_ele); // return TSPortEle to list
      goto Lerror;
    }

    if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "%d", port_ele->port_a)) > 0) {
      pos += psize;
    }
    if (port_ele->port_b != TS_INVALID_PORT) { //. is this a range
      // add in range delimiter & end of range
      if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "%c%d", RANGE_DELIMITER, port_ele->port_b)) > 0) {
        pos += psize;
      }
    }

    if (i != num_ports - 1) {
      if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "%s", delimiter)) > 0) {
        pos += psize;
      }
    }

    enqueue((LLQ *)ports, port_ele); // return TSPortEle to list
  }

  str = ats_strdup(buf);
  return str;

Lerror:
  return NULL;
}

/* ---------------------------------------------------------------
 * string_to_port_list
 * ---------------------------------------------------------------
 * Converts port1<delim>port2<delim>port3, ...==> TSPortList
 * Does checking to make sure that each ip_addr is valid; if encounter
 * an invalid ip addr, then returns TST_INVALID_LIST
 */
TSPortList
string_to_port_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  TSPortList port_list;
  TSPortEle *port_ele;

  // ink_assert(str_list && delimiter);
  if (!str_list || !delimiter)
    return TS_INVALID_LIST;

  tokens.Initialize(str_list);

  numToks = tokens.count();

  port_list = TSPortListCreate();

  for (i = 0; i < numToks; i++) {
    port_ele = string_to_port_ele(tokens[i]);
    if (port_ele) {
      TSPortListEnqueue(port_list, port_ele);
    } else { // error - invalid port ele
      TSPortListDestroy(port_list);
      return TS_INVALID_LIST;
    }
  }

  return port_list;
}

/* ---------------------------------------------------------------------------
 * port_ele_to_string
 * ---------------------------------------------------------------------------
 * Converts a port_ele to string format: <port_a> or <port_a>-<port_b>
 * Returns NULL if invalid PortEle
 */
char *
port_ele_to_string(TSPortEle *ele)
{
  char buf[MAX_BUF_SIZE];
  char *str;

  // ink_assert(ele);
  if (!ele || !ccu_checkPortEle(ele))
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);

  if (ele->port_b == TS_INVALID_PORT) { // Not a range
    snprintf(buf, sizeof(buf), "%d", ele->port_a);
  } else {
    snprintf(buf, sizeof(buf), "%d%c%d", ele->port_a, RANGE_DELIMITER, ele->port_b);
  }

  str = ats_strdup(buf);
  return str;
}

/*----------------------------------------------------------------------------
 * string_to_port_ele
 *---------------------------------------------------------------------------
 * Converts a string formatted port_ele into actual port_ele. Returns NULL if
 * invalid port(s). It is okay to have a single port specified.
 */
TSPortEle *
string_to_port_ele(const char *str)
{
  Tokenizer tokens(RANGE_DELIMITER_STR);
  TSPortEle *ele;
  char copy[MAX_BUF_SIZE];

  // ink_assert(str);
  if (!str)
    return NULL;

  memset(copy, 0, MAX_BUF_SIZE);
  snprintf(copy, sizeof(copy), "%s", str);

  ele = TSPortEleCreate();
  if (tokens.Initialize(copy, COPY_TOKS) > 2)
    goto Lerror;
  if (tokens.count() == 1) { // Not a Range of ports
    if (!isNumber(str))
      goto Lerror;
    ele->port_a = ink_atoi(str);
  } else {
    if (!isNumber(tokens[0]) || !isNumber(tokens[1]))
      goto Lerror;
    ele->port_a = ink_atoi(tokens[0]);
    ele->port_b = ink_atoi(tokens[1]);
  }

  if (!ccu_checkPortEle(ele)) {
    goto Lerror;
  }

  return ele;

Lerror:
  TSPortEleDestroy(ele);
  return NULL;
}

/*----------------------------------------------------------------------------
 * string_list_to_string
 *----------------------------------------------------------------------------
 * Converts str list to delimited string. Does not alter the
 * StringList passed in.
 * eg. str1<delim>str2<delim>str3...
 */
char *
string_list_to_string(TSStringList str_list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int i, numElems, psize;
  char *str_ele, *list_str;

  // ink_assert(str_list != TS_INVALID_LIST && delimiter);
  if (str_list == TS_INVALID_LIST || !delimiter)
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);
  numElems = queue_len((LLQ *)str_list);
  for (i = 0; i < numElems; i++) {
    str_ele = (char *)dequeue((LLQ *)str_list);

    if (i == numElems - 1) { // the last element shouldn't print comma
      if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", str_ele)) > 0)
        buf_pos += psize;
    } else {
      if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s%s", str_ele, delimiter)) > 0)
        buf_pos += psize;
    }

    enqueue((LLQ *)str_list, str_ele);
  }

  list_str = ats_strdup(buf);
  return list_str;
}

/* ---------------------------------------------------------------
 * string_to_string_list
 * ---------------------------------------------------------------
 * Converts port1<delim>port2<delim>port3, ...==> TSStringList
 * Does checking to make sure that each ip_addr is valid; if encounter
 * an invalid ip addr, then returns INVALID_HANDLE
 */
TSStringList
string_to_string_list(const char *str, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  tokens.Initialize(str);

  // ink_assert(str && delimiter);
  if (!str || !delimiter)
    return TS_INVALID_LIST;

  TSStringList str_list = TSStringListCreate();
  for (unsigned i = 0; i < tokens.count(); i++) {
    TSStringListEnqueue(str_list, ats_strdup(tokens[i]));
  }

  return str_list;
}

/*----------------------------------------------------------------------------
 * int_list_to_string
 *----------------------------------------------------------------------------
 * TSList(of char*'s only)==> elem1<delimiter>elem2<delimiter>elem3<delimiter>
 * Note: the string always ends with the delimiter
 * The list and its elements are not changed in any way.
 * Returns NULL if error.
 */
char *
int_list_to_string(TSIntList list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int numElems, i, psize;
  int *elem;

  // ink_assert(list != TS_INVALID_LIST && delimiter);
  if (list == TS_INVALID_LIST || !delimiter)
    return NULL;

  numElems = queue_len((LLQ *)list);

  memset(buf, 0, MAX_BUF_SIZE);
  for (i = 0; i < numElems; i++) {
    elem = (int *)dequeue((LLQ *)list);
    if (i == numElems - 1) {
      if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d", *elem)) > 0)
        buf_pos += psize;
    } else {
      if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d%s", *elem, delimiter)) > 0)
        buf_pos += psize;
    }
    enqueue((LLQ *)list, elem);
  }
  return ats_strdup(buf);
}

/* ---------------------------------------------------------------
 * string_to_int_list
 * ---------------------------------------------------------------
 * converts domain1<delim>domain2<delim>domain3, ... ==> TSList
 * Does checking to make sure that each integer is valid; if encounter
 * an invalid int, then returns TS_INVALID_LIST
 */
TSIntList
string_to_int_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  TSList list;
  int *ele;

  // ink_assert (str_list  && delimiter);
  if (!str_list || !delimiter)
    return TS_INVALID_LIST;

  tokens.Initialize(str_list);

  numToks = tokens.count();
  list    = TSIntListCreate();

  for (i = 0; i < numToks; i++) {
    if (!isNumber(tokens[i]))
      goto Lerror;
    ele  = (int *)ats_malloc(sizeof(int));
    *ele = ink_atoi(tokens[i]); // What about we can't convert? ERROR?
    TSIntListEnqueue(list, ele);
  }

  return list;

Lerror:
  TSIntListDestroy(list);
  return TS_INVALID_LIST;
}

/* ---------------------------------------------------------------
 * string_to_domain_list
 * ---------------------------------------------------------------
 * Converts domain1<delim>domain2<delim>domain3, ... ==> TSDomainList
 * Returns TS_INVALID_LIST if encounter an invalid Domain.
 */
TSDomainList
string_to_domain_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  TSDomainList list;
  TSDomain *ele;

  // ink_assert(str_list && delimiter);
  if (!str_list || !delimiter)
    return TS_INVALID_LIST;

  tokens.Initialize(str_list);

  numToks = tokens.count();

  list = TSDomainListCreate();

  for (i = 0; i < numToks; i++) {
    ele = string_to_domain(tokens[i]);
    if (ele) {
      TSDomainListEnqueue(list, ele);
    } else { // Error: invalid domain
      TSDomainListDestroy(list);
      return TS_INVALID_LIST;
    }
  }

  return list;
}

/*----------------------------------------------------------------------------
 * domain_list_to_string
 *----------------------------------------------------------------------------
 * TSList(of char*'s only)==> elem1<delimiter>elem2<delimiter>elem3<delimiter>
 * Note: the string always ends with the delimiter
 * The list and its elements are not changed in any way.
 * Returns NULL if encounter an invalid TSDomain.
 */
char *
domain_list_to_string(TSDomainList list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int numElems, i, psize;
  char *list_str, *dom_str;
  TSDomain *domain;

  // ink_assert(list != TS_INVALID_LIST && delimiter);
  if (list == TS_INVALID_LIST || !delimiter)
    return NULL;

  numElems = queue_len((LLQ *)list);

  memset(buf, 0, MAX_BUF_SIZE);

  for (i = 0; i < numElems; i++) {
    domain = (TSDomain *)dequeue((LLQ *)list);

    dom_str = domain_to_string(domain);
    if (!dom_str) {
      return NULL;
    }
    if (i == numElems - 1) { // the last element shouldn't print comma
      if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", dom_str)) > 0)
        buf_pos += psize;
    } else {
      if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s%s", dom_str, delimiter)) > 0)
        buf_pos += psize;
    }

    ats_free(dom_str);
    enqueue((LLQ *)list, domain);
  }

  list_str = ats_strdup(buf);
  return list_str;
}

/*----------------------------------------------------------------------------
 * domain_to_string
 *----------------------------------------------------------------------------
 * Converts an TSDomain into string format, eg. www.host.com:8080
 * Return NULL if invalid TSDomain (eg. missing domain value).
 */
char *
domain_to_string(TSDomain *domain)
{
  char buf[MAX_BUF_SIZE];
  char *dom_str;

  // ink_assert(domain);
  if (!domain)
    return NULL;

  if (domain->domain_val) {
    if (domain->port != TS_INVALID_PORT) // host:port
      snprintf(buf, sizeof(buf), "%s:%d", domain->domain_val, domain->port);
    else // host
      snprintf(buf, sizeof(buf), "%s", domain->domain_val);
  } else {
    return NULL; // invalid TSDomain
  }

  dom_str = ats_strdup(buf);

  return dom_str;
}

/*----------------------------------------------------------------------------
 * string_to_domain
 *----------------------------------------------------------------------------
 * Converts string format, eg. www.host.com:8080, into TSDomain.
 * The string can consist of just the host (which can be a name or an IP)
 * or of the host and port.
 * Return NULL if invalid TSDomain (eg. missing domain value).
 */
TSDomain *
string_to_domain(const char *str)
{
  TSDomain *dom;
  char *token, *remain, *token_pos;
  char buf[MAX_BUF_SIZE];

  // ink_assert(str);
  if (!str)
    return NULL;

  dom = TSDomainCreate();

  // get hostname
  ink_strlcpy(buf, str, sizeof(buf));
  token  = strtok_r(buf, ":", &token_pos);
  remain = token_pos;
  if (token)
    dom->domain_val = ats_strdup(token);
  else
    goto Lerror;

  // get port, if exists
  if (remain) {
    // check if the "remain" consist of all integers
    if (!isNumber(remain))
      goto Lerror;
    dom->port = ink_atoi(remain);
  } else {
    dom->port = TS_INVALID_PORT;
  }

  return dom;

Lerror:
  TSDomainDestroy(dom);
  return NULL;
}

/* ---------------------------------------------------------------
 * pdest_sspec_to_string
 * ---------------------------------------------------------------
 * Converts the TSPrimeDest, primary dest, secondary spec struct
 * into string format: <pdT>:pdst_val:sspec1:sspec2:...:
 * <pdT> - dest_domain, dest_host, dest_ip, url_regex
 * even if sspec is missing the delimter is included; so if no
 * sspecs, then : ::: ::: will appear
 */
char *
pdest_sspec_to_string(TSPrimeDestT pd, char *pd_val, TSSspec *sspec)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int psize;
  char hour_a[3], hour_b[3], min_a[3], min_b[3];
  char *src_ip, *str;

  // ink_assert(pd != TS_PD_UNDEFINED && pd_val && sspec);
  if (pd == TS_PD_UNDEFINED || !pd_val || !sspec)
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);

  do {
    // push in primary destination
    switch (pd) {
    case TS_PD_DOMAIN:
      psize = snprintf(buf, sizeof(buf), "dest_domain=%s ", pd_val);
      break;
    case TS_PD_HOST:
      psize = snprintf(buf, sizeof(buf), "dest_host=%s ", pd_val);
      break;
    case TS_PD_IP:
      psize = snprintf(buf, sizeof(buf), "dest_ip=%s ", pd_val);
      break;
    case TS_PD_URL_REGEX:
      psize = snprintf(buf, sizeof(buf), "url_regex=%s ", pd_val);
      break;
    case TS_PD_URL:
      psize = snprintf(buf, sizeof(buf), "url=%s ", pd_val);
      break;
    default:
      psize = 0;
      // Handled here:
      // TS_PD_UNDEFINED
      break;
    }
    if (psize > 0)
      buf_pos += psize;
    if (buf_pos + 1 >= sizeof(buf))
      break;

    // if there are secondary specifiers
    if (sspec) {
      // convert the time into a string, eg. 8 to "08"
      if (sspec->time.hour_a == 0) {
        snprintf(hour_a, sizeof(hour_a), "00");
      } else {
        if (sspec->time.hour_a < 10) {
          snprintf(hour_a, sizeof(hour_a), "0%d", sspec->time.hour_a);
        } else {
          snprintf(hour_a, sizeof(hour_a), "%d", sspec->time.hour_a);
        }
      }
      if (sspec->time.min_a == 0) {
        snprintf(min_a, sizeof(min_a), "00");
      } else {
        if (sspec->time.min_a < 10) {
          snprintf(min_a, sizeof(min_a), "0%d", sspec->time.min_a);
        } else {
          snprintf(min_a, sizeof(min_a), "%d", sspec->time.min_a);
        }
      }
      if (sspec->time.hour_b == 0) {
        snprintf(hour_b, sizeof(hour_b), "00");
      } else {
        if (sspec->time.hour_b < 10) {
          snprintf(hour_b, sizeof(hour_b), "0%d", sspec->time.hour_b);
        } else {
          snprintf(hour_b, sizeof(hour_b), "%d", sspec->time.hour_b);
        }
      }
      if (sspec->time.min_b == 0) {
        snprintf(min_b, sizeof(min_b), "00");
      } else {
        if (sspec->time.min_b < 10) {
          snprintf(min_b, sizeof(min_b), "0%d", sspec->time.min_b);
        } else {
          snprintf(min_b, sizeof(min_b), "%d", sspec->time.min_b);
        }
      }

      if (!(sspec->time.hour_a == 0 && sspec->time.min_a == 0 && sspec->time.hour_b == 0 && sspec->time.min_b == 0)) {
        // time is specified
        if (buf_pos < sizeof(buf) &&
            (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "time=%s:%s-%s:%s ", hour_a, min_a, hour_b, min_b)) > 0)
          buf_pos += psize;
      }
      // src_ip
      if (sspec->src_ip != TS_INVALID_IP_ADDR) {
        src_ip = ip_addr_to_string(sspec->src_ip);
        if (!src_ip) {
          return NULL;
        }
        if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "src_ip=%s ", src_ip)) > 0)
          buf_pos += psize;
        ats_free(src_ip);
      }
      // prefix?
      if (sspec->prefix) {
        if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "prefix=%s ", sspec->prefix)) > 0)
          buf_pos += psize;
      }
      // suffix?
      if (sspec->suffix) {
        if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "suffix=%s ", sspec->suffix)) > 0)
          buf_pos += psize;
      }
      // port?
      if (sspec->port) {
        char *portStr = port_ele_to_string(sspec->port);
        if (portStr) {
          if (buf_pos < sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "port=%s ", portStr)) > 0)
            buf_pos += psize;
          ats_free(portStr);
        }
      }
      // method
      if (buf_pos < sizeof(buf)) {
        switch (sspec->method) {
        case TS_METHOD_GET:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=get ");
          break;
        case TS_METHOD_POST:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=post ");
          break;
        case TS_METHOD_PUT:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=put ");
          break;
        case TS_METHOD_TRACE:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=trace ");
          break;
        case TS_METHOD_PUSH:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=PUSH ");
          break;
        default:
          psize = 0;
          break;
        }
        if (psize > 0)
          buf_pos += psize;
      } else
        break;

      // scheme
      if (buf_pos < sizeof(buf)) {
        switch (sspec->scheme) {
        case TS_SCHEME_NONE:
          snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%c", DELIMITER);
          break;
        case TS_SCHEME_HTTP:
          snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=http ");
          break;
        case TS_SCHEME_HTTPS:
          snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=https ");
          break;
        default:
          break;
        }
      }
    }
  } while (0);

  str = ats_strdup(buf);
  return str;
}

/*----------------------------------------------------------------------------
 * string_to_pdss_format
 *----------------------------------------------------------------------------
 * <pd_type>#<pd_value>#<sspecs> --> TSPdSsFormat
 * NOTE that the entire data line, including the action type is being passed in
 */
TSMgmtError
string_to_pdss_format(const char *str, TSPdSsFormat *pdss)
{
  Tokenizer tokens(DELIMITER_STR);
  Tokenizer time_tokens(":-");
  char copy[MAX_BUF_SIZE];

  // ink_assert(str && pdss);
  if (!str || !pdss)
    return TS_ERR_PARAMS;

  memset(copy, 0, MAX_BUF_SIZE);
  snprintf(copy, sizeof(copy), "%s", str);

  tokens.Initialize(copy, ALLOW_EMPTY_TOKS);

  // pd type
  if (strcmp(tokens[1], "dest_domain") == 0) {
    pdss->pd_type = TS_PD_DOMAIN;
  } else if (strcmp(tokens[1], "dest_host") == 0) {
    pdss->pd_type = TS_PD_HOST;
  } else if (strcmp(tokens[1], "dest_ip") == 0) {
    pdss->pd_type = TS_PD_IP;
  } else if (strcmp(tokens[1], "url_regex") == 0) {
    pdss->pd_type = TS_PD_URL_REGEX;
  } else if (strcmp(tokens[1], "url") == 0) {
    pdss->pd_type = TS_PD_URL;
  } else {
    goto Lerror;
  }

  // pd_value
  if (!tokens[2])
    goto Lerror;
  pdss->pd_val = ats_strdup(tokens[2]);

  // check secondary specifiers; exists only if not empty string
  // time
  if (strlen(tokens[3]) > 0) {
    if (string_to_time_struct(tokens[3], &(pdss->sec_spec)) != TS_ERR_OKAY)
      goto Lerror;
  }
  // src_ip
  if (strlen(tokens[4]) > 0) {
    pdss->sec_spec.src_ip = ats_strdup(tokens[4]);
  }
  // prefix
  if (strlen(tokens[5]) > 0) {
    pdss->sec_spec.prefix = ats_strdup(tokens[5]);
  }
  // suffix
  if (strlen(tokens[6]) > 0) {
    pdss->sec_spec.suffix = ats_strdup(tokens[6]);
  }
  // port
  if (strlen(tokens[7]) > 0) { // no port
    pdss->sec_spec.port = string_to_port_ele(tokens[7]);
  }
  // method
  if (strlen(tokens[8]) > 0) {
    pdss->sec_spec.method = string_to_method_type(tokens[8]);
  }
  // scheme
  if (strlen(tokens[9]) > 0) {
    pdss->sec_spec.scheme = string_to_scheme_type(tokens[9]);
  }
  return TS_ERR_OKAY;

Lerror:
  return TS_ERR_FAIL;
}

/*----------------------------------------------------------------------------
 * hms_time_to_string
 *----------------------------------------------------------------------------
 * Converts an TSHmsTime structure to string format: eg. 5h15m20s
 */
char *
hms_time_to_string(TSHmsTime time)
{
  char buf[MAX_BUF_SIZE];
  size_t s;

  s = snprintf(buf, sizeof(buf), "%dd%dh%dm%ds", time.d, time.h, time.m, time.s);

  if ((s > 0) && (s < sizeof(buf)))
    return ats_strdup(buf);
  else
    return NULL;
}

/*----------------------------------------------------------------------------
 * string_to_hms_time
 *----------------------------------------------------------------------------
 * Convert ?d?h?m?s ==> TSHmsTime
 * Returns TS_ERR_FAIL if invalid hms format, eg. if there are invalid
 * characters, eg. "10xh", "10h15m30s34", or repeated values, eg. "10h15h"
 */
TSMgmtError
string_to_hms_time(const char *str, TSHmsTime *time)
{
  int i, pos = 0;
  int len;
  char unit[10];
  bool valid = false;

  // ink_assert(str && time);
  if (!str || !time)
    return TS_ERR_PARAMS;

  memset(unit, 0, 10);
  len     = strlen(str);
  time->d = time->h = time->m = time->s = 0;
  for (i = 0; i < len; i++) {
    valid = false;
    if ((*str) == 'd') {
      if (time->d > 0 || !isNumber(unit))
        goto Lerror;
      time->d = ink_atoi(unit);
      memset(unit, 0, 10);
      pos   = 0;
      valid = true;
    } else if ((*str) == 'h') {
      if (time->h > 0 || !isNumber(unit))
        goto Lerror;
      time->h = ink_atoi(unit);
      memset(unit, 0, 10);
      pos   = 0;
      valid = true;
    } else if ((*str) == 'm') {
      if (time->m > 0 || !isNumber(unit))
        goto Lerror;
      time->m = ink_atoi(unit);
      memset(unit, 0, 10);
      pos   = 0;
      valid = true;
    } else if ((*str) == 's') {
      if (time->s > 0 || !isNumber(unit))
        goto Lerror;
      time->s = ink_atoi(unit);
      memset(unit, 0, 10);
      pos   = 0;
      valid = true;
    } else {
      unit[pos] = *str;
      pos++;
    }

    ++str;
  }

  if (!valid)
    goto Lerror;
  return TS_ERR_OKAY;

Lerror:
  return TS_ERR_FAIL;
}

/*----------------------------------------------------------------------------
 * string_to_time_struct
 *----------------------------------------------------------------------------
 * convert string "09:00-23:00" to time struct
 *  struct {
 *   int hour_a;
 *   int min_a;
 *   int hour_b;
 *   int min_b;
 *  } time
 * Returns TS_ERR_FAIL if invalid time string.
 */
TSMgmtError
string_to_time_struct(const char *str, TSSspec *sspec)
{
  Tokenizer time_tokens(":-");

  if (time_tokens.Initialize(str) != 4) {
    return TS_ERR_FAIL;
  }

  if (strcmp(time_tokens[0], "00") == 0) {
    sspec->time.hour_a = 0;
  } else {
    if (!isNumber(time_tokens[0]))
      goto Lerror;
    sspec->time.hour_a = ink_atoi(time_tokens[0]);
  }
  if (strcmp(time_tokens[1], "00") == 0) {
    sspec->time.min_a = 0;
  } else {
    if (!isNumber(time_tokens[1]))
      goto Lerror;
    sspec->time.min_a = ink_atoi(time_tokens[1]);
  }
  if (strcmp(time_tokens[2], "00") == 0) {
    sspec->time.hour_b = 0;
  } else {
    if (!isNumber(time_tokens[2]))
      goto Lerror;
    sspec->time.hour_b = ink_atoi(time_tokens[2]);
  }
  if (strcmp(time_tokens[3], "00") == 0) {
    sspec->time.min_b = 0;
  } else {
    if (!isNumber(time_tokens[3]))
      goto Lerror;
    sspec->time.min_b = ink_atoi(time_tokens[3]);
  }

  // check valid time values
  if (!ccu_checkTimePeriod(sspec))
    goto Lerror;

  return TS_ERR_OKAY;

Lerror:
  return TS_ERR_FAIL;
}

/*----------------------------------------------------------------------------
 * string_to_header_type
 *----------------------------------------------------------------------------
 * string ==> TSHdrT
 */
TSHdrT
string_to_header_type(const char *str)
{
  // ink_assert(str);
  if (!str)
    return TS_HDR_UNDEFINED;

  if (strcmp(str, "date") == 0) {
    return TS_HDR_DATE;
  } else if (strcmp(str, "host") == 0) {
    return TS_HDR_HOST;
  } else if (strcmp(str, "cookie") == 0) {
    return TS_HDR_COOKIE;
  } else if (strcmp(str, "client_ip") == 0) {
    return TS_HDR_CLIENT_IP;
  }

  return TS_HDR_UNDEFINED;
}

char *
header_type_to_string(TSHdrT hdr)
{
  // header type
  switch (hdr) {
  case TS_HDR_DATE:
    return ats_strdup("date");
  case TS_HDR_HOST:
    return ats_strdup("host");
  case TS_HDR_COOKIE:
    return ats_strdup("cookie");
  case TS_HDR_CLIENT_IP:
    return ats_strdup("client_ip");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * string_to_scheme_type
 *----------------------------------------------------------------------------
 * converts scheme string into a TSSchemeT type
 */
TSSchemeT
string_to_scheme_type(const char *scheme)
{
  if (strcasecmp(scheme, "http") == 0) {
    return TS_SCHEME_HTTP;
  } else if (strcasecmp(scheme, "https") == 0) {
    return TS_SCHEME_HTTPS;
  }
  return TS_SCHEME_UNDEFINED;
}

char *
scheme_type_to_string(TSSchemeT scheme)
{
  switch (scheme) {
  case TS_SCHEME_HTTP:
    return ats_strdup("http");
  case TS_SCHEME_HTTPS:
    return ats_strdup("https");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * string_to_method_type
 *----------------------------------------------------------------------------
 * converts scheme string into a TSSchemeT type
 */
TSMethodT
string_to_method_type(const char *method)
{
  if (strcasecmp(method, "get") == 0) {
    return TS_METHOD_GET;
  } else if (strcasecmp(method, "post") == 0) {
    return TS_METHOD_POST;
  } else if (strcasecmp(method, "put") == 0) {
    return TS_METHOD_PUT;
  } else if (strcasecmp(method, "trace") == 0) {
    return TS_METHOD_TRACE;
  } else if (strcasecmp(method, "push") == 0) { // could be "push" or "PUSH"
    return TS_METHOD_PUSH;
  }

  return TS_METHOD_UNDEFINED;
}

char *
method_type_to_string(TSMethodT method)
{
  switch (method) {
  case TS_METHOD_GET:
    return ats_strdup("get");
  case TS_METHOD_POST:
    return ats_strdup("post");
  case TS_METHOD_PUT:
    return ats_strdup("put");
  case TS_METHOD_TRACE:
    return ats_strdup("trace");
  case TS_METHOD_PUSH:
    return ats_strdup("push");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * connect_type_to_string
 *----------------------------------------------------------------------------
 */
char *
connect_type_to_string(TSConnectT conn)
{
  switch (conn) {
  case TS_CON_UDP:
    return ats_strdup("udp");
  case TS_CON_TCP:
    return ats_strdup("tcp");
  default:
    break;
  }
  return NULL;
}

TSConnectT
string_to_connect_type(const char *conn)
{
  if (strcmp(conn, "tcp") == 0) {
    return TS_CON_TCP;
  } else {
    return TS_CON_UDP;
  }

  return TS_CON_UNDEFINED;
}

/*----------------------------------------------------------------------------
 * multicast_type_to_string
 *----------------------------------------------------------------------------
 */
char *
multicast_type_to_string(TSMcTtlT mc)
{
  switch (mc) {
  case TS_MC_TTL_SINGLE_SUBNET:
    return ats_strdup("single_subnet");
  case TS_MC_TTL_MULT_SUBNET:
    return ats_strdup("multiple_subnet");
  default:
    break;
  }
  return NULL;
}

/* -------------------------------------------------------------------------
 * string_to_round_robin_type
 * -------------------------------------------------------------------------
 */
TSRrT
string_to_round_robin_type(const char *rr)
{
  if (strcmp(rr, "true") == 0)
    return TS_RR_TRUE;
  else if (strcmp(rr, "false") == 0)
    return TS_RR_FALSE;
  else if (strcmp(rr, "strict") == 0)
    return TS_RR_STRICT;

  return TS_RR_UNDEFINED;
}

char *
round_robin_type_to_string(TSRrT rr)
{
  switch (rr) {
  case TS_RR_TRUE:
    return ats_strdup("true");
  case TS_RR_FALSE:
    return ats_strdup("false");
  case TS_RR_STRICT:
    return ats_strdup("strict");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * filename_to_string
 *----------------------------------------------------------------------------
 * TSFileNameT ==> string
 */
const char *
filename_to_string(TSFileNameT file)
{
  switch (file) {
  case TS_FNAME_CACHE_OBJ:
    return "cache.config";
  case TS_FNAME_CONGESTION:
    return "congestion.config";
  case TS_FNAME_HOSTING:
    return "hosting.config";
  case TS_FNAME_ICP_PEER:
    return "icp.config";
  case TS_FNAME_IP_ALLOW:
    return "ip_allow.config";
  case TS_FNAME_LOGS_XML:
    return "logs_xml.config";
  case TS_FNAME_PARENT_PROXY:
    return "parent.config";
  case TS_FNAME_VOLUME:
    return "volume.config";
  case TS_FNAME_PLUGIN:
    return "plugin.config";
  case TS_FNAME_REMAP:
    return "remap.config";
  case TS_FNAME_SOCKS:
    return "socks.config";
  case TS_FNAME_SPLIT_DNS:
    return "splitdns.config";
  case TS_FNAME_STORAGE:
    return "storage.config";
  case TS_FNAME_VADDRS:
    return "vaddrs.config";
  default: /* no such config file */
    return NULL;
  }
}

/* -------------------------------------------------------------------------
 * string_to_congest_scheme_type
 * -------------------------------------------------------------------------
 */
TSCongestionSchemeT
string_to_congest_scheme_type(const char *scheme)
{
  if (strcmp(scheme, "per_ip") == 0) {
    return TS_HTTP_CONGEST_PER_IP;
  } else if (strcmp(scheme, "per_host") == 0) {
    return TS_HTTP_CONGEST_PER_HOST;
  }

  return TS_HTTP_CONGEST_UNDEFINED;
}

/* -------------------------------------------------------------------------
 * string_to_admin_acc_type
 * -------------------------------------------------------------------------
 */
TSAccessT
string_to_admin_acc_type(const char *access)
{
  if (strcmp(access, "none") == 0) {
    return TS_ACCESS_NONE;
  } else if (strcmp(access, "monitor_only") == 0) {
    return TS_ACCESS_MONITOR;
  } else if (strcmp(access, "monitor_config_view") == 0) {
    return TS_ACCESS_MONITOR_VIEW;
  } else if (strcmp(access, "monitor_config_change") == 0) {
    return TS_ACCESS_MONITOR_CHANGE;
  }

  return TS_ACCESS_UNDEFINED;
}

char *
admin_acc_type_to_string(TSAccessT access)
{
  switch (access) {
  case TS_ACCESS_NONE:
    return ats_strdup("none");
  case TS_ACCESS_MONITOR:
    return ats_strdup("monitor_only");
  case TS_ACCESS_MONITOR_VIEW:
    return ats_strdup("monitor_config_view");
  case TS_ACCESS_MONITOR_CHANGE:
    return ats_strdup("monitor_config_change");
  default:
    break;
  }

  return NULL;
}

/***************************************************************************
 * Tokens-to-Struct Converstion Functions
 ***************************************************************************/
/* -------------------------------------------------------------------------
 * tokens_to_pdss_format
 * -------------------------------------------------------------------------
 * Pass in the TokenList and a pointer to first Token to start iterating from.
 * Iterates through the tokens, checking
 * for tokens that are either primary destination specifiers and secondary
 * specfiers. Returns pointer to the last valid pointer; eg. the last sec
 * spec that was in the list. If there are no primary dest or sec specs,
 * then just returns same pointer originally passed in.
 *
 * Returns NULL, if the first token IS NOT a  Primary Dest Specifier
 */
Token *
tokens_to_pdss_format(TokenList *tokens, Token *first_tok, TSPdSsFormat *pdss)
{
  Token *tok, *last_tok;
  int i                             = 0;
  const char *sspecs[NUM_SEC_SPECS] = {"time", "src_ip", "prefix", "suffix", "port", "method", "scheme", "tag"};

  // ink_assert(tokens && first_tok && pdss);
  if (!tokens || !first_tok || !pdss)
    return NULL;

  // tokens->Print();

  // first token must be primary destination specifier
  if (strcmp(first_tok->name, "dest_domain") == 0) {
    pdss->pd_type = TS_PD_DOMAIN;
  } else if (strcmp(first_tok->name, "dest_host") == 0) {
    pdss->pd_type = TS_PD_HOST;
  } else if (strcmp(first_tok->name, "dest_ip") == 0) {
    pdss->pd_type = TS_PD_IP;
  } else if (strcmp(first_tok->name, "url_regex") == 0) {
    pdss->pd_type = TS_PD_URL_REGEX;
  } else if (strcmp(first_tok->name, "url") == 0) {
    pdss->pd_type = TS_PD_URL;
  } else {
    return NULL; // INVALID primary destination specifier
  }
  pdss->pd_val = ats_strdup(first_tok->value);

  // iterate through tokens checking for sec specifiers
  // state determines which sec specifier being checked
  // the state is only set if there's a sec spec match
  last_tok = first_tok;
  tok      = tokens->next(first_tok);
  while (tok) {
    bool matchFound = false;
    for (i = 0; i < NUM_SEC_SPECS; i++) {
      if (strcmp(tok->name, sspecs[i]) == 0) { // sec spec
        matchFound = true;
        switch (i) {
        case 0: // time
          // convert the time value
          string_to_time_struct(tok->value, &(pdss->sec_spec));
          goto next_token;
        case 1: // src_ip
          pdss->sec_spec.src_ip = ats_strdup(tok->value);
          goto next_token;
        case 2: // prefix
          pdss->sec_spec.prefix = ats_strdup(tok->value);
          goto next_token;
        case 3: // suffix
          pdss->sec_spec.suffix = ats_strdup(tok->value);
          goto next_token;
        case 4: // port
          pdss->sec_spec.port = string_to_port_ele(tok->value);
          goto next_token;
        case 5: // method
          pdss->sec_spec.method = string_to_method_type(tok->value);
          goto next_token;
        case 6: // scheme
          pdss->sec_spec.scheme = string_to_scheme_type(tok->value);
          goto next_token;
        default:
          // This should never happen
          break;
        }
      } // end if statement
    }   // end for loop

    // No longer in the secondary specifer region,
    // return the last valid secondary specifer
    if (!matchFound) {
      return last_tok;
    }

  next_token: // Get the next token
    last_tok = tok;
    tok      = tokens->next(tok);

  } // end while loop

  return NULL;
}

/***************************************************************************
 * Validation Functions
 ***************************************************************************/

/* -------------------------------------------------------------------------
 * isNumber
 * -------------------------------------------------------------------------
 * Returns true if the entire string is numerical
 */
bool
isNumber(const char *strNum)
{
  for (int i = 0; strNum[i] != '\0'; i++) {
    if (!isdigit(strNum[i])) {
      return false;
    }
  }

  return true;
}

/* -------------------------------------------------------------------------
 * ccu_checkIpAddr (from WebFileEdit.cc)
 * -------------------------------------------------------------------------
 *   Checks to see if the passed in string represents a valid
 *     ip address in dot notation (ie 209.1.33.20)
 *   If the min_addr and max_addr are specified, then it checks
 *   that the ip address falls within the range; Otherwise
 *   the min_addr = 0.0.0.0 and max_addr = 255.255.255.255
 *
 *   Returns true if it does and false if it does not
 */
bool
ccu_checkIpAddr(const char *addr, const char *min_addr, const char *max_addr)
{
  Tokenizer addrToks(".");
  Tokenizer minToks(".");
  Tokenizer maxToks(".");
  int len, addrQ, minQ, maxQ;

  if (!addr || !min_addr || !max_addr)
    return false;
  // BZ47440
  // truncate any leading or trailing white spaces from addr,
  // which can occur if IP is from a list of IP addresses
  char *new_addr = chopWhiteSpaces_alloc((char *)addr);
  if (new_addr == NULL)
    return false;

  if ((addrToks.Initialize(new_addr, ALLOW_EMPTY_TOKS)) != 4 || (minToks.Initialize((char *)min_addr, ALLOW_EMPTY_TOKS)) != 4 ||
      (maxToks.Initialize((char *)max_addr, ALLOW_EMPTY_TOKS)) != 4) {
    ats_free(new_addr);
    return false; // Wrong number of parts
  }
  // IP can't end in a "." either
  len = strlen(new_addr);
  if (new_addr[len - 1] == '.') {
    ats_free(new_addr);
    return false;
  }
  // Check all four parts of the ip address to make
  //  sure that they are valid
  for (int i = 0; i < 4; i++) {
    if (!isNumber(addrToks[i])) {
      ats_free(new_addr);
      return false;
    }
    // coverity[secure_coding]
    if (sscanf(addrToks[i], "%d", &addrQ) != 1 || sscanf(minToks[i], "%d", &minQ) != 1 || sscanf(maxToks[i], "%d", &maxQ) != 1) {
      ats_free(new_addr);
      return false;
    }

    if (addrQ < minQ || addrQ > maxQ) {
      ats_free(new_addr);
      return false;
    }
  }

  ats_free(new_addr);
  return true;
}

/* ----------------------------------------------------------------------------
 * ccu_checkIpAddrEle
 * ---------------------------------------------------------------------------
 * very similar to the ip_addr_ele_to_string function
 */
bool
ccu_checkIpAddrEle(TSIpAddrEle *ele)
{
  if (!ele || ele->ip_a == TS_INVALID_IP_ADDR)
    return false;

  if (ele->type == TS_IP_SINGLE) { // SINGLE TYPE
    return (ccu_checkIpAddr(ele->ip_a));
  } else if (ele->type == TS_IP_RANGE) { // RANGE TYPE
    return (ccu_checkIpAddr(ele->ip_a) && ccu_checkIpAddr(ele->ip_b));
  } else {
    return false;
  }
}

bool
ccu_checkPortNum(int port)
{
  return ((port > 0) && (port < 65535)); // What is max. port number?
}

// port_b can be "unspecified"; however if port_b is specified, it must
// be greater than port_a
bool
ccu_checkPortEle(TSPortEle *ele)
{
  if (!ele)
    return false;

  if (ele->port_b == 0) { // single port
    return (ccu_checkPortNum(ele->port_a));
  } else { // range of ports
    if (ele->port_a >= ele->port_b)
      return false;
    return (ccu_checkPortNum(ele->port_a) && ccu_checkPortNum(ele->port_b));
  }
}

/* ---------------------------------------------------------------
 * checkPdSspec
 * ---------------------------------------------------------------
 * must have a prim dest value spsecified and have valid prim dest type
 */
bool
ccu_checkPdSspec(TSPdSsFormat pdss)
{
  if (pdss.pd_type != TS_PD_DOMAIN && pdss.pd_type != TS_PD_HOST && pdss.pd_type != TS_PD_IP && pdss.pd_type != TS_PD_URL_REGEX) {
    goto Lerror;
  }

  if (!pdss.pd_val) {
    goto Lerror;
  }
  // primary destination cannot have spaces
  if (strstr(pdss.pd_val, " ")) {
    goto Lerror;
  }
  // if pdest is IP type, check if valid IP (could be single or range)
  if (pdss.pd_type == TS_PD_IP) {
    TSIpAddrEle *ip = string_to_ip_addr_ele(pdss.pd_val);
    if (!ip)
      goto Lerror;
    else
      TSIpAddrEleDestroy(ip);
  }
  // if src_ip specified, check if valid IP address
  if (pdss.sec_spec.src_ip) {
    if (!ccu_checkIpAddr(pdss.sec_spec.src_ip))
      goto Lerror;
  }

  if (!ccu_checkTimePeriod(&(pdss.sec_spec)))
    goto Lerror;

  return true;

Lerror:
  return false;
}

/* ---------------------------------------------------------------
 * ccu_checkUrl
 * ---------------------------------------------------------------
 * Checks that there is not more than one instance of  ":/" in
 * the url.
 */
bool
ccu_checkUrl(char *url)
{
  // Chop the protocol part, if it exists
  char *slashStr = strstr(url, "://");
  if (!slashStr) {
    return false; // missing protocol
  } else {
    url = slashStr + 3; // advance two positions to get rid of leading '://'
  }

  // check if there's a second occurrence
  slashStr = strstr(url, ":/");
  if (slashStr)
    return false;

  // make sure that after the first solo "/", there are no more ":"
  // to specify ports
  slashStr = strstr(url, "/"); // begin path prefix
  if (slashStr) {
    url = slashStr++;
    if (strstr(url, ":"))
      return false; // the port must be specified before the prefix
  }

  return true;
}

/* ---------------------------------------------------------------
 * ccu_checkTimePeriod
 * ---------------------------------------------------------------
 * Checks that the time struct used to specify the time period in the
 * TSSspec has valid time values (eg. 0-23 hr, 0-59 min) and that
 * time A <= time B
 */
bool
ccu_checkTimePeriod(TSSspec *sspec)
{
  // check valid time values
  if (sspec->time.hour_a < 0 || sspec->time.hour_a > 23 || sspec->time.hour_b < 0 || sspec->time.hour_b > 23 ||
      sspec->time.min_a < 0 || sspec->time.min_a > 59 || sspec->time.min_b < 0 || sspec->time.min_b > 59)
    return false;

  // check time_a < time_b
  if (sspec->time.hour_a > sspec->time.hour_b) {
    return false;
  } else if (sspec->time.hour_a == sspec->time.hour_b) {
    if (sspec->time.min_a > sspec->time.min_b)
      return false;
  }

  return true;
}

/* ----------------------------------------------------------------------------
 * chopWhiteSpaces_alloc
 * ---------------------------------------------------------------------------
 * eliminates any leading and trailing white spaces from str; returns
 * the new allocated string
 */
char *
chopWhiteSpaces_alloc(char *str)
{
  int len;

  if (!str)
    return NULL;

  // skip any leading white spaces
  while (*str != '\0') {
    if ((*str) == ' ') {
      ++str;
      continue;
    }
    break;
  }

  // eliminate trailing white spaces
  len = strcspn(str, " ");

  return ats_strndup(str, len + 1);
}

/***************************************************************
 * General Helper Functions
 ***************************************************************/
/*--------------------------------------------------------------
 * create_ele_obj_from_rule_node
 *--------------------------------------------------------------
 * Calls the appropriate subclasses' constructor using TokenList.
 * Returns NULL if invalid Ele.
 */
CfgEleObj *
create_ele_obj_from_rule_node(Rule *rule)
{
  TSRuleTypeT rule_type;
  TokenList *token_list;
  CfgEleObj *ele = NULL;

  // sanity check
  // ink_assert(rule != NULL);
  if (!rule)
    return NULL;

  // first check if the rule node is a comment
  if (rule->getComment()) {
    ele = (CfgEleObj *)new CommentObj(rule->getComment());
    return ele;
  }

  token_list = rule->tokenList;
  // determine what rule type the TokenList refers to
  rule_type = get_rule_type(token_list, rule->getFiletype());

  // convert TokenList into an Ele
  // need switch statement to determine which Ele constructor to call
  switch (rule_type) {
  case TS_CACHE_NEVER: /* all cache rules use same constructor */
  case TS_CACHE_IGNORE_NO_CACHE:
  case TS_CACHE_CLUSTER_CACHE_LOCAL:
  case TS_CACHE_IGNORE_CLIENT_NO_CACHE:
  case TS_CACHE_IGNORE_SERVER_NO_CACHE:
  case TS_CACHE_PIN_IN_CACHE:
  case TS_CACHE_TTL_IN_CACHE:
  case TS_CACHE_REVALIDATE:
  case TS_CACHE_AUTH_CONTENT:
    ele = (CfgEleObj *)new CacheObj(token_list);
    break;
  case TS_CONGESTION:
    ele = (CfgEleObj *)new CongestionObj(token_list);
    break;
  case TS_HOSTING: /* hosting.config */
    ele = (CfgEleObj *)new HostingObj(token_list);
    break;
  case TS_ICP: /* icp.config */
    ele = (CfgEleObj *)new IcpObj(token_list);
    break;
  case TS_IP_ALLOW: /* ip_allow.config */
    ele = (CfgEleObj *)new IpAllowObj(token_list);
    break;

  case TS_LOG_FILTER: /* logs_xml.config */
  case TS_LOG_OBJECT:
  case TS_LOG_FORMAT:
    // ele = (CfgEleObj *) new LogFilterObj(token_list);
    break;
  case TS_PP_PARENT: /* parent.config */
  case TS_PP_GO_DIRECT:
    ele = (CfgEleObj *)new ParentProxyObj(token_list);
    break;
  case TS_VOLUME: /* volume.config */
    ele = (CfgEleObj *)new VolumeObj(token_list);
    break;
  case TS_PLUGIN:
    ele = (CfgEleObj *)new PluginObj(token_list);
    break;
  case TS_REMAP_MAP: /* remap.config */
  case TS_REMAP_REVERSE_MAP:
  case TS_REMAP_REDIRECT:
  case TS_REMAP_REDIRECT_TEMP:
    ele = (CfgEleObj *)new RemapObj(token_list);
    break;
  case TS_SOCKS_BYPASS: /* socks.config */
  case TS_SOCKS_AUTH:
  case TS_SOCKS_MULTIPLE:
    ele = (CfgEleObj *)new SocksObj(token_list);
    break;
  case TS_SPLIT_DNS: /* splitdns.config */
    ele = (CfgEleObj *)new SplitDnsObj(token_list);
    break;
  case TS_STORAGE:
    ele = (CfgEleObj *)new StorageObj(token_list);
    break;
  case TS_VADDRS: /* vaddrs.config */
    ele = (CfgEleObj *)new VirtIpAddrObj(token_list);
    break;
  default:
    return NULL; // invalid rule type
  }
  if (!ele || !ele->isValid()) {
    return NULL;
  }
  return ele;
}

/*--------------------------------------------------------------
 * create_ele_obj_from_ele
 *--------------------------------------------------------------
 * calls the appropriate subclasses' constructor using actual TSEle
 * need to convert the ele into appropriate Ele object type
 * Note: the ele is not copied, it is actually used, so caller
 * shouldn't free it!
 */
CfgEleObj *
create_ele_obj_from_ele(TSCfgEle *ele)
{
  CfgEleObj *ele_obj = NULL;

  if (!ele)
    return NULL;

  switch (ele->type) {
  case TS_CACHE_NEVER:           /* cache.config */
  case TS_CACHE_IGNORE_NO_CACHE: // fall-through
  case TS_CACHE_CLUSTER_CACHE_LOCAL:
  case TS_CACHE_IGNORE_CLIENT_NO_CACHE: // fall-through
  case TS_CACHE_IGNORE_SERVER_NO_CACHE: // fall-through
  case TS_CACHE_PIN_IN_CACHE:           // fall-through
  case TS_CACHE_REVALIDATE:             // fall-through
  case TS_CACHE_TTL_IN_CACHE:
  case TS_CACHE_AUTH_CONTENT:
    ele_obj = (CfgEleObj *)new CacheObj((TSCacheEle *)ele);
    break;

  case TS_CONGESTION:
    ele_obj = (CfgEleObj *)new CongestionObj((TSCongestionEle *)ele);
    break;

  case TS_HOSTING: /* hosting.config */
    ele_obj = (CfgEleObj *)new HostingObj((TSHostingEle *)ele);
    break;

  case TS_ICP: /* icp.config */
    ele_obj = (CfgEleObj *)new IcpObj((TSIcpEle *)ele);
    break;

  case TS_IP_ALLOW: /* ip_allow.config */
    ele_obj = (CfgEleObj *)new IpAllowObj((TSIpAllowEle *)ele);
    break;

  case TS_LOG_FILTER: /* logs_xml.config */
  case TS_LOG_OBJECT: // fall-through
  case TS_LOG_FORMAT: // fall-through
    // ele_obj = (CfgEleObj*) new LogFilterObj((TSLogFilterEle*)ele);
    break;

  case TS_PP_PARENT:    /* parent.config */
  case TS_PP_GO_DIRECT: // fall-through
    ele_obj = (CfgEleObj *)new ParentProxyObj((TSParentProxyEle *)ele);
    break;

  case TS_VOLUME: /* volume.config */
    ele_obj = (CfgEleObj *)new VolumeObj((TSVolumeEle *)ele);
    break;

  case TS_PLUGIN:
    ele_obj = (CfgEleObj *)new PluginObj((TSPluginEle *)ele);
    break;

  case TS_REMAP_MAP:           /* remap.config */
  case TS_REMAP_REVERSE_MAP:   // fall-through
  case TS_REMAP_REDIRECT:      // fall-through
  case TS_REMAP_REDIRECT_TEMP: // fall-through
    ele_obj = (CfgEleObj *)new RemapObj((TSRemapEle *)ele);
    break;

  case TS_SOCKS_BYPASS: /* socks.config */
  case TS_SOCKS_AUTH:
  case TS_SOCKS_MULTIPLE:
    ele_obj = (CfgEleObj *)new SocksObj((TSSocksEle *)ele);
    break;

  case TS_SPLIT_DNS: /* splitdns.config */
    ele_obj = (CfgEleObj *)new SplitDnsObj((TSSplitDnsEle *)ele);
    break;

  case TS_STORAGE: /* storage.config */
    ele_obj = (CfgEleObj *)new StorageObj((TSStorageEle *)ele);
    break;

  case TS_VADDRS: /* vaddrs.config */
    ele_obj = (CfgEleObj *)new VirtIpAddrObj((TSVirtIpAddrEle *)ele);
    break;
  case TS_TYPE_UNDEFINED:
  default:
    return NULL; // error
  }

  return ele_obj;
}

/*--------------------------------------------------------------
 * get_rule_type
 *--------------------------------------------------------------
 * determine what rule type the TokenList refers to by examining
 * the appropriate token-value pair in the TokenList
 */
TSRuleTypeT
get_rule_type(TokenList *token_list, TSFileNameT file)
{
  Token *tok;

  // ink_asser(ttoken_list);
  if (!token_list)
    return TS_TYPE_UNDEFINED;

  /* Depending on the file and rule type, need to find out which
     token specifies which type of rule it is */
  switch (file) {
  case TS_FNAME_CACHE_OBJ: /* cache.config */
    tok = token_list->first();
    while (tok != NULL) {
      if (strcmp(tok->name, "action") == 0) {
        if (strcmp(tok->value, "never-cache") == 0) {
          return TS_CACHE_NEVER;
        } else if (strcmp(tok->value, "ignore-no-cache") == 0) {
          return TS_CACHE_IGNORE_NO_CACHE;
        } else if (strcmp(tok->value, "cluster-cache-local") == 0) {
          return TS_CACHE_CLUSTER_CACHE_LOCAL;
        } else if (strcmp(tok->value, "ignore-client-no-cache") == 0) {
          return TS_CACHE_IGNORE_CLIENT_NO_CACHE;
        } else if (strcmp(tok->value, "ignore-server-no-cache") == 0) {
          return TS_CACHE_IGNORE_SERVER_NO_CACHE;
        } else if (strcmp(tok->value, "cache-auth-content") == 0) {
          return TS_CACHE_AUTH_CONTENT;
        } else {
          return TS_TYPE_UNDEFINED;
        }
      } else if (strcmp(tok->name, "pin-in-cache") == 0) {
        return TS_CACHE_PIN_IN_CACHE;
      } else if (strcmp(tok->name, "revalidate") == 0) {
        return TS_CACHE_REVALIDATE;
      } else if (strcmp(tok->name, "ttl-in-cache") == 0) {
        return TS_CACHE_TTL_IN_CACHE;
      } else { // try next token
        tok = token_list->next(tok);
      }
    }
    // if reached this point, there is no action specified
    return TS_TYPE_UNDEFINED;

  case TS_FNAME_CONGESTION: /* congestion.config */
    return TS_CONGESTION;

  case TS_FNAME_HOSTING: /* hosting.config */
    return TS_HOSTING;

  case TS_FNAME_ICP_PEER: /* icp.config */
    return TS_ICP;

  case TS_FNAME_IP_ALLOW: /* ip_allow.config */
    return TS_IP_ALLOW;

  case TS_FNAME_LOGS_XML: /* logs_xml.config */
    printf(" *** CfgContextUtils.cc: NOT DONE YET! **\n");
    //  TS_LOG_FILTER,             /* logs_xml.config */
    //  TS_LOG_OBJECT,
    //  TS_LOG_FORMAT,
    return TS_LOG_FILTER;

  case TS_FNAME_PARENT_PROXY: /* parent.config */
    // search fro go_direct action name and recongize the value-> ture or false
    for (tok = token_list->first(); tok; tok = token_list->next(tok)) {
      if ((strcmp(tok->name, "go_direct") == 0) && (strcmp(tok->value, "true") == 0)) {
        return TS_PP_GO_DIRECT;
      }
    }
    return TS_PP_PARENT;

  case TS_FNAME_VOLUME: /* volume.config */
    return TS_VOLUME;

  case TS_FNAME_PLUGIN: /* plugin.config */
    return TS_PLUGIN;

  case TS_FNAME_REMAP: /* remap.config */
    tok = token_list->first();
    if (strcmp(tok->name, "map") == 0) {
      return TS_REMAP_MAP;
    } else if (strcmp(tok->name, "reverse_map") == 0) {
      return TS_REMAP_REVERSE_MAP;
    } else if (strcmp(tok->name, "redirect") == 0) {
      return TS_REMAP_REDIRECT;
    } else if (strcmp(tok->name, "redirect_temporary") == 0) {
      return TS_REMAP_REDIRECT_TEMP;
    } else {
      return TS_TYPE_UNDEFINED;
    }
  case TS_FNAME_SOCKS: /* socks.config */
    tok = token_list->first();
    if (strcmp(tok->name, "no_socks") == 0) {
      return TS_SOCKS_BYPASS;
    } else if (strcmp(tok->name, "auth") == 0) {
      return TS_SOCKS_AUTH;
    } else if (strcmp(tok->name, "dest_ip") == 0) {
      return TS_SOCKS_MULTIPLE;
    } else {
      return TS_TYPE_UNDEFINED;
    }
  case TS_FNAME_SPLIT_DNS: /* splitdns.config */
    return TS_SPLIT_DNS;

  case TS_FNAME_STORAGE: /* storage.config */
    return TS_STORAGE;

  case TS_FNAME_VADDRS: /* vaddrs.config */
    return TS_VADDRS;
  case TS_FNAME_UNDEFINED:
  default:
    return TS_TYPE_UNDEFINED;
  }

  return TS_TYPE_UNDEFINED; // Should not reach here
}

///////////////////////////////////////////////////////////////////
// Since we are using the ele's as C structures wrapped in C++ classes
// we need to write copy function that "copy" the C structures
// 1) need copy functions for each helper information struct which
//    are typically embedded in the Ele's (eg. any lists, TSSspec)
// 2) need copy functions for each Ele; these functiosn will actually
// be called by teh copy constructors and overloaded assignment
// operators in each CfgEleObj subclass!!
///////////////////////////////////////////////////////////////////
void
copy_cfg_ele(TSCfgEle *src_ele, TSCfgEle *dst_ele)
{
  // ink_assert (src_ele && dst_ele);
  if (!src_ele || !dst_ele)
    return;

  dst_ele->type  = src_ele->type;
  dst_ele->error = src_ele->error;
}

void
copy_sspec(TSSspec *src, TSSspec *dst)
{
  // ink_assert(src && dst);
  if (!src || !dst)
    return;

  dst->active      = src->active;
  dst->time.hour_a = src->time.hour_a;
  dst->time.min_a  = src->time.min_a;
  dst->time.hour_b = src->time.hour_b;
  dst->time.min_b  = src->time.min_b;
  if (src->src_ip)
    dst->src_ip = ats_strdup(src->src_ip);
  if (src->prefix)
    dst->prefix = ats_strdup(src->prefix);
  if (src->suffix)
    dst->suffix = ats_strdup(src->suffix);
  dst->port     = copy_port_ele(src->port);
  dst->method   = src->method;
  dst->scheme   = src->scheme;
}

void
copy_pdss_format(TSPdSsFormat *src_pdss, TSPdSsFormat *dst_pdss)
{
  // ink_assert (src_pdss && dst_pdss);
  if (!src_pdss || !dst_pdss)
    return;

  dst_pdss->pd_type = src_pdss->pd_type;
  if (src_pdss->pd_val)
    dst_pdss->pd_val = ats_strdup(src_pdss->pd_val);
  copy_sspec(&(src_pdss->sec_spec), &(dst_pdss->sec_spec));
}

void
copy_hms_time(TSHmsTime *src, TSHmsTime *dst)
{
  if (!src || !dst)
    return;

  dst->d = src->d;
  dst->h = src->h;
  dst->m = src->m;
  dst->s = src->s;
}

TSIpAddrEle *
copy_ip_addr_ele(TSIpAddrEle *src_ele)
{
  TSIpAddrEle *dst_ele;

  if (!src_ele)
    return NULL;

  dst_ele       = TSIpAddrEleCreate();
  dst_ele->type = src_ele->type;
  if (src_ele->ip_a)
    dst_ele->ip_a = ats_strdup(src_ele->ip_a);
  dst_ele->cidr_a = src_ele->cidr_a;
  dst_ele->port_a = src_ele->port_a;
  if (src_ele->ip_b)
    dst_ele->ip_b = ats_strdup(src_ele->ip_b);
  dst_ele->cidr_b = src_ele->cidr_b;
  dst_ele->port_b = src_ele->port_b;

  return dst_ele;
}

TSPortEle *
copy_port_ele(TSPortEle *src_ele)
{
  TSPortEle *dst_ele;

  if (!src_ele)
    return NULL;

  dst_ele         = TSPortEleCreate();
  dst_ele->port_a = src_ele->port_a;
  dst_ele->port_b = src_ele->port_b;

  return dst_ele;
}

TSDomain *
copy_domain(TSDomain *src_dom)
{
  TSDomain *dst_dom;

  if (!src_dom)
    return NULL;

  dst_dom = TSDomainCreate();
  if (src_dom->domain_val)
    dst_dom->domain_val = ats_strdup(src_dom->domain_val);
  dst_dom->port         = src_dom->port;

  return dst_dom;
}

TSIpAddrList
copy_ip_addr_list(TSIpAddrList list)
{
  TSIpAddrList nlist;
  TSIpAddrEle *ele, *nele;
  int count, i;

  if (list == TS_INVALID_LIST)
    return TS_INVALID_LIST;

  nlist = TSIpAddrListCreate();
  count = TSIpAddrListLen(list);
  for (i = 0; i < count; i++) {
    ele  = TSIpAddrListDequeue(list);
    nele = copy_ip_addr_ele(ele);
    TSIpAddrListEnqueue(list, ele);
    TSIpAddrListEnqueue(nlist, nele);
  }

  return nlist;
}

TSPortList
copy_port_list(TSPortList list)
{
  TSPortList nlist;
  TSPortEle *ele, *nele;
  int count, i;

  if (list == TS_INVALID_LIST)
    return TS_INVALID_LIST;

  nlist = TSPortListCreate();
  count = TSPortListLen(list);
  for (i = 0; i < count; i++) {
    ele  = TSPortListDequeue(list);
    nele = copy_port_ele(ele);
    TSPortListEnqueue(list, ele);
    TSPortListEnqueue(nlist, nele);
  }

  return nlist;
}

TSDomainList
copy_domain_list(TSDomainList list)
{
  TSDomainList nlist;
  TSDomain *ele, *nele;
  int count, i;

  if (list == TS_INVALID_LIST)
    return TS_INVALID_LIST;

  nlist = TSDomainListCreate();
  count = TSDomainListLen(list);
  for (i = 0; i < count; i++) {
    ele  = TSDomainListDequeue(list);
    nele = copy_domain(ele);
    TSDomainListEnqueue(list, ele);
    TSDomainListEnqueue(nlist, nele);
  }

  return nlist;
}

TSStringList
copy_string_list(TSStringList list)
{
  TSStringList nlist;
  char *ele, *nele;
  int count, i;

  if (list == TS_INVALID_LIST)
    return TS_INVALID_LIST;

  nlist = TSStringListCreate();
  count = TSStringListLen(list);
  for (i = 0; i < count; i++) {
    ele  = TSStringListDequeue(list);
    nele = ats_strdup(ele);
    TSStringListEnqueue(list, ele);
    TSStringListEnqueue(nlist, nele);
  }

  return nlist;
}

TSIntList
copy_int_list(TSIntList list)
{
  TSIntList nlist;
  int *elem, *nelem;
  int count, i;

  if (list == TS_INVALID_LIST)
    return TS_INVALID_LIST;

  nlist = TSIntListCreate();
  count = TSIntListLen(list);
  for (i = 0; i < count; i++) {
    elem   = TSIntListDequeue(list);
    nelem  = (int *)ats_malloc(sizeof(int));
    *nelem = *elem;
    TSIntListEnqueue(list, elem);
    TSIntListEnqueue(nlist, nelem);
  }

  return nlist;
}

TSCacheEle *
copy_cache_ele(TSCacheEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSCacheEle *nele = TSCacheEleCreate(ele->cfg_ele.type);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  copy_pdss_format(&(ele->cache_info), &(nele->cache_info));
  copy_hms_time(&(ele->time_period), &(nele->time_period));

  return nele;
}

TSCongestionEle *
copy_congestion_ele(TSCongestionEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSCongestionEle *nele = TSCongestionEleCreate();
  if (!nele)
    return NULL;
  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  // copy_pdss_format(&(ele->congestion_info), &(nele->congestion_info));
  nele->pd_type = ele->pd_type;
  nele->pd_val  = ats_strdup(ele->pd_val);
  if (ele->prefix)
    nele->prefix                = ats_strdup(ele->prefix);
  nele->port                    = ele->port;
  nele->scheme                  = ele->scheme;
  nele->max_connection_failures = ele->max_connection_failures;
  nele->fail_window             = ele->fail_window;
  nele->proxy_retry_interval    = ele->proxy_retry_interval;
  nele->client_wait_interval    = ele->client_wait_interval;
  nele->wait_interval_alpha     = ele->wait_interval_alpha;
  nele->live_os_conn_timeout    = ele->live_os_conn_timeout;
  nele->live_os_conn_retries    = ele->live_os_conn_retries;
  nele->dead_os_conn_timeout    = ele->dead_os_conn_timeout;
  nele->dead_os_conn_retries    = ele->dead_os_conn_retries;
  nele->max_connection          = ele->max_connection;
  if (ele->error_page_uri)
    nele->error_page_uri = ats_strdup(ele->error_page_uri);

  return nele;
}

TSHostingEle *
copy_hosting_ele(TSHostingEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSHostingEle *nele = TSHostingEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->pd_type = ele->pd_type;
  if (ele->pd_val)
    nele->pd_val = ats_strdup(ele->pd_val);
  ele->volumes   = copy_int_list(ele->volumes);

  return nele;
}

TSIcpEle *
copy_icp_ele(TSIcpEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSIcpEle *nele = TSIcpEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->peer_hostname)
    nele->peer_hostname = ats_strdup(ele->peer_hostname);
  if (ele->peer_host_ip_addr)
    nele->peer_host_ip_addr = ats_strdup(ele->peer_host_ip_addr);
  nele->peer_type           = ele->peer_type;
  nele->peer_proxy_port     = ele->peer_proxy_port;
  nele->peer_icp_port       = ele->peer_icp_port;
  nele->is_multicast        = ele->is_multicast;
  if (ele->mc_ip_addr)
    nele->mc_ip_addr = ats_strdup(ele->mc_ip_addr);
  nele->mc_ttl       = ele->mc_ttl;

  return nele;
}

TSIpAllowEle *
copy_ip_allow_ele(TSIpAllowEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSIpAllowEle *nele = TSIpAllowEleCreate();
  if (!nele)
    return NULL;
  if (ele->src_ip_addr)
    nele->src_ip_addr = copy_ip_addr_ele(ele->src_ip_addr);
  nele->action        = ele->action;
  return nele;
}

TSLogFilterEle *
copy_log_filter_ele(TSLogFilterEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSLogFilterEle *nele = TSLogFilterEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->action = ele->action;
  if (ele->filter_name)
    ele->filter_name = ats_strdup(nele->filter_name);
  if (ele->log_field)
    nele->log_field = ats_strdup(ele->log_field);
  nele->compare_op  = ele->compare_op;
  if (ele->compare_str)
    nele->compare_str = ats_strdup(ele->compare_str);
  nele->compare_int   = ele->compare_int;

  return nele;
}

TSLogFormatEle *
copy_log_format_ele(TSLogFormatEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSLogFormatEle *nele = TSLogFormatEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->name)
    nele->name = ats_strdup(ele->name);
  if (ele->format)
    nele->format                = ats_strdup(ele->format);
  nele->aggregate_interval_secs = ele->aggregate_interval_secs;

  return nele;
}

TSLogObjectEle *
copy_log_object_ele(TSLogObjectEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSLogObjectEle *nele = TSLogObjectEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->format_name)
    nele->format_name = ats_strdup(ele->format_name);
  if (ele->file_name)
    nele->file_name     = ats_strdup(ele->file_name);
  nele->log_mode        = ele->log_mode;
  nele->collation_hosts = copy_domain_list(ele->collation_hosts);
  nele->filters         = copy_string_list(ele->filters);
  nele->protocols       = copy_string_list(ele->protocols);
  nele->server_hosts    = copy_string_list(ele->server_hosts);

  return nele;
}

TSParentProxyEle *
copy_parent_proxy_ele(TSParentProxyEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSParentProxyEle *nele = TSParentProxyEleCreate(TS_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  copy_pdss_format(&(ele->parent_info), &(nele->parent_info));
  nele->rr         = ele->rr;
  nele->proxy_list = copy_domain_list(ele->proxy_list);
  nele->direct     = ele->direct;

  return nele;
}

TSVolumeEle *
copy_volume_ele(TSVolumeEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSVolumeEle *nele = TSVolumeEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->volume_num  = ele->volume_num;
  nele->scheme      = ele->scheme;
  nele->volume_size = ele->volume_size;
  nele->size_format = ele->size_format;

  return nele;
}

TSPluginEle *
copy_plugin_ele(TSPluginEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSPluginEle *nele = TSPluginEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->name)
    nele->name = ats_strdup(ele->name);
  nele->args   = copy_string_list(ele->args);

  return nele;
}

TSRemapEle *
copy_remap_ele(TSRemapEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSRemapEle *nele = TSRemapEleCreate(TS_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->map         = ele->map;
  nele->from_scheme = ele->from_scheme;
  if (ele->from_host)
    nele->from_host = ats_strdup(ele->from_host);
  nele->from_port   = ele->from_port;
  if (ele->from_path_prefix)
    nele->from_path_prefix = ats_strdup(ele->from_path_prefix);
  nele->to_scheme          = ele->to_scheme;
  if (ele->to_host)
    nele->to_host = ats_strdup(ele->to_host);
  nele->to_port   = ele->to_port;
  if (ele->to_path_prefix)
    nele->to_path_prefix = ats_strdup(ele->to_path_prefix);

  return nele;
}

TSSocksEle *
copy_socks_ele(TSSocksEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSSocksEle *nele = TSSocksEleCreate(TS_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->ip_addrs      = copy_ip_addr_list(ele->ip_addrs);
  nele->dest_ip_addr  = copy_ip_addr_ele(ele->dest_ip_addr);
  nele->socks_servers = copy_domain_list(ele->socks_servers);
  nele->rr            = ele->rr;
  if (ele->username)
    nele->username = ats_strdup(ele->username);
  if (ele->password)
    nele->password = ats_strdup(ele->password);

  return nele;
}

TSSplitDnsEle *
copy_split_dns_ele(TSSplitDnsEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSSplitDnsEle *nele = TSSplitDnsEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->pd_type = ele->pd_type;
  if (ele->pd_val)
    nele->pd_val          = ats_strdup(ele->pd_val);
  nele->dns_servers_addrs = copy_domain_list(ele->dns_servers_addrs);
  if (ele->def_domain)
    nele->def_domain = ats_strdup(ele->def_domain);
  nele->search_list  = copy_domain_list(ele->search_list);

  return nele;
}

TSStorageEle *
copy_storage_ele(TSStorageEle *ele)
{
  if (!ele) {
    return NULL;
  }

  TSStorageEle *nele = TSStorageEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->pathname)
    nele->pathname = ats_strdup(ele->pathname);
  nele->size       = ele->size;

  return nele;
}

TSVirtIpAddrEle *
copy_virt_ip_addr_ele(TSVirtIpAddrEle *ele)
{
  TSVirtIpAddrEle *new_ele;

  if (!ele) {
    return NULL;
  }

  new_ele = TSVirtIpAddrEleCreate();
  if (!new_ele)
    return NULL;

  // copy cfg ele
  copy_cfg_ele(&(ele->cfg_ele), &(new_ele->cfg_ele));
  new_ele->ip_addr  = ats_strdup(ele->ip_addr);
  new_ele->intr     = ats_strdup(ele->intr);
  new_ele->sub_intr = ele->sub_intr;

  return new_ele;
}

INKCommentEle *
copy_comment_ele(INKCommentEle *ele)
{
  if (!ele)
    return NULL;

  INKCommentEle *nele = comment_ele_create(ele->comment);
  return nele;
}

/***************************************************************************
 * Functions needed by implementation but must be hidden from user
 ***************************************************************************/
INKCommentEle *
comment_ele_create(char *comment)
{
  INKCommentEle *ele;

  ele = (INKCommentEle *)ats_malloc(sizeof(INKCommentEle));

  ele->cfg_ele.type  = TS_TYPE_COMMENT;
  ele->cfg_ele.error = TS_ERR_OKAY;
  if (comment)
    ele->comment = ats_strdup(comment);
  else // comment is NULL
    ele->comment = NULL;

  return ele;
}

void
comment_ele_destroy(INKCommentEle *ele)
{
  if (ele) {
    ats_free(ele->comment);
    ats_free(ele);
  }

  return;
}
