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

Test.Summary = '''
Verify proxy.config.http.cache.try_compat_key_read: objects stored under the
ATS 9.2 cache key are served while fresh, revalidated without conditional
headers so the full response is stored under the current key, and left in place
under the legacy key to age out. Deletes reach both keys, a lost write lock
still serves the legacy object, and a plugin update of one is served but not
stored.
'''

Test.ContinueOnFail = True

Test.ATSReplayTest(replay_file="replay/compat-cache-key.replay.yaml")

# The write lock loss is injected through a remap plugin, so it needs a TS of
# its own.
Test.ATSReplayTest(replay_file="replay/compat-cache-key-write-lock.replay.yaml")

# A plugin update of the cached object, driven by a global test plugin.
Test.ATSReplayTest(replay_file="replay/compat-cache-key-plugin-update.replay.yaml")
