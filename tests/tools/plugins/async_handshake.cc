/** @file

  Test TLS handshakes that pause inside an OpenSSL async job.

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

#include <ts/ts.h>

#include <openssl/async.h>
#include <openssl/crypto.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <unistd.h>

#define PLUGIN_NAME "async_handshake"
#define PCP         "[" PLUGIN_NAME "] "

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};
char   async_hook_key;

void
wait_cleanup(ASYNC_WAIT_CTX * /* ctx ATS_UNUSED */, const void * /* key ATS_UNUSED */, OSSL_ASYNC_FD read_fd, void *write_fd_ptr)
{
  auto *write_fd = static_cast<OSSL_ASYNC_FD *>(write_fd_ptr);

  close(read_fd);
  close(*write_fd);
  OPENSSL_free(write_fd);
}

void *
wake_async_job(void *arg)
{
  auto signal_fd = static_cast<OSSL_ASYNC_FD>(reinterpret_cast<intptr_t>(arg));
  char buf       = 'X';

  usleep(100 * 1000);
  if (write(signal_fd, &buf, sizeof(buf)) < 0) {
    fprintf(stderr, PCP "failed to send async wake signal to %d, errno=%d\n", signal_fd, errno);
  } else {
    fprintf(stderr, PCP "sent async wake signal to %d\n", signal_fd);
  }

  return nullptr;
}

bool
install_wait_fd(ASYNC_WAIT_CTX *wait_ctx, OSSL_ASYNC_FD &read_fd)
{
  OSSL_ASYNC_FD *write_fd = nullptr;
  if (ASYNC_WAIT_CTX_get_fd(wait_ctx, &async_hook_key, &read_fd, reinterpret_cast<void **>(&write_fd))) {
    return true;
  }

  OSSL_ASYNC_FD pipe_fds[2] = {-1, -1};
  write_fd                  = static_cast<OSSL_ASYNC_FD *>(OPENSSL_malloc(sizeof(*write_fd)));
  if (write_fd == nullptr) {
    TSError(PCP "failed to allocate async wait fd storage");
    return false;
  }

  if (pipe(pipe_fds) < 0) {
    TSError(PCP "failed to create async wait pipe: errno=%d", errno);
    OPENSSL_free(write_fd);
    return false;
  }
  *write_fd = pipe_fds[1];

  if (!ASYNC_WAIT_CTX_set_wait_fd(wait_ctx, &async_hook_key, pipe_fds[0], write_fd, wait_cleanup)) {
    TSError(PCP "failed to install async wait fd");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    OPENSSL_free(write_fd);
    return false;
  }

  pthread_t thread_id;
  if (pthread_create(&thread_id, nullptr, wake_async_job, reinterpret_cast<void *>(static_cast<intptr_t>(*write_fd))) == 0) {
    pthread_detach(thread_id);
  } else if (write(*write_fd, "X", 1) < 0) {
    TSError(PCP "failed to start async wake thread and immediate wake failed: errno=%d", errno);
    return false;
  }

  read_fd = pipe_fds[0];
  return true;
}

void
pause_current_async_job(TSVConn ssl_vc)
{
  ASYNC_JOB *job = ASYNC_get_current_job();
  if (job == nullptr) {
    TSError(PCP "SSL cert hook is not running in an OpenSSL async job");
    TSVConnReenable(ssl_vc);
    return;
  }

  ASYNC_WAIT_CTX *wait_ctx = ASYNC_get_wait_ctx(job);
  if (wait_ctx == nullptr) {
    TSError(PCP "OpenSSL async job has no wait context");
    TSVConnReenable(ssl_vc);
    return;
  }

  OSSL_ASYNC_FD read_fd = -1;
  if (!install_wait_fd(wait_ctx, read_fd)) {
    TSVConnReenable(ssl_vc);
    return;
  }

  TSVConnReenable(ssl_vc);

  fprintf(stderr, PCP "pausing OpenSSL async job\n");
  if (!ASYNC_pause_job()) {
    TSError(PCP "ASYNC_pause_job failed");
    return;
  }

  fprintf(stderr, PCP "resumed OpenSSL async job\n");
}

int
handle_ssl_cert(TSCont /* cont ATS_UNUSED */, TSEvent event, void *edata)
{
  TSVConn ssl_vc = static_cast<TSVConn>(edata);

  if (event == TS_EVENT_SSL_CERT) {
    pause_current_async_job(ssl_vc);
  } else {
    TSVConnReenable(ssl_vc);
  }
  return TS_SUCCESS;
}

} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed");
    return;
  }

  TSCont cb_cert = TSContCreate(handle_ssl_cert, TSMutexCreate());
  if (cb_cert == nullptr) {
    TSError(PCP "failed to create SSL cert hook");
    return;
  }

  TSHttpHookAdd(TS_SSL_CERT_HOOK, cb_cert);
  Dbg(dbg_ctl, "plugin online");
}
