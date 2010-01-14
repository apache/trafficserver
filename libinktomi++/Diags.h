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

/****************************************************************************

  Diags.h

  This file contains code to manipulate run-time diagnostics, and print
  warnings and errors at runtime.  Action tags and debugging tags are
  supported, allowing run-time conditionals affecting diagnostics.

  
 ****************************************************************************/

#ifndef __DIAGS_H___
#define __DIAGS_H___

#include <stdarg.h>
#include "ink_bool.h"
#include "ink_error.h"
#include "ink_mutex.h"
#include "Regex.h"
#include "ink_apidefs.h"

#define DIAGS_MAGIC 0x12345678

#if !defined(USE_DIAGS)
#define NO_DIAG 1
#endif

// eInsertStringType - defined in ink_error.h //
//typedef enum
//{
//    keNo_OP = 0,
//    keENCAPSULATED_STRING = 1,        
//    keINSERT_STRING = 2
//} eInsertStringType;

//////////////////////////////////////////////////////////////////////////////
//
//      On Feb 10, 1998, Peter Mattis showed Diags overhead was non-existant
//      when the run-time enable flag was turned off.  So, on this great and
//      glorious date, run-time diags were incorporated into Traffic Server.
//
//      Date: Tue, 10 Feb 1998 19:52:25 -0800
//      Subject: Re: ~5 ops/sec faster when statistics are turned off
//
//      (1) diags commented out
//              40% hit-rate...372.85 ops/sec
//      (2) diags in code, but enabled = 0
//              40% hit-rate...375.72 ops/sec
//      (3) diags in code, enabled = 1, but no tags match
//              40% hit-rate...111.86 ops/sec
//
//      Looks like we're faster with diagnostics compiled in but
//      turned off in records.config. :)  My runs are not long enough
//      for the above numbers to be anything other than
//      approximations.  But it does show that diagnostics cost
//      basically nothing when not enabled.
//
//      Peter
//
//  On November 23, 1999, determined that the performance
//  impact was insignificant only because of the heinously inefficient
//  code written, and turned of Diags in the optimized 
//  code.
// 
//
//////////////////////////////////////////////////////////////////////////////


class Diags;

// extern int diags_on_for_plugins;
typedef enum
{
  DiagsTagType_Debug = 0,       // do not renumber --- used as array index
  DiagsTagType_Action = 1
} DiagsTagType;

struct DiagsModeOutput
{
  bool to_stdout;
  bool to_stderr;
  bool to_syslog;
  bool to_diagslog;
};

typedef enum
{                               // do not renumber --- used as array index
  DL_Diag = 0,                  // process does not die
  DL_Debug,                     // process does not die
  DL_Status,                    // process does not die
  DL_Note,                      // process does not die
  DL_Warning,                   // process does not die
  DL_Error,                     // process does not die
  DL_Fatal,                     // causes process termination
  DL_Alert,                     // causes process termination
  DL_Emergency,                 // causes process termination
  DL_Undefined                  // must be last, used for size!
} DiagsLevel;

#define DiagsLevel_Count DL_Undefined

#define DiagsLevel_IsTerminal(_l) (((_l) >= DL_Fatal) && ((_l) < DL_Undefined))

#ifndef INK_NO_DIAGS
// Cleanup Function Prototype - Called before ink_fatal to
//   cleanup process state
typedef void (*DiagsCleanupFunc) ();

struct DiagsConfigState
{
  bool enabled[2];              // one debug, one action
  DiagsModeOutput outputs[DiagsLevel_Count];    // where each level prints
};


//////////////////////////////////////////////////////////////////////////////
//
//      class SrcLoc
//
//      The SrcLoc class wraps up a source code location, including file
//      name, function name, and line number, and contains a method to
//      format the result into a string buffer.
//
//////////////////////////////////////////////////////////////////////////////

class SrcLoc
{
public:
  bool valid;
  const char *file;
  const char *func;
  int line;

  void set(const char *_file, const char *_func, int _line)
  {
    valid = true;
    file = _file;
    func = _func;
    line = _line;
  }

  SrcLoc(const char *_file, const char *_func, int _line)
  {
    set(_file, _func, _line);
  }

SrcLoc():valid(false), file(NULL), func(NULL), line(0) {
  }
  ~SrcLoc() {
  };

  char *str(char *buf, int buflen);
};


//////////////////////////////////////////////////////////////////////////////
//
//      class Diags
//
//      The Diags class is used for global configuration of the run-time
//      diagnostics system.  This class provides the following services:
//
//      * run-time notices, debugging, warnings, errors
//      * debugging tags to selectively enable & disable diagnostics
//      * action tags to selectively enable & disable code paths
//      * configurable output to stdout, stderr, syslog, error logs
//      * traffic_manager interface supporting on-the-fly reconfiguration
//
//////////////////////////////////////////////////////////////////////////////

class Diags
{
public:
  Diags(char *base_debug_tags, char *base_action_tags, FILE * _diags_log_fp = NULL);
   ~Diags();

