/**
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
#include "RecordsUtils.h"

#include <system_error>
#include <string>

#include "convert.h"
#include "records/P_RecCore.h"
#include "tscore/Tokenizer.h"

namespace
{ // anonymous namespace

struct RPCRecordErrorCategory : std::error_category {
  const char *name() const noexcept override;
  std::string message(int ev) const override;
};

const char *
RPCRecordErrorCategory::name() const noexcept
{
  return "rpc_handler_record_error";
}
std::string
RPCRecordErrorCategory::message(int ev) const
{
  switch (static_cast<rpc::handlers::errors::RecordError>(ev)) {
  case rpc::handlers::errors::RecordError::RECORD_NOT_FOUND:
    return {"Record not found."};
  case rpc::handlers::errors::RecordError::RECORD_NOT_CONFIG:
    return {"Record is not a configuration type."};
  case rpc::handlers::errors::RecordError::RECORD_NOT_METRIC:
    return {"Record is not a metric type."};
  case rpc::handlers::errors::RecordError::INVALID_RECORD_NAME:
    return {"Invalid Record Name."};
  case rpc::handlers::errors::RecordError::VALIDITY_CHECK_ERROR:
    return {"Validity check failed."};
  case rpc::handlers::errors::RecordError::GENERAL_ERROR:
    return {"Error reading the record."};
  case rpc::handlers::errors::RecordError::RECORD_WRITE_ERROR:
    return {"We could not write the record."};
  case rpc::handlers::errors::RecordError::REQUESTED_TYPE_MISMATCH:
    return {"Found record does not match the requested type"};
  case rpc::handlers::errors::RecordError::INVALID_INCOMING_DATA:
    return {"Invalid request data provided"};
  default:
    return "Record error error " + std::to_string(ev);
  }
}

const RPCRecordErrorCategory rpcRecordErrorCategory{};
} // anonymous namespace

namespace rpc::handlers::errors
{
std::error_code
make_error_code(rpc::handlers::errors::RecordError e)
{
  return {static_cast<int>(e), rpcRecordErrorCategory};
}
} // namespace rpc::handlers::errors

namespace
{
struct Context {
  using CbType = std::function<bool(RecT, std::error_code &)>;
  YAML::Node yaml;
  std::error_code ec;
  // regex do not need to set the callback.
  CbType checkCb;
};
} // namespace

namespace rpc::handlers::records::utils
{
void static get_record_impl(std::string const &name, Context &ctx)
{
  auto yamlConverter = [](const RecRecord *record, void *data) {
    auto &ctx = *static_cast<Context *>(data);

    if (!record) {
      ctx.ec = rpc::handlers::errors::RecordError::RECORD_NOT_FOUND;
      return;
    }

    if (!ctx.checkCb(record->rec_type, ctx.ec)) {
      // error_code in the callback will be set.
      return;
    }

    try {
      ctx.yaml = *record;
    } catch (std::exception const &ex) {
      ctx.ec = rpc::handlers::errors::RecordError::GENERAL_ERROR;
      return;
    }
  };

  const auto ret = RecLookupRecord(name.c_str(), yamlConverter, &ctx);

  if (ctx.ec) {
    // This will be set if the invocation of the callback inside the context have something to report, in this case
    // we give this priority of tracking the error back to the caller.
    return;
  }

  if (ret != REC_ERR_OKAY) {
    ctx.ec = rpc::handlers::errors::RecordError::RECORD_NOT_FOUND;
    return;
  }
}

void static get_record_regex_impl(std::string const &regex, unsigned recType, Context &ctx)
{
  // In this case, where we lookup base on a regex, the only validation we need is base on the recType and the ability to be
  // converted to a Yaml Node.
  auto yamlConverter = [](const RecRecord *record, void *data) {
    auto &ctx = *static_cast<Context *>(data);

    if (!record) {
      return;
    }

    YAML::Node recordYaml;

    try {
      recordYaml = *record;
    } catch (std::exception const &ex) {
      ctx.ec = rpc::handlers::errors::RecordError::GENERAL_ERROR;
      return;
    }

    // we have to append the records to the context one.
    ctx.yaml.push_back(recordYaml);
  };

  const auto ret = RecLookupMatchingRecords(recType, regex.c_str(), yamlConverter, &ctx);
  // if the passed regex didn't match, it will not report any error. We will only get errors when converting
  // the record into yaml(so far).
  if (ctx.ec) {
    return;
  }

  if (ret != REC_ERR_OKAY) {
    ctx.ec = rpc::handlers::errors::RecordError::GENERAL_ERROR;
    return;
  }
}

// This two functions may look similar but they are not. First runs the validation in a different way.
std::tuple<YAML::Node, std::error_code>
get_yaml_record_regex(std::string const &name, unsigned recType)
{
  Context ctx;

  // librecord API will use the recType to validate the type.
  get_record_regex_impl(name, recType, ctx);

  return {ctx.yaml, ctx.ec};
}

std::tuple<YAML::Node, std::error_code>
get_yaml_record(std::string const &name, ValidateRecType check)
{
  Context ctx;

  // Set the validation callback.
  ctx.checkCb = check;

  // librecords will use the callback we provide in the ctx.checkCb to run the validation.
  get_record_impl(name, ctx);

  return {ctx.yaml, ctx.ec};
}

// Basic functions to help setting a record value properly. All this functionality is originally from WebMgmtUtils.
// TODO: we can work out something different.
namespace
{ // anonymous namespace
  bool
  recordRegexCheck(const char *pattern, const char *value)
  {
    pcre *regex;
    const char *error;
    int erroffset;

    regex = pcre_compile(pattern, 0, &error, &erroffset, nullptr);
    if (!regex) {
      return false;
    } else {
      int r = pcre_exec(regex, nullptr, value, strlen(value), 0, 0, nullptr, 0);

      pcre_free(regex);
      return (r != -1) ? true : false;
    }

    return false; // no-op
  }

  bool
  recordRangeCheck(const char *pattern, const char *value)
  {
    char *p = const_cast<char *>(pattern);
    Tokenizer dashTok("-");

    if (recordRegexCheck("^[0-9]+$", value)) {
      while (*p != '[') {
        p++;
      } // skip to '['
      if (dashTok.Initialize(++p, COPY_TOKS) == 2) {
        int l_limit = atoi(dashTok[0]);
        int u_limit = atoi(dashTok[1]);
        int val     = atoi(value);
        if (val >= l_limit && val <= u_limit) {
          return true;
        }
      }
    }
    return false;
  }

  bool
  recordIPCheck(const char *pattern, const char *value)
  {
    //  regex_t regex;
    //  int result;
    bool check;
    const char *range_pattern = R"(\[[0-9]+\-[0-9]+\]\\\.\[[0-9]+\-[0-9]+\]\\\.\[[0-9]+\-[0-9]+\]\\\.\[[0-9]+\-[0-9]+\])";
    const char *ip_pattern    = "[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9]";

    Tokenizer dotTok1(".");
    Tokenizer dotTok2(".");

    check = true;
    if (recordRegexCheck(range_pattern, pattern) && recordRegexCheck(ip_pattern, value)) {
      if (dotTok1.Initialize(const_cast<char *>(pattern), COPY_TOKS) == 4 &&
          dotTok2.Initialize(const_cast<char *>(value), COPY_TOKS) == 4) {
        for (int i = 0; i < 4 && check; i++) {
          if (!recordRangeCheck(dotTok1[i], dotTok2[i])) {
            check = false;
          }
        }
        if (check) {
          return true;
        }
      }
    } else if (strcmp(value, "") == 0) {
      return true;
    }
    return false;
  }
} // namespace

bool
recordValidityCheck(const char *value, RecCheckT checkType, const char *pattern)
{
  switch (checkType) {
  case RECC_STR:
    if (recordRegexCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_INT:
    if (recordRangeCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_IP:
    if (recordIPCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_NULL:
    // skip checking
    return true;
  default:
    // unknown RecordCheckType...
    ;
  }

  return false;
}

} // namespace rpc::handlers::records::utils
