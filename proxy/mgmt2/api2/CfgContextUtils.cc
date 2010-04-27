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
#include "ink_platform.h"
#include "CfgContextUtils.h"
#include "Tokenizer.h"
#if defined(OEM)
#include "CoreAPI.h"
#endif
/***************************************************************************
 * Conversion Functions
 ***************************************************************************/
/* ---------------------------------------------------------------------------
 * string_to_ip_addr_ele
 * ---------------------------------------------------------------------------
 * Converts ip address string format to an INKIpAddrEle.
 * Determines if single/range, cidr/not-cidr based on format of string.
 *  
 * if SINGLE =  ip_a/cidr_a
 * if RANGE =   ip_a/cidr_a-ip_b/cidr_b (possible to have spaces next to dash)
 * Returns NULL if invalid ele (eg. if ip's are invalid)
 */
INKIpAddrEle *
string_to_ip_addr_ele(const char *str)
{
  Tokenizer range_tokens(RANGE_DELIMITER_STR);
  Tokenizer cidr_tokens(CIDR_DELIMITER_STR);
  Tokenizer cidr_tokens2(CIDR_DELIMITER_STR);
  INKIpAddrEle *ele;
  const char *const_ip_a, *const_ip_b;
  char *ip_a = NULL, *ip_b = NULL;
  int numTokens = 0;
  char buf[MAX_BUF_SIZE];

  //ink_assert(str);
  if (!str)
    return NULL;

  ele = INKIpAddrEleCreate();
  if (!ele)
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);
  snprintf(buf, sizeof(buf), "%s", str);

  // determine if range or single type 
  range_tokens.Initialize(buf, COPY_TOKS);
  numTokens = range_tokens.getNumber();
  if (numTokens == 1) {         // SINGLE TYPE 
    ele->type = INK_IP_SINGLE;
    // determine if cidr type
    cidr_tokens.Initialize(buf, COPY_TOKS);
    numTokens = cidr_tokens.getNumber();
    if (numTokens == 1) {       // Single, NON-CIDR TYPE 
      ele->ip_a = string_to_ip_addr(str);
    } else {                    // Single, CIDR TYPE
      if (!isNumber(cidr_tokens[1]))
        goto Lerror;
      ele->ip_a = string_to_ip_addr(cidr_tokens[0]);
      ele->cidr_a = ink_atoi(cidr_tokens[1]);
    }
    if (!ele->ip_a)             // ERROR: Invalid ip 
      goto Lerror;
  } else {                      // RANGE TYPE 
    ele->type = INK_IP_RANGE;
    const_ip_a = range_tokens[0];
    const_ip_b = range_tokens[1];
    ip_a = xstrdup(const_ip_a);
    ip_b = xstrdup(const_ip_b);

    // determine if ip's are cidr type; only test if ip_a is cidr, assume both are same 
    cidr_tokens.Initialize(ip_a, COPY_TOKS);
    numTokens = cidr_tokens.getNumber();
    if (numTokens == 1) {       // Range, NON-CIDR TYPE 
      ele->ip_a = string_to_ip_addr(ip_a);
      ele->ip_b = string_to_ip_addr(ip_b);
    } else {                    // Range, CIDR TYPE */
      ele->ip_a = string_to_ip_addr(cidr_tokens[0]);
      ele->cidr_a = ink_atoi(cidr_tokens[1]);
      cidr_tokens2.Initialize(ip_b, COPY_TOKS);
      ele->ip_b = string_to_ip_addr(cidr_tokens2[0]);
      ele->cidr_b = ink_atoi(cidr_tokens2[1]);
      if (!isNumber(cidr_tokens[1]) || !isNumber(cidr_tokens2[1]))
        goto Lerror;
    }
    if (!ele->ip_a || !ele->ip_b)       // ERROR: invalid IP
      goto Lerror;
  }

  if (ip_a)
    xfree(ip_a);
  if (ip_b)
    xfree(ip_b);
  return ele;

Lerror:
  if (ip_a)
    xfree(ip_a);
  if (ip_b)
    xfree(ip_b);
  INKIpAddrEleDestroy(ele);
  return NULL;

}


/* ----------------------------------------------------------------------------
 * ip_addr_ele_to_string
 * ---------------------------------------------------------------------------
 * Converts an  INKIpAddrEle to following string format:
 * Output:
 * if SINGLE =             ip_a/cidr_a
 * if RANGE =              ip_a/cidr_a-ip_b/cidr_b
 * If there is no cidr =   ip_a-ip_b
 * Returns NULL if invalid ele (needs to check that the ip's are valid too)
 */
char *
ip_addr_ele_to_string(INKIpAddrEle * ele)
{
  char buf[MAX_BUF_SIZE];
  char *str, *ip_a_str = NULL, *ip_b_str = NULL;

  //ink_assert(ele);
  if (!ele)
    goto Lerror;

  memset(buf, 0, MAX_BUF_SIZE);

  if (ele->ip_a == INK_INVALID_IP_ADDR)
    goto Lerror;                // invalid ip_addr

  if (ele->type == INK_IP_SINGLE) {     // SINGLE TYPE 
    ip_a_str = ip_addr_to_string(ele->ip_a);
    if (!ip_a_str)              // ERROR: invalid IP address 
      goto Lerror;
    if (ele->cidr_a != INK_INVALID_IP_CIDR) {   // a cidr type  
      snprintf(buf, sizeof(buf), "%s%c%d", ip_a_str, CIDR_DELIMITER, ele->cidr_a);
    } else {                    // not cidr type
      snprintf(buf, sizeof(buf), "%s", ip_a_str);
    }

    xfree(ip_a_str);
    str = xstrdup(buf);

    return str;
  } else if (ele->type == INK_IP_RANGE) {       // RANGE TYPE 
    ip_a_str = ip_addr_to_string(ele->ip_a);
    ip_b_str = ip_addr_to_string(ele->ip_b);

    if (!ip_a_str || !ip_b_str)
      goto Lerror;

    if (ele->cidr_a != INK_INVALID_IP_CIDR && ele->cidr_b != INK_INVALID_IP_CIDR) {
      // a cidr type 
      snprintf(buf, sizeof(buf), "%s%c%d%c%s%c%d",
               ip_a_str, CIDR_DELIMITER, ele->cidr_a, RANGE_DELIMITER, ip_b_str, CIDR_DELIMITER, ele->cidr_b);
    } else {                    // not cidr type 
      snprintf(buf, sizeof(buf), "%s%c%s", ip_a_str, RANGE_DELIMITER, ip_b_str);
    }
    if (ip_a_str)
      xfree(ip_a_str);
    if (ip_b_str)
      xfree(ip_b_str);
    str = xstrdup(buf);
    return str;
  }

Lerror:
  if (ip_a_str)
    xfree(ip_a_str);
  if (ip_b_str)
    xfree(ip_b_str);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * ip_addr_to_string
 * ---------------------------------------------------------------------------
 * Converts an  INKIpAddr (char*) to dotted decimal string notation. Allocates
 * memory for the new INKIpAddr. 
 * Returns NULL if invalid INKIpAddr
 */
char *
ip_addr_to_string(INKIpAddr ip)
{
  //ink_assert(ip != INK_INVALID_IP_ADDR);
  if (ip == INK_INVALID_IP_ADDR) {
    return NULL;
  }
  if (!ccu_checkIpAddr(ip)) {
    return NULL;
  }
  return xstrdup((char *) ip);
}

/* ----------------------------------------------------------------------------
 * string_to_ip_addr
 * ---------------------------------------------------------------------------
 * Converts an ip address in dotted-decimal string format into a string; 
 * allocates memory for string. If IP is invalid, then returns INK_INVALID_IP_ADDR.
 */
INKIpAddr
string_to_ip_addr(const char *str)
{
  //ink_assert(str);
  if (!ccu_checkIpAddr(str))
    return INK_INVALID_IP_ADDR;

  char *copy;
  copy = xstrdup(str);
  return (INKIpAddr) copy;
}

/* --------------------------------------------------------------- 
 * ip_addr_list_to_string
 * --------------------------------------------------------------- 
 * converts INKIpAddrList <==> ip_addr1<delim>ip_addr2<delim>ip_addr3, ... 
 * Returns INKIpAddrList with original elements.
 * If encounters invalid INKIpAddrEle, returns NULL.
 */
char *
ip_addr_list_to_string(IpAddrList * list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  int buf_pos = 0;
  INKIpAddrEle *ip_ele;
  char *ip_str, *new_str;
  int num, i;

  //ink_assert(list && delimiter);
  if (!list || !delimiter)
    return NULL;

  num = queue_len((LLQ *) list);

  for (i = 0; i < num; i++) {
    ip_ele = (INKIpAddrEle *) dequeue((LLQ *) list);    //read next ip
    ip_str = ip_addr_ele_to_string(ip_ele);

    if (!ip_str) {
      enqueue((LLQ *) list, ip_ele);
      return NULL;
    }
    if (i == num - 1)
      snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", ip_str);
    else
      snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s%s", ip_str, delimiter);
    buf_pos = strlen(buf);
    xfree(ip_str);

    enqueue((LLQ *) list, ip_ele);      // return ip to list
  }

  new_str = xstrdup(buf);

  return new_str;
}

/* --------------------------------------------------------------- 
 * string_to_ip_addr_list
 * --------------------------------------------------------------- 
 * Converts ip_addr1<delim>ip_addr2<delim>ip_addr3, ...==> INKIpAddrList 
 * Does checking to make sure that each ip_addr is valid; if encounter
 * an invalid ip addr, then returns INK_INVALID_LIST
 */
INKIpAddrList
string_to_ip_addr_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  INKIpAddrList ip_list;
  INKIpAddrEle *ip_ele;

  //ink_assert(str_list && delimiter);
  if (!str_list || !delimiter)
    return INK_INVALID_LIST;

  tokens.Initialize(str_list);
  numToks = tokens.getNumber();

  ip_list = INKIpAddrListCreate();

  for (i = 0; i < numToks; i++) {
    ip_ele = string_to_ip_addr_ele(tokens[i]);
    if (ip_ele) {
      INKIpAddrListEnqueue(ip_list, ip_ele);
    } else {                    // Error: invalid IP
      INKIpAddrListDestroy(ip_list);
      return INK_INVALID_LIST;
    }
  }
  return ip_list;
}

/* --------------------------------------------------------------- 
 * port_list_to_string (REPLACE BY sprintf_ports)
 * --------------------------------------------------------------- 
 * Purpose: prints a list of ports in a PortList into string delimited format
 * Input:  ports - the queue of INKPortEle *'s. 
 * Output: port_0<delim>port_1<delim>...<delim>port_n
 *         (each port can refer to a port range, eg 80-90)
 *         Return NULL if encounter invalid port or empty port list 
 */
char *
port_list_to_string(PortList * ports, const char *delimiter)
{
  int num_ports;
  size_t pos = 0;
  int i, psize;
  INKPortEle *port_ele;
  char buf[MAX_BUF_SIZE];
  char *str;

  //ink_assert(ports && delimiter);
  if (!ports || !delimiter)
    goto Lerror;

  num_ports = queue_len((LLQ *) ports);
  if (num_ports <= 0) {         // no ports specified
    goto Lerror;
  }
  // now list all the ports, including ranges 
  for (i = 0; i < num_ports; i++) {
    port_ele = (INKPortEle *) dequeue((LLQ *) ports);
    if (!ccu_checkPortEle(port_ele)) {
      enqueue((LLQ *) ports, port_ele); // return INKPortEle to list 
      goto Lerror;
    }

    if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "%d", port_ele->port_a)) > 0) {
      pos += psize;
    }
    if (port_ele->port_b != INK_INVALID_PORT) { //. is this a range 
      // add in range delimiter & end of range 
      if (pos < sizeof(buf) &&
          (psize = snprintf(buf + pos, sizeof(buf) - pos, "%c%d", RANGE_DELIMITER, port_ele->port_b)) > 0)
        pos += psize;
    }

    if (i != num_ports - 1) {
      if (pos<sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf - pos), "%s", delimiter))> 0)
        pos += psize;
    }

    enqueue((LLQ *) ports, port_ele);   // return INKPortEle to list 
  }

  str = xstrdup(buf);
  return str;

