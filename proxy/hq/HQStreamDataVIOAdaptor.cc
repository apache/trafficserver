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

#include "HQStreamDataVIOAdaptor.h"
#include "I_VIO.h"

HQStreamDataVIOAdaptor::HQStreamDataVIOAdaptor(VIO *sink) : _sink_vio(sink)
{
}

std::vector<HQFrameType>
HQStreamDataVIOAdaptor::interests()
{
  return {HQFrameType::DATA};
}

HQErrorUPtr
HQStreamDataVIOAdaptor::handle_frame(std::shared_ptr<const HQFrame> frame)
{
  ink_assert(frame->type() == HQFrameType::DATA);
  const HQDataFrame *dframe = dynamic_cast<const HQDataFrame *>(frame.get());

  SCOPED_MUTEX_LOCK(lock, this->_sink_vio->mutex, this_ethread());

  MIOBuffer *writer = this->_sink_vio->get_writer();
  writer->write(dframe->payload(), dframe->payload_length());

  return HQErrorUPtr(new HQNoError());
}
