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

#include "ink_config.h"

#ifdef HAVE_LIBSSL

#include "P_SSLCertLookup.h"
#include "P_UnixNet.h"

SSLCertLookup sslCertLookup;

#define SSL_IP_TAG "dest_ip"
#define SSL_CERT_TAG "ssl_cert_name"
#define SSL_PRIVATE_KEY_TAG "ssl_key_name"
const char *moduleName = "SSLCertLookup";

const matcher_tags sslCertTags = {
  NULL, NULL, SSL_IP_TAG, NULL, NULL, false
};

SSLCertLookup::SSLCertLookup():
param(NULL), multipleCerts(false)
{
  SSLCertLookupHashTable = ink_hash_table_create(InkHashTableKeyType_String);
  *config_file_path = '\0';
}

void
SSLCertLookup::init(SslConfigParams * p)
{
  param = p;
  multipleCerts = buildTable();
}

bool
SSLCertLookup::buildTable()
{
  char *tok_state = NULL;
  char *line = NULL;
  const char *errPtr = NULL;
  char errBuf[1024];
  char *file_buf = NULL;
  int line_num = 0;
  bool ret = 0;
  char *addr = NULL;
  char *sslCert = NULL;
  char *priKey = NULL;
  matcher_line line_info;
  bool alarmAlready = false;
  char *configFilePath = NULL;

  if (param != NULL)
    configFilePath = param->getConfigFilePath();

  // Table should be empty
//  ink_assert(num_el == 0);

  if (configFilePath)
    file_buf = readIntoBuffer(configFilePath, moduleName, NULL);

  if (file_buf == NULL) {
    Warning("%s Failed to read %s. Using default server cert for all connections", moduleName, configFilePath);
    return ret;
  }

  line = tokLine(file_buf, &tok_state);
  while (line != NULL) {

    line_num++;

    // skip all blank spaces at beginning of line
    while (*line && isspace(*line)) {
      line++;
    }

    if (*line != '\0' && *line != '#') {

      errPtr = parseConfigLine(line, &line_info, &sslCertTags);

      if (errPtr != NULL) {
        ink_snprintf(errBuf, 1024, "%s discarding %s entry at line %d : %s",
                     moduleName, configFilePath, line_num, errPtr);
        IOCORE_SignalError(errBuf, alarmAlready);
      } else {
        ink_assert(line_info.type == MATCH_IP);

        errPtr = extractIPAndCert(&line_info, &addr, &sslCert, &priKey);

        if (errPtr != NULL) {
          ink_snprintf(errBuf, 1024, "%s discarding %s entry at line %d : %s",
                       moduleName, configFilePath, line_num, errPtr);
          IOCORE_SignalError(errBuf, alarmAlready);
        } else {
          if (addr != NULL && sslCert != NULL) {
            addInfoToHash(addr, sslCert, priKey);
            ret = 1;
          }
          xfree(sslCert);
          xfree(priKey);
          xfree(addr);
          addr = NULL;
          sslCert = NULL;
          priKey = NULL;
        }
      }                         // else
    }                           // if(*line != '\0' && *line != '#') 

    line = tokLine(NULL, &tok_state);
  }                             //  while(line != NULL)

/*  if(num_el == 0) 
  {
    Warning("%s No entries in %s. Using default server cert for all connections",
	    moduleName, configFilePath);
  }

  if(is_debug_tag_set("ssl")) 
  {
    Print();
  }
*/
  xfree(file_buf);
  return ret;
}

const char *
SSLCertLookup::extractIPAndCert(matcher_line * line_info, char **addr, char **cert, char **priKey)
{
//  ip_addr_t testAddr;
  char *label;
  char *value;

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {

    label = line_info->line[0][i];
    value = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }

    if (strcasecmp(label, SSL_IP_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *addr = (char *) xmalloc(buf_len);
        ink_strncpy(*addr, (const char *) value, buf_len);
//              testAddr = inet_addr (addr);
      }
    }

    if (strcasecmp(label, SSL_CERT_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *cert = (char *) xmalloc(buf_len);
        ink_strncpy(*cert, (const char *) value, buf_len);
      }
    }

    if (strcasecmp(label, SSL_PRIVATE_KEY_TAG) == 0) {
      if (value != NULL) {
        int buf_len = sizeof(char) * (strlen(value) + 1);

        *priKey = (char *) xmalloc(buf_len);
        ink_strncpy(*priKey, (const char *) value, buf_len);
      }
    }
  }

  if ( /*testAddr == INADDR_NONE || */ addr != NULL && cert == NULL)
    return "Bad address or certificate.";
  else
    return NULL;
}

int
SSLCertLookup::addInfoToHash(char *strAddr, char *cert, char *serverPrivateKey)
{

#if (OPENSSL_VERSION_NUMBER >= 0x10000000L) // openssl returns a const SSL_METHOD now
  const SSL_METHOD *meth = NULL;
#else
  SSL_METHOD *meth = NULL;
#endif
  meth = SSLv23_server_method();
  SSL_CTX *ctx = SSL_CTX_new(meth);
  if (!ctx) {
    ssl_NetProcessor.logSSLError("Cannot create new server contex.");
    return (false);
  }
//  if (serverPrivateKey == NULL)
//      serverPrivateKey = cert;

  ssl_NetProcessor.initSSLServerCTX(param, ctx, cert, serverPrivateKey, false);
  ink_hash_table_insert(SSLCertLookupHashTable, strAddr, (void *) ctx);
  return (true);
}

SSL_CTX *
SSLCertLookup::findInfoInHash(char *strAddr)
{

  InkHashTableValue hash_value;
  if (ink_hash_table_lookup(SSLCertLookupHashTable, strAddr, &hash_value) == 0) {
    return NULL;
  } else {
    return (SSL_CTX *) hash_value;
  }
}

SSLCertLookup::~SSLCertLookup()
{
  ink_hash_table_destroy_and_xfree_values(SSLCertLookupHashTable);
}

#endif
