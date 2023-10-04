/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "QUICPathManager.h"
#include "QUICPathValidator.h"
#include "QUICConnection.h"

#define QUICDebug(fmt, ...) Debug("quic_path", "[%s] " fmt, this->_cinfo.cids().data(), ##__VA_ARGS__)

void
QUICPathManagerImpl::open_new_path(const QUICPath &path, ink_hrtime timeout_in)
{
  if (this->_verify_timeout_at == 0) {
    // Overwrite _previous_path only if _current_path is verified
    // _previous_path should always have a verified path if available
    this->_previous_path = this->_current_path;
  }
  this->_current_path = path;
  this->_path_validator.validate(path);
  this->_verify_timeout_at = ink_get_hrtime() + timeout_in;
}

void
QUICPathManagerImpl::set_trusted_path(const QUICPath &path)
{
  this->_current_path  = path;
  this->_previous_path = path;
}

void
QUICPathManagerImpl::_check_verify_timeout()
{
  if (this->_verify_timeout_at != 0) {
    if (this->_path_validator.is_validated(this->_current_path)) {
      // Address validation succeeded
      this->_verify_timeout_at = 0;
      this->_previous_path     = {{}, {}};
    } else if (this->_verify_timeout_at < ink_get_hrtime()) {
      // Address validation failed
      QUICDebug("Switching back to the previous path");
      this->_current_path      = this->_previous_path;
      this->_verify_timeout_at = 0;
      this->_previous_path     = {{}, {}};
    }
  }
}

const QUICPath &
QUICPathManagerImpl::get_current_path()
{
  this->_check_verify_timeout();
  return this->_current_path;
}

const QUICPath &
QUICPathManagerImpl::get_verified_path()
{
  this->_check_verify_timeout();
  if (this->_verify_timeout_at != 0) {
    return this->_previous_path;
  } else {
    return this->_current_path;
  }
}
