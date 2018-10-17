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
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	OriginHostHeaderAdapterParameter = "origin_host_header"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&PreserveHostHeaderAdapter{})
}

type PreserveHostHeaderAdapter struct {
}

func (*PreserveHostHeaderAdapter) Weight() int {
	return 25
}

func (p *PreserveHostHeaderAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *PreserveHostHeaderAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("preserve_host_header")
}

func (p *PreserveHostHeaderAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *PreserveHostHeaderAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ParentConfig}
}

func (p *PreserveHostHeaderAdapter) ConfigParameters() []string {
	return []string{OriginHostHeaderAdapterParameter}
}

func (p *PreserveHostHeaderAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	intval := 0
	if env.RemapOptions.HasOption(OriginHostHeaderAdapterParameter) {
		ohhValue, err := env.RemapOptions.ValueByNameAsString(OriginHostHeaderAdapterParameter)
		if err != nil {
			return nil, util.FormatError(OriginHostHeaderAdapterParameter, err)
		}
		switch ohhValue {
		case "alias":
			intval = 1
		case "origin":
			intval = 0
		default:
			return nil, util.FormatError(OriginHostHeaderAdapterParameter, fmt.Errorf("unknown value %q", ohhValue))
		}
	}

	return []string{fmt.Sprintf("proxy.config.url_remap.pristine_host_hdr=%d", intval)}, nil
}