Lerror:
  return NULL;
}

/* --------------------------------------------------------------- 
 * string_to_port_list
 * --------------------------------------------------------------- 
 * Converts port1<delim>port2<delim>port3, ...==> INKPortList 
 * Does checking to make sure that each ip_addr is valid; if encounter
 * an invalid ip addr, then returns INKT_INVALID_LIST
 */
INKPortList
string_to_port_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  INKPortList port_list;
  INKPortEle *port_ele;

  //ink_assert(str_list && delimiter);
  if (!str_list || !delimiter)
    return INK_INVALID_LIST;

  tokens.Initialize(str_list);

  numToks = tokens.getNumber();

  port_list = INKPortListCreate();

  for (i = 0; i < numToks; i++) {
    port_ele = string_to_port_ele(tokens[i]);
    if (port_ele) {
      INKPortListEnqueue(port_list, port_ele);
    } else {                    // error - invalid port ele
      INKPortListDestroy(port_list);
      return INK_INVALID_LIST;
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
port_ele_to_string(INKPortEle * ele)
{
  char buf[MAX_BUF_SIZE];
  char *str;

  //ink_assert(ele);
  if (!ele || !ccu_checkPortEle(ele))
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);

  if (ele->port_b == INK_INVALID_PORT) {        // Not a range 
    snprintf(buf, sizeof(buf), "%d", ele->port_a);
  } else {
    snprintf(buf, sizeof(buf), "%d%c%d", ele->port_a, RANGE_DELIMITER, ele->port_b);
  }

  str = xstrdup(buf);
  return str;
}

/*----------------------------------------------------------------------------
 * string_to_port_ele
 *---------------------------------------------------------------------------
 * Converts a string formatted port_ele into actual port_ele. Returns NULL if 
 * invalid port(s). It is okay to have a single port specified. 
 */
INKPortEle *
string_to_port_ele(const char *str)
{
  Tokenizer tokens(RANGE_DELIMITER_STR);
  INKPortEle *ele;
  char copy[MAX_BUF_SIZE];

  //ink_assert(str);
  if (!str)
    return NULL;

  memset(copy, 0, MAX_BUF_SIZE);
  snprintf(copy, sizeof(copy), "%s", str);

  ele = INKPortEleCreate();
  if (tokens.Initialize(copy, COPY_TOKS) > 2)
    goto Lerror;
  if (tokens.getNumber() == 1) {        // Not a Range of ports
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
  INKPortEleDestroy(ele);
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
string_list_to_string(INKStringList str_list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int i, numElems, psize;
  char *str_ele, *list_str;

  //ink_assert(str_list != INK_INVALID_LIST && delimiter);
  if (str_list == INK_INVALID_LIST || !delimiter)
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);
  numElems = queue_len((LLQ *) str_list);
  for (i = 0; i < numElems; i++) {
    str_ele = (char *) dequeue((LLQ *) str_list);

    if (i == numElems - 1) {    // the last element shouldn't print comma 
      if (buf_pos<sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", str_ele))> 0)
        buf_pos += psize;
    } else {
      if (buf_pos < sizeof(buf) &&
          (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s%s", str_ele, delimiter)) > 0)
        buf_pos += psize;
    }

    enqueue((LLQ *) str_list, str_ele);
  }

  list_str = xstrdup(buf);
  return list_str;
}

/* --------------------------------------------------------------- 
 * string_to_string_list
 * --------------------------------------------------------------- 
 * Converts port1<delim>port2<delim>port3, ...==> INKStringList 
 * Does checking to make sure that each ip_addr is valid; if encounter
 * an invalid ip addr, then returns INVALID_HANDLE
 */
INKStringList
string_to_string_list(const char *str, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  tokens.Initialize(str);

  //ink_assert(str && delimiter);
  if (!str || !delimiter)
    return INK_INVALID_LIST;

  INKStringList str_list = INKStringListCreate();
  for (int i = 0; i < tokens.getNumber(); i++) {
    INKStringListEnqueue(str_list, xstrdup(tokens[i]));
  }

  return str_list;
}

/*----------------------------------------------------------------------------
 * int_list_to_string
 *----------------------------------------------------------------------------
 * INKList(of char*'s only)==> elem1<delimiter>elem2<delimiter>elem3<delimiter>
 * Note: the string always ends with the delimiter
 * The list and its elements are not changed in any way.
 * Returns NULL if error.
 */
char *
int_list_to_string(INKIntList list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int numElems, i, psize;
  int *elem;

  //ink_assert(list != INK_INVALID_LIST && delimiter);
  if (list == INK_INVALID_LIST || !delimiter)
    return NULL;


  numElems = queue_len((LLQ *) list);

  memset(buf, 0, MAX_BUF_SIZE);
  for (i = 0; i < numElems; i++) {
    elem = (int *) dequeue((LLQ *) list);
    if (i == numElems - 1) {
      if (buf_pos<sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d", *elem))> 0)
        buf_pos += psize;
    } else {
      if (buf_pos < sizeof(buf) &&
          (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d%s", *elem, delimiter)) > 0)
        buf_pos += psize;
    }
    enqueue((LLQ *) list, elem);
  }
  return xstrdup(buf);
}

/* --------------------------------------------------------------- 
 * string_to_int_list
 * --------------------------------------------------------------- 
 * converts domain1<delim>domain2<delim>domain3, ... ==> INKList 
 * Does checking to make sure that each integer is valid; if encounter
 * an invalid int, then returns INK_INVALID_LIST
 */
INKIntList
string_to_int_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  INKList list;
  int *ele;

  //ink_assert (str_list  && delimiter);
  if (!str_list || !delimiter)
    return INK_INVALID_LIST;

  tokens.Initialize(str_list);

  numToks = tokens.getNumber();
  list = INKIntListCreate();

  for (i = 0; i < numToks; i++) {
    if (!isNumber(tokens[i]))
      goto Lerror;
    ele = (int *) xmalloc(sizeof(int));
    if (ele) {
      *ele = ink_atoi(tokens[i]);       // What about we can't convert? ERROR?
      INKIntListEnqueue(list, ele);
    }
  }

  return list;

Lerror:
  INKIntListDestroy(list);
  return INK_INVALID_LIST;
}


/* --------------------------------------------------------------- 
 * string_to_domain_list
 * --------------------------------------------------------------- 
 * Converts domain1<delim>domain2<delim>domain3, ... ==> INKDomainList 
 * Returns INK_INVALID_LIST if encounter an invalid Domain. 
 */
INKDomainList
string_to_domain_list(const char *str_list, const char *delimiter)
{
  Tokenizer tokens(delimiter);
  int numToks, i;
  INKDomainList list;
  INKDomain *ele;

  //ink_assert(str_list && delimiter);
  if (!str_list || !delimiter)
    return INK_INVALID_LIST;

  tokens.Initialize(str_list);

  numToks = tokens.getNumber();

  list = INKDomainListCreate();

  for (i = 0; i < numToks; i++) {
    ele = string_to_domain(tokens[i]);
    if (ele) {
      INKDomainListEnqueue(list, ele);
    } else {                    // Error: invalid domain
      INKDomainListDestroy(list);
      return INK_INVALID_LIST;
    }
  }

  return list;
}


/*----------------------------------------------------------------------------
 * domain_list_to_string
 *----------------------------------------------------------------------------
 * INKList(of char*'s only)==> elem1<delimiter>elem2<delimiter>elem3<delimiter>
 * Note: the string always ends with the delimiter
 * The list and its elements are not changed in any way.
 * Returns NULL if encounter an invalid INKDomain. 
 */
char *
domain_list_to_string(INKDomainList list, const char *delimiter)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int numElems, i, psize;
  char *list_str, *dom_str;
  INKDomain *domain;

  //ink_assert(list != INK_INVALID_LIST && delimiter);
  if (list == INK_INVALID_LIST || !delimiter)
    return NULL;

  numElems = queue_len((LLQ *) list);

  memset(buf, 0, MAX_BUF_SIZE);

  for (i = 0; i < numElems; i++) {
    domain = (INKDomain *) dequeue((LLQ *) list);

    dom_str = domain_to_string(domain);
    if (!dom_str) {
      return NULL;
    }
    if (i == numElems - 1) {    // the last element shouldn't print comma 
      if (buf_pos<sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", dom_str))> 0)
        buf_pos += psize;
    } else {
      if (buf_pos < sizeof(buf) &&
          (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s%s", dom_str, delimiter)) > 0)
        buf_pos += psize;
    }

    xfree(dom_str);
    enqueue((LLQ *) list, domain);
  }

  list_str = xstrdup(buf);
  return list_str;
}

/*----------------------------------------------------------------------------
 * domain_to_string
 *----------------------------------------------------------------------------
 * Converts an INKDomain into string format, eg. www.host.com:8080
 * Return NULL if invalid INKDomain (eg. missing domain value). 
 */
char *
domain_to_string(INKDomain * domain)
{
  char buf[MAX_BUF_SIZE];
  char *dom_str;

  //ink_assert(domain);
  if (!domain)
    return NULL;

  if (domain->domain_val) {
    if (domain->port != INK_INVALID_PORT)       // host:port 
      snprintf(buf, sizeof(buf), "%s:%d", domain->domain_val, domain->port);
    else                        // host
      snprintf(buf, sizeof(buf), "%s", domain->domain_val);
  } else {
    return NULL;                // invalid INKDomain 
  }

  dom_str = xstrdup(buf);

  return dom_str;
}

/*----------------------------------------------------------------------------
 * string_to_domain
 *----------------------------------------------------------------------------
 * Converts string format, eg. www.host.com:8080, into INKDomain.
 * The string can consist of just the host (which can be a name or an IP)
 * or of the host and port. 
 * Return NULL if invalid INKDomain (eg. missing domain value). 
 */
INKDomain *
string_to_domain(const char *str)
{
  INKDomain *dom;
  char *token, *remain, *token_pos;
  char buf[MAX_BUF_SIZE];

  //ink_assert(str);
  if (!str)
    return NULL;

  dom = INKDomainCreate();

  // get hostname
  ink_strncpy(buf, str, sizeof(buf));
  token = ink_strtok_r(buf, ":", &token_pos);
  remain = token_pos;
  if (token)
    dom->domain_val = xstrdup(token);
  else
    goto Lerror;

  // get port, if exists
  if (remain) {
    // check if the "remain" consist of all integers 
    if (!isNumber(remain))
      goto Lerror;
    dom->port = ink_atoi(remain);
  } else {
    dom->port = INK_INVALID_PORT;
  }

  return dom;

Lerror:
  INKDomainDestroy(dom);
  return NULL;
}

/* ---------------------------------------------------------------
 * pdest_sspec_to_string
 * --------------------------------------------------------------- 
 * Converts the INKPrimeDest, primary dest, secondary spec struct 
 * into string format: <pdT>:pdst_val:sspec1:sspec2:...:
 * <pdT> - dest_domain, dest_host, dest_ip, url_regex
 * even if sspec is missing the delimter is included; so if no 
 * sspecs, then : ::: ::: will appear
 */
char *
pdest_sspec_to_string(INKPrimeDestT pd, char *pd_val, INKSspec * sspec)
{
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int psize;
  char hour_a[3], hour_b[3], min_a[3], min_b[3];
  char *src_ip, *str;

  //ink_assert(pd != INK_PD_UNDEFINED && pd_val && sspec);
  if (pd == INK_PD_UNDEFINED || !pd_val || !sspec)
    return NULL;

  memset(buf, 0, MAX_BUF_SIZE);

  do {
    // push in primary destination
    switch (pd) {
    case INK_PD_DOMAIN:
      psize = snprintf(buf, sizeof(buf), "dest_domain=%s ", pd_val);
      break;
    case INK_PD_HOST:
      psize = snprintf(buf, sizeof(buf), "dest_host=%s ", pd_val);
      break;
    case INK_PD_IP:
      psize = snprintf(buf, sizeof(buf), "dest_ip=%s ", pd_val);
      break;
    case INK_PD_URL_REGEX:
      psize = snprintf(buf, sizeof(buf), "url_regex=%s ", pd_val);
      break;
    default:
      psize = 0;
      // Handled here:
      // INK_PD_UNDEFINED
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
            (psize =
             snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "time=%s:%s-%s:%s ", hour_a, min_a, hour_b, min_b)) > 0)
          buf_pos += psize;
      }
      // src_ip
      if (sspec->src_ip != INK_INVALID_IP_ADDR) {
        src_ip = ip_addr_to_string(sspec->src_ip);
        if (!src_ip) {
          return NULL;
        }
        if (buf_pos<sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "src_ip=%s ", src_ip))> 0)
          buf_pos += psize;
        xfree(src_ip);
      }
      // prefix?
      if (sspec->prefix) {
        if (buf_pos < sizeof(buf) &&
            (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "prefix=%s ", sspec->prefix)) > 0)
          buf_pos += psize;
      }
      // suffix?
      if (sspec->suffix) {
        if (buf_pos < sizeof(buf) &&
            (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "suffix=%s ", sspec->suffix)) > 0)
          buf_pos += psize;
      }
      // port?
      if (sspec->port) {
        char *portStr = port_ele_to_string(sspec->port);
        if (portStr) {
          if (buf_pos < sizeof(buf) &&
              (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "port=%s ", portStr)) > 0)
            buf_pos += psize;
          xfree(portStr);
        }
      }
      // method
      if (buf_pos < sizeof(buf)) {
        switch (sspec->method) {
        case INK_METHOD_GET:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=get ");
          break;
        case INK_METHOD_POST:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=post ");
          break;
        case INK_METHOD_PUT:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=put ");
          break;
        case INK_METHOD_TRACE:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "method=trace ");
          break;
        case INK_METHOD_PUSH:
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
        case INK_SCHEME_NONE:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%c", DELIMITER);
          break;
        case INK_SCHEME_HTTP:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=http ");
          break;
        case INK_SCHEME_HTTPS:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=https ");
          break;
        case INK_SCHEME_FTP:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=ftp ");
          break;
        case INK_SCHEME_RTSP:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=rtsp ");
          break;
        case INK_SCHEME_MMS:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "scheme=mms ");
          break;
        default:
          psize = 0;
          break;
        }
        if (psize > 0)
          buf_pos += psize;
      } else
        break;


      if (buf_pos < sizeof(buf)) {
        switch (sspec->mixt) {
        case INK_MIXT_RNI:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "tag=RNI ");
          break;
        case INK_MIXT_QT:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "tag=QT ");
          break;
        case INK_MIXT_WMT:
          psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "tag=WMT ");
          break;
        default:
          psize = 0;
          break;
        }
        if (psize > 0)
          buf_pos += psize;
      } else
        break;
    }
  } while (0);

  str = xstrdup(buf);
  return str;
}


