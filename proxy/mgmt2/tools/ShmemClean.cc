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

/*
 *
 * ShmemCleanup.cc
 *   Cleanup tool for dangling shem segs.
 *
 * $Date: 2003-06-01 18:38:21 $
 *
 *
 */

#include "ink_config.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#if defined(linux)
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
union semun
{
  int val;                      /* value for SETVAL */
  struct semid_ds *buf;         /* buffer for IPC_STAT, IPC_SET */
  unsigned short int *array;    /* array for GETALL, SETALL */
  struct seminfo *__buf;        /* buffer for IPC_INFO */
};
#endif  // linux check

/*
 * main(...)
 *   Function takes an int id to start and walks through (as the Local
 * manager does) ids until it fails to delete an id. To delete a seg id
 * you need the uid of the creator or root, be careful not to nuke any
 * that may be running.
 *
 * Usage:   shmem_clean <id>  or
 *          shmem_clean <id> <id2>
 *
 * Action:  The first usage will delete from id until it fails to find an
 *          id to delete id++.
 *          The second will try to delete all ids from range id to id2.
 */
int
main(int argc, char **argv)
{

  int start, end, tmp;

  if (argc != 2 && argc != 3) {
    fprintf(stderr, "Usage: shmem_clean [<id> or <id> <id2>]\n");
    exit(1);
  } else if (argc == 2) {
    start = atoi(argv[1]);
    for (;;) {
#if defined(linux)
      union semun semun_dummy;
      if ((tmp = semget(start, 1, 0666)) < 0 || semctl(tmp, 1, IPC_RMID, semun_dummy) < 0) {
#else
      if ((tmp = semget(start, 1, 0666)) < 0 || semctl(tmp, 1, IPC_RMID) < 0) {
#endif
        return 0;
      }
      ++start;
    }
  }
  start = atoi(argv[1]);
  end = atoi(argv[2]);
  for (int i = start; i < end; i++) {
#if defined(linux)
    union semun semun_dummy;
    if ((tmp = semget(i, 1, 0666)) < 0 || semctl(tmp, 1, IPC_RMID, semun_dummy) < 0) {
#else
    if ((tmp = semget(i, 1, 0666)) < 0 || semctl(tmp, 1, IPC_RMID) < 0) {
#endif
    }
  }
  fprintf(stderr, "[shmem_clean] Done!\n");
  return 0;
}                               /* End main */
