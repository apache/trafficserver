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

#include "ats_rewrite_driver_factory.h"

#include <cstdio>
#include <vector>

#include "ats_thread_system.h"
#include "ats_message_handler.h"
#include "ats_server_context.h"

#include "net/instaweb/http/public/content_type.h"
#include "net/instaweb/http/public/rate_controller.h"
#include "net/instaweb/http/public/rate_controlling_url_async_fetcher.h"
#include "net/instaweb/http/public/wget_url_fetcher.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_driver_factory.h"
#include "net/instaweb/rewriter/public/server_context.h"
#include "net/instaweb/rewriter/public/static_asset_manager.h"
#include "net/instaweb/system/public/in_place_resource_recorder.h"
#include "net/instaweb/system/public/serf_url_async_fetcher.h"
#include "net/instaweb/system/public/system_caches.h"
#include "net/instaweb/system/public/system_rewrite_options.h"
#include "net/instaweb/util/public/google_message_handler.h"
#include "net/instaweb/util/public/null_shared_mem.h"
#include "net/instaweb/util/public/posix_timer.h"
#include "net/instaweb/util/public/property_cache.h"
#include "net/instaweb/util/public/pthread_shared_mem.h"
#include "net/instaweb/util/public/scheduler_thread.h"
#include "net/instaweb/util/public/shared_circular_buffer.h"
#include "net/instaweb/util/public/shared_mem_statistics.h"
#include "net/instaweb/util/public/slow_worker.h"
#include "net/instaweb/util/public/stdio_file_system.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"
#include "net/instaweb/util/public/thread_system.h"

namespace net_instaweb
{
AtsRewriteDriverFactory::AtsRewriteDriverFactory(const ProcessContext &process_context, AtsThreadSystem *thread_system,
                                                 StringPiece hostname, int port)
  : SystemRewriteDriverFactory(process_context, thread_system, NULL /*default shared mem runtime*/, "" /*hostname, not used*/,
                               -1 /*port, not used*/),
    ats_message_handler_(new AtsMessageHandler(thread_system->NewMutex())),
    ats_html_parse_message_handler_(new AtsMessageHandler(thread_system->NewMutex())),
    use_per_vhost_statistics_(false),
    threads_started_(false)
{
  InitializeDefaultOptions();
  default_options()->set_beacon_url("/ats_pagespeed_beacon");
  default_options()->set_enabled(RewriteOptions::kEnabledOn);
  default_options()->SetRewriteLevel(RewriteOptions::kCoreFilters);

  SystemRewriteOptions *system_options = dynamic_cast<SystemRewriteOptions *>(default_options());
  system_options->set_log_dir("/tmp/ps_log/");
  system_options->set_statistics_logging_enabled(true);

  system_options->set_file_cache_clean_inode_limit(500000);
  system_options->set_file_cache_clean_size_kb(1024 * 10000); // 10 GB
  system_options->set_avoid_renaming_introspective_javascript(true);
  system_options->set_file_cache_path("/tmp/ats_ps/");
  system_options->set_lru_cache_byte_limit(163840);
  system_options->set_lru_cache_kb_per_process(1024 * 500); // 500 MB

  system_options->set_flush_html(true);

  AtsRewriteOptions *ats_options = (AtsRewriteOptions *)system_options;
  std::vector<std::string> args;
  args.push_back("RateLimitBackgroundFetches");
  args.push_back("on");
  global_settings settings;
  const char *msg = ats_options->ParseAndSetOptions(args, ats_message_handler_, settings);
  CHECK(!msg);

  set_message_buffer_size(1024 * 128);
  set_message_handler(ats_message_handler_);
  set_html_parse_message_handler(ats_html_parse_message_handler_);
  StartThreads();
}

AtsRewriteDriverFactory::~AtsRewriteDriverFactory()
{
  ShutDown();
  delete ats_message_handler_;
  ats_message_handler_ = NULL;
  delete ats_html_parse_message_handler_;
  ats_html_parse_message_handler_ = NULL;
  STLDeleteElements(&uninitialized_server_contexts_);
}

// void AtsRewriteDriverFactory::InitStaticAssetManager(StaticAssetManager* static_js_manager) {
//    //static_js_manager->set_library_url_prefix("/ats_pagespeed_static/");
//}

Hasher *
AtsRewriteDriverFactory::NewHasher()
{
  return new MD5Hasher;
}

MessageHandler *
AtsRewriteDriverFactory::DefaultHtmlParseMessageHandler()
{
  return ats_html_parse_message_handler_;
}

MessageHandler *
AtsRewriteDriverFactory::DefaultMessageHandler()
{
  return ats_message_handler_;
}

FileSystem *
AtsRewriteDriverFactory::DefaultFileSystem()
{
  return new StdioFileSystem();
}

Timer *
AtsRewriteDriverFactory::DefaultTimer()
{
  return new PosixTimer;
}

NamedLockManager *
AtsRewriteDriverFactory::DefaultLockManager()
{
  CHECK(false) << "default lock manager should not be called";
  return NULL;
}

RewriteOptions *
AtsRewriteDriverFactory::NewRewriteOptions()
{
  AtsRewriteOptions *options = new AtsRewriteOptions(thread_system());
  options->SetRewriteLevel(RewriteOptions::kCoreFilters);
  return options;
}

ServerContext *
AtsRewriteDriverFactory::NewDecodingServerContext()
{
  ServerContext *sc = new AtsServerContext(this);
  InitStubDecodingServerContext(sc);
  return sc;
}

void
AtsRewriteDriverFactory::InitStats(Statistics *statistics)
{
  // Init standard PSOL stats.
  SystemRewriteDriverFactory::InitStats(statistics);
  // Init Ats-specific stats.
  AtsServerContext::InitStats(statistics);
}

AtsServerContext *
AtsRewriteDriverFactory::MakeAtsServerContext()
{
  AtsServerContext *server_context = new AtsServerContext(this);
  uninitialized_server_contexts_.insert(server_context);
  return server_context;
}

ServerContext *
AtsRewriteDriverFactory::NewServerContext()
{
  LOG(DFATAL) << "MakeAtsServerContext should be used instead";
  return NULL;
}

net_instaweb::QueuedWorkerPool *
AtsRewriteDriverFactory::CreateWorkerPool(net_instaweb::RewriteDriverFactory::WorkerPoolCategory pool, StringPiece name)
{
  int tc = 8;
  TSDebug("ats_pagespeed", "Created new QueuedWorkerPool of type %d named '%s' of size %d", pool, name.data(), tc);
  net_instaweb::QueuedWorkerPool *q_pool = new net_instaweb::QueuedWorkerPool(tc, name, thread_system());
  return q_pool;
}

void
AtsRewriteDriverFactory::StartThreads()
{
  if (threads_started_) {
    CHECK(false) << "threads already started";
  }
  SchedulerThread *thread = new SchedulerThread(thread_system(), scheduler());
  bool ok                 = thread->Start();
  CHECK(ok) << "Unable to start scheduler thread";
  defer_cleanup(thread->MakeDeleter());
  threads_started_ = true;
}

} // namespace net_instaweb
