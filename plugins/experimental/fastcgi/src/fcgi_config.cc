/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fcgi_config.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <iterator>
#include <iostream>

static char DEFAULT_HOSTNAME[]        = "localhost";
static char DEFAULT_SERVER_IP[]       = "127.0.0.1";
static char DEFAULT_SERVER_PORT[]     = "60000";
static char DEFAULT_INCLUDE_FILE[]    = "fastcgi.config";
static char DEFAULT_DOCUMENT_ROOT[]   = "/var/www/html/";
static char DEFAULT_HTML[]            = "index.php";
static int DEFAULT_MIN_CONNECTION     = 4;
static int DEFAULT_MAX_CONNECTION     = 10;
static int DEFAULT_MAX_REQUEST        = 1000;
static int DEFAULT_REQUEST_QUEUE_SIZE = 250;
using namespace ats_plugin;
inline TSRecordDataType
str_to_datatype(char *str)
{
  TSRecordDataType type = TS_RECORDDATATYPE_NULL;

  if (!str || !*str) {
    return TS_RECORDDATATYPE_NULL;
  }

  if (!strcmp(str, "INT")) {
    type = TS_RECORDDATATYPE_INT;
  } else if (!strcmp(str, "STRING")) {
    type = TS_RECORDDATATYPE_STRING;
  }

  return type;
}
bool
FcgiPluginConfig::getFcgiEnabledStatus()
{
  return enabled;
}
void
FcgiPluginConfig::setFcgiEnabledStatus(bool val)
{
  enabled = val;
}

TSMgmtString
FcgiPluginConfig::getHostname()
{
  return hostname;
}
void
FcgiPluginConfig::setHostname(char *str)
{
  hostname = str;
}
TSMgmtString
FcgiPluginConfig::getServerIp()
{
  return server_ip;
}
void
FcgiPluginConfig::setServerIp(char *str)
{
  server_ip = str;
}
TSMgmtString
FcgiPluginConfig::getServerPort()
{
  return server_port;
}
void
FcgiPluginConfig::setServerPort(char *str)
{
  server_port = str;
}

TSMgmtString
FcgiPluginConfig::getIncludeFilePath()
{
  return include;
}
void
FcgiPluginConfig::setIncludeFilePath(char *str)
{
  include = str;
}
FCGIParams *
FcgiPluginConfig::getFcgiParams()
{
  return params;
}
void
FcgiPluginConfig::setFcgiParams(FCGIParams *params)
{
  params = params;
}
TSMgmtString
FcgiPluginConfig::getDocumentRootDir()
{
  return document_root;
}
void
FcgiPluginConfig::setDocumentRootDir(char *str)
{
  document_root = str;
}

TSMgmtString
FcgiPluginConfig::getHtml()
{
  return html;
}

void
FcgiPluginConfig::setHtml(char *str)
{
  html = str;
}

TSMgmtInt
FcgiPluginConfig::getMinConnLength()
{
  return min_connections;
}
void
FcgiPluginConfig::setMinConnLength(int64_t minLen)
{
  min_connections = minLen;
}
TSMgmtInt
FcgiPluginConfig::getMaxConnLength()
{
  return max_connections;
}
void
FcgiPluginConfig::setMaxConnLength(int64_t maxLen)
{
  max_connections = maxLen;
}

TSMgmtInt
FcgiPluginConfig::getMaxReqLength()
{
  return max_requests;
}

void
FcgiPluginConfig::setMaxReqLength(int64_t maxLen)
{
  max_requests = maxLen;
}

TSMgmtInt
FcgiPluginConfig::getRequestQueueSize()
{
  return request_queue_size;
}
void
FcgiPluginConfig::setRequestQueueSize(int64_t queueSize)
{
  request_queue_size = queueSize;
}

