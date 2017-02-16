--  Licensed to the Apache Software Foundation (ASF) under one
--  or more contributor license agreements.  See the NOTICE file
--  distributed with this work for additional information
--  regarding copyright ownership.  The ASF licenses this file
--  to you under the Apache License, Version 2.0 (the
--  "License"); you may not use this file except in compliance
--  with the License.  You may obtain a copy of the License at
--
--  http://www.apache.org/licenses/LICENSE-2.0
--
--  Unless required by applicable law or agreed to in writing, software
--  distributed under the License is distributed on an "AS IS" BASIS,
--  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--  See the License for the specific language governing permissions and
--  limitations under the License.

_G.ts = { client_request = {}  }
_G.TS_LUA_REMAP_DID_REMAP = 1

describe("Busted unit testing framework", function()
  describe("script for ATS Lua Plugin", function()

    it("test - script", function()
      stub(ts.client_request, "set_url_host")
      stub(ts.client_request, "set_url_port")
      stub(ts.client_request, "set_url_scheme")

      require("script")
      local result = do_remap()

      assert.stub(ts.client_request.set_url_host).was.called_with("192.168.231.130")
      assert.stub(ts.client_request.set_url_port).was.called_with(80)
      assert.stub(ts.client_request.set_url_scheme).was.called_with("http")

      assert.are.equals(TS_LUA_REMAP_DID_REMAP, result)
    end)

  end)
end)