  FILE *diags_log_fp;
  unsigned int magic;
  volatile DiagsConfigState config;
  int show_location;
  DiagsCleanupFunc cleanup_func;
  char *prefix_str;

  ///////////////////////////
  // conditional debugging //
  ///////////////////////////

  bool on(DiagsTagType mode = DiagsTagType_Debug) {
    return (config.enabled[mode]);
  }
  bool on(const char *tag, DiagsTagType mode = DiagsTagType_Debug) {
    return (config.enabled[mode] && tag_activated(tag, mode));
  }

  /////////////////////////////////////
  // low-level tag inquiry functions //
  /////////////////////////////////////

  inkcoreapi bool tag_activated(const char *tag, DiagsTagType mode = DiagsTagType_Debug);

  /////////////////////////////
  // raw printing interfaces //
  /////////////////////////////

  const char *level_name(DiagsLevel dl);

  inkcoreapi void print_va(const char *tag, DiagsLevel dl, const char *prefix,
                           SrcLoc * loc, const char *format_string, va_list ap,
                           unsigned int wID = 0, eInsertStringType eIST = keNo_OP);

  //////////////////////////////
  // user printing interfaces //
  //////////////////////////////

  void print(const char *tag, DiagsLevel dl, const char *prefix, SrcLoc * loc, const char *format_string, ...)
  {
    va_list ap;
    va_start(ap, format_string);
    print_va(tag, dl, prefix, loc, format_string, ap);
    va_end(ap);
  }

  ///////////////////////////////////////////////////////////////////////
  // user diagnostic output interfaces --- enabled on or off based     //
  // on the value of the enable flag, and the state of the debug tags. //
  ///////////////////////////////////////////////////////////////////////

  void log_va(const char *tag, DiagsLevel dl, const char *prefix, SrcLoc * loc, const char *format_string, va_list ap)
  {
    if (!on(tag))
      return;
    print_va(tag, dl, prefix, loc, format_string, ap);
  }

  void log(const char *tag, DiagsLevel dl, const char *prefix, SrcLoc * loc, const char *format_string, ...)
  {
    va_list ap;
    va_start(ap, format_string);
    log_va(tag, dl, prefix, loc, format_string, ap);
    va_end(ap);
  }

  void dump(FILE * fp = stdout);

  void activate_taglist(char *taglist, DiagsTagType mode = DiagsTagType_Debug);

  void deactivate_all(DiagsTagType mode = DiagsTagType_Debug);

  char *base_debug_tags;        // internal copy of default debug tags
  char *base_action_tags;       // internal copy of default action tags

private:
  ink_mutex tag_table_lock;     // prevents reconfig/read races
  DFA *activated_tags[2];       // 1 table for debug, 1 for action

  void lock()
  {
    ink_mutex_acquire(&tag_table_lock);
  }
  void unlock()
  {
    ink_mutex_release(&tag_table_lock);
  }
};


//////////////////////////////////////////////////////////////////////////////
//
//      class DiagsBaseClosure
//      class DiagsDClosure
//      class DiagsEClosure
//
//      The following classes are hacks.  Their whole raison d'etre is to
//      make a macro that substitutes in __LINE__ and __FILE__ location
//      info in addition to the normal diagnostic logging arguments.  But,
//      the lame cpp preprocessor doesn't let us represent and substitute
//      variable numbers of arguments, so we can't do this.
//
//      Instead, a DiagsClosure is created at a point in the code (usually
//      within a macro that adds __LINE__ and __FILE__).  DiagsClosure acts
//      as a closure, wrapping up this location context, and supporting
//      an operator() method that takes all the normal diagnostic log args.
//
//      If the end, we can just say:
//
//              Debug("http", "status = %d", status);
//
//      This macro expands into an initialized creation of a DiagsClosure
//      that saves the location of the Debug macro & debug level, and
//      in effect invokes itself as a function of the original arguments:
//
//              if (diags->on())
//                (*(new DiagsDClosure(diags,DL_Debug,_FILE_,_FUNC_,_LINE_)))
//                 ("http", "status = %d", status)
//
//      If you know a way to make this directly into a macro structure, and
//      bypass the DiagsClosure closures, be my guest...
//
//      The DiagsDClosure and DiagsEClosure classes differ in that the
//      DiagsDClosure supports debug tags, and DiagsEClosure does not.
//
//////////////////////////////////////////////////////////////////////////////

class DiagsBaseClosure
{
public:
  Diags * diags;
  DiagsLevel level;
  SrcLoc src_location;

    DiagsBaseClosure(Diags * d, DiagsLevel l, const char *_file, const char *_func, int _line)
  {
    diags = d;
    level = l;
    src_location.file = _file;
    src_location.func = _func;
    src_location.line = _line;
    src_location.valid = true;
  }
   ~DiagsBaseClosure()
  {
  }
};


