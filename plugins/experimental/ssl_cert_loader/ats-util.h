/** @file

    Include file for  ...

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

#if !defined(_ats_util_h)
#define _ats_util_h

#if defined(__cplusplus)
/** Set data to zero.

    Calls @c memset on @a t with a value of zero and a length of @c
    sizeof(t). This can be used on ordinary and array variables. While
    this can be used on variables of intrinsic type it's inefficient.

    @note Because this uses templates it cannot be used on unnamed or
    locally scoped structures / classes. This is an inherent
    limitation of templates.

    Examples:
    @code
    foo bar; // value.
    ink_zero(bar); // zero bar.

    foo *bar; // pointer.
    ink_zero(bar); // WRONG - makes the pointer @a bar zero.
    ink_zero(*bar); // zero what bar points at.

    foo bar[ZOMG]; // Array of structs.
    ink_zero(bar); // Zero all structs in array.

    foo *bar[ZOMG]; // array of pointers.
    ink_zero(bar); // zero all pointers in the array.
    @endcode

 */
template <typename T>
inline void
ink_zero(T &t ///< Object to zero.
         )
{
  memset(&t, 0, sizeof(t));
}
#endif /* __cplusplus */

#endif // ats-util.h
