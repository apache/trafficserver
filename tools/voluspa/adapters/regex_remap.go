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

package adapters

import (
	"bytes"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	RegexRemapAdapterParameter  = "regex_remap"
	regexRemapSharedLibraryName = "regex_remap.so"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&RegexRemapAdapter{})
}

type RegexRemapAdapter struct {
}

func (*RegexRemapAdapter) Weight() int {
	return 15
}

func (p *RegexRemapAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *RegexRemapAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("regex_remap")
}

func (p *RegexRemapAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ParentConfig}
}

func (p *RegexRemapAdapter) Name() string {
	return "regex"
}

func (p *RegexRemapAdapter) SharedLibraryName() string {
	return regexRemapSharedLibraryName
}

func (p *RegexRemapAdapter) ConfigParameters() []string {
	return []string{RegexRemapAdapterParameter}
}

func (p *RegexRemapAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	regex, err := env.RemapOptions.ValueByNameAsString(RegexRemapAdapterParameter)
	if err != nil {
		return nil, util.FormatError(RegexRemapAdapterParameter, err)
	}

	return bytes.NewBufferString(regex), nil
}
