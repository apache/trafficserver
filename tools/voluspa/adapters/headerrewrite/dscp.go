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
	DSCPAdapterParameterPriority = "priority"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&DSCPAdapter{})
}

type DSCPAdapter struct {
}

func (p *DSCPAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *DSCPAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("dscp")
}

func (p *DSCPAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *DSCPAdapter) ConfigParameters() []string {
	return []string{DSCPAdapterParameterPriority}
}

func (p *DSCPAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ChildConfig}
}

func (p *DSCPAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}
	value, err := env.RemapOptions.ValueByNameAsString(DSCPAdapterParameterPriority)
	if err != nil {
		return nil, err
	}

	// http://www.tucny.com/Home/dscp-tos
	var dscpvalue int
	switch value {
	case "background":
		// Queue 1 - Low-Priority Data - CS1
		dscpvalue = 8
	case "foreground":
		// Queue 0 - Best-Effort - AF31, AF32, AF33, AF11, AF12, AF13
		//
		// AF11 = High-Throughput Data - iTunes Downloads (App Store, Songs, etc.)
		dscpvalue = 10
	case "streamingaudio":
		// AF31 = Multimedia Streaming - HTTP Live Streaming - Streaming Audio
		dscpvalue = 26
	case "streamingvideo":
		// AF32 = Multimedia Streaming - HTTP Live Streaming - Streaming Video
		dscpvalue = 28
	default:
		return nil, util.FormatError(DSCPAdapterParameterPriority, fmt.Errorf("Unhandled priority %q", value))
	}

	content.WriteString(util.FormatSimpleHeaderRewrite("REMAP_PSEUDO_HOOK", fmt.Sprintf("set-conn-dscp %d", dscpvalue)))

	return content, nil
}
