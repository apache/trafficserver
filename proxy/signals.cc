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

#include "ink_unused.h"  /* MAGIC_EDITING_TAG */
/**************************************************************************
  Signal functions and handlers.

**************************************************************************/

#include <unistd.h>
#include "inktomi++.h"
#include "signals.h"
#include "Config.h"
#include "P_EventSystem.h"
#include "StatSystem.h"
#include "P_Cache.h"
#include "Main.h"


// For backtraces on crash
#include "ink_stack_trace.h"

#ifdef __alpha
#include <obj.h>
#include <sym.h>
#include <demangle.h>
#include <ucontext.h>
#include <excpt.h>

struct obj_list *ObjList;
#endif

#if (HOST_OS != linux) && (HOST_OS != freebsd)
typedef void (*SigActionFunc_t) (int sig, siginfo_t * t, void *f);
#else
typedef void (*SigActionFunc_t) (int sig);
#endif

int exited_children = 0;
extern int stack_trace_flag;

static volatile int sigusr1_received = 0;
extern int fastmemtotal;

class SignalContinuation:public Continuation
{
public:
  char *end;
  char *snap;
  int fastmemsnap;
    SignalContinuation()
  : Continuation(new_ProxyMutex())
  {
    end = snap = 0;
    fastmemsnap = 0;
    SET_HANDLER(&SignalContinuation::periodic);
  }

  int periodic(int event, Event * e)
  {
    if (sigusr1_received) {
      sigusr1_received = 0;

      Cache::print_stats(stderr);
      xdump_to_file(stderr);
      ink_freelists_dump(stderr);
      if (!end)
        end = (char *) sbrk(0);
      if (!snap)
        snap = (char *) sbrk(0);
      char *now = (char *) sbrk(0);
      fprintf(stderr, "sbrk %llX from first %lld from last %lld\n",
              (inku64) ((int_pointer) now), (inku64) ((int_pointer) (now - end)),
              (inku64) ((int_pointer) (now - snap)));
#ifdef DEBUG
      int fmdelta = fastmemtotal - fastmemsnap;
      fprintf(stderr, "fastmem %lld from last %lld\n", (ink64) fastmemtotal, (ink64) fmdelta);
      fastmemsnap += fmdelta;
#endif
      snap = now;
    }

    return EVENT_CONT;
  }
};

class TrackerContinuation:public Continuation
{
public:
  int baseline_taken;
  int use_baseline;
    TrackerContinuation()
  : Continuation(new_ProxyMutex())
  {
    SET_HANDLER(&TrackerContinuation::periodic);
    use_baseline = 0;
    if (getenv("MEMTRACK_BASELINE"))
    {
      use_baseline = 1;
    }
    baseline_taken = 0;
  }

  int periodic(int event, Event * e)
  {
    if (use_baseline) {
      xdump_to_file_baseline_rel(stderr);
      ink_freelists_dump_baselinerel(stderr);
    } else {
      xdump_to_file(stderr);
      ink_freelists_dump(stderr);
    }
    if (!baseline_taken && use_baseline) {
      ink_freelists_snap_baseline();
      xdump_snap_baseline();
      baseline_taken = 1;
    }
    return EVENT_CONT;
  }
};

#if defined (__alpha)

