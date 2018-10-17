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

package headerrewrite

import (
	"bytes"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	AddHeaderAdapterParameter       = "add_header"
	AddHeaderAdapterOriginParameter = "add_header_origin"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&AddHeaderAdapter{})
}

type AddHeaderAdapter struct {
}

func (p *AddHeaderAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *AddHeaderAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("add_header")
}

func (p *AddHeaderAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *AddHeaderAdapter) ConfigParameters() []string {
	return []string{AddHeaderAdapterParameter, AddHeaderAdapterOriginParameter}
}

func (p *AddHeaderAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	header, _ := env.RemapOptions.ValueByNameAsString(AddHeaderAdapterParameter)

	var buf bytes.Buffer
	if len(header) > 0 {
		buf.WriteString(util.FormatSimpleHeaderRewrite("READ_RESPONSE_HDR_HOOK", fmt.Sprintf("add-header %s", header)))
	}

	header, _ = env.RemapOptions.ValueByNameAsString(AddHeaderAdapterOriginParameter)
	if len(header) > 0 {
		if buf.Len() > 0 {
			buf.WriteByte('\n')
		}
		buf.WriteString(util.FormatSimpleHeaderRewrite("SEND_REQUEST_HDR_HOOK", fmt.Sprintf("add-header %s", header)))
	}

	return &buf, nil
}