static TSReturnCode
fcgiHttpTxnConfigFind(const char *name, int length, FcgiConfigKey *conf, TSRecordDataType *type)
{
  *type = TS_RECORDDATATYPE_NULL;
  if (length == -1) {
    length = strlen(name);
  }

  if (!strncmp(name, "proxy.config.http.fcgi.enabled", length)) {
    *conf = fcgiEnabled;
    *type = TS_RECORDDATATYPE_INT;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.hostname", length)) {
    *conf = fcgiHostname;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.server_ip", length)) {
    *conf = fcgiServerIp;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.server_port", length)) {
    *conf = fcgiServerPort;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  if (!strncmp(name, "proxy.config.http.fcgi.host.include", length)) {
    *conf = fcgiInclude;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "proxy.config.http.fcgi.host.document_root", length)) {
    *conf = fcgiDocumentRoot;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "proxy.config.http.fcgi.host.html", length)) {
    *conf = fcgiHtml;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "proxy.config.http.fcgi.host.min_connections", length)) {
    *conf = fcgiMinConnections;
    *type = TS_RECORDDATATYPE_INT;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "proxy.config.http.fcgi.host.max_connections", length)) {
    *conf = fcgiMaxConnections;
    *type = TS_RECORDDATATYPE_INT;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "proxy.config.http.fcgi.host.max_requests", length)) {
    *conf = fcgiMaxRequests;
    *type = TS_RECORDDATATYPE_INT;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "proxy.config.http.fcgi.host.request_queue_size", length)) {
    *conf = fcgiRequestQueueSize;
    *type = TS_RECORDDATATYPE_INT;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}
