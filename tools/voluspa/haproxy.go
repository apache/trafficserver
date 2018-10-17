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

package voluspa

import (
	"bytes"
	"fmt"
	"strings"
)

// HAProxyVIP describes a haproxy VIP
type HAProxyVIP struct {
	Port int
	RIPs []RIP
}

// RIP is the real IP and Port for a HAProxyVIP
type RIP struct {
	IP   string
	Port int
}

type HAProxyConfigGenerator struct {
}

func (h *HAProxyConfigGenerator) Do(parsedConfigs []*CustomerConfig, merged bool) ([]ManagedFile, error) {
	var results []ManagedFile
	for _, parsedConfig := range parsedConfigs {
		if len(parsedConfig.HAProxyVIPs) == 0 {
			continue
		}

		var buf bytes.Buffer
		buf.WriteString(generatedFileBanner)

		for _, hapConfig := range parsedConfig.HAProxyVIPs {

			buf.WriteString(fmt.Sprintf("frontend %s 127.0.0.1:%d\n", parsedConfig.property, hapConfig.Port))
			buf.WriteString("    default_backend app\n")
			buf.WriteString("    mode http\n")
			buf.WriteString("\n")
			buf.WriteString(fmt.Sprintf("backend %s\n", parsedConfig.property))
			buf.WriteString("    mode http\n")
			buf.WriteString("    balance roundrobin\n")
			buf.WriteString("    option httpchk GET / HTTP/1.1\\r\\nHost:localhost\n")

			for i, rip := range hapConfig.RIPs {
				buf.WriteString(fmt.Sprintf("    server %s%03d %s:%d check\n", parsedConfig.property, i+1, rip.IP, rip.Port))
			}

			filename := fmt.Sprintf("%s/%s.hap.config", strings.ToLower(parsedConfig.property), strings.ToLower(parsedConfig.property))

			results = append(results, ManagedFile{
				Filename:   filename,
				Role:       "",
				Property:   parsedConfig.property,
				Contents:   &buf,
				ConfigType: UnknownLocation,
			})
		}
	}

	return results, nil
}
