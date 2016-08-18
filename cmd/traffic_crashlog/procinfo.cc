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

#include "traffic_crashlog.h"
#include <sys/utsname.h>

static int
procfd_open(pid_t pid, const char *fname)
{
  char path[128];
  snprintf(path, sizeof(path), "/proc/%ld/%s", (long)pid, fname);
  return open(path, O_RDONLY);
}

static char *
procfd_readlink(pid_t pid, const char *fname)
{
  char path[128];
  ssize_t nbytes;
  ats_scoped_str resolved((char *)ats_malloc(MAXPATHLEN + 1));

  snprintf(path, sizeof(path), "/proc/%ld/%s", (long)pid, fname);
  nbytes = readlink(path, resolved, MAXPATHLEN);
  if (nbytes == -1) {
    Note("readlink failed with %s", strerror(errno));
    return NULL;
  }

  resolved[nbytes] = '\0';
  return resolved.release();
}

bool
crashlog_write_regions(FILE *fp, const crashlog_target &target)
{
  ats_scoped_fd fd;
  textBuffer text(0);

  fd = procfd_open(target.pid, "maps");
  if (fd != -1) {
    text.slurp(fd);
    text.chomp();
    fprintf(fp, "Memory Regions:\n%.*s\n", (int)text.spaceUsed(), text.bufPtr());
  }

  return !text.empty();
}

bool
crashlog_write_uname(FILE *fp, const crashlog_target &)
{
  struct utsname uts;

  if (uname(&uts) == 0) {
    fprintf(fp, LABELFMT "%s %s %s %s\n", "System Version:", uts.sysname, uts.machine, uts.version, uts.release);
  } else {
    fprintf(fp, LABELFMT "%s\n", "System Version:", "unknown");
  }

  return true;
}

bool
crashlog_write_exename(FILE *fp, const crashlog_target &target)
{
  ats_scoped_str str;

  str = procfd_readlink(target.pid, "exe");
  if (str) {
    fprintf(fp, LABELFMT "%s\n", "File:", (const char *)str);
    return true;
  }

  return false;
}

bool
crashlog_write_procname(FILE *fp, const crashlog_target &target)
{
  ats_scoped_fd fd;
  ats_scoped_str str;
  textBuffer text(0);

  fd = procfd_open(target.pid, "comm");
  if (fd != -1) {
    text.slurp(fd);
    text.chomp();
    fprintf(fp, LABELFMT "%s [%ld]\n", "Process:", text.bufPtr(), (long)target.pid);
  } else {
    fprintf(fp, LABELFMT "%ld\n", "Process:", (long)target.pid);
  }

  return true;
}

bool
crashlog_write_datime(FILE *fp, const crashlog_target &target)
{
  char buf[128];

  strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z", &target.timestamp);
  fprintf(fp, LABELFMT "%s\n", "Date:", buf);
  return true;
}

bool
crashlog_write_procstatus(FILE *fp, const crashlog_target &target)
{
  ats_scoped_fd fd;
  textBuffer text(0);

  fd = procfd_open(target.pid, "status");
  if (fd != -1) {
    text.slurp(fd);
    text.chomp();

    fprintf(fp, "Process Status:\n%s\n", text.bufPtr());
  }

  return !text.empty();
}

bool
crashlog_write_backtrace(FILE *fp, const crashlog_target &)
{
  TSString trace = NULL;
  TSMgmtError mgmterr;

  // NOTE: sometimes we can't get a backtrace because the ptrace attach will fail with
  // EPERM. I've seen this happen when a debugger is attached, which makes sense, but it
  // can also happen without a debugger. Possibly in that case, there is a race with the
  // kernel locking the process information?

  if ((mgmterr = TSProxyBacktraceGet(0, &trace)) != TS_ERR_OKAY) {
    char *msg = TSGetErrorMessage(mgmterr);
    fprintf(fp, "Unable to retrieve backtrace: %s\n", msg);
    TSfree(msg);
    return false;
  }

  fprintf(fp, "%s", trace);
  TSfree(trace);
  return true;
}

bool
crashlog_write_records(FILE *fp, const crashlog_target &)
{
  TSMgmtError mgmterr;
  TSList list  = TSListCreate();
  bool success = false;

  if ((mgmterr = TSRecordGetMatchMlt(".", list)) != TS_ERR_OKAY) {
    char *msg = TSGetErrorMessage(mgmterr);
    fprintf(fp, "Unable to retrieve Traffic Server records: %s\n", msg);
    TSfree(msg);
    goto done;
  }

  // If the RPC call failed, the list will be empty, so we won't print anything. Otherwise,
  // print all the results, freeing them as we go.
  for (TSRecordEle *rec_ele = (TSRecordEle *)TSListDequeue(list); rec_ele; rec_ele = (TSRecordEle *)TSListDequeue(list)) {
    if (!success) {
      success = true;
      fprintf(fp, "Traffic Server Configuration Records:\n");
    }

    switch (rec_ele->rec_type) {
    case TS_REC_INT:
      fprintf(fp, "%s %" PRId64 "\n", rec_ele->rec_name, rec_ele->valueT.int_val);
      break;
    case TS_REC_COUNTER:
      fprintf(fp, "%s %" PRId64 "\n", rec_ele->rec_name, rec_ele->valueT.counter_val);
      break;
    case TS_REC_FLOAT:
      fprintf(fp, "%s %f\n", rec_ele->rec_name, rec_ele->valueT.float_val);
      break;
    case TS_REC_STRING:
      fprintf(fp, "%s %s\n", rec_ele->rec_name, rec_ele->valueT.string_val);
      break;
    default:
      // just skip it ...
      break;
    }

    TSRecordEleDestroy(rec_ele);
  }

done:
  TSListDestroy(list);
  return success;
}