static TSReturnCode
fcgiParamConfigFind(const char *name, int length, FcgiParamKey *conf, TSRecordDataType *type)
{
  *type = TS_RECORDDATATYPE_NULL;
  if (length == -1) {
    length = strlen(name);
  }
  if (!strncmp(name, "GATEWAY_INTERFACE", length)) {
    *conf = gatewayInterface;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SERVER_SOFTWARE", length)) {
    *conf = serverSoftware;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "QUERY_STRING", length)) {
    *conf = queryString;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "REQUEST_METHOD", length)) {
    *conf = requestMethod;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "CONTENT_TYPE", length)) {
    *conf = contentType;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "CONTENT_LENGTH", length)) {
    *conf = contentLength;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SCRIPT_FILENAME", length)) {
    *conf = scriptFilename;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SCRIPT_NAME", length)) {
    *conf = scriptName;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "REQUEST_URI", length)) {
    *conf = requestUri;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "DOCUMENT_URI", length)) {
    *conf = documentUri;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "DOCUMENT_ROOT", length)) {
    *conf = documentRoot;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SERVER_PROTOCOL", length)) {
    *conf = serverProtocol;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "REMOTE_ADDR", length)) {
    *conf = remoteAddr;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "REMOTE_PORT", length)) {
    *conf = remotePort;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SERVER_ADDR", length)) {
    *conf = serverAddr;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SERVER_PORT", length)) {
    *conf = serverPort;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }
  if (!strncmp(name, "SERVER_NAME", length)) {
    *conf = serverName;
    *type = TS_RECORDDATATYPE_STRING;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

static void
initFcgiParam(const char *fn, FCGIParams *fcgiParams)
{
  int line_num = 0;
  TSFile file;
  char buf[8192];
  FcgiParamKey name;
  TSRecordDataType type, expected_type;
  if (nullptr == (file = TSfopen(fn, "r"))) {
    TSError("[ats_fastcgi] Could not open fcgiParam.config file %s", fn);
  } else {
    while (nullptr != TSfgets(file, buf, sizeof(buf))) {
      char *ln, *tok;
      char *s = buf;

      ++line_num; // First line is #1 ...
      while (isspace(*s)) {
        ++s;
      }
      tok = strtok_r(s, " \t", &ln);

      // check for blank lines and comments
      if ((!tok) || (tok && ('#' == *tok))) {
        continue;
      }

      if (strncmp(tok, "fastcgi_param", 13)) {
        TSError("[ats_fastcgi] File %s, line %d: non-CONFIG line encountered", fn, line_num);
        continue;
      }

      // Find the configuration name
      tok = strtok_r(nullptr, " \t", &ln);
      if (fcgiParamConfigFind(tok, -1, &name, &expected_type) != TS_SUCCESS) {
        TSError("[ats_fastcgi] File %s, line %d: no ats_fastcgi.config name given", fn, line_num);
        continue;
      }

      // Find the type (INT or STRING only)
      tok = strtok_r(nullptr, " \t", &ln);
      if (TS_RECORDDATATYPE_NULL == (type = str_to_datatype(tok))) {
        TSError("[ats_fastcgi] File %s, line %d: only INT and STRING types supported", fn, line_num);
        continue;
      }

      if (type != expected_type) {
        TSError("[ats_fastcgi] File %s, line %d: mismatch between provide data type, and expected type", fn, line_num);
        continue;
      }

      // Find the value (which depends on the type above)
      if (ln) {
        while (isspace(*ln)) {
          ++ln;
        }

        if ('\0' == *ln) {
          tok = nullptr;
        } else {
          tok = ln;

          while (*ln != '\0') {
            ++ln;
          }

          --ln;

          while (isspace(*ln) && (ln > tok)) {
            --ln;
          }

          ++ln;
          *ln = '\0';
        }
      } else {
        tok = nullptr;
      }

      if (!tok) {
        TSError("[ats_fastcgi] File %s, line %d: the configuration must provide a value", fn, line_num);
        continue;
      }
      // Now store the new config
      switch (name) {
      case gatewayInterface:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("GATEWAY_INTERFACE", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("GATEWAY_INTERFACE", tok));
        }
        break;
      case serverSoftware:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_SOFTWARE", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_SOFTWARE", tok));
        }
        break;
      case queryString:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("QUERY_STRING", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("QUERY_STRING", tok));
        }
        break;
      case requestMethod:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("REQUEST_METHOD", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("REQUEST_METHOD", tok));
        }
        break;
      case contentType:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("CONTENT_TYPE", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("CONTENT_TYPE", tok));
        }
        break;
      case contentLength:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("CONTENT_LENGTH", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("CONTENT_LENGTH", tok));
        }
        break;
      case scriptFilename:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SCRIPT_FILENAME", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SCRIPT_FILENAME", tok));
        }
        break;
      case scriptName:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SCRIPT_NAME", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SCRIPT_NAME", tok));
        }
        break;
      case requestUri:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("REQUEST_URI", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("REQUEST_URI", tok));
        }
        break;
      case documentUri:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("DOCUMENT_URI", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("DOCUMENT_URI", tok));
        }
        break;
      case documentRoot:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("DOCUMENT_ROOT", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("DOCUMENT_ROOT", tok));
        }
        break;
      case serverProtocol:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_PROTOCOL", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_PROTOCOL", tok));
        }
        break;
      case remoteAddr:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("REMOTE_ADDR", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("REMOTE_ADDR", tok));
        }
        break;
      case remotePort:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("REMOTE_PORT", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("REMOTE_PORT", tok));
        }
        break;
      case serverAddr:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_ADDR", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_ADDR", tok));
        }
        break;
      case serverPort:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_PORT", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_PORT", tok));
        }
        break;
      case serverName:
        if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_NAME", ""));
        } else {
          fcgiParams->insert(std::pair<std::string, std::string>("SERVER_NAME", tok));
        }
        break;
      default:
        break;
      }
    }

    TSfclose(file);
  }
}

