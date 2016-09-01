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

/****************************************************************************

  NetVConnection.cc

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/

#include "P_Net.h"
#include "ts/apidefs.h"

Action *
NetVConnection::send_OOB(Continuation *, char *, int)
{
  return ACTION_RESULT_DONE;
}

void
NetVConnection::cancel_OOB()
{
  return;
}

const char *
NetVCOptions::get_proto_string() const
{
  switch (ip_proto) {
  case USE_TCP:
    return TS_PROTO_TAG_TCP;
  case USE_UDP:
    return TS_PROTO_TAG_UDP;
  default:
    return NULL;
  }
}

const char *
NetVCOptions::get_family_string() const
{
  const char *retval;
  switch (ip_family) {
  case AF_INET:
    retval = TS_PROTO_TAG_IPV4;
    break;
  case AF_INET6:
    retval = TS_PROTO_TAG_IPV6;
    break;
  default:
    retval = NULL;
    break;
  }
  return retval;
}

// Add ProfileSM for NetVConnection
TS_INLINE void
NetVConnection::add_profile_sm(NetProfileSMType_t sm_type, EThread *t)
{
  switch (sm_type) {
  case PROFILE_SM_TCP:
    if (TcpProfileSM::check_dep(this->profile_sm)) {
      this->profile_sm        = TcpProfileSM::allocate(t);
      this->profile_sm->vc    = this;
      this->profile_sm->mutex = mutex;
    } else {
      ink_assert(!"ProfileSM: check dependency failed!");
    }
    break;
  case PROFILE_SM_UDP:
    // TODO:
    break;
  case PROFILE_SM_SSL:
    if (SSLProfileSM::check_dep(this->profile_sm)) {
      NetProfileSM *curr              = this->profile_sm;
      this->profile_sm                = SSLProfileSM::allocate(t);
      this->profile_sm->vc            = this;
      this->profile_sm->mutex         = mutex;
      this->profile_sm->low_profileSM = curr;
    } else {
      ink_assert(!"ProfileSM: check dependency failed!");
    }
    break;
  default:
    ink_assert(!"not reached!");
  }
}

// Delete / Deattach current ProfileSM then free it and restore to Low ProfileSM
TS_INLINE void
NetVConnection::del_profile_sm(EThread *t)
{
  NetProfileSM *tmp;

  switch (profile_sm->type) {
  case PROFILE_SM_SSL:
    ink_assert(this->profile_sm->low_profileSM != NULL);
    tmp              = this->profile_sm;
    this->profile_sm = tmp->low_profileSM;
    tmp->free(t);
    break;
  case PROFILE_SM_TCP:
    ink_assert(!"Can not delete a base ProfileSM!");
    break;
  case PROFILE_SM_UDP:
    ink_assert(!"Can not delete a base ProfileSM!");
    break;
  default:
    ink_assert(!"not reached!");
  }
}

// Free all attached ProfileSM
TS_INLINE void
NetVConnection::free_profile_sm(EThread *t)
{
  NetProfileSM *profilesm;

  while ((profilesm = this->profile_sm) != NULL) {
    this->profile_sm = profilesm->low_profileSM;
    profilesm->free(t);
  }
}
