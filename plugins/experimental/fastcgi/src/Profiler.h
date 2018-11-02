/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <chrono>
#include <sys/types.h>
#include <unistd.h>

namespace ats_plugin
{
// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(TypeName &) = delete;           \
  void operator=(TypeName) = delete;

// A single profile, stores data of a taken profile
class Profile
{
public:
  // Computes the start time and sets the thread id
  // The duration of the profiles is in microseconds
  void
  ComputeStartTime()
  {
    this->start_time_ =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    this->thread_id_  = std::hash<std::thread::id>()(std::this_thread::get_id());
    this->process_id_ = getpid();
  }

  // Computes the end time
  void
  ComputeEndTime()
  {
    this->end_time_ =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  }

  // Gettors
  std::size_t
  start_time() const
  {
    return this->start_time_;
  }
  std::size_t
  end_time() const
  {
    return this->end_time_;
  }
  std::size_t
  thread_id() const
  {
    return this->thread_id_;
  }

  std::size_t
  process_id() const
  {
    return this->process_id_;
  }
  std::size_t
  object_id() const
  {
    return this->object_id_;
  }
  void
  set_object_id(std::size_t objId)
  {
    object_id_ = objId;
  }
  const std::string &
  task_name() const
  {
    return this->task_name_;
  }

  // Settors
  void
  set_task_name(const std::string &task_name)
  {
    this->task_name_ = task_name;
  }

  const std::string &
  obj_stage() const
  {
    return this->obj_stage_;
  }
  void
  set_obj_stage(const std::string &obj_stage)
  {
    this->obj_stage_ = obj_stage;
  }

private:
  // The time the profile started
  std::chrono::high_resolution_clock::rep start_time_;

  // The time the profile ended
  std::chrono::high_resolution_clock::rep end_time_;

  // The id of the thread
  std::size_t thread_id_;
  std::size_t process_id_;

  std::size_t object_id_;
  // The name of the task of the profile
  std::string task_name_;
  std::string obj_stage_;
};

// Keeps tracks of the taken profiles and saves the data to a JSON.
// In order to take profiles use the embedded class profile taker
// The duration is in microseconds
class Profiler
{
public:
  // The storage of profiles
  typedef std::vector<Profile> ProfileContainer;

  // Empty constructor
  Profiler() : record_enabled_(false) {}
  // Submits a new profile
  void
  SubmitProfile(const Profile &profile)
  {
    // Ignore if not enabled
    if (!this->record_enabled_)
      return;
    std::unique_lock<std::mutex> lock(this->profiles_mutex_);
    this->profiles_.push_back(profile);
  }

  // Removes all the profiles
  void
  Clear()
  {
    std::unique_lock<std::mutex> lock(this->profiles_mutex_);
    this->profiles_.clear();
  }

  // Gettors
  bool
  record_enabled() const
  {
    return this->record_enabled_;
  }
  const ProfileContainer &
  profiles() const
  {
    return this->profiles_;
  }

  void
  printProfileLength()
  {
    std::cout << "Profile Length: " << this->profiles_.size() << std::endl;
  }
  // Settors
  void
  set_record_enabled(bool enabled)
  {
    this->record_enabled_ = enabled;
    if (!this->record_enabled_)
      this->Clear();
  }

private:
  // The profiles
  ProfileContainer profiles_;

  // If true, enabled
  bool record_enabled_;

  // The mutex for safe access
  mutable std::mutex profiles_mutex_;

  // DISALLOW_COPY_AND_ASSIGN(Profiler);
};

// Takes a profile during its life time
class ProfileTaker
{
public:
  // Initializes a profile
  ProfileTaker(Profiler *owner, const std::string &task_name, std::size_t objId, const std::string &phase) : owner_(owner)
  {
    profile_.ComputeStartTime();
    profile_.set_task_name(task_name);
    profile_.set_obj_stage(phase);
    profile_.set_object_id(objId);
  }

  // Releases a profile and submits it
  ~ProfileTaker()
  {
    // profile_.ComputeEndTime();
    owner_->SubmitProfile(this->profile_);
    // profile_.set_task_name(this->profile_.task_name());
    // profile_.set_obj_stage("E");
    Profile endProf;
    endProf.ComputeStartTime();
    endProf.set_obj_stage("E");
    endProf.set_task_name(this->profile_.task_name());
    endProf.set_object_id(this->profile_.object_id());
    owner_->SubmitProfile(endProf);
  }

private:
  // The profile to take care of
  Profile profile_;

  // The owner profiler
  Profiler *owner_;

  // DISALLOW_COPY_AND_ASSIGN(ProfileTaker);
};
} // namespace ats_plugin
