// Copyright 2013 We-Amp B.V.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: oschaaf@we-amp.com (Otto van der Schaaf)
#ifndef ATS_REWRITE_DRIVER_FACTORY_H_
#define ATS_REWRITE_DRIVER_FACTORY_H_

#include <set>

#include "net/instaweb/system/public/system_rewrite_driver_factory.h"
#include "net/instaweb/util/public/md5_hasher.h"
#include "net/instaweb/util/public/scoped_ptr.h"


namespace net_instaweb {


  class AbstractSharedMem;
  //class NgxMessageHandler;
  //class NgxRewriteOptions;
  class AtsServerContext;
  class AtsThreadSystem;
  class GoogleMessageHandler;
  //class NgxUrlAsyncFetcher;
  class SharedCircularBuffer;
  class SharedMemRefererStatistics;
  class SharedMemStatistics;
  class SlowWorker;
  class StaticAssetManager;
  class Statistics;
  class StaticAssetManager;
  //class SystemCaches;

class AtsRewriteDriverFactory : public SystemRewriteDriverFactory { 
 public:
  explicit AtsRewriteDriverFactory(AtsThreadSystem* thread_system);
  virtual ~AtsRewriteDriverFactory();

  virtual Hasher* NewHasher();
  virtual MessageHandler* DefaultHtmlParseMessageHandler();
  virtual MessageHandler* DefaultMessageHandler();
  virtual FileSystem* DefaultFileSystem();
  virtual Timer* DefaultTimer();
  virtual NamedLockManager* DefaultLockManager();
  virtual RewriteOptions* NewRewriteOptions();

  virtual bool UseBeaconResultsInFilters() const {
    return true;
  }

  virtual void InitStaticAssetManager(StaticAssetManager* static_js_manager);
  
  // Initializes all the statistics objects created transitively by
  // AtsRewriteDriverFactory, including nginx-specific and
  // platform-independent statistics.
  static void InitStats(Statistics* statistics);

  virtual net_instaweb::QueuedWorkerPool* CreateWorkerPool(WorkerPoolCategory pool,
                                                           StringPiece name);
  virtual void NonStaticInitStats(Statistics* statistics) {
    InitStats(statistics);
  }

  AtsServerContext* MakeAtsServerContext();
  ServerContext* NewServerContext();
  //AbstractSharedMem* shared_mem_runtime() const {
  //  return shared_mem_runtime_.get();
  //}

  // Starts pagespeed threads if they've not been started already.  Must be
  // called after the caller has finished any forking it intends to do.
  void StartThreads();
  bool use_per_vhost_statistics() const {
    return use_per_vhost_statistics_;
  }
  void set_use_per_vhost_statistics(bool x) {
    use_per_vhost_statistics_ = x;
  }

 protected:
 private:
  //scoped_ptr<AbstractSharedMem> shared_mem_runtime_;
  GoogleMessageHandler* ats_message_handler_;
  GoogleMessageHandler* ats_html_parse_message_handler_;
  bool use_per_vhost_statistics_;
  bool threads_started_;
};

}  // namespace net_instaweb

#endif // ATS_REWRITE_DRIVER_FACTORY_H_