void
print_context(sigcontext & c, int frame)
{
  unsigned long pc = c.sc_pc;

  //
  // Get the object corresponding to this PC
  //
  struct obj *obj = address_to_obj(ObjList, pc);
  char *procedure_name = "<unknown>";
  char *filename = "<unknown>";
  int line_number = -1;

  if (obj) {
    //
    // Get the procedure in this object corresponding to this PC.
    //
    unsigned long mldproc = address_to_procedure(obj, pc);
    if (mldproc != OBJ_FAIL) {
      //
      // Get the procedure name
      //
      unsigned long mldsym = procedure_symbol(obj, mldproc);
      if (mldsym != OBJ_FAIL) {
        char proc_name_buf[1024];

        procedure_name = symbol_name(obj, mldsym);

        //
        // And try to demangle it..
        //
        int result = MLD_demangle_string(procedure_name,
                                         proc_name_buf, sizeof(proc_name_buf),
                                         MLD_SHOW_DEMANGLED_NAME | MLD_SHOW_INFO);

        if (result == MLD_SOMETHING_RECOGNIZED) {
          procedure_name = proc_name_buf;
        }
      }

      long mldfile = procedure_to_file(obj, mldproc);
      if (mldfile != OBJ_FAIL) {
        filename = file_name(obj, mldfile);
      }

      long mldline = address_to_line(obj, pc);
      if (mldline != OBJ_FAIL) {
        line_number = mldline;
      }
    }
  }

  //
  // See /usr/include/machine/context.h for structure
  //
  fprintf(stderr, "[%d]: '%s', line '%d', file '%s' (0x%x)\n", frame, procedure_name, line_number, filename, c.sc_pc);

  fprintf(stderr, "");
  for (int i = 0; i < 32; i++) {

    if (((i + 1) % 4) == 0) {

      fprintf(stderr, "\tReg[%d] = 0x%x\n", i, c.sc_regs[i]);

    } else {

      if (c.sc_regs[i] > 0xff || c.sc_regs[i] < 0) {
        fprintf(stderr, "\tReg[%d] = 0x%x", i, c.sc_regs[i]);
      } else {
        fprintf(stderr, "\tReg[%d] = 0x%x\t", i, c.sc_regs[i]);
      }
    }
  }
  fprintf(stderr, "\n");
}

#endif


static void
interrupt_handler(int sig)
{
  (void) sig;
  fprintf(stderr, "interrupt caught...exit\n");
  shutdown_system();
  _exit(1);
}

#if (HOST_OS == linux)
static void
signal_handler(int sig)
#else
static void
signal_handler(int sig, siginfo_t * t, void *c)
#endif
{

  if (sig == SIGUSR1) {
    sigusr1_received = 1;
    return;
  }
#if defined (__alpha)
  sigcontext starting_context;

  //
  // Establish the current context.  We ought to be
  //  able to extract this from the signal handler arguments
  //  but the expirements to do so failed for unknown
  //  reasons
  //
  exc_capture_context(&starting_context);
#endif

#ifdef __alpha
  if (sig == SIGUSR2) {
    fprintf(stderr, "Entering pthreads debugger\n");
    pthread_debug();
    return;
  }
#endif

#ifdef __alpha
  // If stack trace of fatal errors is enabled, dump it out and
  //  exit the process
  if (stack_trace_flag) {
    int cur_frame = 0;
    print_context(starting_context, cur_frame++);

    for (;;) {
      exc_virtual_unwind(NULL, &starting_context);
      print_context(starting_context, cur_frame++);
      if (starting_context.sc_pc == NULL) {
        fprintf(stderr, "Stack completely unwound\n");
        break;
      }
    }
    _exit(1);
  }
#endif

  char sig_msg[2048];
#if (HOST_OS != linux) && (HOST_OS != freebsd)
  // Print out information about where the signal came from
  //  so that we can debug signal related problems
  //
  //  I'm avoiding use of the Diags stuff since it is more
  //    likely to deadlock from a signal handler.  The syslog
  //    if questionable and probably should be eventually be
  //    turned off but should be helpful through Rator alpha
  //
  //    lomew adds on May 03, 2002: don't call syslog here because it
  //    calls malloc and can deadlock you if the SEGV happened in free after
  //    the heap-mutex has been taken, like if free was called with garbage.
  //
  if (t) {
    if (t->si_code <= 0) {
      snprintf(sig_msg, sizeof(sig_msg), "NOTE: Traffic Server received User Sig %d from pid: %d uid: %d\n",
               sig, t->si_pid, t->si_uid);
    } else {
      snprintf(sig_msg, sizeof(sig_msg), "NOTE: Traffic Server received Kernel Sig %d, Reason: %d\n", sig, t->si_code);
    }

    write(2, sig_msg, strlen(sig_msg));
    //syslog(LOG_ERR, sig_msg);
  }
#else
  snprintf(sig_msg, sizeof(sig_msg), "NOTE: Traffic Server received Sig %d: %s\n", sig, strsignal(sig));
  write(2, sig_msg, strlen(sig_msg));
  //syslog(LOG_ERR, sig_msg); 
#endif

  shutdown_system();

  // Make sure to drop a core for signals that normally
  // would do so.
  switch (sig) {
  case SIGQUIT:
  case SIGILL:
  case SIGTRAP:
#if (HOST_OS != linux)
  case SIGEMT:
  case SIGSYS:
#endif
  case SIGFPE:
  case SIGBUS:
  case SIGXCPU:
  case SIGXFSZ:
  case SIGSEGV:
#if (HOST_OS == linux)
    ink_stack_trace_dump(2);
#endif
    signal(sig, SIG_DFL);
    return;
#if (HOST_OS == linux)
  case SIGUSR2:
    ink_stack_trace_dump(2);
    return;
#endif
  case SIGABRT:
  case SIGUSR1:
  default:
    _exit(sig);
  }
}

