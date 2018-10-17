/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package lua

import (
	"bytes"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	LuaAdapterParameter = "lua"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&FreeformLuaAdapter{})
	luaScriptName = "inline.lua"
}

type FreeformLuaAdapter struct {
}

func (*FreeformLuaAdapter) Weight() int {
	return 25
}

func (p *FreeformLuaAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *FreeformLuaAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("raw_lua")
}

func (p *FreeformLuaAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *FreeformLuaAdapter) Name() string {
	return ""
}

func (p *FreeformLuaAdapter) ConfigParameters() []string {
	return []string{LuaAdapterParameter}
}

func (p *FreeformLuaAdapter) SharedLibraryName() string {
	return "tslua.so"
}

func (p *FreeformLuaAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	val, err := env.RemapOptions.ValueByNameAsString(LuaAdapterParameter)
	if err != nil {
		return nil, util.FormatError(LuaAdapterParameter, err)
	}

	// Do nothing with the config, just pass through
	return bytes.NewBufferString(fmt.Sprintf("%s\n", val)), nil
}
