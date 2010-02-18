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

	Test for the INKfopen family of functions  

INKfclose
INKfflush
INKfgets
INKfopen
INKfread
INKfwrite

	Test for the Memory allocation family of functions  
INKfree
INKmalloc
INKrealloc
INKstrdup
INKstrndup 

	Test for the Thread family of functions  

INKThreadCreate
INKThreadDestroy
INKThreadInit
INKThreadSelf
INKThread

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
 * INKDebug() is used for generic messaging 
 * INKerror() is used as an error reporting 
*/

/* Used to create tmp file */
#define TMP_DIR "/var/tmp"
#define	PFX	"TS_INKfopen_"



/* There are three threads tests */
#define NUM_THREAD_TESTS (3)
/* This is the third test
 * Arbritrary number of to INKThreadInit()/INKThreadDestroy() 
*/
#define	NUM_THREADS	(1000)


/* Number of total INKfopen test: 16 
 * non-negative tests: 10
*/
#define	NUM_FOPEN_TESTS	(10)
/* 
1. INKfclose:
   1.1 INKfclose on read file	         1.2 INKfclose on write file 
   1.3 flush data on read file [neg]     1.4 flush data on write file

2. INKfflush
   2.1 INKfflush on read file [neg]      2.2 INKflush on write file

3. INKfgets
   3.1 INKfgets on read file 	         3.2 INKfgets on write file [neg]

4. INKfopen
   4.1 INKfopen (create) for read [neg]  4.2 INKfopen (create) for write
   4.3 INKfopen for read	         4.4 INKfopen for write

5. INKfread
   5.1 INKfread on read file	         5.2 INKfread on write file [neg]

6. INKfwrite
   6.1 INKfwrite on a write file         6.2 INKfwrite on a read file [neg]
*/