static void
child_signal_handler(int sig)
{
  (void) sig;
/* No forked children in VxWorks */
  int pid;
  int saved_errno = errno;
  while ((pid = waitpid(-1, 0, WNOHANG)) > 0) {
    fprintf(stderr, "child %d exited\n", pid);
    ++exited_children;
  }
  errno = saved_errno;
}

static void
set_signal(int signal, SigActionFunc_t action_func)
{
  struct sigaction action;
  struct sigaction o_action;

#if (HOST_OS != linux) && (HOST_OS != freebsd)
  action.sa_handler = NULL;
  action.sa_sigaction = action_func;
#else
  action.sa_handler = action_func;
#endif
  // action.sa_mask = 0;                // changed 10/17/97 to make portable
  sigemptyset(&(action.sa_mask));       // changed 10/17/97 to make portable
  action.sa_flags = 0;

  int res = sigaction(signal, &action, &o_action);
  ink_release_assert(res == 0);
}

static void
check_signal(int signal, SigActionFunc_t action_func)
{
  struct sigaction action;
  struct sigaction o_action;

#if (HOST_OS != linux) && (HOST_OS != freebsd)
  action.sa_handler = NULL;
  action.sa_sigaction = action_func;
  action.sa_flags = SA_SIGINFO;
#else
  action.sa_handler = action_func;
  action.sa_flags = 0;
#endif
  // action.sa_mask = 0;                // changed 10/17/97 to make portable
  sigemptyset(&(action.sa_mask));       // changed 10/17/97 to make portable

  int res = sigaction(signal, &action, &o_action);
  ink_release_assert(res == 0);

#if (HOST_OS != linux) && (HOST_OS != freebsd)
  if (o_action.sa_sigaction != action_func) {
    fprintf(stderr, "Handler for signal %d was %p, not %p as expected\n", signal, o_action.sa_sigaction, action_func);
  }
#endif
}

//
// This is used during debugging to insure that the signals
// don't change from under us, as they did on the DEC alpha
// with a specific version of pthreads.
//

void
check_signals()
{
  check_signal(SIGPIPE, (SigActionFunc_t) SIG_IGN);
  check_signal(SIGQUIT, (SigActionFunc_t) signal_handler);
  check_signal(SIGHUP, (SigActionFunc_t) interrupt_handler);

#if defined (__alpha)
  if (stack_trace_flag) {
    set_signal(SIGABRT, (SigActionFunc_t) signal_handler);
    set_signal(SIGILL, (SigActionFunc_t) signal_handler);
    set_signal(SIGBUS, (SigActionFunc_t) signal_handler);
    set_signal(SIGSEGV, (SigActionFunc_t) signal_handler);
  }
#endif

  check_signal(SIGTERM, (SigActionFunc_t) signal_handler);
  check_signal(SIGUSR1, (SigActionFunc_t) signal_handler);
#ifdef __alpha
  check_signal(SIGUSR2, (SigActionFunc_t) signal_handler);
#endif
}


//
// This thread checks the signals every 2 seconds to make
// certain the DEC pthreads SIGPIPE bug isn't back..
//
#if (HOST_OS != linux) && (HOST_OS != freebsd) && defined(DEBUG)
static void *
check_signal_thread(void *)
{
  for (;;) {
    check_signals();
    sleep(2);
  }
  return NULL;
}
#endif

