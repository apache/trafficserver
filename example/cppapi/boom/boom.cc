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

/*
 *   This plugin does one thing and one thing only it will
 *   eat the origin error responses codes if instructed to do so.
 *
 *   boom.so error_page_path error_codes
 *
 *   Configuration is specified as two arguments in plugin.config
 *   the first argument is the path to a folder containing the error
 *   files, if you specify a error code such as 5xx or 4xx then it
 *   will look for a file called 5xx.html or 4xx.html respectively, if it's not
 *   found, then it will try to use default.html, if default.html is not found
 *   the response will be the hard coded html string below.
 *
 *   You will specify a comma separated list WITH NO SPACES!!!
 *   of error codes to BOOM on, for example you can do:
 *    3xx 4xx 5xx 6xx or you can specify individual error codes such as 501 502 404, etc...
 *   You would put 3xx,4xx,5xx,200 in your config argument REMEMBER NO SPACES!!!!
 *
 *   If you specify an individual error code, it's expected that there will be a file
 *   in your error page folder with that error code, for example 404 would expect
 *   a page called 404.html. Error codes will try to apply the more specific rule first
 *   for example if you have a 404 and 4xx, the 404 will attempt to match first and would
 *   serve the 404.html page instead of the 4xx.html page. Similarly, a 403 would
 *   serve the 4xx.html page.
 *
 *   EXAMPLE:
 *   boom.so /usr/local/boom 404,5xx
 *
 **/
#include <map>
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <dirent.h>

#include <atscppapi/Transaction.h>

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Headers.h>
#include <atscppapi/Stat.h>
#include <atscppapi/Logger.h>

using namespace atscppapi;
#define TAG "boom"

namespace
{
/// Name for the Boom invocation counter
const std::string BOOM_COUNTER = "BOOM_COUNTER";

// Default file name for the error HTML page TBD when this is going to be used
const std::string DEFAULT_ERROR_FILE = "default"; // default.html will be searched for

// Default error response TBD when the default response will be used
const std::string DEFAULT_ERROR_RESPONSE = "<html><body><h1>This page will be back soon</h1></body></html>";

// Default HTTP status code to use after booming
// const int DEFAULT_BOOM_HTTP_STATUS_CODE = 200;

// Default HTTP status string to use after booming
const std::string DEFAULT_BOOM_HTTP_STATUS = "OK (BOOM)";

Stat boom_counter;
} // namespace

namespace
{
GlobalPlugin *plugin;
}

// Functor that decides whether the HTTP error can be rewritten or not.
// Rewritable codes are: 2xx, 3xx, 4xx, 5xx and 6xx.
// 1xx is NOT rewritable!
class IsRewritableCode : public std::unary_function<std::string, bool>
{ // could probably be replaced with mem_ptr_fun()..
private:
  int current_code_;
  std::string current_code_string_;

public:
  IsRewritableCode(int current_code) : current_code_(current_code)
  {
    std::ostringstream oss;
    oss << current_code_;
    current_code_string_ = oss.str();
  }

  bool
  operator()(const std::string &code) const
  {
    TS_DEBUG(TAG, "Checking if %s matches code %s", current_code_string_.c_str(), code.c_str());
    if (code == current_code_string_) {
      return true;
    }
    if (code == "2xx" && current_code_ >= 200 && current_code_ <= 299) {
      return true;
    }
    if (code == "3xx" && current_code_ >= 300 && current_code_ <= 399) {
      return true;
    }
    if (code == "4xx" && current_code_ >= 400 && current_code_ <= 499) {
      return true;
    }
    if (code == "5xx" && current_code_ >= 500 && current_code_ <= 599) {
      return true;
    }
    if (code == "6xx" && current_code_ >= 600 && current_code_ <= 699) {
      return true;
    }

    return false;
  }
};

class BoomResponseRegistry
{
  // Boom error codes
  std::set<std::string> error_codes_;

  // Map of error codes to error responses
  std::map<std::string, std::string> error_responses_;

  // Base directory for the file name
  std::string base_error_directory_;

  // Global default response string
  std::string global_response_string_;

  // Convert HTTP status code to string
  std::string code_from_status(int http_status);

  // Convert HTTP status code to string
  std::string generic_code_from_status(int http_status);

public:
  // Set a "catchall" global default response
  void set_global_default_response(const std::string &global_default_response);

  // Populate the registry lookup table with contents of files in
  // the base directory
  void populate_error_responses(const std::string &base_directory);

