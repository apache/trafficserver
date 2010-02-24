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

/* ------------------------------------------------------------------------- */  
/* -                             PTRDEF.H                                  - */ 
/* ------------------------------------------------------------------------- */ 
 
  
#ifndef H_PTRDEF_H
#define H_PTRDEF_H
  
/* lv: special typedef for pointer to integer conversion in order to avoid incorrect cast in 64 bit case. */ 
#ifdef __WORDSIZE
#if (__WORDSIZE == 64) || (__WORDSIZE == 32)
#if (__WORDSIZE == 64)
typedef long long int_pointer;

#else   /*  (__WORDSIZE == 32) */
typedef unsigned long int_pointer;

#endif  /* #if (__WORDSIZE == 64) */
#else   /*  (__WORDSIZE != 64) && (__WORDSIZE != 32) */
#error "Invalid __WORDSIZE!"
#endif  /* #if (__WORDSIZE == 64) || (__WORDSIZE == 32) */
#else   /*  !__WORDSIZE */
#ifdef _LP64
typedef long long int_pointer;
#else /* !_LP64 */
#if defined(__PTRDIFF_TYPE__)
typedef unsigned  __PTRDIFF_TYPE__ int_pointer;
#else /* !__PTRDIFF_TYPE__ */
#error "__WORDSIZE not defined!"
#endif /* __PTRDIFF_TYPE__ */
#endif /* _LP64 */
#endif  /* #ifdef __WORDSIZE  */
  
#endif  /* #ifndef H_PTRDEF_H */
