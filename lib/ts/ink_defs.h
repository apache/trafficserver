/** @file

  Some small general interest definitions

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

#ifndef _ink_defs_h
#define	_ink_defs_h

/* Some popular defines
*/
#define SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define SOCKOPT_ON ((char*)&on)
#define SOCKOPT_OFF ((char*)&off)

#ifndef ABS
#define ABS(x) (((x) < 0) ? ( - (x)) : (x))
#endif

#ifndef MAX
#define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif

#define ATS_UNUSED __attribute__ ((unused))
#define ATS_WARN_IF_UNUSED __attribute__ ((warn_unused_result))
#define	ATS_UNUSED_RETURN(x)	if (x) {}

#define	NOWARN_UNUSED(x)	(void)(x)


#ifndef likely
#define likely(x)	__builtin_expect (!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x)	__builtin_expect (!!(x), 0)
#endif


#if TS_USE_HWLOC
#  include <hwloc.h>
#endif

/* Debugging
*/
#ifdef NDEBUG

#define FDBG
#define DBG(s)
#define DBG1(s,a)
#define DBG2(s,a1,a2)
#define DBG3(s,a1,a2,a3)
#define DBG4(s,a1,a2,a3,a4)

#else

#define FDBG                if (debug_level==1) printf("debug "__FILE__":%d %s : entered\n" ,__LINE__,__FUNCTION__)
#define DBG(s)              if (debug_level==1) printf("debug "__FILE__":%d %s :" s ,__LINE__,__FUNCTION__)
#define DBG1(s,a)           if (debug_level==1) printf("debug "__FILE__":%d %s :" s ,__LINE__,__FUNCTION__, a)
#define DBG2(s,a1,a2)       if (debug_level==1) printf("debug "__FILE__":%d %s :" s ,__LINE__,__FUNCTION__, a1,a2)
#define DBG3(s,a1,a2,a3)    if (debug_level==1) printf("debug "__FILE__":%d %s :" s ,__LINE__,__FUNCTION__, a1,a2,a3)
#define DBG4(s,a1,a2,a3,a4) if (debug_level==1) printf("debug "__FILE__":%d %s :" s ,__LINE__,__FUNCTION__, a1,a2,a3,a4)

#endif

/* Types
*/
typedef void *(*VPVP_PFN) (void *);
typedef void (*VVP_PFN) (void *);
typedef void (*VV_PFN) (void);
typedef void (*VI_PFN) (int);

/* Variables
*/
extern int debug_level;
extern int off;
extern int on;

/* Functions
*/
int ink_sys_name_release(char *name, int namelen, char *release, int releaselen);
int ink_number_of_processors();

#if TS_USE_HWLOC
// Get the hardware topology
hwloc_topology_t ink_get_topology();
#endif

/** Constants.
 */
#ifdef __cplusplus
namespace ts {
  static const int NO_FD = -1; ///< No or invalid file descriptor.
}
#endif

#endif /*__ink_defs_h*/
