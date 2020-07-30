'''
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import os

Test.Summary = '''
Test custom log file format
'''
Test.SkipUnless(
    Condition.HasATSFeature('TS_HAS_PIPE_BUFFER_SIZE_CONFIG')
)

ts_counter = 1


def get_ts(logging_config):
    """
    Create a Traffic Server process.
    """
    global ts_counter
    ts = Test.MakeATSProcess("ts{}".format(ts_counter))
    ts_counter += 1

    ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'log-file',
        'proxy.config.log.max_secs_per_buffer': 1,
    })

    # Since we're only verifying logs and not traffic, we don't need an origin
    # server. The following will simply deny the requests and emit a log
    # message.
    ts.Disk.remap_config.AddLine(
        'map / http://www.linkedin.com/ @action=deny'
    )

    ts.Disk.logging_yaml.AddLines(logging_config)

    return ts


#
# Test 1: Default configured log pipe size.
#
tr = Test.AddTestRun()
pipe_name = "default_pipe_size.pipe"
ts = get_ts(
    '''
logging:
  formats:
    - name: custom
      format: "%<hii> %<hiih>"
  logs:
    - filename: '{}'
      mode: ascii_pipe
      format: custom
'''.format(pipe_name).split("\n")
)

pipe_path = os.path.join(ts.Variables.LOGDIR, pipe_name)

ts.Streams.All += Testers.ContainsExpression(
    "Created named pipe .*{}".format(pipe_name),
    "Verify that the named pipe was created")

ts.Streams.All += Testers.ContainsExpression(
    "no readers for pipe .*{}".format(pipe_name),
    "Verify that no readers for the pipe was detected.")

ts.Streams.All += Testers.ExcludesExpression(
    "New buffer size for pipe".format(pipe_name),
    "Verify that the default pipe size was used.")

curl = tr.Processes.Process("client_request", 'curl "http://127.0.0.1:{0}" --verbose'.format(
    ts.Variables.port))

reader_output = os.path.join(ts.Variables.LOGDIR, "reader_output")
pipe_reader = tr.Processes.Process("pipe_reader", 'cat {} | tee {}'.format(pipe_path, reader_output))

# Create an arbitrary process that just sleeps so that we can provide a wait
# condition upon the log being emitted. The test won't wait this entire sleep
# period, it will only poll until the FileContains Ready condition is
# fulfilled.
wait_for_log = tr.Processes.Process("wait_for_log", 'sleep 15')
wait_for_log.Ready = When.FileContains(reader_output, '127.0.0.1')

# This is an arbitrary Default process that will simply provide for
# ordering of the Processes.
tr.Processes.Default.Command = "echo 'Default place holder for process ordering.'"
tr.Processes.Default.Return = 0

# Process ordering.
tr.Processes.Default.StartBefore(wait_for_log)
wait_for_log.StartBefore(curl)
curl.StartBefore(pipe_reader)
pipe_reader.StartBefore(ts)


#
# Test 2: Change the log's buffer size.
#
tr = Test.AddTestRun()
pipe_name = "change_pipe_size.pipe"
# 64 KB is the default, so set the size larger than that to verify we can
# increase the size.
pipe_size = 75000
ts = get_ts(
    '''
logging:
  formats:
    - name: custom
      format: "%<hii> %<hiih>"
  logs:
    - filename: '{}'
      mode: ascii_pipe
      format: custom
      pipe_buffer_size: {}
      '''.format(pipe_name, pipe_size).split("\n")
)

pipe_path = os.path.join(ts.Variables.LOGDIR, pipe_name)

ts.Streams.All += Testers.ContainsExpression(
    "Created named pipe .*{}".format(pipe_name),
    "Verify that the named pipe was created")

ts.Streams.All += Testers.ContainsExpression(
    "no readers for pipe .*{}".format(pipe_name),
    "Verify that no readers for the pipe was detected.")

ts.Streams.All += Testers.ContainsExpression(
    "Previous buffer size for pipe .*{}".format(pipe_name),
    "Verify that the named pipe's size was adjusted")

# See fcntl:
#   "Attempts to set the pipe capacity below the page size
#    are silently rounded up to the page size."
#
# As a result of this, we cannot check that the pipe size is the exact size we
# requested, but it should be at least that big. We use the
# pipe_buffer_is_larger_than.py helper script to verify that the pipe grew in
# size.
ts.Streams.All += Testers.ContainsExpression(
    "New buffer size for pipe.*{}".format(pipe_name),
    "Verify that the named pipe's size was adjusted")
buffer_verifier = "pipe_buffer_is_larger_than.py"
tr.Setup.Copy(buffer_verifier)
verify_buffer_size = tr.Processes.Process(
    "verify_buffer_size",
    "python3 {} {} {}".format(buffer_verifier, pipe_path, pipe_size))
verify_buffer_size.Return = 0
verify_buffer_size.Streams.All += Testers.ContainsExpression(
    "Success",
    "The buffer size verifier should report success.")

curl = tr.Processes.Process("client_request", 'curl "http://127.0.0.1:{0}" --verbose'.format(
    ts.Variables.port))

reader_output = os.path.join(ts.Variables.LOGDIR, "reader_output")
pipe_reader = tr.Processes.Process("pipe_reader", 'cat {} | tee {}'.format(pipe_path, reader_output))

# Create an arbitrary process that just sleeps so that we can provide a wait
# condition upon the log being emitted. The test won't wait this entire sleep
# period, it will only poll until the FileContains Ready condition is
# fulfilled.
wait_for_log = tr.Processes.Process("wait_for_log", 'sleep 15')
wait_for_log.Ready = When.FileContains(reader_output, '127.0.0.1')

# This is an arbitrary Default process that will simply provide for
# ordering of the Processes.
tr.Processes.Default.Command = "echo 'Default place holder for process ordering.'"
tr.Processes.Default.Return = 0


# Process ordering.
tr.Processes.Default.StartBefore(verify_buffer_size)
verify_buffer_size.StartBefore(wait_for_log)
wait_for_log.StartBefore(curl)
curl.StartBefore(pipe_reader)
pipe_reader.StartBefore(ts)