// debug closures support debug tags
class DiagsDClosure:public DiagsBaseClosure
{
public:
  DiagsDClosure(Diags * d, DiagsLevel l,
                const char *file, const char *func, int line):DiagsBaseClosure(d, l, file, func, line)
  {
  }
   ~DiagsDClosure()
  {
  }

  // default: no location printed
  void inkcoreapi operator() (const char *tag, const char *format_string ...);

  // location optionally printed
  void operator() (const char *tag, int show_loc, const char *format_string ...);
};


// error closures do not support debug tags
class DiagsEClosure:public DiagsBaseClosure
{
public:
  DiagsEClosure(Diags * d, DiagsLevel l,
                const char *file, const char *func, int line):DiagsBaseClosure(d, l, file, func, line)
  {
  }
   ~DiagsEClosure()
  {
  }

  // default: no location printed
  void inkcoreapi operator() (const char *format_string ...);

  // default: no location printed
  void operator() (unsigned long wID, eInsertStringType eIST, const char *format_string ...);

  // location optionally printed
  void operator() (int show_loc, const char *format_string ...);
};

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//      Macros                                                          //
//                                                                      //
//      The following are diagnostic macros that wrap up the compiler   //
//      __FILE__, __FUNCTION__, and __LINE__ macros into closures       //
//      and then invoke the closure on the remaining arguments.         //
//                                                                      //
//      This closure hack is done, because the cpp preprocessor doesn't //
//      support manipulation and union of varargs parameters.           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#if !defined (__GNUC__)
#ifndef __FUNCTION__
#define __FUNCTION__ NULL
#endif
#endif

extern inkcoreapi Diags *diags;

#define	DTA(l)    diags,l,__FILE__,__FUNCTION__,__LINE__
inline void
dummy_debug(char *dummy_arg ...)
{
  (void) dummy_arg;
}
inline void
dummy_debug(const char *dummy_arg ...)
{
  (void) dummy_arg;
}

#ifndef NO_DIAGS
#define Diag      if (diags->on()) DiagsDClosure(DTA(DL_Diag))  /*args */
#define Debug     if (diags->on()) DiagsDClosure(DTA(DL_Debug)) /*args */
#define DebugOn   DiagsDClosure(DTA(DL_Debug))  /*args */
#else
#define Diag      if (0) dummy_debug
#define Debug     if (0) dummy_debug
#define DebugOn   if (0) dummy_debug
#endif

#define	Status    DiagsEClosure(DTA(DL_Status)) /*(args...) */
#define	Note      DiagsEClosure(DTA(DL_Note))   /*(args...) */
#define	Warning   DiagsEClosure(DTA(DL_Warning))        /*(args...) */
#define	Error     DiagsEClosure(DTA(DL_Error))  /*(args...) */
#define	Fatal     DiagsEClosure(DTA(DL_Fatal))  /*(args...) */
#define	Alert     DiagsEClosure(DTA(DL_Alert))  /*(args...) */
#define	Emergency DiagsEClosure(DTA(DL_Emergency))      /*(args...) */

#ifndef NO_DIAGS
#define is_debug_tag_set(_t)     diags->on(_t,DiagsTagType_Debug)
#define is_action_tag_set(_t)    diags->on(_t,DiagsTagType_Action)
#define debug_tag_assert(_t,_a)  (is_debug_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define action_tag_assert(_t,_a) (is_action_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define is_diags_on(_t)          diags->on(_t)
#else
#define is_debug_tag_set(_t)     0
#define is_action_tag_set(_t)    0
#define debug_tag_assert(_t,_a) /**/
#define action_tag_assert(_t,_a) /**/
#endif
#define	stat_debug_assert(_tst) (void)((_tst) || (Warning(#_tst), debug_tag_assert("stat_check",! #_tst), 0))
#else // INK_NO_DIAGS

class Diags
{
public:
  Diags(char *base_debug_tags, char *base_action_tags, FILE * diags_log_fp = NULL) {
  }

  bool on(DiagsTagType mode = DiagsTagType_Debug) {
    return false;
  }

  bool on(const char *tag, DiagsTagType mode = DiagsTagType_Debug) {
    return false;
  }
};

extern inkcoreapi Diags *diags;

#define Warning      ink_warning
#define Note         ink_notice
#define Status       ink_notice
#define Fatal        ink_fatal_die
#define Error        ink_error
#define Alert        ink_error
#define Emergency    ink_fatal_die

inline void
dummy_debug(char *dummy_arg ...)
{
  (void) dummy_arg;
}

#define Diag      if (0) dummy_debug
#define Debug     if (0) dummy_debug
#define DebugOn   if (0) dummy_debug
#define is_debug_tag_set(_t)     0
#define is_action_tag_set(_t)    0
#define debug_tag_assert(_t,_a) /**/
#define action_tag_assert(_t,_a)
#define is_diags_on(_t)          0
#endif // INK_NO_DIAGS
#endif  /*_Diags_h_*/