/*----------------------------------------------------------------------------
 * string_to_pdss_format
 *----------------------------------------------------------------------------
 * <pd_type>#<pd_value>#<sspecs> --> INKPdSsFormat 
 * NOTE that the entire data line, including the action type is being passed in
 */
INKError
string_to_pdss_format(const char *str, INKPdSsFormat * pdss)
{
  Tokenizer tokens(DELIMITER_STR);
  Tokenizer time_tokens(":-");
  char copy[MAX_BUF_SIZE];

  //ink_assert(str && pdss);
  if (!str || !pdss)
    return INK_ERR_PARAMS;

  memset(copy, 0, MAX_BUF_SIZE);
  snprintf(copy, sizeof(copy), "%s", str);

  tokens.Initialize(copy, ALLOW_EMPTY_TOKS);

  // pd type 
  if (strcmp(tokens[1], "dest_domain") == 0) {
    pdss->pd_type = INK_PD_DOMAIN;
  } else if (strcmp(tokens[1], "dest_host") == 0) {
    pdss->pd_type = INK_PD_HOST;
  } else if (strcmp(tokens[1], "dest_ip") == 0) {
    pdss->pd_type = INK_PD_IP;
  } else if (strcmp(tokens[1], "url_regex") == 0) {
    pdss->pd_type = INK_PD_URL_REGEX;
  } else {
    goto Lerror;
  }

  // pd_value 
  if (!tokens[2])
    goto Lerror;
  pdss->pd_val = xstrdup(tokens[2]);

  // check secondary specifiers; exists only if not empty string 
  // time
  if (strlen(tokens[3]) > 0) {
    if (string_to_time_struct(tokens[3], &(pdss->sec_spec)) != INK_ERR_OKAY)
      goto Lerror;
  }
  // src_ip 
  if (strlen(tokens[4]) > 0) {
    pdss->sec_spec.src_ip = xstrdup(tokens[4]);
  }
  // prefix 
  if (strlen(tokens[5]) > 0) {
    pdss->sec_spec.prefix = xstrdup(tokens[5]);
  }
  // suffix 
  if (strlen(tokens[6]) > 0) {
    pdss->sec_spec.suffix = xstrdup(tokens[6]);
  }
  // port 
  if (strlen(tokens[7]) > 0) {  // no port 
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
  // mixt tag 
  if (strlen(tokens[10]) > 0) {
    pdss->sec_spec.mixt = string_to_mixt_type(tokens[10]);
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}


/*----------------------------------------------------------------------------
 * hms_time_to_string
 *----------------------------------------------------------------------------
 * Converts an INKHmsTime structure to string format: eg. 5h15m20s
 */
char *
hms_time_to_string(INKHmsTime time)
{
  char *str;
  char buf[MAX_BUF_SIZE];
  size_t buf_pos = 0;
  int psize = 0;

  memset(buf, 0, MAX_BUF_SIZE);
  if (time.d > 0 && buf_pos < sizeof(buf) &&
      (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%dd", time.d)) > 0)
    buf_pos += psize;
  if (time.h > 0 && buf_pos < sizeof(buf) &&
      (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%dh", time.h)) > 0)
    buf_pos += psize;
  if (time.m > 0 && buf_pos < sizeof(buf) &&
      (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%dm", time.m)) > 0)
    buf_pos += psize;
  if (time.s > 0 && buf_pos < sizeof(buf) &&
      (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%ds", time.s)) > 0)
    buf_pos += psize;

  str = xstrdup(buf);

  return str;
}

/*----------------------------------------------------------------------------
 * string_to_hms_time
 *----------------------------------------------------------------------------
 * Convert ?d?h?m?s ==> INKHmsTime 
 * Returns INK_ERR_FAIL if invalid hms format, eg. if there are invalid 
 * characters, eg. "10xh", "10h15m30s34", or repeated values, eg. "10h15h"
 */
INKError
string_to_hms_time(const char *str, INKHmsTime * time)
{
  int i, pos = 0;
  int len;
  char unit[10];
  bool valid = false;

  //ink_assert(str && time);
  if (!str || !time)
    return INK_ERR_PARAMS;

  memset(unit, 0, 10);
  len = strlen(str);
  time->d = time->h = time->m = time->s = 0;
  for (i = 0; i < len; i++) {
    valid = false;
    if ((*str) == 'd') {
      if (time->d > 0 || !isNumber(unit))
        goto Lerror;
      time->d = ink_atoi(unit);
      memset(unit, 0, 10);
      pos = 0;
      valid = true;
    } else if ((*str) == 'h') {
      if (time->h > 0 || !isNumber(unit))
        goto Lerror;
      time->h = ink_atoi(unit);
      memset(unit, 0, 10);
      pos = 0;
      valid = true;
    } else if ((*str) == 'm') {
      if (time->m > 0 || !isNumber(unit))
        goto Lerror;
      time->m = ink_atoi(unit);
      memset(unit, 0, 10);
      pos = 0;
      valid = true;
    } else if ((*str) == 's') {
      if (time->s > 0 || !isNumber(unit))
        goto Lerror;
      time->s = ink_atoi(unit);
      memset(unit, 0, 10);
      pos = 0;
      valid = true;
    } else {
      unit[pos] = *str;
      pos++;
    }

    ++str;
  }

  if (!valid)
    goto Lerror;
  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
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
 * Returns INK_ERR_FAIL if invalid time string. 
 */
INKError
string_to_time_struct(const char *str, INKSspec * sspec)
{
  Tokenizer time_tokens(":-");

  if (time_tokens.Initialize(str) != 4) {
    return INK_ERR_FAIL;
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

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}


/*----------------------------------------------------------------------------
 * string_to_header_type
 *----------------------------------------------------------------------------
 * string ==> INKHdrT 
 */
INKHdrT
string_to_header_type(const char *str)
{
  //ink_assert(str);
  if (!str)
    return INK_HDR_UNDEFINED;

  if (strcmp(str, "date") == 0) {
    return INK_HDR_DATE;
  } else if (strcmp(str, "host") == 0) {
    return INK_HDR_HOST;
  } else if (strcmp(str, "cookie") == 0) {
    return INK_HDR_COOKIE;
  } else if (strcmp(str, "client_ip") == 0) {
    return INK_HDR_CLIENT_IP;
  }

  return INK_HDR_UNDEFINED;
}

char *
header_type_to_string(INKHdrT hdr)
{
  // header type
  switch (hdr) {
  case INK_HDR_DATE:
    return xstrdup("date");
  case INK_HDR_HOST:
    return xstrdup("host");
  case INK_HDR_COOKIE:
    return xstrdup("cookie");
  case INK_HDR_CLIENT_IP:
    return xstrdup("client_ip");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * string_to_scheme_type
 *----------------------------------------------------------------------------
 * converts scheme string into a INKSchemeT type
 */
INKSchemeT
string_to_scheme_type(const char *scheme)
{
  if (strcasecmp(scheme, "http") == 0) {
    return INK_SCHEME_HTTP;
  } else if (strcasecmp(scheme, "ftp") == 0) {
    return INK_SCHEME_FTP;
  } else if (strcasecmp(scheme, "https") == 0) {
    return INK_SCHEME_HTTPS;
  } else if (strcasecmp(scheme, "rtsp") == 0) {
    return INK_SCHEME_RTSP;
  } else if (strcasecmp(scheme, "mms") == 0) {
    return INK_SCHEME_MMS;
  }

  return INK_SCHEME_UNDEFINED;
}


char *
scheme_type_to_string(INKSchemeT scheme)
{
  switch (scheme) {
  case INK_SCHEME_HTTP:
    return xstrdup("http");
  case INK_SCHEME_HTTPS:
    return xstrdup("https");
  case INK_SCHEME_FTP:
    return xstrdup("ftp");
  case INK_SCHEME_RTSP:
    return xstrdup("rtsp");
  case INK_SCHEME_MMS:
    return xstrdup("mms");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * string_to_method_type
 *----------------------------------------------------------------------------
 * converts scheme string into a INKSchemeT type
 */
INKMethodT
string_to_method_type(const char *method)
{
  if (strcasecmp(method, "get") == 0) {
    return INK_METHOD_GET;
  } else if (strcasecmp(method, "post") == 0) {
    return INK_METHOD_POST;
  } else if (strcasecmp(method, "put") == 0) {
    return INK_METHOD_PUT;
  } else if (strcasecmp(method, "trace") == 0) {
    return INK_METHOD_TRACE;
  } else if (strcasecmp(method, "push") == 0) { // could be "push" or "PUSH" 
    return INK_METHOD_PUSH;
  }

  return INK_METHOD_UNDEFINED;
}

char *
method_type_to_string(INKMethodT method)
{
  switch (method) {
  case INK_METHOD_GET:
    return xstrdup("get");
  case INK_METHOD_POST:
    return xstrdup("post");
  case INK_METHOD_PUT:
    return xstrdup("put");
  case INK_METHOD_TRACE:
    return xstrdup("trace");
  case INK_METHOD_PUSH:
    return xstrdup("push");
  default:
    break;
  }

  return NULL;
}


/*----------------------------------------------------------------------------
 * string_to_mixt_type
 *----------------------------------------------------------------------------
 * converts mixt tag string into a INKMixtTagT type
 */
INKMixtTagT
string_to_mixt_type(const char *mixt)
{
  if (strcasecmp(mixt, "WMT") == 0) {
    return INK_MIXT_WMT;
  } else if (strcasecmp(mixt, "QT") == 0) {
    return INK_MIXT_QT;
  } else if (strcasecmp(mixt, "RNI") == 0) {
    return INK_MIXT_RNI;
  }

  return INK_MIXT_UNDEFINED;
}

char *
mixt_type_to_string(INKMixtTagT mixt)
{
  switch (mixt) {
  case INK_MIXT_RNI:
    return xstrdup("rni");
  case INK_MIXT_QT:
    return xstrdup("qt");
  case INK_MIXT_WMT:
    return xstrdup("wmt");
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
connect_type_to_string(INKConnectT conn)
{
  switch (conn) {
  case INK_CON_UDP:
    return xstrdup("udp");
  case INK_CON_TCP:
    return xstrdup("tcp");
  default:
    break;
  }
  return NULL;
}

INKConnectT
string_to_connect_type(const char *conn)
{
  if (strcmp(conn, "tcp") == 0) {
    return INK_CON_TCP;
  } else {
    return INK_CON_UDP;
  }

  return INK_CON_UNDEFINED;
}

/*----------------------------------------------------------------------------
 * multicast_type_to_string
 *----------------------------------------------------------------------------
 */
char *
multicast_type_to_string(INKMcTtlT mc)
{
  switch (mc) {
  case INK_MC_TTL_SINGLE_SUBNET:
    return xstrdup("single_subnet");
  case INK_MC_TTL_MULT_SUBNET:
    return xstrdup("multiple_subnet");
  default:
    break;
  }
  return NULL;
}

/* ------------------------------------------------------------------------- 
 * string_to_round_robin_type
 * -------------------------------------------------------------------------
 */
INKRrT
string_to_round_robin_type(const char *rr)
{
  if (strcmp(rr, "true") == 0)
    return INK_RR_TRUE;
  else if (strcmp(rr, "false") == 0)
    return INK_RR_FALSE;
  else if (strcmp(rr, "strict") == 0)
    return INK_RR_STRICT;

  return INK_RR_UNDEFINED;
}

char *
round_robin_type_to_string(INKRrT rr)
{
  switch (rr) {
  case INK_RR_TRUE:
    return xstrdup("true");
  case INK_RR_FALSE:
    return xstrdup("false");
  case INK_RR_STRICT:
    return xstrdup("strict");
  default:
    break;
  }

  return NULL;
}

/*----------------------------------------------------------------------------
 * filename_to_string
 *----------------------------------------------------------------------------
 * INKFileNameT ==> string
 */
char *
filename_to_string(INKFileNameT file)
{
  switch (file) {
  case INK_FNAME_ADMIN_ACCESS:
    return xstrdup("admin_access.config");

  case INK_FNAME_CACHE_OBJ:
    return xstrdup("cache.config");

  case INK_FNAME_CONGESTION:
    return xstrdup("congestion.config");

  case INK_FNAME_FILTER:
    return xstrdup("filter.config");

  case INK_FNAME_FTP_REMAP:
    return xstrdup("ftp_remap.config");

  case INK_FNAME_HOSTING:
    return xstrdup("hosting.config");

  case INK_FNAME_ICP_PEER:
    return xstrdup("icp.config");

  case INK_FNAME_IP_ALLOW:
    return xstrdup("ip_allow.config");

#if 0
  case INK_FNAME_LOG_HOSTS:
    return xstrdup("log_hosts.config");
#endif

  case INK_FNAME_LOGS_XML:
    return xstrdup("logs_xml.config");

  case INK_FNAME_MGMT_ALLOW:
    return xstrdup("mgmt_allow.config");

  case INK_FNAME_NNTP_ACCESS:
    return xstrdup("nntp_access.config");

  case INK_FNAME_NNTP_SERVERS:
    return xstrdup("nntp_servers.config");

  case INK_FNAME_NNTP_CONFIG_XML:
    return xstrdup("nntp_config.xml");

  case INK_FNAME_PARENT_PROXY:
    return xstrdup("parent.config");

  case INK_FNAME_PARTITION:
    return xstrdup("partition.config");

  case INK_FNAME_PLUGIN:
    return xstrdup("plugin.config");

  case INK_FNAME_REMAP:
    return xstrdup("remap.config");

  case INK_FNAME_SOCKS:
    return xstrdup("socks.config");

  case INK_FNAME_SPLIT_DNS:
    return xstrdup("splitdns.config");

  case INK_FNAME_STORAGE:
    return xstrdup("storage.config");

  case INK_FNAME_UPDATE_URL:
    return xstrdup("update.config");

  case INK_FNAME_VADDRS:
    return xstrdup("vaddrs.config");

#if defined(OEM)
  case INK_FNAME_RMSERVER:
    return xstrdup("rmserver.cfg");
  case INK_FNAME_VSCAN:
    return xstrdup("plugins/vscan.config");
  case INK_FNAME_VS_TRUSTED_HOST:
    return xstrdup("plugins/trusted-host.config");
  case INK_FNAME_VS_EXTENSION:
    return xstrdup("plugins/extensions.config");
#endif

  default:                     /* no such config file */
    return NULL;
  }
}

/* ------------------------------------------------------------------------- 
 * nntp_acc_type_to_string
 * -------------------------------------------------------------------------
 */
char *
nntp_acc_type_to_string(INKNntpAccessT acc)
{
  switch (acc) {
  case INK_NNTP_ACC_ALLOW:
    return xstrdup("allow");
    break;
  case INK_NNTP_ACC_DENY:
    return xstrdup("deny");
    break;
  case INK_NNTP_ACC_BASIC:
    return xstrdup("basic");
    break;
  case INK_NNTP_ACC_GENERIC:
    return xstrdup("generic");
    break;
  case INK_NNTP_ACC_CUSTOM:
    return xstrdup("custom");
    break;
  default:
    break;
  }

  return NULL;
}

/* ------------------------------------------------------------------------- 
 * string_to_nntp_treat_type
 * -------------------------------------------------------------------------
 */
INKNntpTreatmentT
string_to_nntp_treat_type(const char *treat)
{
  if (strcasecmp(treat, "feed") == 0) {
    return INK_NNTP_TRMT_FEED;
  } else if (strcasecmp(treat, "push") == 0) {
    return INK_NNTP_TRMT_PUSH;
  } else if (strcasecmp(treat, "pull") == 0) {
    return INK_NNTP_TRMT_PULL;
  } else if (strcasecmp(treat, "pullover") == 0) {
    return INK_NNTP_TRMT_PULLOVER;
  } else if (strcasecmp(treat, "dynamic") == 0) {
    return INK_NNTP_TRMT_DYNAMIC;
  } else if (strcasecmp(treat, "port") == 0) {
    return INK_NNTP_TRMT_POST;
  }

  return INK_NNTP_TRMT_UNDEFINED;
}

/* ------------------------------------------------------------------------- 
 * string_to_congest_scheme_type
 * -------------------------------------------------------------------------
 */
INKCongestionSchemeT
string_to_congest_scheme_type(const char *scheme)
{
  if (strcmp(scheme, "per_ip") == 0) {
    return INK_HTTP_CONGEST_PER_IP;
  } else if (strcmp(scheme, "per_host") == 0) {
    return INK_HTTP_CONGEST_PER_HOST;
  }

  return INK_HTTP_CONGEST_UNDEFINED;
}

/* ------------------------------------------------------------------------- 
 * string_to_admin_acc_type
 * -------------------------------------------------------------------------
 */
INKAccessT
string_to_admin_acc_type(const char *access)
{
  if (strcmp(access, "none") == 0) {
    return INK_ACCESS_NONE;
  } else if (strcmp(access, "monitor_only") == 0) {
    return INK_ACCESS_MONITOR;
  } else if (strcmp(access, "monitor_config_view") == 0) {
    return INK_ACCESS_MONITOR_VIEW;
  } else if (strcmp(access, "monitor_config_change") == 0) {
    return INK_ACCESS_MONITOR_CHANGE;
  }

  return INK_ACCESS_UNDEFINED;
}

char *
admin_acc_type_to_string(INKAccessT access)
{
  switch (access) {
  case INK_ACCESS_NONE:
    return xstrdup("none");
  case INK_ACCESS_MONITOR:
    return xstrdup("monitor_only");
  case INK_ACCESS_MONITOR_VIEW:
    return xstrdup("monitor_config_view");
  case INK_ACCESS_MONITOR_CHANGE:
    return xstrdup("monitor_config_change");
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
tokens_to_pdss_format(TokenList * tokens, Token * first_tok, INKPdSsFormat * pdss)
{
  Token *tok, *last_tok;
  int i = 0;
  const char *sspecs[NUM_SEC_SPECS] = { "time", "src_ip", "prefix", "suffix", "port", "method", "scheme", "tag" };

  //ink_assert(tokens && first_tok && pdss);
  if (!tokens || !first_tok || !pdss)
    return NULL;

  //tokens->Print();

  // first token must be primary destination specifier
  if (strcmp(first_tok->name, "dest_domain") == 0) {
    pdss->pd_type = INK_PD_DOMAIN;
  } else if (strcmp(first_tok->name, "dest_host") == 0) {
    pdss->pd_type = INK_PD_HOST;
  } else if (strcmp(first_tok->name, "dest_ip") == 0) {
    pdss->pd_type = INK_PD_IP;
  } else if (strcmp(first_tok->name, "url_regex") == 0) {
    pdss->pd_type = INK_PD_URL_REGEX;
  } else {
    return NULL;                //INVALID primary destination specifier
  }
  pdss->pd_val = xstrdup(first_tok->value);


  // iterate through tokens checking for sec specifiers
  // state determines which sec specifier being checked
  // the state is only set if there's a sec spec match
  last_tok = first_tok;
  tok = tokens->next(first_tok);
  while (tok) {

    bool matchFound = false;
    for (i = 0; i < NUM_SEC_SPECS; i++) {
      if (strcmp(tok->name, sspecs[i]) == 0) {  // sec spec
        matchFound = true;
        switch (i) {
        case 0:                // time
          // convert the time value
          string_to_time_struct(tok->value, &(pdss->sec_spec));
          goto next_token;
        case 1:                // src_ip
          pdss->sec_spec.src_ip = xstrdup(tok->value);
          goto next_token;
        case 2:                // prefix
          pdss->sec_spec.prefix = xstrdup(tok->value);
          goto next_token;
        case 3:                // suffix
          pdss->sec_spec.suffix = xstrdup(tok->value);
          goto next_token;
        case 4:                // port
          pdss->sec_spec.port = string_to_port_ele(tok->value);
          goto next_token;
        case 5:                // method
          pdss->sec_spec.method = string_to_method_type(tok->value);
          goto next_token;
        case 6:                // scheme
          pdss->sec_spec.scheme = string_to_scheme_type(tok->value);
          goto next_token;
        case 7:                // mixt tag
          pdss->sec_spec.mixt = string_to_mixt_type(tok->value);
          goto next_token;
        default:
          // This should never happen
          break;
        }
      }                         // end if statement
    }                           // end for loop

    // No longer in the secondary specifer region,
    // return the last valid secondary specifer
    if (!matchFound) {
      return last_tok;
    }

  next_token:                  // Get the next token
    last_tok = tok;
    tok = tokens->next(tok);

  }                             // end while loop

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
  char *new_addr = chopWhiteSpaces_alloc((char *) addr);
  if (new_addr == NULL)
    return false;

  if ((addrToks.Initialize(new_addr, ALLOW_EMPTY_TOKS)) != 4 ||
      (minToks.Initialize((char *) min_addr, ALLOW_EMPTY_TOKS)) != 4 ||
      (maxToks.Initialize((char *) max_addr, ALLOW_EMPTY_TOKS)) != 4) {
    xfree(new_addr);
    return false;               // Wrong number of parts
  }
  // IP can't end in a "." either
  len = strlen(new_addr);
  if (new_addr[len - 1] == '.') {
    xfree(new_addr);
    return false;
  }
  // Check all four parts of the ip address to make
  //  sure that they are valid
  for (int i = 0; i < 4; i++) {
    if (!isNumber(addrToks[i])) {
      xfree(new_addr);
      return false;
    }
    // coverity[secure_coding]
    if (sscanf(addrToks[i], "%d", &addrQ) != 1 ||
        sscanf(minToks[i], "%d", &minQ) != 1 || sscanf(maxToks[i], "%d", &maxQ) != 1) {
      xfree(new_addr);
      return false;
    }

    if (addrQ<minQ || addrQ> maxQ) {
      xfree(new_addr);
      return false;
    }
  }

  xfree(new_addr);
  return true;
}

/* ----------------------------------------------------------------------------
 * ccu_checkIpAddrEle
 * ---------------------------------------------------------------------------
 * very similar to the ip_addr_ele_to_string function
 */
bool
ccu_checkIpAddrEle(INKIpAddrEle * ele)
{
  if (!ele || ele->ip_a == INK_INVALID_IP_ADDR)
    return false;

  if (ele->type == INK_IP_SINGLE) {     // SINGLE TYPE 
    return (ccu_checkIpAddr(ele->ip_a));
  } else if (ele->type == INK_IP_RANGE) {       // RANGE TYPE 
    return (ccu_checkIpAddr(ele->ip_a) && ccu_checkIpAddr(ele->ip_b));
  } else {
    return false;
  }
}

bool
ccu_checkPortNum(int port)
{
  return ((port > 0) && (port < 65535));        // What is max. port number?
}

// port_b can be "unspecified"; however if port_b is specified, it must
// be greater than port_a
bool
ccu_checkPortEle(INKPortEle * ele)
{
  if (!ele)
    return false;

  if (ele->port_b == 0) {       // single port
    return (ccu_checkPortNum(ele->port_a));
  } else {                      // range of ports
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
ccu_checkPdSspec(INKPdSsFormat pdss)
{
  if (pdss.pd_type != INK_PD_DOMAIN && pdss.pd_type != INK_PD_HOST &&
      pdss.pd_type != INK_PD_IP && pdss.pd_type != INK_PD_URL_REGEX) {
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
  if (pdss.pd_type == INK_PD_IP) {
    INKIpAddrEle *ip = string_to_ip_addr_ele(pdss.pd_val);
    if (!ip)
      goto Lerror;
    else
      INKIpAddrEleDestroy(ip);
  }
  // if src_ip specified, check if valid IP address
  if (pdss.sec_spec.src_ip) {
    if (!ccu_checkIpAddr(pdss.sec_spec.src_ip))
      goto Lerror;
  }
  // if the mixt tag is specified, the only possible scheme is "rtsp"
  if (pdss.sec_spec.mixt != INK_MIXT_UNDEFINED) {
    if (pdss.sec_spec.scheme != INK_SCHEME_RTSP)
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
    return false;               // missing protocol 
  } else {
    url = slashStr + 3;         // advance two positions to get rid of leading '://'
  }

  // check if there's a second occurrence
  slashStr = strstr(url, ":/");
  if (slashStr)
    return false;

  // make sure that after the first solo "/", there are no more ":"
  // to specify ports
  slashStr = strstr(url, "/");  // begin path prefix
  if (slashStr) {
    url = slashStr++;
    if (strstr(url, ":"))
      return false;             // the port must be specified before the prefix
  }

  return true;
}

/* --------------------------------------------------------------- 
 * ccu_checkTimePeriod
 * --------------------------------------------------------------- 
 * Checks that the time struct used to specify the time period in the 
 * INKSspec has valid time values (eg. 0-23 hr, 0-59 min) and that 
 * time A <= time B
 */
bool
ccu_checkTimePeriod(INKSspec * sspec)
{
  // check valid time values 
  if (sspec->time.hour_a < 0 || sspec->time.hour_a > 23 ||
      sspec->time.hour_b < 0 || sspec->time.hour_b > 23 ||
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
  char *newStr;

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
  newStr = (char *) xmalloc(len + 1);
  memset(newStr, 0, len + 1);
  strncpy(newStr, str, len);

  return newStr;
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
create_ele_obj_from_rule_node(Rule * rule)
{
  INKRuleTypeT rule_type;
  TokenList *token_list;
  CfgEleObj *ele = NULL;

  // sanity check
  //ink_assert(rule != NULL);
  if (!rule)
    return NULL;

  // first check if the rule node is a comment
  if (rule->getComment()) {
    ele = (CfgEleObj *) new CommentObj(rule->getComment());
    return ele;
  }

  token_list = rule->tokenList;
  // determine what rule type the TokenList refers to
  rule_type = get_rule_type(token_list, rule->getFiletype());

  // convert TokenList into an Ele
  // need switch statement to determine which Ele constructor to call
  switch (rule_type) {
  case INK_ADMIN_ACCESS:       /* admin_access.config */
    ele = (CfgEleObj *) new AdminAccessObj(token_list);
    break;
  case INK_CACHE_NEVER:        /* all cache rules use same constructor */
  case INK_CACHE_IGNORE_NO_CACHE:
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
  case INK_CACHE_PIN_IN_CACHE:
  case INK_CACHE_TTL_IN_CACHE:
  case INK_CACHE_REVALIDATE:
  case INK_CACHE_AUTH_CONTENT:
    ele = (CfgEleObj *) new CacheObj(token_list);
    break;
  case INK_CONGESTION:
    ele = (CfgEleObj *) new CongestionObj(token_list);
    break;
  case INK_FILTER_ALLOW:       /* filter.config */
  case INK_FILTER_DENY:
  case INK_FILTER_LDAP:
  case INK_FILTER_NTLM:
  case INK_FILTER_RADIUS:
  case INK_FILTER_KEEP_HDR:
  case INK_FILTER_STRIP_HDR:
    ele = (CfgEleObj *) new FilterObj(token_list);
    break;
  case INK_FTP_REMAP:          /* ftp_remap.config */
    ele = (CfgEleObj *) new FtpRemapObj(token_list);
    break;
  case INK_HOSTING:            /* hosting.config */
    ele = (CfgEleObj *) new HostingObj(token_list);
    break;
  case INK_ICP:                /* icp.config */
    ele = (CfgEleObj *) new IcpObj(token_list);
    break;
  case INK_IP_ALLOW:           /* ip_allow.config */
    ele = (CfgEleObj *) new IpAllowObj(token_list);
    break;
#if 0
  case INK_LOG_HOSTS:          /* log_hosts.config */
    break;
#endif

  case INK_LOG_FILTER:         /* logs_xml.config */
  case INK_LOG_OBJECT:
  case INK_LOG_FORMAT:
    //ele = (CfgEleObj *) new LogFilterObj(token_list);
    break;
  case INK_MGMT_ALLOW:         /* mgmt_allow.config */
    ele = (CfgEleObj *) new MgmtAllowObj(token_list);
    break;
  case INK_NNTP_ACCESS:        /* nntp_access.config */
    ele = (CfgEleObj *) new NntpAccessObj(token_list);
    break;
  case INK_NNTP_SERVERS:       /* nntp_servers.config */
    ele = (CfgEleObj *) new NntpSrvrObj(token_list);
    break;
  case INK_PP_PARENT:          /* parent.config */
  case INK_PP_GO_DIRECT:
    ele = (CfgEleObj *) new ParentProxyObj(token_list);
    break;
  case INK_PARTITION:          /* partition.config */
    ele = (CfgEleObj *) new PartitionObj(token_list);
    break;
  case INK_PLUGIN:
    ele = (CfgEleObj *) new PluginObj(token_list);
    break;
  case INK_REMAP_MAP:          /* remap.config */
  case INK_REMAP_REVERSE_MAP:
  case INK_REMAP_REDIRECT:
  case INK_REMAP_REDIRECT_TEMP:
    ele = (CfgEleObj *) new RemapObj(token_list);
    break;
  case INK_SOCKS_BYPASS:       /* socks.config */
  case INK_SOCKS_AUTH:
  case INK_SOCKS_MULTIPLE:
    ele = (CfgEleObj *) new SocksObj(token_list);
    break;
  case INK_SPLIT_DNS:          /* splitdns.config */
    ele = (CfgEleObj *) new SplitDnsObj(token_list);
    break;
  case INK_STORAGE:
    ele = (CfgEleObj *) new StorageObj(token_list);
    break;
  case INK_UPDATE_URL:         /* update.config */
    ele = (CfgEleObj *) new UpdateObj(token_list);
    break;
  case INK_VADDRS:             /* vaddrs.config */
    ele = (CfgEleObj *) new VirtIpAddrObj(token_list);
    break;
#if defined(OEM)
  case INK_RM_ADMIN_PORT:      /* rmserver.cfg */
  case INK_RM_PNA_PORT:
  case INK_RM_MAX_PROXY_CONN:
  case INK_RM_MAX_GWBW:
  case INK_RM_MAX_PXBW:
  case INK_RM_REALM:
  case INK_RM_PNA_RDT_PORT:
  case INK_RM_PNA_RDT_IP:
    ele = (CfgEleObj *) new RmServerObj(token_list);
    break;
  case INK_VSCAN:
    ele = (CfgEleObj *) new VscanObj(token_list);
    break;
  case INK_VS_TRUSTED_HOST:
    ele = (CfgEleObj *) new VsTrustedHostObj(token_list);
    break;
  case INK_VS_EXTENSION:
    ele = (CfgEleObj *) new VsExtensionObj(token_list);
    break;
#endif
  default:
    return NULL;                //invalid rule type
  }
  if (!ele || !ele->isValid()) {
    return NULL;
  }
  return ele;
}

/*--------------------------------------------------------------
 * create_ele_obj_from_ele
 *--------------------------------------------------------------
 * calls the appropriate subclasses' constructor using actual INKEle
 * need to convert the ele into appropriate Ele object type
 * Note: the ele is not copied, it is actually used, so caller
 * shouldn't free it!
 */
CfgEleObj *
create_ele_obj_from_ele(INKCfgEle * ele)
{
  CfgEleObj *ele_obj = NULL;

  if (!ele)
    return NULL;

  switch (ele->type) {
  case INK_ADMIN_ACCESS:       /* admin_access.config */
    ele_obj = (CfgEleObj *) new AdminAccessObj((INKAdminAccessEle *) ele);
    break;

  case INK_CACHE_NEVER:        /* cache.config */
  case INK_CACHE_IGNORE_NO_CACHE:      // fall-through
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:       // fall-through
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:       // fall-through
  case INK_CACHE_PIN_IN_CACHE: // fall-through
  case INK_CACHE_REVALIDATE:   // fall-through
  case INK_CACHE_TTL_IN_CACHE:
  case INK_CACHE_AUTH_CONTENT:
    ele_obj = (CfgEleObj *) new CacheObj((INKCacheEle *) ele);
    break;

  case INK_CONGESTION:
    ele_obj = (CfgEleObj *) new CongestionObj((INKCongestionEle *) ele);
    break;

  case INK_FILTER_ALLOW:       /* filter.config */
  case INK_FILTER_DENY:        // fall-through
  case INK_FILTER_LDAP:        // fall-through
  case INK_FILTER_NTLM:        // fall-through
  case INK_FILTER_RADIUS:      // fall-through
  case INK_FILTER_KEEP_HDR:    // fall-through
  case INK_FILTER_STRIP_HDR:   // fall-through
    ele_obj = (CfgEleObj *) new FilterObj((INKFilterEle *) ele);
    break;

  case INK_FTP_REMAP:          /* ftp_remap.config */
    ele_obj = (CfgEleObj *) new FtpRemapObj((INKFtpRemapEle *) ele);
    break;

  case INK_HOSTING:            /* hosting.config */
    ele_obj = (CfgEleObj *) new HostingObj((INKHostingEle *) ele);
    break;

  case INK_ICP:                /* icp.config */
    ele_obj = (CfgEleObj *) new IcpObj((INKIcpEle *) ele);
    break;

  case INK_IP_ALLOW:           /* ip_allow.config */
    ele_obj = (CfgEleObj *) new IpAllowObj((INKIpAllowEle *) ele);
    break;

  case INK_LOG_FILTER:         /* logs_xml.config */
  case INK_LOG_OBJECT:         // fall-through
  case INK_LOG_FORMAT:         // fall-through
    //ele_obj = (CfgEleObj*) new LogFilterObj((INKLogFilterEle*)ele); 
    break;

  case INK_MGMT_ALLOW:         /* mgmt_allow.config */
    ele_obj = (CfgEleObj *) new MgmtAllowObj((INKMgmtAllowEle *) ele);
    break;

  case INK_NNTP_ACCESS:        /* nntp_access.config */
    ele_obj = (CfgEleObj *) new NntpAccessObj((INKNntpAccessEle *) ele);
    break;

  case INK_NNTP_SERVERS:       /* nntp_servers.config */
    ele_obj = (CfgEleObj *) new NntpSrvrObj((INKNntpSrvrEle *) ele);
    break;

  case INK_PP_PARENT:          /* parent.config */
  case INK_PP_GO_DIRECT:       // fall-through
    ele_obj = (CfgEleObj *) new ParentProxyObj((INKParentProxyEle *) ele);
    break;

  case INK_PARTITION:          /* partition.config */
    ele_obj = (CfgEleObj *) new PartitionObj((INKPartitionEle *) ele);
    break;

  case INK_PLUGIN:
    ele_obj = (CfgEleObj *) new PluginObj((INKPluginEle *) ele);
    break;

  case INK_REMAP_MAP:          /* remap.config */
  case INK_REMAP_REVERSE_MAP:  // fall-through
  case INK_REMAP_REDIRECT:     // fall-through
  case INK_REMAP_REDIRECT_TEMP:        // fall-through
    ele_obj = (CfgEleObj *) new RemapObj((INKRemapEle *) ele);
    break;

  case INK_SOCKS_BYPASS:       /* socks.config */
  case INK_SOCKS_AUTH:
  case INK_SOCKS_MULTIPLE:
    ele_obj = (CfgEleObj *) new SocksObj((INKSocksEle *) ele);
    break;

  case INK_SPLIT_DNS:          /* splitdns.config */
    ele_obj = (CfgEleObj *) new SplitDnsObj((INKSplitDnsEle *) ele);
    break;

  case INK_STORAGE:            /* storage.config */
    ele_obj = (CfgEleObj *) new StorageObj((INKStorageEle *) ele);
    break;

  case INK_UPDATE_URL:         /* update.config */
    ele_obj = (CfgEleObj *) new UpdateObj((INKUpdateEle *) ele);
    break;

  case INK_VADDRS:             /* vaddrs.config */
    ele_obj = (CfgEleObj *) new VirtIpAddrObj((INKVirtIpAddrEle *) ele);
    break;
#if defined(OEM)
  case INK_RM_ADMIN_PORT:      /* rmserver.cfg */
  case INK_RM_PNA_PORT:
  case INK_RM_MAX_PROXY_CONN:
  case INK_RM_MAX_GWBW:
  case INK_RM_REALM:
  case INK_RM_PNA_RDT_PORT:
  case INK_RM_PNA_RDT_IP:
    ele_obj = (CfgEleObj *) new RmServerObj((INKRmServerEle *) ele);
    break;
  case INK_VSCAN:
    ele_obj = (CfgEleObj *) new VscanObj((INKVscanEle *) ele);
    break;
  case INK_VS_TRUSTED_HOST:
    ele_obj = (CfgEleObj *) new VsTrustedHostObj((INKVsTrustedHostEle *) ele);
    break;
  case INK_VS_EXTENSION:
    ele_obj = (CfgEleObj *) new VsExtensionObj((INKVsExtensionEle *) ele);
    break;
#endif
  case INK_TYPE_UNDEFINED:
  default:
    return NULL;                // error
  }

  return ele_obj;
}

/*--------------------------------------------------------------
 * get_rule_type
 *--------------------------------------------------------------
 * determine what rule type the TokenList refers to by examining
 * the appropriate token-value pair in the TokenList
 */
INKRuleTypeT
get_rule_type(TokenList * token_list, INKFileNameT file)
{
  Token *tok;

  //ink_asser(ttoken_list);
  if (!token_list)
    return INK_TYPE_UNDEFINED;

  /* Depending on the file and rule type, need to find out which
     token specifies which type of rule it is */
  switch (file) {
  case INK_FNAME_ADMIN_ACCESS: /* admin_access.config */
    return INK_ADMIN_ACCESS;

  case INK_FNAME_CACHE_OBJ:    /* cache.config */
    tok = token_list->first();
    while (tok != NULL) {
      if (strcmp(tok->name, "action") == 0) {
        if (strcmp(tok->value, "never-cache") == 0) {
          return INK_CACHE_NEVER;
        } else if (strcmp(tok->value, "ignore-no-cache") == 0) {
          return INK_CACHE_IGNORE_NO_CACHE;
        } else if (strcmp(tok->value, "ignore-client-no-cache") == 0) {
          return INK_CACHE_IGNORE_CLIENT_NO_CACHE;
        } else if (strcmp(tok->value, "ignore-server-no-cache") == 0) {
          return INK_CACHE_IGNORE_SERVER_NO_CACHE;
        } else if (strcmp(tok->value, "cache-auth-content") == 0) {
          return INK_CACHE_AUTH_CONTENT;
        } else {
          return INK_TYPE_UNDEFINED;
        }
      } else if (strcmp(tok->name, "pin-in-cache") == 0) {
        return INK_CACHE_PIN_IN_CACHE;
      } else if (strcmp(tok->name, "revalidate") == 0) {
        return INK_CACHE_REVALIDATE;
      } else if (strcmp(tok->name, "ttl-in-cache") == 0) {
        return INK_CACHE_TTL_IN_CACHE;
      } else {                  // try next token
        tok = token_list->next(tok);
      }
    }
    // if reached this point, there is no action specified
    return INK_TYPE_UNDEFINED;

  case INK_FNAME_CONGESTION:   /* congestion.config */
    return INK_CONGESTION;

  case INK_FNAME_FILTER:       /* filter.config */
    /* action tag should be last token-value pair in TokenList */
    //tok = token_list->last();
    for (tok = token_list->first(); tok; tok = token_list->next(tok)) {
      if (strcmp(tok->name, "action") == 0) {
        if (strcmp(tok->value, "allow") == 0) {
          return INK_FILTER_ALLOW;
        } else if (strcmp(tok->value, "deny") == 0) {
          return INK_FILTER_DENY;
        } else if (strcmp(tok->value, "ldap") == 0) {
          return INK_FILTER_LDAP;
        } else if (strcmp(tok->value, "ntlm") == 0) {
          return INK_FILTER_NTLM;
        } else if (strcmp(tok->value, "radius") == 0) {
          return INK_FILTER_RADIUS;
        } else {
          return INK_TYPE_UNDEFINED;
        }
      } else if (strcmp(tok->name, "keep_hdr") == 0) {
        return INK_FILTER_KEEP_HDR;
      } else if (strcmp(tok->name, "strip_hdr") == 0) {
        return INK_FILTER_STRIP_HDR;
      }
    }
    return INK_FILTER_ALLOW;

  case INK_FNAME_FTP_REMAP:    /* ftp_remap.config */
    return INK_FTP_REMAP;

  case INK_FNAME_HOSTING:      /* hosting.config */
    return INK_HOSTING;

  case INK_FNAME_ICP_PEER:     /* icp.config */
    return INK_ICP;

  case INK_FNAME_IP_ALLOW:     /* ip_allow.config */
    return INK_IP_ALLOW;

#if 0
  case INK_FNAME_LOG_HOSTS:    /* log_hosts.config */
    return INK_LOG_HOSTS;
#endif

  case INK_FNAME_LOGS_XML:     /* logs_xml.config */
    printf(" *** CfgContextUtils.cc: NOT DONE YET! **\n");
    //  INK_LOG_FILTER,             /* logs_xml.config */
    //  INK_LOG_OBJECT,
    //  INK_LOG_FORMAT,
    return INK_LOG_FILTER;

  case INK_FNAME_MGMT_ALLOW:   /* mgmt_allow.config */
    return INK_MGMT_ALLOW;

  case INK_FNAME_NNTP_ACCESS:  /* nnpt_access.config */
    return INK_NNTP_ACCESS;

  case INK_FNAME_NNTP_SERVERS: /* nntp_servers.config */
    return INK_NNTP_SERVERS;

  case INK_FNAME_PARENT_PROXY: /* parent.config */
    // search fro go_direct action name and recongize the value-> ture or false
    for (tok = token_list->first(); tok; tok = token_list->next(tok)) {
      if ((strcmp(tok->name, "go_direct") == 0) && (strcmp(tok->value, "true") == 0)) {
        return INK_PP_GO_DIRECT;
      }
    }
    return INK_PP_PARENT;

  case INK_FNAME_PARTITION:    /* partition.config */
    return INK_PARTITION;

  case INK_FNAME_PLUGIN:       /* plugin.config */
    return INK_PLUGIN;

  case INK_FNAME_REMAP:        /* remap.config */
    tok = token_list->first();
    if (strcmp(tok->name, "map") == 0) {
      return INK_REMAP_MAP;
    } else if (strcmp(tok->name, "reverse_map") == 0) {
      return INK_REMAP_REVERSE_MAP;
    } else if (strcmp(tok->name, "redirect") == 0) {
      return INK_REMAP_REDIRECT;
    } else if (strcmp(tok->name, "redirect_temporary") == 0) {
      return INK_REMAP_REDIRECT_TEMP;
    } else {
      return INK_TYPE_UNDEFINED;
    }
  case INK_FNAME_SOCKS:        /* socks.config */
    tok = token_list->first();
    if (strcmp(tok->name, "no_socks") == 0) {
      return INK_SOCKS_BYPASS;
    } else if (strcmp(tok->name, "auth") == 0) {
      return INK_SOCKS_AUTH;
    } else if (strcmp(tok->name, "dest_ip") == 0) {
      return INK_SOCKS_MULTIPLE;
    } else {
      return INK_TYPE_UNDEFINED;
    }
  case INK_FNAME_SPLIT_DNS:    /* splitdns.config */
    return INK_SPLIT_DNS;

  case INK_FNAME_STORAGE:      /* storage.config */
    return INK_STORAGE;

  case INK_FNAME_UPDATE_URL:   /* update.config */
    return INK_UPDATE_URL;

  case INK_FNAME_VADDRS:       /* vaddrs.config */
    return INK_VADDRS;
#if defined(OEM)
  case INK_FNAME_RMSERVER:
    tok = token_list->first();
    if (strcmp(tok->name, RM_ADMIN_PORT) == 0) {
      return INK_RM_ADMIN_PORT;
    } else if (strcmp(tok->name, RM_PNA_PORT) == 0) {
      return INK_RM_PNA_PORT;
    } else if (strcmp(tok->name, RM_REALM) == 0) {
      return INK_RM_REALM;
    } else if (strcmp(tok->name, RM_MAX_PROXY_CONN) == 0) {
      return INK_RM_MAX_PROXY_CONN;
    } else if (strcmp(tok->name, RM_MAX_GWBW) == 0) {
      return INK_RM_MAX_GWBW;
    } else if (strcmp(tok->name, RM_MAX_PXBW) == 0) {
      return INK_RM_MAX_PXBW;
    } else if (strcmp(tok->name, RM_PNA_RDT_PORT) == 0) {
      return INK_RM_PNA_RDT_PORT;
    } else if (strcmp(tok->name, RM_PNA_RDT_IP) == 0) {
      return INK_RM_PNA_RDT_IP;
    } else {
      return INK_TYPE_UNDEFINED;
    }
  case INK_FNAME_VSCAN:        /* vscan.config */
    return INK_VSCAN;
  case INK_FNAME_VS_TRUSTED_HOST:      /* trusted-host.config */
    return INK_VS_TRUSTED_HOST;
  case INK_FNAME_VS_EXTENSION: /* extensions.config */
    return INK_VS_EXTENSION;
#endif
  case INK_FNAME_UNDEFINED:
  default:
    return INK_TYPE_UNDEFINED;
  }

  return INK_TYPE_UNDEFINED;    // Should not reach here
}

///////////////////////////////////////////////////////////////////
// Since we are using the ele's as C structures wrapped in C++ classes
// we need to write copy function that "copy" the C structures 
// 1) need copy functions for each helper information struct which 
//    are typically embedded in the Ele's (eg. any lists, INKSspec)
// 2) need copy functions for each Ele; these functiosn will actually
// be called by teh copy constructors and overloaded assignment 
// operators in each CfgEleObj subclass!!
///////////////////////////////////////////////////////////////////
void
copy_cfg_ele(INKCfgEle * src_ele, INKCfgEle * dst_ele)
{
  //ink_assert (src_ele && dst_ele);
  if (!src_ele || !dst_ele)
    return;

  dst_ele->type = src_ele->type;
  dst_ele->error = src_ele->error;
}


void
copy_sspec(INKSspec * src, INKSspec * dst)
{
  //ink_assert(src && dst);
  if (!src || !dst)
    return;

  dst->active = src->active;
  dst->time.hour_a = src->time.hour_a;
  dst->time.min_a = src->time.min_a;
  dst->time.hour_b = src->time.hour_b;
  dst->time.min_b = src->time.min_b;
  if (src->src_ip)
    dst->src_ip = xstrdup(src->src_ip);
  if (src->prefix)
    dst->prefix = xstrdup(src->prefix);
  if (src->suffix)
    dst->suffix = xstrdup(src->suffix);
  dst->port = copy_port_ele(src->port);
  dst->method = src->method;
  dst->scheme = src->scheme;
  dst->mixt = src->mixt;
}

void
copy_pdss_format(INKPdSsFormat * src_pdss, INKPdSsFormat * dst_pdss)
{
  //ink_assert (src_pdss && dst_pdss);
  if (!src_pdss || !dst_pdss)
    return;

  dst_pdss->pd_type = src_pdss->pd_type;
  if (src_pdss->pd_val)
    dst_pdss->pd_val = xstrdup(src_pdss->pd_val);
  copy_sspec(&(src_pdss->sec_spec), &(dst_pdss->sec_spec));
}

void
copy_hms_time(INKHmsTime * src, INKHmsTime * dst)
{
  if (!src || !dst)
    return;

  dst->d = src->d;
  dst->h = src->h;
  dst->m = src->m;
  dst->s = src->s;
}

INKIpAddrEle *
copy_ip_addr_ele(INKIpAddrEle * src_ele)
{
  INKIpAddrEle *dst_ele;

  if (!src_ele)
    return NULL;

  dst_ele = INKIpAddrEleCreate();
  dst_ele->type = src_ele->type;
  if (src_ele->ip_a)
    dst_ele->ip_a = xstrdup(src_ele->ip_a);
  dst_ele->cidr_a = src_ele->cidr_a;
  dst_ele->port_a = src_ele->port_a;
  if (src_ele->ip_b)
    dst_ele->ip_b = xstrdup(src_ele->ip_b);
  dst_ele->cidr_b = src_ele->cidr_b;
  dst_ele->port_b = src_ele->port_b;

  return dst_ele;
}

INKPortEle *
copy_port_ele(INKPortEle * src_ele)
{
  INKPortEle *dst_ele;

  if (!src_ele)
    return NULL;

  dst_ele = INKPortEleCreate();
  dst_ele->port_a = src_ele->port_a;
  dst_ele->port_b = src_ele->port_b;

  return dst_ele;
}

INKDomain *
copy_domain(INKDomain * src_dom)
{
  INKDomain *dst_dom;

  if (!src_dom)
    return NULL;

  dst_dom = INKDomainCreate();
  if (src_dom->domain_val)
    dst_dom->domain_val = xstrdup(src_dom->domain_val);
  dst_dom->port = src_dom->port;

  return dst_dom;
}

INKIpAddrList
copy_ip_addr_list(INKIpAddrList list)
{
  INKIpAddrList nlist;
  INKIpAddrEle *ele, *nele;
  int count, i;

  if (list == INK_INVALID_LIST)
    return INK_INVALID_LIST;

  nlist = INKIpAddrListCreate();
  count = INKIpAddrListLen(list);
  for (i = 0; i < count; i++) {
    ele = INKIpAddrListDequeue(list);
    nele = copy_ip_addr_ele(ele);
    INKIpAddrListEnqueue(list, ele);
    INKIpAddrListEnqueue(nlist, nele);
  }

  return nlist;
}

INKPortList
copy_port_list(INKPortList list)
{
  INKPortList nlist;
  INKPortEle *ele, *nele;
  int count, i;

  if (list == INK_INVALID_LIST)
    return INK_INVALID_LIST;

  nlist = INKPortListCreate();
  count = INKPortListLen(list);
  for (i = 0; i < count; i++) {
    ele = INKPortListDequeue(list);
    nele = copy_port_ele(ele);
    INKPortListEnqueue(list, ele);
    INKPortListEnqueue(nlist, nele);
  }

  return nlist;
}

INKDomainList
copy_domain_list(INKDomainList list)
{
  INKDomainList nlist;
  INKDomain *ele, *nele;
  int count, i;

  if (list == INK_INVALID_LIST)
    return INK_INVALID_LIST;

  nlist = INKDomainListCreate();
  count = INKDomainListLen(list);
  for (i = 0; i < count; i++) {
    ele = INKDomainListDequeue(list);
    nele = copy_domain(ele);
    INKDomainListEnqueue(list, ele);
    INKDomainListEnqueue(nlist, nele);
  }

  return nlist;
}

INKStringList
copy_string_list(INKStringList list)
{
  INKStringList nlist;
  char *ele, *nele;
  int count, i;

  if (list == INK_INVALID_LIST)
    return INK_INVALID_LIST;

  nlist = INKStringListCreate();
  count = INKStringListLen(list);
  for (i = 0; i < count; i++) {
    ele = INKStringListDequeue(list);
    nele = xstrdup(ele);
    INKStringListEnqueue(list, ele);
    INKStringListEnqueue(nlist, nele);
  }

  return nlist;

}

INKIntList
copy_int_list(INKIntList list)
{
  INKIntList nlist;
  int *elem, *nelem;
  int count, i;

  if (list == INK_INVALID_LIST)
    return INK_INVALID_LIST;

  nlist = INKIntListCreate();
  count = INKIntListLen(list);
  for (i = 0; i < count; i++) {
    elem = INKIntListDequeue(list);
    nelem = (int *) xmalloc(sizeof(int));
    *nelem = *elem;
    INKIntListEnqueue(list, elem);
    INKIntListEnqueue(nlist, nelem);
  }

  return nlist;
}

//////////////////////////////////////////////////
INKAdminAccessEle *
copy_admin_access_ele(INKAdminAccessEle * ele)
{
  if (!ele)
    return NULL;

  INKAdminAccessEle *nele = INKAdminAccessEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));

  if (ele->user)
    nele->user = xstrdup(ele->user);
  if (ele->password)
    nele->password = xstrdup(ele->password);
  nele->access = ele->access;

  return nele;
}

INKCacheEle *
copy_cache_ele(INKCacheEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKCacheEle *nele = INKCacheEleCreate(ele->cfg_ele.type);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  copy_pdss_format(&(ele->cache_info), &(nele->cache_info));
  copy_hms_time(&(ele->time_period), &(nele->time_period));

  return nele;
}

INKCongestionEle *
copy_congestion_ele(INKCongestionEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKCongestionEle *nele = INKCongestionEleCreate();
  if (!nele)
    return NULL;
  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  //copy_pdss_format(&(ele->congestion_info), &(nele->congestion_info));
  nele->pd_type = ele->pd_type;
  nele->pd_val = xstrdup(ele->pd_val);
  if (ele->prefix)
    nele->prefix = xstrdup(ele->prefix);
  nele->port = ele->port;
  nele->scheme = ele->scheme;
  nele->max_connection_failures = ele->max_connection_failures;
  nele->fail_window = ele->fail_window;
  nele->proxy_retry_interval = ele->proxy_retry_interval;
  nele->client_wait_interval = ele->client_wait_interval;
  nele->wait_interval_alpha = ele->wait_interval_alpha;
  nele->live_os_conn_timeout = ele->live_os_conn_timeout;
  nele->live_os_conn_retries = ele->live_os_conn_retries;
  nele->dead_os_conn_timeout = ele->dead_os_conn_timeout;
  nele->dead_os_conn_retries = ele->dead_os_conn_retries;
  nele->max_connection = ele->max_connection;
  if (ele->error_page_uri)
    nele->error_page_uri = xstrdup(ele->error_page_uri);

  return nele;
}

INKFilterEle *
copy_filter_ele(INKFilterEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKFilterEle *nele = INKFilterEleCreate(INK_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  copy_pdss_format(&(ele->filter_info), &(nele->filter_info));
  nele->hdr = ele->hdr;
  /* LDAP */
  if (ele->server)
    nele->server = xstrdup(ele->server);
  if (ele->dn)
    nele->dn = xstrdup(ele->dn);
  if (ele->realm)
    nele->realm = xstrdup(ele->realm);
  if (ele->uid_filter)
    nele->uid_filter = xstrdup(ele->uid_filter);
  if (ele->attr)
    nele->attr = xstrdup(ele->attr);
  if (ele->attr_val)
    nele->attr_val = xstrdup(ele->attr_val);
  if (ele->redirect_url)
    nele->redirect_url = xstrdup(ele->redirect_url);
  if (ele->bind_dn)
    nele->bind_dn = xstrdup(ele->bind_dn);
  if (ele->bind_pwd_file)
    nele->bind_pwd_file = xstrdup(ele->bind_pwd_file);

  return nele;
}

INKFtpRemapEle *
copy_ftp_remap_ele(INKFtpRemapEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKFtpRemapEle *nele = INKFtpRemapEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->from_val)
    nele->from_val = xstrdup(ele->from_val);
  nele->from_port = ele->from_port;
  if (ele->to_val)
    nele->to_val = xstrdup(ele->to_val);
  nele->to_port = ele->to_port;

  return nele;
}

INKHostingEle *
copy_hosting_ele(INKHostingEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKHostingEle *nele = INKHostingEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->pd_type = ele->pd_type;
  if (ele->pd_val)
    nele->pd_val = xstrdup(ele->pd_val);
  ele->partitions = copy_int_list(ele->partitions);

  return nele;
}

INKIcpEle *
copy_icp_ele(INKIcpEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKIcpEle *nele = INKIcpEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->peer_hostname)
    nele->peer_hostname = xstrdup(ele->peer_hostname);
  if (ele->peer_host_ip_addr)
    nele->peer_host_ip_addr = xstrdup(ele->peer_host_ip_addr);
  nele->peer_type = ele->peer_type;
  nele->peer_proxy_port = ele->peer_proxy_port;
  nele->peer_icp_port = ele->peer_icp_port;
  nele->is_multicast = ele->is_multicast;
  if (ele->mc_ip_addr)
    nele->mc_ip_addr = xstrdup(ele->mc_ip_addr);
  nele->mc_ttl = ele->mc_ttl;

  return nele;
}

INKIpAllowEle *
copy_ip_allow_ele(INKIpAllowEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKIpAllowEle *nele = INKIpAllowEleCreate();
  if (!nele)
    return NULL;
  if (ele->src_ip_addr)
    nele->src_ip_addr = copy_ip_addr_ele(ele->src_ip_addr);
  nele->action = ele->action;
  return nele;
}

INKLogFilterEle *
copy_log_filter_ele(INKLogFilterEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKLogFilterEle *nele = INKLogFilterEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->action = ele->action;
  if (ele->filter_name)
    ele->filter_name = xstrdup(nele->filter_name);
  if (ele->log_field)
    nele->log_field = xstrdup(ele->log_field);
  nele->compare_op = ele->compare_op;
  if (ele->compare_str)
    nele->compare_str = xstrdup(ele->compare_str);
  nele->compare_int = ele->compare_int;

  return nele;
}

INKLogFormatEle *
copy_log_format_ele(INKLogFormatEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKLogFormatEle *nele = INKLogFormatEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->name)
    nele->name = xstrdup(ele->name);
  if (ele->format)
    nele->format = xstrdup(ele->format);
  nele->aggregate_interval_secs = ele->aggregate_interval_secs;

  return nele;
}

INKMgmtAllowEle *
copy_mgmt_allow_ele(INKMgmtAllowEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKMgmtAllowEle *nele = INKMgmtAllowEleCreate();
  if (!nele)
    return NULL;
  if (ele->src_ip_addr)
    nele->src_ip_addr = copy_ip_addr_ele(ele->src_ip_addr);
  nele->action = ele->action;
  return nele;
}

INKLogObjectEle *
copy_log_object_ele(INKLogObjectEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKLogObjectEle *nele = INKLogObjectEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->format_name)
    nele->format_name = xstrdup(ele->format_name);
  if (ele->file_name)
    nele->file_name = xstrdup(ele->file_name);
  nele->log_mode = ele->log_mode;
  nele->collation_hosts = copy_domain_list(ele->collation_hosts);
  nele->filters = copy_string_list(ele->filters);
  nele->protocols = copy_string_list(ele->protocols);
  nele->server_hosts = copy_string_list(ele->server_hosts);

  return nele;
}

INKNntpAccessEle *
copy_nntp_access_ele(INKNntpAccessEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKNntpAccessEle *nele = INKNntpAccessEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->client_t = INK_CLIENT_GRP_UNDEFINED;
  if (ele->clients)
    nele->clients = xstrdup(ele->clients);
  nele->access = INK_NNTP_ACC_UNDEFINED;
  if (ele->authenticator)
    nele->authenticator = xstrdup(ele->authenticator);
  if (ele->user)
    nele->user = xstrdup(ele->user);
  if (ele->pass)
    nele->pass = xstrdup(ele->pass);
  nele->group_wildmat = copy_string_list(ele->group_wildmat);
  nele->deny_posting = false;

  return nele;
}

INKNntpSrvrEle *
copy_nntp_srvr_ele(INKNntpSrvrEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKNntpSrvrEle *nele = INKNntpSrvrEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->hostname)
    nele->hostname = xstrdup(ele->hostname);
  nele->group_wildmat = copy_string_list(ele->group_wildmat);
  nele->treatment = ele->treatment;
  nele->priority = ele->priority;
  nele->interface = xstrdup(ele->interface);

  return nele;
}

INKParentProxyEle *
copy_parent_proxy_ele(INKParentProxyEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKParentProxyEle *nele = INKParentProxyEleCreate(INK_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  copy_pdss_format(&(ele->parent_info), &(nele->parent_info));
  nele->rr = ele->rr;
  nele->proxy_list = copy_domain_list(ele->proxy_list);
  nele->direct = ele->direct;

  return nele;
}

INKPartitionEle *
copy_partition_ele(INKPartitionEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKPartitionEle *nele = INKPartitionEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->partition_num = ele->partition_num;
  nele->scheme = ele->scheme;
  nele->partition_size = ele->partition_size;
  nele->size_format = ele->size_format;

  return nele;
}

INKPluginEle *
copy_plugin_ele(INKPluginEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKPluginEle *nele = INKPluginEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->name)
    nele->name = xstrdup(ele->name);
  nele->args = copy_string_list(ele->args);

  return nele;
}

INKRemapEle *
copy_remap_ele(INKRemapEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKRemapEle *nele = INKRemapEleCreate(INK_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->map = ele->map;
  nele->from_scheme = ele->from_scheme;
  if (ele->from_host)
    nele->from_host = xstrdup(ele->from_host);
  nele->from_port = ele->from_port;
  if (ele->from_path_prefix)
    nele->from_path_prefix = xstrdup(ele->from_path_prefix);
  nele->to_scheme = ele->to_scheme;
  if (ele->to_host)
    nele->to_host = xstrdup(ele->to_host);
  nele->to_port = ele->to_port;
  if (ele->to_path_prefix)
    nele->to_path_prefix = xstrdup(ele->to_path_prefix);
  nele->mixt = ele->mixt;

  return nele;
}

INKSocksEle *
copy_socks_ele(INKSocksEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKSocksEle *nele = INKSocksEleCreate(INK_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->ip_addrs = copy_ip_addr_list(ele->ip_addrs);
  nele->dest_ip_addr = copy_ip_addr_ele(ele->dest_ip_addr);
  nele->socks_servers = copy_domain_list(ele->socks_servers);
  nele->rr = ele->rr;
  if (ele->username)
    nele->username = xstrdup(ele->username);
  if (ele->password)
    nele->password = xstrdup(ele->password);

  return nele;
}

INKSplitDnsEle *
copy_split_dns_ele(INKSplitDnsEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKSplitDnsEle *nele = INKSplitDnsEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  nele->pd_type = ele->pd_type;
  if (ele->pd_val)
    nele->pd_val = xstrdup(ele->pd_val);
  nele->dns_servers_addrs = copy_domain_list(ele->dns_servers_addrs);
  if (ele->def_domain)
    nele->def_domain = xstrdup(ele->def_domain);
  nele->search_list = copy_domain_list(ele->search_list);

  return nele;
}

INKStorageEle *
copy_storage_ele(INKStorageEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKStorageEle *nele = INKStorageEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->pathname)
    nele->pathname = xstrdup(ele->pathname);
  nele->size = ele->size;

  return nele;
}

INKUpdateEle *
copy_update_ele(INKUpdateEle * ele)
{
  if (!ele) {
    return NULL;
  }

  INKUpdateEle *nele = INKUpdateEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->url)
    nele->url = xstrdup(ele->url);
  nele->headers = copy_string_list(ele->headers);
  nele->offset_hour = ele->offset_hour;
  nele->interval = ele->interval;
  nele->recursion_depth = ele->recursion_depth;

  return nele;
}

INKVirtIpAddrEle *
copy_virt_ip_addr_ele(INKVirtIpAddrEle * ele)
{
  INKVirtIpAddrEle *new_ele;

  if (!ele) {
    return NULL;
  }

  new_ele = INKVirtIpAddrEleCreate();
  if (!new_ele)
    return NULL;

  // copy cfg ele
  copy_cfg_ele(&(ele->cfg_ele), &(new_ele->cfg_ele));
  new_ele->ip_addr = xstrdup(ele->ip_addr);
  new_ele->intr = xstrdup(ele->intr);
  new_ele->sub_intr = ele->sub_intr;

  return new_ele;
}

INKCommentEle *
copy_comment_ele(INKCommentEle * ele)
{
  if (!ele)
    return NULL;

  INKCommentEle *nele = comment_ele_create(ele->comment);
  return nele;
}

#if defined(OEM)
INKRmServerEle *
copy_rmserver_ele(INKRmServerEle * ele)
{
  if (!ele)
    return NULL;

  INKRmServerEle *nele = INKRmServerEleCreate(INK_TYPE_UNDEFINED);
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->Vname)
    nele->Vname = xstrdup(ele->Vname);
  if (ele->str_val)
    nele->str_val = xstrdup(ele->str_val);
  nele->int_val = ele->int_val;

  return nele;
}

INKVscanEle *
copy_vscan_ele(INKVscanEle * ele)
{
  if (!ele)
    return NULL;

  INKVscanEle *nele = INKVscanEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->attr_name)
    nele->attr_name = xstrdup(ele->attr_name);
  if (ele->attr_val)
    nele->attr_val = xstrdup(ele->attr_val);

  return nele;
}

INKVsTrustedHostEle *
copy_vs_trusted_host_ele(INKVsTrustedHostEle * ele)
{
  if (!ele)
    return NULL;

  INKVsTrustedHostEle *nele = INKVsTrustedHostEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->hostname)
    nele->hostname = xstrdup(ele->hostname);

  return nele;
}

INKVsExtensionEle *
copy_vs_extension_ele(INKVsExtensionEle * ele)
{
  if (!ele)
    return NULL;

  INKVsExtensionEle *nele = INKVsExtensionEleCreate();
  if (!nele)
    return NULL;

  copy_cfg_ele(&(ele->cfg_ele), &(nele->cfg_ele));
  if (ele->file_ext)
    nele->file_ext = xstrdup(ele->file_ext);

  return nele;
}

#endif
/***************************************************************************
 * Functions needed by implementation but must be hidden from user
 ***************************************************************************/
INKCommentEle *
comment_ele_create(char *comment)
{
  INKCommentEle *ele;

  ele = (INKCommentEle *) xmalloc(sizeof(INKCommentEle));

  ele->cfg_ele.type = INK_TYPE_COMMENT;
  ele->cfg_ele.error = INK_ERR_OKAY;
  if (comment)
    ele->comment = xstrdup(comment);
  else                          // comment is NULL
    ele->comment = NULL;

  return ele;
}

void
comment_ele_destroy(INKCommentEle * ele)
{
  if (ele) {
    if (ele->comment)
      xfree(ele->comment);
    xfree(ele);
  }
  return;
}
