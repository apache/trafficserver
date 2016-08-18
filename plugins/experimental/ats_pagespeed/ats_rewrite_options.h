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

#ifndef ATS_REWRITE_OPTIONS_H_
#define ATS_REWRITE_OPTIONS_H_

#include <string>
#include <vector>

#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/system/public/system_rewrite_options.h"

//#include "ats_configuration.h"

namespace net_instaweb
{
class ThreadSystem;

struct global_settings {
  global_settings()
    : info_urls_local_only(true),
      use_native_fetcher(false),
      use_per_vhost_statistics(true),
      message_buffer_size(1024 * 128),
      shm_cache_size_kb(0)
  //, rate_limit_background_fetches(true)
  //, force_caching(false)
  //, list_outstanding_urls_on_error(false)
  //, track_original_content_length(false)
  {
  }
  bool info_urls_local_only;
  bool use_native_fetcher;
  bool use_per_vhost_statistics;
  int message_buffer_size;
  // bool rate_limit_background_fetches;
  // bool force_caching;
  // bool list_outstanding_urls_on_error;
  // bool track_original_content_length;
  int shm_cache_size_kb;
};

class AtsRewriteOptions : public SystemRewriteOptions
{
public:
  // See rewrite_options::Initialize and ::Terminate
  static void Initialize();
  static void Terminate();

  AtsRewriteOptions(ThreadSystem *thread_system);
  virtual ~AtsRewriteOptions() {}
  const char *ParseAndSetOptions(std::vector<std::string> args, MessageHandler *handler, global_settings &global_config);

  virtual AtsRewriteOptions *Clone() const;
  OptionSettingResult ParseAndSetOptions0(StringPiece directive, GoogleString *msg, MessageHandler *handler);

  virtual OptionSettingResult ParseAndSetOptionFromName1(StringPiece name, StringPiece arg, GoogleString *msg,
                                                         MessageHandler *handler);

private:
  bool SetBoolFlag(bool *v, StringPiece arg);
  static Properties *ats_properties_;
  static void AddProperties();
  void Init();

  bool IsDirective(StringPiece config_directive, StringPiece compare_directive);

  DISALLOW_COPY_AND_ASSIGN(AtsRewriteOptions);
};

} // namespace net_instaweb

#endif // ATS_REWRITE_OPTIONS_H_
