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


#include "diags.i"

class MaybeKillMe
{
public:
  MaybeKillMe(Continuation * c, int *flag, int *refct)
  : m_c(c)
   , m_flag(flag)
   , m_refct(refct)
  {
    (*m_refct)++;
  };
  virtual ~ MaybeKillMe() {
    (*m_refct)--;
    if (*m_refct == 0 && *m_flag) {
      printf("killing C1\n");
      delete m_c;
    }
  };
private:
  Continuation * m_c;
  int *m_flag;
  int *m_refct;
};

class P1:public Continuation
{
public:
  P1()
  :Continuation(new_ProxyMutex())
  , m_cbtimer(NULL)
  {
    SET_HANDLER(handleOpDone);
  };
  virtual ~ P1() {
  };
  Action *doOp(Continuation * caller)
  {
    Callback *cb = opQ.newCallback(caller);
    // take reference to 'cb' so not removed just yet.
    if (opQ.in_progress) {
      printf("--op in progress\n");
      opQ.toOpWaitQ(cb);
      return cb->action();
    } else {
      opQ.in_progress = true;
      opQ.toWaitCompletionQ(cb);
      // start operation
      // our operation in this case is a random delay
      ink_hrtime t = HRTIME_MSECONDS(10) + HRTIME_MSECONDS(lrand48() % 2000);
      m_op = eventProcessor.schedule_in(this, t);

      printf("--starting op\n");
      if (!cb->calledback) {
        return cb->action();
      } else {
        // reentrancy -- called back from within operation
        return ACTION_RESULT_DONE;
      }
    }
  };
  int handleOpDone(int event, void *data)
  {
    if (event == EVENT_INTERVAL && data == m_cbtimer) {
      m_cbtimer = NULL;
      if (opQ.processCallbacks()) {
        // need to call back again
        m_cbtimer = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
      }
      return EVENT_DONE;
    }
    if (event == EVENT_INTERVAL && data == m_op) {
      printf("--op complete\n");
      m_op = NULL;
      // operation is complete
      opQ.opIsDone();
      // XXX: perform another operation
    }
    if (opQ.processCallbacks()) {
      // need to call back again
      m_cbtimer = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    }
    return EVENT_DONE;
  };
private:
  OpQueue opQ;
  Action *m_cbtimer;
  Action *m_op;
};

class C1:public Continuation
{
public:
  C1(P1 * processor)
  :Continuation(new_ProxyMutex())
  , m_die(0)
  , m_reentrant(0)
  , m_proc(processor)
  , m_op_action(NULL)
  {
    m_working_timer = eventProcessor.schedule_in(this, HRTIME_SECONDS(2));
    SET_HANDLER(workingState);
    count = 5;
  };
  virtual ~ C1() {
    printf("C1 dying\n");
  };
  void die()
  {
    if (m_op_action) {
      m_op_action->cancel();
    };
    if (m_working_timer) {
      m_working_timer->cancel();
    };
    m_die = 1;
  }
  int workingState(int event, void *data)
  {
    MaybeKillMe(this, &m_die, &m_reentrant);
    if (event == EVENT_INTERVAL && data == m_working_timer) {
      printf("tick\n");
      m_working_timer = eventProcessor.schedule_in(this, HRTIME_MSECONDS(1000));
      if (!m_op_action) {
        // send another operation to processor
        printf("performing op\n");
        count--;
        Action *a = m_proc->doOp(this);
        if (a != ACTION_RESULT_DONE) {
          m_op_action = a;
          if (count == 0) {
            die();
          }
        }
        return EVENT_DONE;
      }
      return EVENT_DONE;
    } else {                    // callback from P1
      printf("Got %d,%d\n", event, data);
      m_op_action = NULL;
    }
    return EVENT_DONE;
  };
  int dieState(int event, void *data)
  {
    MaybeKillMe(this, &m_die, &m_reentrant);
    if (event == EVENT_INTERVAL) {
      m_die = 1;
    }
    return EVENT_DONE;
  };
  int m_die;
  int m_reentrant;
  P1 *m_proc;
  Action *m_working_timer;
  Action *m_op_action;
  int count;
};

int
main(int argc, char *argv[])
{
  (void) argc;
  (void) argv;
  int i;
  int num_net_threads = ink_number_of_processors();
  RecProcessInit(RECM_STAND_ALONE);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(num_net_threads);
  RecProcessStart();
  srand48(time(NULL));

  P1 *p1 = new P1;

  C1 *c1 = new C1(p1);

  this_thread()->execute();
}
