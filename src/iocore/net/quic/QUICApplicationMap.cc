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

#include <QUICApplicationMap.h>

QUICApplication *
QUICApplicationMap::get(QUICStreamId id)
{
  auto it = this->_map.find(id);
  if (it == this->_map.end()) {
    return this->_default_app;
  } else {
    return it->second;
  }
}

void
QUICApplicationMap::set(QUICStreamId id, QUICApplication *app)
{
  this->_map[id] = app;
}

void
QUICApplicationMap::set_default(QUICApplication *app)
{
  this->_default_app = app;
}