static int
INKfopenTest(INKHttpTxn txnp, INKCont contp)
{
  char *tempnamWriteFile = NULL;
  char *tempnamFlushFile = NULL;

  INKFile sourceReadFile;       /* existing file */
  INKFile writeFile;            /* to be created */
  INKFile readFile;             /* to be created */
  INKFile cmpReadFile;          /* read & compare */

  char inputBuf[BUFSIZ];
  char cmpBuf[BUFSIZ];
  struct stat statBuffPre, statBuffPost, statBuffInputText;
  char *retVal;
  int fopenErrCount = 0, re = 0, wrote = 0, readAmount = 0;
  const char *plugInDir;
  char INPUT_TEXT_FILE[] = "INKfopenText.txt";
  char inputTextFileFullPath[BUFSIZ];


  /* Set full path to file at run time.
   */
  if ((plugInDir = INKPluginDirGet()) == NULL) {
    /* TODO this error does not write to any log file */
    INKError("INKfopenTest(): INKPluginDirGet failed.\n");
    return ++fopenErrCount;
  }
  sprintf(inputTextFileFullPath, "%s/", plugInDir);
  strncat(inputTextFileFullPath, INPUT_TEXT_FILE, sizeof(INPUT_TEXT_FILE));

  /* 4. INKfopen
   * 4.3 INKfopen for read on existing file INPUT_TEXT_FILE
   */
  if (!(sourceReadFile = INKfopen(inputTextFileFullPath, "r"))) {
    INKError("INKfopenTest(): INKfopen failed on open read mode for existing file\n");
    return ++fopenErrCount;
  } else
    INKDebug("INKfopen_plug-in", "INKfopenTest(): INKfopen for read mode of existing file passed\n");

  /* Create unique tmp _file_name_, do not use any TS file_name */
  if ((tempnamWriteFile = tempnam(TMP_DIR, PFX)) == NULL) {
    INKError("INKfopenTest(): tempnam failed \n");
    return ++fopenErrCount;
  }

  /* 
   * 4. INKfopen
   *    4.2 INKfopen (create) for write
   *
   * DOCS: Files does not have to exist. File will be created.
   */
  if (!(writeFile = INKfopen(tempnamWriteFile, "w"))) {
    INKError("INKfopenTest(): INKfopen failed to open for write mode \n");
    return ++fopenErrCount;
  }
  INKDebug("INKfopen_plug-in", "INKfopenTest: INKfopen (create) for write passed \n");

  memset(inputBuf, '\0', BUFSIZ);
  /*
   *  3. INKfgets
   *     3.1 INKfgets on read file
   * inputReadFile                is the file to read from, 
   * inputBuf                     is the buffer to write to
   * statBuffInputText.st_size    is the size of the buffer to read into
   */
  /* sourceReadFile and inputTextFileFullPath are the same file */
  if (stat(inputTextFileFullPath, &statBuffInputText) != 0) {
    INKError("INKfopenTest() stat failed on sourceReadFile\n");
    return ++fopenErrCount;
  }

  readAmount = (statBuffInputText.st_size <= sizeof(inputBuf)) ? (statBuffInputText.st_size) : (sizeof(inputBuf));

  if ((retVal = INKfgets(sourceReadFile, inputBuf, readAmount))
      == NULL) {
    INKError("INKfopenTest(): INKfgets failed\n");
    return ++fopenErrCount;
  } else
    /* TODO doc retVal: NULL on error, inputBuff on success */
  if (retVal != inputBuf) {
    INKError("INKfopenTest() INKfgets failed (retVal != inputBuf)\n");
    return ++fopenErrCount;
  } else
    INKDebug("INKfopen_plug-in", "INKfopenTest() INKfgets passed on file open for read mode.\n");

        /*************************************************************** 
	* TODO how do we rewind to the begining of a file (sourceReadFile)?
	*****************************************************************/

  /* 
   * 6. INKfwrite
   *    6.1 INKfwrite on a write file 
   * writeFile    is the (tmp) file to write data to
   * inputBuf             buffer to read data from 
   * BUFSIZ       is the amount of data to write
   *
   */
  wrote = INKfwrite(writeFile, inputBuf, statBuffInputText.st_size);
  if (wrote != statBuffInputText.st_size) {
    INKError("INKfopenTest() INKfwrite failed: write %d/%d bytes\n", wrote, statBuffInputText.st_size);
    return ++fopenErrCount;
  }
  INKDebug("INKfopen_plug-in", "INKfopenTest(): INKfwrite: passed, data written to file\n");

  /* 2. INKfflush
   *   2.2 INKflush on write file
   * No return value: one way to test INKfflush is to verify that
   * the data been written to the file by calling stat(2) and verifying
   * a change in file size.
   */

  if (stat(tempnamWriteFile, &statBuffPre) != 0) {
    INKError("INKfopenTest() stat failed on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  INKfflush(writeFile);         /* writeFile should point to tempnamWriteFile */

  if (stat(tempnamWriteFile, &statBuffPost) != 0) {
    INKError("INKfopenTest() stat failed on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  if ((statBuffPre.st_size == 0) && (statBuffPost.st_size == statBuffInputText.st_size)) {
    INKDebug("INKfopen_plug-in",
             "INKfopenTest(): INKfflush: passed: flushed pending data (%d bytes) written to file\n",
             statBuffPost.st_size);
  } else {
    INKError
      ("INKfopenTest(): INKfflush failed to flush pending data to file: file size: before INKfflush (%d = 0), after %d == %d \n",
       statBuffPost.st_size, statBuffInputText.st_size);

    return ++fopenErrCount;
  }

  /*  
   * 5. INKfread
   *    5.1 INKfread on read file
   * Read the tempnameWriteFile back in and compare to original
   */

  /* open again for reading */
  cmpReadFile = INKfopen(tempnamWriteFile, "r");
  if (cmpReadFile == NULL) {
    INKError("INKfopenTest() INKfopen failed on cmpReadFile\n");
    return ++fopenErrCount;
  }

  readAmount = (statBuffInputText.st_size <= sizeof(cmpBuf)) ? (statBuffInputText.st_size) : (sizeof(cmpBuf));

  /* 5.1 INKfread on read file */
  re = INKfread(cmpReadFile, cmpBuf, readAmount);
  if (re != statBuffInputText.st_size) {
    INKError("INKfopenTest() INKfread failed: read %d/%d bytes\n", re, statBuffInputText.st_size);
    return ++fopenErrCount;
  }
  /* compare inputBuf and cmpBuf buffers */
  if (memcmp(inputBuf, cmpBuf, statBuffInputText.st_size) != 0) {
    INKError("INKfopenTest(): INKfread failed\n");
    return ++fopenErrCount;
  } else
    INKDebug("INKfopen_plug-in", "INKfopenTest(): INKfread: passed, read %d/%d bytes\n", re, statBuffInputText.st_size);

  /* remove the tmp file */
  if (unlink(tempnamWriteFile) != 0) {
    INKError("INKfopenTest(): unlink failed on tempnamWriteFile\n");
  }

  /* 1. INKfclose:
   * 1.1 INKfclose on read file
   * INKfclose test:  close and attempt another operation
   * should get error message about closed file.
   */
  INKfclose(sourceReadFile);
  re = INKfread(sourceReadFile, inputBuf, 1);
  if (re != (-1)) {
    INKError("INKfopenTest(): INKfclose on a read file failed:\n");
    INKError("expected -1, read %d bytes\n", re);
    return ++fopenErrCount;
  } else
    INKDebug("INKfopen_plug-in", "INKfopen: INKfclose: on read file passed\n");

  /* 1. INKfclose: 
   * 1.2 INKfclose on write file
   * Any operation (read) on a closed file should return an error 
   * message 
   */
  INKfclose(writeFile);

  wrote = INKfwrite(writeFile, inputBuf, 1);
  if (wrote != (-1)) {
    INKError("INKfopenTest(): INKfclose on a write file failed:\n");
    INKError("expected -1, wrote %d bytes\n", wrote);
    return ++fopenErrCount;
  }
  INKDebug("INKfopen_plug-in", "INKfopen: INKfclose: on write file passed\n");

  /* 1. INKfclose:
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
    INKError("INKfopenTest(): tempnam failed for tempnamFlushFile\n");
    return ++fopenErrCount;
  }

  if (!(writeFile = INKfopen(tempnamFlushFile, "w"))) {
    INKError("INKfopenTest(): INKfopen failed to open for write mode on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  if (!(sourceReadFile = INKfopen(inputTextFileFullPath, "r"))) {
    /* Returns zero on erro */
    INKError("INKfopenTest(): INKfopen read mode failed on %s\n", inputTextFileFullPath);
    /* TODO try to do other part of test (goto) */
    return ++fopenErrCount;
  }

  if (stat(inputTextFileFullPath, &statBuffInputText) != 0) {
    INKError("INKfopenTest() stat failed on %s\n", inputTextFileFullPath);
    return ++fopenErrCount;
  }

  /* clear inputBuf, could fill buffer here and avoid the file read */
  memset(inputBuf, '\0', BUFSIZ);

  /* inputReadFile is the file to read from, 
   *  inputBuf         is the buffer to write to
   *  statBuffInputText.st_size   is the amount  of data to read 
   */
  if ((retVal = INKfgets(sourceReadFile, inputBuf, statBuffInputText.st_size))
      == NULL) {
    INKError("INKfopenTest(): INKfgets failed\n");
    return ++fopenErrCount;
  }

  wrote = INKfwrite(writeFile, inputBuf, statBuffInputText.st_size);
  if (wrote != statBuffInputText.st_size) {
    INKError("INKfopenTest() INKfwrite failed: write %d/%d bytes\n", wrote, BUFSIZ);
    return ++fopenErrCount;
  }

  /* stat() */
  if (stat(tempnamFlushFile, &statBuffPre) != 0) {
    INKError("INKfopenTest() stat failed on tempnamFlushFile\n");
    return ++fopenErrCount;
  }

  /* INKfclose should indirectly call INKflush on pending data */
  INKfclose(writeFile);

  if (stat(tempnamFlushFile, &statBuffPost) != 0) {
    INKError("INKfopenTest() stat failed on tmpFlushFile\n");
    return ++fopenErrCount;
  }

  if ((statBuffPre.st_size == 0) && (statBuffPost.st_size == statBuffInputText.st_size)) {
    INKDebug("INKfopen_plug-in", "INKfopenTest(): INKfclose: passed, pending data flushed to file\n");
  } else {
    INKError("INKfopenTest(): INKfclose failed to flush pending data to file\n");
    INKError("file size: before INKfclose %d, after %d\n", statBuffPre.st_size, statBuffPost.st_size);
    return ++fopenErrCount;
  }

  /* 
   * 4. INKfopen
   *    4.4 INKfopen for write: Open existing file for write 
   */

  /* Does this append or does this truncate ? 
   * Guess: truncate as in fopen(3S) 
   * API: open(2) or fopen(3S) or NT? We just need to pick one.
   */
  /* writeFile just closed, temporary file still exists, reopen */
  if (!(writeFile = INKfopen(tempnamFlushFile, "w"))) {
    INKError("INKfopenTest(): INKfopen: open existing file for write failed\n");
    return ++fopenErrCount;
  }

  re = INKfwrite(writeFile, cmpBuf, statBuffInputText.st_size);

  if (re != statBuffInputText.st_size) {
    INKError("INKfopenTest(): INKfopen: INKfwrite on existing file open for write failed\n");
    return ++fopenErrCount;
  }

  if ((stat(tempnamFlushFile, &statBuffPost) == 0) && (statBuffPost.st_size == statBuffInputText.st_size)) {
    INKDebug("INKfopen_plug-in", "INKfopenTest(): INKfopen: INKfwrite on existing file open for write passed\n");
  } else {
    INKError("INKfopenTest(): INKfopen: INKfwrite on existing file open for write failed, reason unkown. \n");
    return ++fopenErrCount;
  }

  INKfclose(writeFile);

  /* remove the tmp file */
  if (unlink(tempnamFlushFile) != 0) {
    INKError("INKfopenTest() unlink failed on tmpFlushFile\n");
  }

  /* Need generic test pass/test fail routine */

  /* TODO report on total errors/total tests */
  INKDebug("INKfopen_plug-in", "INKfopen: completed  %d tests\n", NUM_FOPEN_TESTS);

  return fopenErrCount;
}

static int
INKMemAllocTest(INKHttpTxn txnp, INKCont contp)
{
  int INKMemAllocErrors = 0;

  INKDebug("INKfopen_plug-in", "INKMemAllocTest() called \n");
/*
INKmalloc
INKfree
INKfree
INKmalloc
INKrealloc
INKstrdup
INKstrndup 
*/

  return INKMemAllocErrors;
}

void *
pthreadStartFunc(void *arg)
{
  INKThread tmpINKThread = 0;

  /* Init this thread */
  tmpINKThread = INKThreadInit();

  /* sleep(6) ; */

  if (!tmpINKThread) {
    INKError("INKfopen_plug-in", "pthreadStartFunc():  INKThreadInit failed\n");
    /* TODO track threads created in INKThreadTest() with the threads being called/terminiting here */
     /* errCreateCount--; */
  } else
    INKDebug("INKfopen_plug-in", "pthreadStartFunc(): INKThreadInit pass\n");

  INKDebug("INKfopen_plug-in", "pthreadStartFunc(): created by thread: \
		%d running on pthread: %d\n", (pthread_t) arg, pthread_self());

  /* Clean-up this thread */
  if (tmpINKThread)
    INKThreadDestroy(tmpINKThread);

  return NULL;
}

static void *
INKThreadCallee(void *arg)
{
  char buf[BUFSIZ];
  INKThread INKthread;
  /* TODO do more useful work here */
  sleep(10);
  INKDebug("INKfopen_plug-in", "INKThreadTest(): INKThreadCallee: INKThreadCreate passed\n");

  INKthread = INKThreadSelf();  /* Get thread id for this thread */
  if (INKthread == 0) {
    INKError("INKfopen_plug-in", "INKThreadCallee(): INKThreadSelf() failed\n");
    /* TODO can't use return values track errors in global data (or tsd ?) */
  }
  INKDebug("INKfopen_plug-in",
           "INKThreadTest(): INKThreadCallee: created by pthread_t:%d running on pthread_t:%d, and INKThread at 0x%08x.\n",
           (int) arg, pthread_self(), INKthread);
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
INKThreadTest(INKHttpTxn txnp, INKCont contp)
{
  INKThreadFunc threadFunc;
  struct stat statBuffPre, statBuffPost;
  INKThread INKCurrThread = 0;
  INKThread INKCreateThread = 0;
  INKThread tmpINKThread = 0;
  int errCreateCount = NUM_THREADS;
  int threadErrCount = 0;
  int i, rval;

  currTid = pthread_self();
  INKDebug("INKfopen_plug-in", "INKThreadTest(): Begin: running on thread %d\n", currTid);

  /* call C++ code here */

  /* Test #1: Get current INKThread */
  INKCurrThread = INKThreadSelf();
  if (INKCurrThread == 0) {
    INKError("INKfopen_plug-in", "INKThreadSelf(): failed\n");
    threadErrCount++;
  } else
    INKDebug("INKfopen_plug-in", "INKThreadSelf(): passed: current INKThread:0x%08x\n", INKCurrThread);
  /* Test 2: */
  threadFunc = (INKThreadFunc) INKThreadCallee;
  /* Try to attach this thread to a function that does not have the 
   * the same prototype described in the p.g. 
   */
  /* INKThreadCreate(threadFunc, (void*)&argInt); */
  /* INKThreadCreate((INKThreadFunc)INKThreadCallee, (void*)&argInt); */
  INKCreateThread = INKThreadCreate(INKThreadCallee, (void *) currTid);
  if (INKCreateThread) {
    INKDebug("INKfopen_plug-in", "INKThreadCreate(): registration of thread init passed\n");
  } else {
    INKError("INKfopen_plug-in", "INKThreadCreate(): registration of thread init failed\n");
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
      INKError("INKfopen_plug-in",
               "INKThreadTest(): thread %d called pthread_create and failed to create pthread, errno:%d\n", currTid,
               errno);

                        /********************************************
			if (firstThreadError)
				continue;          skip these tests 
			********************************************/
    } else
      INKDebug("INKfopen_plug-in", "INKThreadTest(): pthread_create created: %d\n", newTid);
  }

  /* No errors means all threads were created */
  INKDebug("INKfopen_plug-in", "INKThreadTest():  created %d/%d pthreads\n", errCreateCount, NUM_THREADS);
  if (errCreateCount != NUM_THREADS)
    threadErrCount++;

  INKDebug("INKfopen_plug-in",
           "INKThreadTest(): results:  %d/%d test passed \n", (NUM_THREAD_TESTS - threadErrCount), NUM_THREAD_TESTS);

  return threadErrCount;
}

/* only callable from this file and as a call-back */
static int
INKfopen_plugin(INKCont contp, INKEvent event, void *edata)
{
  int status = 0;               /* assume success */

  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_OS_DNS:

#if 0
    status = INKfopenTest(txnp, contp);

    status += INKMemAllocTest(txnp, contp);
#endif

    status += INKThreadTest(txnp, contp);

  default:
    break;
  }
  /* Always reeneable HTTP transaction after processing of each
   * transaction event. 
   */
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  return status;
}


void
INKPluginInit(int argc, const char *argv[])
{
  INKCont contp;

  /* Parse args */
  if (!strcmp(argv[1], "INKfopenTest")) {
    INKDebug("INKfopen_plug-in", "\nRun: INKfopenTest \n");
  }
  /* Associate this data at time call-back is called */
  contp = INKContCreate(INKfopen_plugin, NULL /* mutex */ );

  if (contp == NULL) {
    INKError("usage: %s INKContCreate() returned NULL\n", argv[0]);
    return;
  }
  /* Set at INK_HTTP_OS_DNS_HOOK for no specific reason */
  INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, contp);
}
