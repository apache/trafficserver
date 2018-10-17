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
	RemoveHeaderAdapterParameter       = "remove_header"
	RemoveHeaderAdapterOriginParameter = "remove_header_origin"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&RemoveHeaderAdapter{})
}

type RemoveHeaderAdapter struct {
}

func (p *RemoveHeaderAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *RemoveHeaderAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("remove_header")
}

func (p *RemoveHeaderAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *RemoveHeaderAdapter) ConfigParameters() []string {
	return []string{RemoveHeaderAdapterParameter, RemoveHeaderAdapterOriginParameter}
}

func (p *RemoveHeaderAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	header, _ := env.RemapOptions.ValueByNameAsString(RemoveHeaderAdapterParameter)

	var buf bytes.Buffer
	if len(header) > 0 {
		buf.WriteString(util.FormatSimpleHeaderRewrite("REMAP_PSEUDO_HOOK", fmt.Sprintf("rm-header %s", header)))
	}
	header, _ = env.RemapOptions.ValueByNameAsString(RemoveHeaderAdapterOriginParameter)
	if len(header) > 0 {
		if buf.Len() > 0 {
			buf.WriteByte('\n')
		}
		buf.WriteString(util.FormatSimpleHeaderRewrite("SEND_REQUEST_HDR_HOOK", fmt.Sprintf("rm-header %s", header)))
	}

	return &buf, nil
}
