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




#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/kmem.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include "fio_dev.h"

#include "fastio.h"


#define inline

int
fio_add_split_rule(fio_devstate_t * rsp, int id, struct fastIO_split_rule *rule)
{
  return 1;

}

int
fio_delete_split_rule(fio_devstate_t * rsp, int id, struct fastIO_split_rule *rule)
{
  return 1;
}

int
fio_flush_split_rules(fio_devstate_t * rsp, int id)
{
  return 1;

}

int
fio_vsession_cmd(fio_devstate_t * rsp, struct ink_cmd_msg *msg)
{
  return 1;
}

int
fio_vsession_create(fio_devstate_t * rsp)
{
  return -1;
}

int
fio_vsession_destroy(fio_devstate_t * rsp, int id)
{
  return 0;
}

/*
 * Handle vsession-related ioctls
 *
 */
int
fio_vsession_ioctl(fio_devstate_t * rsp, int cmd, intptr_t arg)
{
  struct ink_cmd_msg msg;

  switch (cmd) {
  case INKFIO_VSESSION_CREATE:
    return fio_vsession_create(rsp);
  case INKFIO_VSESSION_DESTROY:
    return fio_vsession_destroy(rsp, (int) arg);
  case INKFIO_VSESSION_CMD:
    if (ddi_copyin((char *) arg, &msg, sizeof(struct ink_cmd_msg), 0)) {
      cmn_err(CE_WARN, "fio_vsession_ioctl: Invalid userspace pointer 0x%x.\n", (int) arg);
      return -1;
    }
    return fio_vsession_cmd(rsp, &msg);


  }

  cmn_err(CE_WARN, "fio: Unrecognized vsession ioctl 0x%x\n", cmd);
  return -1;
}

