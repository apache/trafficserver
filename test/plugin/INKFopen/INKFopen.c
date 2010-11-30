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


/*****************************************************************************
TODO test for the debugging family of functions

	Test for the TSfopen family of functions

TSfclose
TSfflush
TSfgets
TSfopen
TSfread
TSfwrite

	Test for the Memory allocation family of functions
TSfree
TSmalloc
TSrealloc
TSstrdup
TSstrndup

	Test for the Thread family of functions

TSThreadCreate
TSThreadDestroy
TSThreadInit
TSThreadSelf
TSThread

The approach taken is to write a large test that excercises the major
portions of each api within each section. The order of the test is based
on the order that the tests are written in the programmers guide.

This approach makes it difficult to allow for a test case to fail and to
continue with other tests. In this approach, if a tests fails the remaining
tests are not attempted since the remaining tests depend on results from
prior tests.

A different approach would be to write each test case as completely
individual tests not depending on the results of prior tests.
This approach leads to less complicated code but takes more time to
write the code.  Guesstimate: 3X more code and time. Easier to maintane.

*****************************************************************************/

#include "ts.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
extern int errno;
#include <unistd.h>
#include <stdio.h>
#include <string.h>
/* solaris threads: deprecated */
#include <pthread.h>

/*
 * TSDebug() is used for generic messaging
 * TSerror() is used as an error reporting
*/

/* Used to create tmp file */
#define TMP_DIR "/var/tmp"
#define	PFX	"TS_TSfopen_"



/* There are three threads tests */
#define NUM_THREAD_TESTS (3)
/* This is the third test
 * Arbritrary number of to TSThreadInit()/TSThreadDestroy()
*/
#define	NUM_THREADS	(1000)


/* Number of total TSfopen test: 16
 * non-negative tests: 10
*/
#define	NUM_FOPEN_TESTS	(10)
/*
1. TSfclose:
   1.1 TSfclose on read file	         1.2 TSfclose on write file
   1.3 flush data on read file [neg]     1.4 flush data on write file

2. TSfflush
   2.1 TSfflush on read file [neg]      2.2 TSflush on write file

3. TSfgets
   3.1 TSfgets on read file 	         3.2 TSfgets on write file [neg]

4. TSfopen
   4.1 TSfopen (create) for read [neg]  4.2 TSfopen (create) for write
   4.3 TSfopen for read	         4.4 TSfopen for write

5. TSfread
   5.1 TSfread on read file	         5.2 TSfread on write file [neg]

6. TSfwrite
   6.1 TSfwrite on a write file         6.2 TSfwrite on a read file [neg]
*/

