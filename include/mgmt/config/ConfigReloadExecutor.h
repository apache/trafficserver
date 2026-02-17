/** @file

  Config reload execution logic - schedules async reload work on ET_TASK.

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

#pragma once

#include <chrono>

namespace config
{

/// Schedule the config reload work to be executed on the ET_TASK thread.
///
/// This function schedules the actual reload work which includes:
/// - Calling FileManager::rereadConfig() to detect and reload changed files
/// - Calling FileManager::invokeConfigPluginCallbacks() to notify registered plugins
///
/// The reload status is tracked via ReloadCoordinator which must have been
/// initialized with a task before calling this function.
///
/// @param delay Delay before executing the reload (default: 10ms)
void schedule_reload_work(std::chrono::milliseconds delay = std::chrono::milliseconds{10});

} // namespace config