bool
crashlog_write_siginfo(FILE *fp, const crashlog_target &target)
{
  char tmp[32];

  if (!(CRASHLOG_HAVE_THREADINFO & target.flags)) {
    fprintf(fp, "No target signal information\n");
    return false;
  }

  fprintf(fp, "Signal Status:\n");
  fprintf(fp, LABELFMT "%d (%s)\n", "siginfo.si_signo:", target.siginfo.si_signo, strsignal(target.siginfo.si_signo));

  snprintf(tmp, sizeof(tmp), "%ld", (long)target.siginfo.si_pid);
  fprintf(fp, LABELFMT LABELFMT, "siginfo.si_pid:", tmp);
  fprintf(fp, LABELFMT "%ld", "siginfo.si_uid:", (long)target.siginfo.si_uid);
  fprintf(fp, "\n");

  snprintf(tmp, sizeof(tmp), "0x%x (%d)", target.siginfo.si_code, target.siginfo.si_code);
  fprintf(fp, LABELFMT LABELFMT, "siginfo.si_code:", tmp);
  fprintf(fp, LABELFMT ADDRFMT, "siginfo.si_addr:", ADDRCAST(target.siginfo.si_addr));
  fprintf(fp, "\n");

  if (target.siginfo.si_code == SI_USER) {
    fprintf(fp, "Signal delivered by user %ld from process %ld\n", (long)target.siginfo.si_uid, (long)target.siginfo.si_pid);
    return true;
  }

  if (target.siginfo.si_signo == SIGSEGV) {
    const char *msg = "Unknown error";

    switch (target.siginfo.si_code) {
    case SEGV_MAPERR:
      msg = "No object mapped";
      break;
    case SEGV_ACCERR:
      msg = "Invalid permissions for mapped object";
      break;
    }

    fprintf(fp, "%s at address " ADDRFMT "\n", msg, ADDRCAST(target.siginfo.si_addr));
    return true;
  }

  if (target.siginfo.si_signo == SIGSEGV) {
    const char *msg = "Unknown error";

    switch (target.siginfo.si_code) {
    case BUS_ADRALN:
      msg = "Invalid address alignment";
      break;
    case BUS_ADRERR:
      msg = "Nonexistent physical address";
      break;
    case BUS_OBJERR:
      msg = "Object-specific hardware error";
      break;
    }

    fprintf(fp, "%s at address " ADDRFMT "\n", msg, ADDRCAST(target.siginfo.si_addr));
    return true;
  }

  return true;
}

bool
crashlog_write_registers(FILE *fp, const crashlog_target &target)
{
  if (!(CRASHLOG_HAVE_THREADINFO & target.flags)) {
    fprintf(fp, "No target CPU registers\n");
    return false;
  }

#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))

// x86 register names as per ucontext.h.
#if defined(__i386__)
#define REGFMT "0x%08" PRIx32
#define REGCAST(x) ((uint32_t)(x))
  static const char *names[NGREG] = {"GS",  "FS",  "ES",     "DS",  "EDI", "ESI", "EBP", "ESP",  "EBX", "EDX",
                                     "ECX", "EAX", "TRAPNO", "ERR", "EIP", "CS",  "EFL", "UESP", "SS"};
#endif

#if defined(__x86_64__)
#define REGFMT "0x%016" PRIx64
#define REGCAST(x) ((uint64_t)(x))
  static const char *names[NGREG] = {"R8",  "R9",  "R10", "R11", "R12", "R13", "R14",    "R15", "RDI",    "RSI",     "RBP", "RBX",
                                     "RDX", "RAX", "RCX", "RSP", "RIP", "EFL", "CSGSFS", "ERR", "TRAPNO", "OLDMASK", "CR2"};
#endif

  fprintf(fp, "CPU Registers:\n");
  for (unsigned i = 0; i < countof(names); ++i) {
    const char *trailer = ((i % 4) == 3) ? "\n" : " ";
    fprintf(fp, "%-3s:" REGFMT "%s", names[i], REGCAST(target.ucontext.uc_mcontext.gregs[i]), trailer);
  }

  fprintf(fp, "\n");
  return true;
#else
  fprintf(fp, "No target CPU register support on this architecture\n");
  return false;
#endif
}