static int
TSfopenTest(TSHttpTxn txnp, TSCont contp)
{
  char *tempnamWriteFile = NULL;
  char *tempnamFlushFile = NULL;

  TSFile sourceReadFile;       /* existing file */
  TSFile writeFile;            /* to be created */
  TSFile readFile;             /* to be created */
  TSFile cmpReadFile;          /* read & compare */

  char inputBuf[BUFSIZ];
  char cmpBuf[BUFSIZ];
  struct stat statBuffPre, statBuffPost, statBuffInputText;
  char *retVal;
  int fopenErrCount = 0, re = 0, wrote = 0, readAmount = 0;
  const char *plugInDir;
  char INPUT_TEXT_FILE[] = "TSfopenText.txt";
  char inputTextFileFullPath[BUFSIZ];


  /* Set full path to file at run time.
   */
  if ((plugInDir = TSPluginDirGet()) == NULL) {
    /* TODO this error does not write to any log file */
    TSError("TSfopenTest(): TSPluginDirGet failed.\n");
    return ++fopenErrCount;
  }
  sprintf(inputTextFileFullPath, "%s/", plugInDir);
  strncat(inputTextFileFullPath, INPUT_TEXT_FILE, sizeof(INPUT_TEXT_FILE));

  /* 4. TSfopen
   * 4.3 TSfopen for read on existing file INPUT_TEXT_FILE
   */
  if (!(sourceReadFile = TSfopen(inputTextFileFullPath, "r"))) {
    TSError("TSfopenTest(): TSfopen failed on open read mode for existing file\n");
    return ++fopenErrCount;
  } else
    TSDebug("TSfopen_plug-in", "TSfopenTest(): TSfopen for read mode of existing file passed\n");

  /* Create unique tmp _file_name_, do not use any TS file_name */
  if ((tempnamWriteFile = tempnam(TMP_DIR, PFX)) == NULL) {
    TSError("TSfopenTest(): tempnam failed \n");
    return ++fopenErrCount;
  }

  /*
   * 4. TSfopen
   *    4.2 TSfopen (create) for write
   *
   * DOCS: Files does not have to exist. File will be created.
   */
  if (!(writeFile = TSfopen(tempnamWriteFile, "w"))) {
    TSError("TSfopenTest(): TSfopen failed to open for write mode \n");
    return ++fopenErrCount;
  }
  TSDebug("TSfopen_plug-in", "TSfopenTest: TSfopen (create) for write passed \n");

  memset(inputBuf, '\0', BUFSIZ);
  /*
   *  3. TSfgets
   *     3.1 TSfgets on read file
   * inputReadFile                is the file to read from,
   * inputBuf                     is the buffer to write to
   * statBuffInputText.st_size    is the size of the buffer to read into
   */
  /* sourceReadFile and inputTextFileFullPath are the same file */
  if (stat(inputTextFileFullPath, &statBuffInputText) != 0) {
    TSError("TSfopenTest() stat failed on sourceReadFile\n");
    return ++fopenErrCount;
  }

  readAmount = (statBuffInputText.st_size <= sizeof(inputBuf)) ? (statBuffInputText.st_size) : (sizeof(inputBuf));

  if ((retVal = TSfgets(sourceReadFile, inputBuf, readAmount))
      == NULL) {
    TSError("TSfopenTest(): TSfgets failed\n");
    return ++fopenErrCount;
  } else
    /* TODO doc retVal: NULL on error, inputBuff on success */
  if (retVal != inputBuf) {
    TSError("TSfopenTest() TSfgets failed (retVal != inputBuf)\n");
    return ++fopenErrCount;
  } else
    TSDebug("TSfopen_plug-in", "TSfopenTest() TSfgets passed on file open for read mode.\n");

        /***************************************************************
	* TODO how do we rewind to the begining of a file (sourceReadFile)?
	*****************************************************************/

  /*
   * 6. TSfwrite
   *    6.1 TSfwrite on a write file
   * writeFile    is the (tmp) file to write data to
   * inputBuf             buffer to read data from
   * BUFSIZ       is the amount of data to write
   *
   */
  wrote = TSfwrite(writeFile, inputBuf, statBuffInputText.st_size);
  if (wrote != statBuffInputText.st_size) {
    TSError("TSfopenTest() TSfwrite failed: write %d/%d bytes\n", wrote, statBuffInputText.st_size);
    return ++fopenErrCount;
  }
  TSDebug("TSfopen_plug-in", "TSfopenTest(): TSfwrite: passed, data written to file\n");

  /* 2. TSfflush
   *   2.2 TSflush on write file
   * No return value: one way to test TSfflush is to verify that
   * the data been written to the file by calling stat(2) and verifying
   * a change in file size.
   */

  if (stat(tempnamWriteFile, &statBuffPre) != 0) {
    TSError("TSfopenTest() stat failed on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  TSfflush(writeFile);         /* writeFile should point to tempnamWriteFile */

  if (stat(tempnamWriteFile, &statBuffPost) != 0) {
    TSError("TSfopenTest() stat failed on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  if ((statBuffPre.st_size == 0) && (statBuffPost.st_size == statBuffInputText.st_size)) {
    TSDebug("TSfopen_plug-in",
             "TSfopenTest(): TSfflush: passed: flushed pending data (%d bytes) written to file\n",
             statBuffPost.st_size);
  } else {
    TSError
      ("TSfopenTest(): TSfflush failed to flush pending data to file: file size: before TSfflush (%d = 0), after %d == %d \n",
       statBuffPost.st_size, statBuffInputText.st_size);

    return ++fopenErrCount;
  }

  /*
   * 5. TSfread
   *    5.1 TSfread on read file
   * Read the tempnameWriteFile back in and compare to original
   */

  /* open again for reading */
  cmpReadFile = TSfopen(tempnamWriteFile, "r");
  if (cmpReadFile == NULL) {
    TSError("TSfopenTest() TSfopen failed on cmpReadFile\n");
    return ++fopenErrCount;
  }

  readAmount = (statBuffInputText.st_size <= sizeof(cmpBuf)) ? (statBuffInputText.st_size) : (sizeof(cmpBuf));

  /* 5.1 TSfread on read file */
  re = TSfread(cmpReadFile, cmpBuf, readAmount);
  if (re != statBuffInputText.st_size) {
    TSError("TSfopenTest() TSfread failed: read %d/%d bytes\n", re, statBuffInputText.st_size);
    return ++fopenErrCount;
  }
  /* compare inputBuf and cmpBuf buffers */
  if (memcmp(inputBuf, cmpBuf, statBuffInputText.st_size) != 0) {
    TSError("TSfopenTest(): TSfread failed\n");
    return ++fopenErrCount;
  } else
    TSDebug("TSfopen_plug-in", "TSfopenTest(): TSfread: passed, read %d/%d bytes\n", re, statBuffInputText.st_size);

  /* remove the tmp file */
  if (unlink(tempnamWriteFile) != 0) {
    TSError("TSfopenTest(): unlink failed on tempnamWriteFile\n");
  }

  /* 1. TSfclose:
   * 1.1 TSfclose on read file
   * TSfclose test:  close and attempt another operation
   * should get error message about closed file.
   */
  TSfclose(sourceReadFile);
  re = TSfread(sourceReadFile, inputBuf, 1);
  if (re != (-1)) {
    TSError("TSfopenTest(): TSfclose on a read file failed:\n");
    TSError("expected -1, read %d bytes\n", re);
    return ++fopenErrCount;
  } else
    TSDebug("TSfopen_plug-in", "TSfopen: TSfclose: on read file passed\n");

  /* 1. TSfclose:
   * 1.2 TSfclose on write file
   * Any operation (read) on a closed file should return an error
   * message
   */
  TSfclose(writeFile);

  wrote = TSfwrite(writeFile, inputBuf, 1);
  if (wrote != (-1)) {
    TSError("TSfopenTest(): TSfclose on a write file failed:\n");
    TSError("expected -1, wrote %d bytes\n", wrote);
    return ++fopenErrCount;
  }
  TSDebug("TSfopen_plug-in", "TSfopen: TSfclose: on write file passed\n");

  /* 1. TSfclose:
   *  1.4 flush data on write file by writing then closing file
   *
   * TODO address how we do rewind on a file
   * Currently: re-open inputReadFile.
   * We could have this in the API, or if file has been read into
   * a buffer, then just read the buffer.
   */

  /* Create unique tmp _file_name_ do not use any TS file_name
   */
  if ((tempnamFlushFile = tempnam(TMP_DIR, PFX)) == NULL) {
    TSError("TSfopenTest(): tempnam failed for tempnamFlushFile\n");
    return ++fopenErrCount;
  }

  if (!(writeFile = TSfopen(tempnamFlushFile, "w"))) {
    TSError("TSfopenTest(): TSfopen failed to open for write mode on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  if (!(sourceReadFile = TSfopen(inputTextFileFullPath, "r"))) {
    /* Returns zero on erro */
    TSError("TSfopenTest(): TSfopen read mode failed on %s\n", inputTextFileFullPath);
    /* TODO try to do other part of test (goto) */
    return ++fopenErrCount;
  }

  if (stat(inputTextFileFullPath, &statBuffInputText) != 0) {
    TSError("TSfopenTest() stat failed on %s\n", inputTextFileFullPath);
    return ++fopenErrCount;
  }

  /* clear inputBuf, could fill buffer here and avoid the file read */
  memset(inputBuf, '\0', BUFSIZ);

  /* inputReadFile is the file to read from,
   *  inputBuf         is the buffer to write to
   *  statBuffInputText.st_size   is the amount  of data to read
   */
  if ((retVal = TSfgets(sourceReadFile, inputBuf, statBuffInputText.st_size))
      == NULL) {
    TSError("TSfopenTest(): TSfgets failed\n");
    return ++fopenErrCount;
  }

  wrote = TSfwrite(writeFile, inputBuf, statBuffInputText.st_size);
  if (wrote != statBuffInputText.st_size) {
    TSError("TSfopenTest() TSfwrite failed: write %d/%d bytes\n", wrote, BUFSIZ);
    return ++fopenErrCount;
  }

  /* stat() */
  if (stat(tempnamFlushFile, &statBuffPre) != 0) {
    TSError("TSfopenTest() stat failed on tempnamFlushFile\n");
    return ++fopenErrCount;
  }

  /* TSfclose should indirectly call TSflush on pending data */
  TSfclose(writeFile);

  if (stat(tempnamFlushFile, &statBuffPost) != 0) {
    TSError("TSfopenTest() stat failed on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  if ((statBuffPre.st_size == 0) && (statBuffPost.st_size == statBuffInputText.st_size)) {
    TSDebug("TSfopen_plug-in", "TSfopenTest(): TSfclose: passed, pending data flushed to file\n");
  } else {
    TSError("TSfopenTest(): TSfclose failed to flush pending data to file\n");
    TSError("file size: before TSfclose %d, after %d\n", statBuffPre.st_size, statBuffPost.st_size);
    return ++fopenErrCount;
  }

  /*
   * 4. TSfopen
   *    4.4 TSfopen for write: Open existing file for write
   */

  /* Does this append or does this truncate ?
   * Guess: truncate as in fopen(3S)
   * API: open(2) or fopen(3S) or NT? We just need to pick one.
   */
  /* writeFile just closed, temporary file still exists, reopen */
  if (!(writeFile = TSfopen(tempnamFlushFile, "w"))) {
    TSError("TSfopenTest(): TSfopen: open existing file for write failed\n");
    return ++fopenErrCount;
  }

  re = TSfwrite(writeFile, cmpBuf, statBuffInputText.st_size);

  if (re != statBuffInputText.st_size) {
    TSError("TSfopenTest(): TSfopen: TSfwrite on existing file open for write failed\n");
    return ++fopenErrCount;
  }

  if ((stat(tempnamFlushFile, &statBuffPost) == 0) && (statBuffPost.st_size == statBuffInputText.st_size)) {
    TSDebug("TSfopen_plug-in", "TSfopenTest(): TSfopen: TSfwrite on existing file open for write passed\n");
  } else {
    TSError("TSfopenTest(): TSfopen: TSfwrite on existing file open for write failed, reason unkown. \n");
    return ++fopenErrCount;
  }

  TSfclose(writeFile);

  /* remove the tmp file */
  if (unlink(tempnamFlushFile) != 0) {
    TSError("TSfopenTest() unlink failed on tmpFlushFile\n");
  }

  /* Need generic test pass/test fail routine */

  /* TODO report on total errors/total tests */
  TSDebug("TSfopen_plug-in", "TSfopen: completed  %d tests\n", NUM_FOPEN_TESTS);

  return fopenErrCount;
}

static int
TSMemAllocTest(TSHttpTxn txnp, TSCont contp)
{
  int TSMemAllocErrors = 0;

  TSDebug("TSfopen_plug-in", "TSMemAllocTest() called \n");
/*
TSmalloc
TSfree
TSfree
TSmalloc
TSrealloc
TSstrdup
TSstrndup
*/

  return TSMemAllocErrors;
}

void *
pthreadStartFunc(void *arg)
{
  TSThread tmpTSThread = 0;

  /* Init this thread */
  tmpTSThread = TSThreadInit();

  /* sleep(6) ; */

  if (!tmpTSThread) {
    TSError("TSfopen_plug-in", "pthreadStartFunc():  TSThreadInit failed\n");
    /* TODO track threads created in TSThreadTest() with the threads being called/terminiting here */
     /* errCreateCount--; */
  } else
    TSDebug("TSfopen_plug-in", "pthreadStartFunc(): TSThreadInit pass\n");

  TSDebug("TSfopen_plug-in", "pthreadStartFunc(): created by thread: \
		%d running on pthread: %d\n", (pthread_t) arg, pthread_self());

  /* Clean-up this thread */
  if (tmpTSThread)
    TSThreadDestroy(tmpTSThread);

  return NULL;
}

static void *
TSThreadCallee(void *arg)
{
  char buf[BUFSIZ];
  TSThread TSthread;
  /* TODO do more useful work here */
  sleep(10);
  TSDebug("TSfopen_plug-in", "TSThreadTest(): TSThreadCallee: TSThreadCreate passed\n");

  TSthread = TSThreadSelf();  /* Get thread id for this thread */
  if (TSthread == 0) {
    TSError("TSfopen_plug-in", "TSThreadCallee(): TSThreadSelf() failed\n");
    /* TODO can't use return values track errors in global data (or tsd ?) */
  }
  TSDebug("TSfopen_plug-in",
           "TSThreadTest(): TSThreadCallee: created by pthread_t:%d running on pthread_t:%d, and TSThread at 0x%08x.\n",
           (int) arg, pthread_self(), TSthread);
  return NULL;
}

/* TODO POSIX/Solaris Threads
 * It appears that Traffic Server support both POSIX and Solaris threads
 * as part of the SDK.
 * 1. Verify.
 * 2. Write Solaris based thread code here to compliment these POSIX
 *    (pthread) based test.
 * 3. POSIX tests were written first since Solaris threads are being
 *    deprecated in favor of POSIX.
*/

/* Argument data passed to thread init functions
 * cannot be allocated on the stack.
*/
pthread_t currTid, newTid;

static int
TSThreadTest(TSHttpTxn txnp, TSCont contp)
{
  TSThreadFunc threadFunc;
  struct stat statBuffPre, statBuffPost;
  TSThread TSCurrThread = 0;
  TSThread TSCreateThread = 0;
  TSThread tmpTSThread = 0;
  int errCreateCount = NUM_THREADS;
  int threadErrCount = 0;
  int i, rval;

  currTid = pthread_self();
  TSDebug("TSfopen_plug-in", "TSThreadTest(): Begin: running on thread %d\n", currTid);

  /* call C++ code here */

  /* Test #1: Get current TSThread */
  TSCurrThread = TSThreadSelf();
  if (TSCurrThread == 0) {
    TSError("TSfopen_plug-in", "TSThreadSelf(): failed\n");
    threadErrCount++;
  } else
    TSDebug("TSfopen_plug-in", "TSThreadSelf(): passed: current TSThread:0x%08x\n", TSCurrThread);
  /* Test 2: */
  threadFunc = (TSThreadFunc) TSThreadCallee;
  /* Try to attach this thread to a function that does not have the
   * the same prototype described in the p.g.
   */
  /* TSThreadCreate(threadFunc, (void*)&argInt); */
  /* TSThreadCreate((TSThreadFunc)TSThreadCallee, (void*)&argInt); */
  TSCreateThread = TSThreadCreate(TSThreadCallee, (void *) currTid);
  if (TSCreateThread) {
    TSDebug("TSfopen_plug-in", "TSThreadCreate(): registration of thread init passed\n");
  } else {
    TSError("TSfopen_plug-in", "TSThreadCreate(): registration of thread init failed\n");
    threadErrCount++;
  }

  /* Test 3:
   * Create pthreads and register the thread init code.
   *
   * TODO track threads created and on what thread with actual init function invocation
   * Could use global data with mutex lock or
   * tsd. If a thread created here does not execute in the registered
   * init function these tests will not catch this as an error. This
   * type of error would be less of an SDK error an more of a system
   * error or TrafficServer/system interaction.
   */
  for (i = 0; i < NUM_THREADS; i++) {
#ifdef _WIN32
    /* Future */
#else
    errno = 0;
    rval = pthread_create(&newTid, NULL, pthreadStartFunc, (void *) currTid);
#endif
    if (rval != 0 || errno) {
      errCreateCount--;
      TSError("TSfopen_plug-in",
               "TSThreadTest(): thread %d called pthread_create and failed to create pthread, errno:%d\n", currTid,
               errno);

                        /********************************************
			if (firstThreadError)
				continue;          skip these tests
			********************************************/
    } else
      TSDebug("TSfopen_plug-in", "TSThreadTest(): pthread_create created: %d\n", newTid);
  }

  /* No errors means all threads were created */
  TSDebug("TSfopen_plug-in", "TSThreadTest():  created %d/%d pthreads\n", errCreateCount, NUM_THREADS);
  if (errCreateCount != NUM_THREADS)
    threadErrCount++;

  TSDebug("TSfopen_plug-in",
           "TSThreadTest(): results:  %d/%d test passed \n", (NUM_THREAD_TESTS - threadErrCount), NUM_THREAD_TESTS);

  return threadErrCount;
}

/* only callable from this file and as a call-back */
static int
TSfopen_plugin(TSCont contp, TSEvent event, void *edata)
{
  int status = 0;               /* assume success */

  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_OS_DNS:


    status += TSThreadTest(txnp, contp);

  default:
    break;
  }
  /* Always reeneable HTTP transaction after processing of each
   * transaction event.
   */
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return status;
}


void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp;

  /* Parse args */
  if (!strcmp(argv[1], "TSfopenTest")) {
    TSDebug("TSfopen_plug-in", "\nRun: TSfopenTest \n");
  }
  /* Associate this data at time call-back is called */
  contp = TSContCreate(TSfopen_plugin, NULL /* mutex */ );

  if (contp == NULL) {
    TSError("usage: %s TSContCreate() returned NULL\n", argv[0]);
    return;
  }
  /* Set at TS_HTTP_OS_DNS_HOOK for no specific reason */
  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, contp);
}