  // Return custom response string for the custom code
  // Lookup logic (using 404 as example)
  //  1. Check for exact match (i.e. contents of "404.html")
  //  2. Check for generic response match (i.e. contents of "4xx.html")
  //  3. Check for default response (i.e. contents of "default.html")
  //  4. Check for global default response (settable through "set_global_default_response" method)
  //  5. If all else fails, return compiled in response code
  const std::string &get_response_for_error_code(int http_status_code);

  // Returns true iff either of the three conditions are true:
  // 1. Exact match for the error is registered (e.g. "404.html" for HTTP 404)
  // 2. Generic response match for the error is registered (e.g. "4xx.html" for HTTP 404)
  // 3. Default response match is registered (e.g. "default.html" for HTTP 404)
  // Return false otherwise
  bool has_code_registered(int http_status_code);

  // Register error codes
  void register_error_codes(const std::vector<std::string> &error_codes);
};

void
BoomResponseRegistry::register_error_codes(const std::vector<std::string> &error_codes)
{
  std::vector<std::string>::const_iterator i = error_codes.begin(), e = error_codes.end();
  for (; i != e; ++i) {
    TS_DEBUG(TAG, "Registering error code %s", (*i).c_str());
    error_codes_.insert(*i);
  }
}
// forward declaration
bool get_file_contents(const std::string &fileName, std::string &contents);

// Examine the error file directory and populate the error_response
// map with the file contents.
void
BoomResponseRegistry::populate_error_responses(const std::string &base_directory)
{
  base_error_directory_ = base_directory;

  // Make sure we have a trailing / after the base directory
  if (!base_error_directory_.empty() && base_error_directory_[base_error_directory_.length() - 1] != '/') {
    base_error_directory_.append("/"); // make sure we have a trailing /
  }

  // Iterate over files in the base directory.
  // Filename (sans the .html suffix) becomes the entry to the
  // registry lookup table
  DIR *pDIR = nullptr;
  struct dirent *entry;

  pDIR = opendir(base_error_directory_.c_str());
  if (pDIR != nullptr) {
    while (true) {
      entry = readdir(pDIR);
      if (entry == nullptr) {
        break;
      }

      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        std::string file_name(entry->d_name, strlen(entry->d_name));
        if (file_name.length() > 5 && file_name.substr(file_name.length() - 5, 5) == ".html") {
          // File is .html, load the file into the map...
          std::string file_contents;
          if (get_file_contents(base_error_directory_ + file_name, file_contents)) {
            std::string error_code(file_name.substr(0, file_name.length() - 5));
            TS_DEBUG(TAG, "Adding response to error code %s from file %s", error_code.c_str(), file_name.c_str());
            error_responses_[error_code] = file_contents;
          }
        }
      }
    }
    closedir(pDIR);
  }
}

void
BoomResponseRegistry::set_global_default_response(const std::string &global_default_response)
{
  global_response_string_ = global_default_response;
}

const std::string &
BoomResponseRegistry::get_response_for_error_code(int http_status_code)
{
  std::string code_str = code_from_status(http_status_code);

  if (error_responses_.count(code_str)) {
    return error_responses_[code_str];
  }

  std::string gen_code_str = generic_code_from_status(http_status_code);

  if (error_responses_.count(gen_code_str)) {
    return error_responses_[gen_code_str];
  }

  if (error_responses_.count(DEFAULT_ERROR_FILE)) {
    return error_responses_[DEFAULT_ERROR_FILE];
  }

  return DEFAULT_ERROR_RESPONSE;
}

bool
BoomResponseRegistry::has_code_registered(int http_status_code)
{
  // Only rewritable codes are allowed.
  std::set<std::string>::iterator ii = std::find_if(error_codes_.begin(), error_codes_.end(), IsRewritableCode(http_status_code));
  if (ii == error_codes_.end()) {
    return false;
  } else {
    return true;
  }
}

std::string
BoomResponseRegistry::generic_code_from_status(int code)
{
  if (code >= 200 && code <= 299) {
    return "2xx";
  } else if (code >= 300 && code <= 399) {
    return "3xx";
  } else if (code >= 400 && code <= 499) {
    return "4xx";
  } else if (code >= 500 && code <= 599) {
    return "5xx";
  } else {
    return "default";
  }
}

std::string
BoomResponseRegistry::code_from_status(int code)
{
  std::ostringstream oss;
  oss << code;
  std::string code_str = oss.str();
  return code_str;
}

