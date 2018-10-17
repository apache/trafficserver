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

package confremap

import (
	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&BaseConfRemapAdapter{})
}

type BaseConfRemapAdapter struct {
}

func (*BaseConfRemapAdapter) Weight() int {
	return 17
}

func (p *BaseConfRemapAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *BaseConfRemapAdapter) Type() voluspa.AdapterType {
	return adapterType
}

func (p *BaseConfRemapAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *BaseConfRemapAdapter) Name() string {
	return "base_conf_remap"
}

func (p *BaseConfRemapAdapter) SharedLibraryName() string {
	return "conf_remap.so"
}

func (p *BaseConfRemapAdapter) ConfigParameters() []string {
	return []string{""}
}

func (p *BaseConfRemapAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	return nil, nil
}
