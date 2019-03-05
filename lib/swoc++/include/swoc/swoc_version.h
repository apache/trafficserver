/** @file

    Solid Wall of C++ Library

    @mainpage

    A collection of basic utilities derived from Apache Traffic Server code.
    Much of the focus is on low level text manipulation, in particular

    - @c TextView, an extension of @c std::string_view with a collection od
      methods to make working with the text in the view fast and convenient.

    - @c BufferWriter, a safe mechanism for writing to fixed sized buffers. As
      an optional extension this supports python like output formatting along
      with the ability to extend the formatting to arbitrary types, bind names
      in to the formatting context, and substitute alternate parsers for
      custom format styles.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.

 */

#pragma once

namespace swoc
{
static constexpr unsigned MAJOR_VERION  = 1;
static constexpr unsigned MINOR_VERSION = 0;
static constexpr unsigned POINT_VERSION = 4;

} // namespace swoc
