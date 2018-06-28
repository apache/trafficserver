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

_G.ts = { http = {}, client_request = {}, ctx = {} }
_G.TS_LUA_HOOK_TXN_CLOSE = 4
_G.TS_LUA_REMAP_DID_REMAP = 1

describe("Busted unit testing framework", function()
  describe("script for ATS Lua Plugin", function()

    it("test - module.split", function()

      local module = require("module")

      local results = module.split('a,b,c', ',')
      assert.are.equals('a', results[1])
      assert.are.equals('b', results[2])
    end)

    it("test - module.set_hook", function()
      stub(ts, "hook")
      local module = require("module")

      module.set_hook()

      assert.stub(ts.hook).was.called_with(TS_LUA_HOOK_TXN_CLOSE, module.test)
    end)

    it("test - module.set_context", function()

      local module = require("module")

      module.set_context()

      assert.are.equals('test10', ts.ctx['test'])
    end)

    it("test - module.check_internal", function()
      stub(ts.http, "is_internal_request").returns(0)
      local module = require("module")
      local result = module.check_internal()

      assert.are.equals(0, result)
    end)

    it("test - module.return_constant", function()
      local module = require("module")

      local result = module.return_constant()

      assert.are.equals(TS_LUA_REMAP_DID_REMAP, result)
    end)

  end)
end)