FcgiPluginConfig *
FcgiPluginConfig::initConfig(const char *fn)
{
  InterceptPluginData *plugin_data = new InterceptPluginData();
  FcgiPluginConfig *config         = new FcgiPluginConfig(); // static_cast<FcgiPluginConfig *>(TSmalloc(sizeof(FcgiPluginConfig)));
                                                             // Default config
  TSDebug(PLUGIN_NAME, "Setting config...");
  if (plugin_data || nullptr == plugin_data->getGlobalConfigObj()) {
    config->enabled            = true;
    config->hostname           = DEFAULT_HOSTNAME;
    config->server_ip          = DEFAULT_SERVER_IP;
    config->server_port        = DEFAULT_SERVER_PORT;
    config->include            = DEFAULT_INCLUDE_FILE;
    config->document_root      = DEFAULT_DOCUMENT_ROOT;
    config->html               = DEFAULT_HTML;
    config->params             = new FCGIParams();
    config->max_connections    = DEFAULT_MAX_CONNECTION;
    config->min_connections    = DEFAULT_MIN_CONNECTION;
    config->max_requests       = DEFAULT_MAX_REQUEST;
    config->request_queue_size = DEFAULT_REQUEST_QUEUE_SIZE;
  } else {
    // Inherit from global config
    FcgiPluginConfig *global_config = plugin_data->getGlobalConfigObj();
    config->enabled                 = global_config->getFcgiEnabledStatus();
    config->hostname                = TSstrdup(global_config->getHostname());
    config->server_ip               = TSstrdup(global_config->getServerIp());
    config->server_port             = TSstrdup(global_config->getServerPort());
    config->include                 = TSstrdup(global_config->getIncludeFilePath());
    config->params                  = new FCGIParams();
    config->document_root           = TSstrdup(global_config->getDocumentRootDir());
    config->html                    = TSstrdup(global_config->getHtml());
    config->max_connections         = global_config->getMaxConnLength();
    config->min_connections         = global_config->getMinConnLength();
    config->max_requests            = global_config->getMaxReqLength();
    config->request_queue_size      = global_config->getRequestQueueSize();
  }

  if (fn) {
    if (1 == strlen(fn)) {
      if (0 == strcmp("0", fn)) {
        config->enabled = false;
      } else if (0 == strcmp("1", fn)) {
        config->enabled = true;
      } else {
        TSError("[ats_fastcgi] Parameter '%s' ignored", fn);
      }
    } else {
      int line_num = 0;
      TSFile file;
      char buf[8192];
      FcgiConfigKey name;
      TSRecordDataType type, expected_type;
      if (nullptr == (file = TSfopen(fn, "r"))) {
        TSError("[ats_fastcgi] Could not open config file %s", fn);
      } else {
        while (nullptr != TSfgets(file, buf, sizeof(buf))) {
          char *ln, *tok;
          char *s = buf;

          ++line_num; // First line is #1 ...
          while (isspace(*s)) {
            ++s;
          }
          tok = strtok_r(s, " \t", &ln);

          // check for blank lines and comments
          if ((!tok) || (tok && ('#' == *tok))) {
            continue;
          }

          if (strncmp(tok, "CONFIG", 6)) {
            TSError("[ats_fastcgi] File %s, line %d: non-CONFIG line encountered", fn, line_num);
            continue;
          }

          // Find the configuration name
          tok = strtok_r(nullptr, " \t", &ln);
          if (fcgiHttpTxnConfigFind(tok, -1, &name, &expected_type) != TS_SUCCESS) {
            TSError("[ats_fastcgi] File %s, line %d: no records.config name given", fn, line_num);
            continue;
          }

          // Find the type (INT or STRING only)
          tok = strtok_r(nullptr, " \t", &ln);
          if (TS_RECORDDATATYPE_NULL == (type = str_to_datatype(tok))) {
            TSError("[ats_fastcgi] File %s, line %d: only INT and STRING "
                    "types supported",
                    fn, line_num);
            continue;
          }

          if (type != expected_type) {
            TSError("[ats_fastcgi] File %s, line %d: mismatch between provide "
                    "data type, and expected type",
                    fn, line_num);
            continue;
          }

          // Find the value (which depends on the type above)
          if (ln) {
            while (isspace(*ln)) {
              ++ln;
            }

            if ('\0' == *ln) {
              tok = nullptr;
            } else {
              tok = ln;

              while (*ln != '\0') {
                ++ln;
              }

              --ln;

              while (isspace(*ln) && (ln > tok)) {
                --ln;
              }

              ++ln;
              *ln = '\0';
            }
          } else {
            tok = nullptr;
          }

          if (!tok) {
            TSError("[ats_fastcgi] File %s, line %d: the configuration must "
                    "provide a value",
                    fn, line_num);
            continue;
          }
          // Now store the new config
          switch (name) {
          case fcgiEnabled:
            config->enabled = tok;
            break;

          case fcgiHostname:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->hostname = nullptr;
            } else {
              config->hostname = TSstrdup(tok);
            }
            break;

          case fcgiServerIp:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->server_ip = nullptr;
            } else {
              config->server_ip = TSstrdup(tok);
            }
            break;

          case fcgiServerPort:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->server_port = nullptr;
            } else {
              config->server_port = TSstrdup(tok);
            }
            break;

          case fcgiInclude:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->include = nullptr;
              TSDebug(PLUGIN_NAME, "Failed to load FCGIParams config file.");
            } else {
              config->include = TSstrdup(tok);
              // This will read fcgiparams from config file and stored in config->params map
              FCGIParams *params = config->params;
              initFcgiParam(config->include, params);
              TSDebug(PLUGIN_NAME, "Reading fcgiParams config from %s file complete.", config->include);
              // FCGIParams::iterator it = params->begin();
              // std::cout << "mymap contains:\n";
              // for (it = params->begin(); it != params->end(); ++it)
              //   std::cout << it->first << " => " << it->second << '\n';
            }

            break;
          case fcgiDocumentRoot:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->document_root = nullptr;
            } else {
              config->document_root = TSstrdup(tok);
            }
            break;
          case fcgiHtml:
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->html = nullptr;
            } else {
              config->html = TSstrdup(tok);
            }
            break;

          case fcgiMinConnections: {
            config->min_connections = strtoll(tok, nullptr, 10);
            TSDebug(PLUGIN_NAME, "min_connections = %ld", config->min_connections);
          } break;

          case fcgiMaxConnections: {
            config->max_connections = strtoll(tok, nullptr, 10);
            TSDebug(PLUGIN_NAME, "max_connections = %ld", config->max_connections);
          } break;
          case fcgiMaxRequests: {
            config->max_requests = strtoll(tok, nullptr, 10);
            TSDebug(PLUGIN_NAME, "max_requests = %ld", config->max_requests);
          } break;
          case fcgiRequestQueueSize: {
            config->request_queue_size = strtoll(tok, nullptr, 10);
            TSDebug(PLUGIN_NAME, "request_queue_size = %ld", config->request_queue_size);
          } break;
          default:
            break;
          }
        }

        TSfclose(file);
      }
    }
  }

  TSDebug(PLUGIN_NAME, "enabled = %d", static_cast<int>(config->enabled));
  TSDebug(PLUGIN_NAME, "hostname = %s", config->hostname);
  TSDebug(PLUGIN_NAME, "server_ip = %s", config->server_ip);
  TSDebug(PLUGIN_NAME, "server_port = %s", config->server_port);
  TSDebug(PLUGIN_NAME, "include = %s", config->include);
  TSDebug(PLUGIN_NAME, "document_root = %s", config->document_root);
  TSDebug(PLUGIN_NAME, "html = %s", config->html);
  return config;
}

// Getter setter of global config obj
FcgiPluginConfig *
InterceptPluginData::getGlobalConfigObj()
{
  return global_config;
}
void
InterceptPluginData::setGlobalConfigObj(FcgiPluginConfig *config)
{
  global_config = config;
}