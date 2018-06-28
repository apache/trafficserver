/**
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

#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/AsyncTimer.h>
#include <atscppapi/GlobalPlugin.h>

using namespace atscppapi;
using std::string;

#define TAG "async_timer"

class TimerEventReceiver : public AsyncReceiver<AsyncTimer>
{
public:
  TimerEventReceiver(AsyncTimer::Type type, int period_in_ms, int initial_period_in_ms = 0, int max_instances = 0,
                     bool cancel = false)
    : max_instances_(max_instances), instance_count_(0), type_(type), cancel_(cancel)
  {
    timer_ = new AsyncTimer(type, period_in_ms, initial_period_in_ms);
    Async::execute<AsyncTimer>(this, timer_, std::shared_ptr<Mutex>()); // letting the system create the mutex
  }

  void
  handleAsyncComplete(AsyncTimer &timer ATSCPPAPI_UNUSED) override
  {
    TS_DEBUG(TAG, "Got timer event in object %p!", this);
    if ((type_ == AsyncTimer::TYPE_ONE_OFF) || (max_instances_ && (++instance_count_ == max_instances_))) {
      TS_DEBUG(TAG, "Stopping timer in object %p!", this);
      cancel_ ? timer_->cancel() : delete this;
    }
  }

  ~TimerEventReceiver() override { delete timer_; }

private:
  int max_instances_;
  int instance_count_;
  AsyncTimer::Type type_;
  AsyncTimer *timer_;
  bool cancel_;
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_AsyncTimer", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  int period_in_ms           = 1000;
  TimerEventReceiver *timer1 = new TimerEventReceiver(AsyncTimer::TYPE_PERIODIC, period_in_ms);
  TS_DEBUG(TAG, "Created periodic timer %p with initial period 0, regular period %d and max instances 0", timer1, period_in_ms);

  int initial_period_in_ms   = 100;
  TimerEventReceiver *timer2 = new TimerEventReceiver(AsyncTimer::TYPE_PERIODIC, period_in_ms, initial_period_in_ms);
  TS_DEBUG(TAG, "Created periodic timer %p with initial period %d, regular period %d and max instances 0", timer2,
           initial_period_in_ms, period_in_ms);

  initial_period_in_ms       = 200;
  int max_instances          = 10;
  TimerEventReceiver *timer3 = new TimerEventReceiver(AsyncTimer::TYPE_PERIODIC, period_in_ms, initial_period_in_ms, max_instances);
  TS_DEBUG(TAG, "Created periodic timer %p with initial period %d, regular period %d and max instances %d", timer3,
           initial_period_in_ms, period_in_ms, max_instances);

  TimerEventReceiver *timer4 = new TimerEventReceiver(AsyncTimer::TYPE_ONE_OFF, period_in_ms);
  TS_DEBUG(TAG, "Created one-off timer %p with period %d", timer4, period_in_ms);

  initial_period_in_ms = 0;
  max_instances        = 5;
  TimerEventReceiver *timer5 =
    new TimerEventReceiver(AsyncTimer::TYPE_PERIODIC, period_in_ms, initial_period_in_ms, max_instances, true /* cancel */);
  TS_DEBUG(TAG, "Created canceling timer %p with initial period %d, regular period %d and max instances %d", timer5,
           initial_period_in_ms, period_in_ms, max_instances);

  (void)timer1;
  (void)timer2;
  (void)timer3;
  (void)timer4;
  (void)timer5;
}
