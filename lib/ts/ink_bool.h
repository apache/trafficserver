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

/*************************** -*- Mod: C++ -*- ******************************

   ink_bool.h --
   Created On      : Wed Dec  4 11:36:03 1996
   Last Modified By: Ivry Semel
   Last Modified On: Tue Dec 17 14:27:41 1996
   Update Count    : 3
   Status          :

   Description:
   define type bool for the compilers that don't define it.


 ****************************************************************************/
#if !defined (_ink_bool_h_)
#define _ink_bool_h_

#if defined(openbsd)

#include <stdbool.h>

#elif !defined(linux)

#if (defined (__GNUC__) || ! defined(__cplusplus))
/*
 * bool, true, and false already declared in C++
 */
#if !defined (bool)
#if !defined(freebsd) && !defined(solaris)
#define bool int
#endif
#endif

#if !defined (true)
#define true 1
#endif

#if !defined (false)
#define false 0
#endif

#endif /* #if (defined (__GNUC__) || ! defined(__cplusplus)) */
#endif /* not openbsd, not linux */

/*
 * TRUE and FALSE not declared in C++
 */
#if !defined (TRUE)
#define TRUE 1
#endif
#if !defined (FALSE)
#define FALSE 0
#endif

#endif /* #if !defined (_ink_bool_h_) */
