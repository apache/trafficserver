/** @file

  Layout

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

  @section details Details

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

 */

#pragma once

// use std string and string view for layout
#include <string>

/**
  The Layout is a simple place holder for the distribution layout.

 */
struct Layout {
  Layout(std::string_view const _prefix = {});
  ~Layout();

  /**
   Return file path relative to Layout->prefix

  */
  std::string relative(std::string_view file);

  /**
   update the sysconfdir to a test conf dir

   */
  void update_sysconfdir(std::string_view dir);

  /**
   Return file path relative to dir
   Example usage: Layout::relative_to(default_layout()->sysconfdir, "foo.bar");

  */
  static std::string relative_to(std::string_view dir, std::string_view file);

  /**
   Return file path relative to dir
   Store the path to buf. The buf should be large eough to store
   Example usage: Layout::relative_to(default_layout()->sysconfdir, "foo.bar");

  */
  static void relative_to(char *buf, size_t bufsz, std::string_view dir, std::string_view file);

  /**
   Creates a Layout Object with the given prefix.  If no
   prefix is given, the prefix defaults to the one specified
   at the compile time.

  */
  static void create(std::string_view const prefix = {});

  /**
   Returns the Layout object created by create_default_layout().

  */
  static Layout *get();

  std::string prefix;
  std::string exec_prefix;
  std::string bindir;
  std::string sbindir;
  std::string sysconfdir;
  std::string datadir;
  std::string includedir;
  std::string libdir;
  std::string libexecdir;
  std::string localstatedir;
  std::string runtimedir;
  std::string logdir;
  std::string mandir;
  std::string infodir;
  std::string cachedir;
};