void
init_signals()
{
  sigset_t sigsToBlock;
  sigemptyset(&sigsToBlock);
  ink_thread_sigsetmask(SIG_SETMASK, &sigsToBlock, NULL);

  set_signal(SIGPIPE, (SigActionFunc_t) SIG_IGN);
  set_signal(SIGQUIT, (SigActionFunc_t) signal_handler);
  set_signal(SIGTERM, (SigActionFunc_t) signal_handler);
  set_signal(SIGHUP, (SigActionFunc_t) interrupt_handler);
  set_signal(SIGILL, (SigActionFunc_t) signal_handler);
  set_signal(SIGBUS, (SigActionFunc_t) signal_handler);
  set_signal(SIGSEGV, (SigActionFunc_t) signal_handler);

//
//    Presviously the following lines were #if 0
//
//  set_signal(SIGILL,(SigActionFunc_t)signal_handler);
//  set_signal(SIGBUS,(SigActionFunc_t)signal_handler);
//  set_signal(SIGSEGV,(SigActionFunc_t)signal_handler);
//
//  There was an an addtional #if 0 w/ a note about SIGABRT
//   // Do not catch, results in recursive 
//   //  SIGABRT loop on solaris assert() failures
//  set_signal(SIGABRT,(SigActionFunc_t)signal_handler);
//
#if defined (__alpha)
  if (stack_trace_flag) {
    set_signal(SIGABRT, (SigActionFunc_t) signal_handler);
    set_signal(SIGILL, (SigActionFunc_t) signal_handler);
    set_signal(SIGBUS, (SigActionFunc_t) signal_handler);
    set_signal(SIGSEGV, (SigActionFunc_t) signal_handler);
  }
#endif

#if (HOST_OS != freebsd)
  set_signal(SIGUSR1, (SigActionFunc_t) signal_handler);
#endif

#ifdef __alpha
  set_signal(SIGUSR2, (SigActionFunc_t) signal_handler);
#endif

#if (HOST_OS == linux)
  set_signal(SIGUSR2, (SigActionFunc_t) signal_handler);
#endif

#if (HOST_OS != linux) && (HOST_OS != freebsd) && defined(DEBUG)
  ink_thread_create(check_signal_thread, NULL);
#endif

  // do not handle these
  // ink_assert(signal(SIGINT,(SigActionFunc_t)interrupt_handler) != SIG_ERR);
}


int
init_tracker(const char *config_var, RecDataT type, RecData data, void *cookie)
{
  static Event *tracker_event = NULL;
  int dump_mem_info_frequency = 0;
  if (config_var)
    dump_mem_info_frequency = data.rec_int;
  else
    dump_mem_info_frequency = REC_ConfigReadInteger("proxy.config.dump_mem_info_frequency");
  Debug("tracker", "init_tracker called [%d]\n", dump_mem_info_frequency);
  if (tracker_event)
    tracker_event->cancel();
  if (dump_mem_info_frequency > 0) {
    tracker_event = eventProcessor.schedule_every(new TrackerContinuation,
                                                  HRTIME_SECONDS(dump_mem_info_frequency), ET_CALL);
  }
  return 1;
}

void
init_signals2()
{
  eventProcessor.schedule_every(new SignalContinuation, HRTIME_MSECOND * 500, ET_CALL);
  REC_RegisterConfigUpdateFunc("proxy.config.dump_mem_info_frequency", init_tracker, NULL);
  RecData data;
  init_tracker(NULL, RECD_INT, data, NULL);
}


void
init_daemon_signals()
{
  struct sigaction act;
  ink_assert(signal(SIGCHLD, (VI_PFN) child_signal_handler) != SIG_ERR);
  act.sa_handler = (VI_PFN) child_signal_handler;
  ink_assert(!sigemptyset(&act.sa_mask));
  act.sa_flags = SA_NOCLDSTOP;
  ink_assert(!sigaction(SIGCHLD, &act, NULL));
}
