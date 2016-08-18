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

#include "ats_rewrite_options.h"

#include "net/instaweb/public/version.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/util/public/timer.h"

#include "net/instaweb/util/public/message_handler.h"
#include "net/instaweb/rewriter/public/file_load_policy.h"

#include "net/instaweb/util/public/stdio_file_system.h"

#include "ats_message_handler.h"
#include "ats_rewrite_driver_factory.h"

using namespace std;

namespace net_instaweb
{
RewriteOptions::Properties *AtsRewriteOptions::ats_properties_ = NULL;

AtsRewriteOptions::AtsRewriteOptions(ThreadSystem *thread_system) : SystemRewriteOptions(thread_system)
{
  Init();
}

void
AtsRewriteOptions::Init()
{
  DCHECK(ats_properties_ != NULL) << "Call AtsRewriteOptions::Initialize() before construction";
  InitializeOptions(ats_properties_);
}

void
AtsRewriteOptions::AddProperties()
{
  MergeSubclassProperties(ats_properties_);
  AtsRewriteOptions dummy_config(NULL);

  dummy_config.set_default_x_header_value(MOD_PAGESPEED_VERSION_STRING "-" LASTCHANGE_STRING);
}

void
AtsRewriteOptions::Initialize()
{
  if (Properties::Initialize(&ats_properties_)) {
    SystemRewriteOptions::Initialize();
    AddProperties();
  }
}

void
AtsRewriteOptions::Terminate()
{
  if (Properties::Terminate(&ats_properties_)) {
    SystemRewriteOptions::Terminate();
  }
}

bool
AtsRewriteOptions::IsDirective(StringPiece config_directive, StringPiece compare_directive)
{
  return StringCaseEqual(config_directive, compare_directive);
}

RewriteOptions::OptionSettingResult
AtsRewriteOptions::ParseAndSetOptions0(StringPiece directive, GoogleString *msg, MessageHandler *handler)
{
  if (IsDirective(directive, "on")) {
    set_enabled(RewriteOptions::kEnabledOn);
  } else if (IsDirective(directive, "off")) {
    set_enabled(RewriteOptions::kEnabledOff);
  } else if (IsDirective(directive, "unplugged")) {
    set_enabled(RewriteOptions::kEnabledUnplugged);
  } else {
    return RewriteOptions::kOptionNameUnknown;
  }
  return RewriteOptions::kOptionOk;
}

RewriteOptions::OptionSettingResult
AtsRewriteOptions::ParseAndSetOptionFromName1(StringPiece name, StringPiece arg, GoogleString *msg, MessageHandler *handler)
{
  // FileCachePath needs error checking.
  if (StringCaseEqual(name, kFileCachePath)) {
    if (!StringCaseStartsWith(arg, "/")) {
      *msg = "must start with a slash";
      return RewriteOptions::kOptionValueInvalid;
    }
  }

  return SystemRewriteOptions::ParseAndSetOptionFromName1(name, arg, msg, handler);
}

bool
AtsRewriteOptions::SetBoolFlag(bool *v, StringPiece arg)
{
  if (IsDirective(arg, "on")) {
    *v = true;
    return true;
  } else if (IsDirective(arg, "off")) {
    *v = false;
    return true;
  }
  return false;
}

const char *
AtsRewriteOptions::ParseAndSetOptions(vector<string> args, MessageHandler *handler, global_settings &global_config)
{
  int n_args = args.size();
  CHECK_GE(n_args, 1);

  StringPiece directive = args[0];

  // Remove initial "ModPagespeed" if there is one.
  StringPiece mod_pagespeed("ModPagespeed");
  if (StringCaseStartsWith(directive, mod_pagespeed)) {
    directive.remove_prefix(mod_pagespeed.size());
  }

  GoogleString msg;
  OptionSettingResult result;
  if (n_args == 1) {
    result = ParseAndSetOptions0(directive, &msg, handler);
  } else if (n_args == 2) {
    StringPiece arg = args[1];
    if (IsDirective(directive, "UsePerVHostStatistics")) {
      if (!SetBoolFlag(&global_config.use_per_vhost_statistics, arg)) {
        msg    = "Failed to set UsePerVHostStatistics value";
        result = RewriteOptions::kOptionValueInvalid;
      } else {
        result = RewriteOptions::kOptionOk;
      }
    } /* else if (IsDirective(directive, "InstallCrashHandler")) {
         // Not applicable
         } */ else if (IsDirective(directive, "MessageBufferSize")) {
      int message_buffer_size;
      bool ok = StringToInt(arg.as_string(), &message_buffer_size);
      if (ok && message_buffer_size >= 0) {
        global_config.message_buffer_size = message_buffer_size;
        result                            = RewriteOptions::kOptionOk;
      } else {
        msg    = "Failed to set MessageBufferSize value";
        result = RewriteOptions::kOptionValueInvalid;
      }
    } else if (IsDirective(directive, "UseNativeFetcher")) {
      if (!SetBoolFlag(&global_config.info_urls_local_only, arg)) {
        msg    = "Failed to set UseNativeFetcher value";
        result = RewriteOptions::kOptionValueInvalid;
      } else {
        msg = "Native fetcher is not available in this release";

        result = RewriteOptions::kOptionValueInvalid;
      }
    } else if (IsDirective(directive, "InfoUrlsLocalOnly")) {
      if (!SetBoolFlag(&global_config.info_urls_local_only, arg)) {
        msg    = "Failed to set InfoUrlsLocalOnly value";
        result = RewriteOptions::kOptionValueInvalid;
      } else {
        result = RewriteOptions::kOptionOk;
      }
    } /* else if (IsDirective(directive, "RateLimitBackgroundFetches")) {
        if (!SetBoolFlag(&global_config.rate_limit_background_fetches, arg)) {
        msg = "Failed to set RateLimitBackgroundFetches value";
        result = RewriteOptions::kOptionValueInvalid;
        } else {
        result = RewriteOptions::kOptionOk;
        }
            }  else if (IsDirective(directive, "ForceCaching")) {
            if (!SetBoolFlag(&global_config.force_caching, arg)) {
            msg = "Failed to set ForceCaching value";
            result = RewriteOptions::kOptionValueInvalid;
            } else {
            result = RewriteOptions::kOptionOk;
            }
                } else if (IsDirective(directive, "ListOutstandingUrlsOnError")) {
                if (!SetBoolFlag(&global_config.list_outstanding_urls_on_error, arg)) {
                msg = "Failed to set ListOutstandingUrlsOnError value";
                result = RewriteOptions::kOptionValueInvalid;
                } else {
                result = RewriteOptions::kOptionOk;
                }
                    } else if (IsDirective(directive, "TrackOriginalContentLength")) {
                    if (!SetBoolFlag(&global_config.track_original_content_length, arg)) {
                    msg = "Failed to set TrackOriginalContentLength value";
                    result = RewriteOptions::kOptionValueInvalid;
                    } else {
                    result = RewriteOptions::kOptionOk;
                    }
                    } */ else {
      result = ParseAndSetOptionFromName1(directive, args[1], &msg, handler);
    }
  } else if (n_args == 3) {
    if (StringCaseEqual(directive, "CreateSharedMemoryMetadataCache")) {
      int64 kb = 0;
      if (!StringToInt64(args[2], &kb) || kb < 0) {
        result = RewriteOptions::kOptionValueInvalid;
        msg    = "size_kb must be a positive 64-bit integer";
      } else {
        global_config.shm_cache_size_kb = kb;
        result                          = kOptionOk;
        // bool ok = driver_factory->caches()->CreateShmMetadataCache(
        //    args[1].as_string(), kb, &msg);
        // result = ok ? kOptionOk : kOptionValueInvalid;
      }
    } else {
      result = ParseAndSetOptionFromName2(directive, args[1], args[2], &msg, handler);
    }
  } else if (n_args == 4) {
    result = ParseAndSetOptionFromName3(directive, args[1], args[2], args[3], &msg, handler);
  } else {
    return "unknown option";
  }

  if (msg.size()) {
    handler->Message(kWarning, "Error handling config line [%s]: [%s]", JoinString(args, ' ').c_str(), msg.c_str());
  }

  switch (result) {
  case RewriteOptions::kOptionOk:
    return NULL;
  case RewriteOptions::kOptionNameUnknown:
    handler->Message(kWarning, "%s", JoinString(args, ' ').c_str());
    return "unknown option";
  case RewriteOptions::kOptionValueInvalid: {
    handler->Message(kWarning, "%s", JoinString(args, ' ').c_str());
    return "Invalid value";
  }
  }

  CHECK(false);
  return NULL;
}

AtsRewriteOptions *
AtsRewriteOptions::Clone() const
{
  AtsRewriteOptions *options = new AtsRewriteOptions(this->thread_system());
  options->Merge(*this);
  return options;
}

} // namespace net_instaweb