// Transaction plugin that intercepts error and displays
// a error page as configured
class BoomTransactionPlugin : public TransactionPlugin
{
public:
  BoomTransactionPlugin(Transaction &transaction, HttpStatus status, const std::string &reason, const std::string &body)
    : TransactionPlugin(transaction), status_(status), reason_(reason), body_(body)
  {
    TransactionPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
    TS_DEBUG(TAG, "Created BoomTransaction plugin for txn=%p, status=%d, reason=%s, body length=%d", transaction.getAtsHandle(),
             status, reason.c_str(), static_cast<int>(body.length()));
    transaction.error(body_); // Set the error body now, and change the status and reason later.
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    transaction.getClientResponse().setStatusCode(status_);
    transaction.getClientResponse().setReasonPhrase(reason_);
    transaction.resume();
  }

private:
  HttpStatus status_;
  std::string reason_;
  std::string body_;
};

// Utility routine to split string by delimiter.
void
stringSplit(const std::string &in, char delim, std::vector<std::string> &res)
{
  std::istringstream ss(in);
  std::string item;
  while (std::getline(ss, item, delim)) {
    res.push_back(item);
  }
}

// Utility routine to read file contents into a string
// @returns true if the file exists and has been successfully read
bool
get_file_contents(const std::string &fileName, std::string &contents)
{
  if (fileName.empty()) {
    return false;
  }

  std::ifstream file(fileName.c_str());
  if (!file.good()) {
    return false;
  }

  size_t BUF_SIZE = 1024;
  std::vector<char> buf(BUF_SIZE);

  while (!file.eof()) {
    file.read(&buf[0], BUF_SIZE);
    if (file.gcount() > 0) {
      contents.append(&buf[0], file.gcount());
    }
  }

  return true;
}

class BoomGlobalPlugin : public atscppapi::GlobalPlugin
{
private:
  BoomResponseRegistry *response_registry_;

public:
  BoomGlobalPlugin(BoomResponseRegistry *response_registry) : response_registry_(response_registry)
  {
    TS_DEBUG(TAG, "Creating BoomGlobalHook %p", this);
    registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  // Upcall method that is called for every transaction.
  void handleReadResponseHeaders(Transaction &transaction) override;

private:
  BoomGlobalPlugin();
};

void
BoomGlobalPlugin::handleReadResponseHeaders(Transaction &transaction)
{
  // Get response status code from the transaction
  HttpStatus http_status_code = transaction.getServerResponse().getStatusCode();

  TS_DEBUG(TAG, "Checking if response with code %d is in the registry.", http_status_code);

  // If the custom response for the error code is registered,
  // attach the BoomTransactionPlugin to the transaction
  if (response_registry_->has_code_registered(http_status_code)) {
    // Get the original reason phrase string from the transaction
    std::string http_reason_phrase = transaction.getServerResponse().getReasonPhrase();

    TS_DEBUG(TAG, "Response has code %d which matches a registered code, TransactionPlugin will be created.", http_status_code);
    // Increment the statistics counter
    boom_counter.increment();

    // Get custom response code from the registry
    const std::string &custom_response = response_registry_->get_response_for_error_code(http_status_code);

    // Add the transaction plugin to the transaction
    transaction.addPlugin(new BoomTransactionPlugin(transaction, http_status_code, http_reason_phrase, custom_response));
    // No need to resume/error the transaction,
    // as BoomTransactionPlugin will take care of terminating the transaction
    return;
  } else {
    TS_DEBUG(TAG, "Code %d was not in the registry, transaction will be resumed", http_status_code);
    transaction.resume();
  }
}

/*
 * This is the plugin registration point
 */
void
TSPluginInit(int argc, const char *argv[])
{
  if (!RegisterGlobalPlugin("CPP_Example_Boom", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  boom_counter.init(BOOM_COUNTER);
  BoomResponseRegistry *pregistry = new BoomResponseRegistry();

  // If base directory and list of codes are specified,
  // create a custom registry and initialize Boom with it.
  // Otherwise, run with default registry.
  if (argc == 3) {
    std::string base_directory(argv[1], strlen(argv[1]));
    pregistry->populate_error_responses(base_directory);

    std::string error_codes_argument(argv[2], strlen(argv[2]));
    std::vector<std::string> error_codes;
    stringSplit(error_codes_argument, ',', error_codes);
    pregistry->register_error_codes(error_codes);
  } else {
    TS_ERROR(TAG, "Invalid number of command line arguments, using compile time defaults.");
  }

  plugin = new BoomGlobalPlugin(pregistry);
}
