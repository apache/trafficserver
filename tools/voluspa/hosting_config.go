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
	"net/url"
	"sort"
	"strings"
)

type HostingConfigurator struct {
	options *Options
}

const (
	ramVolString     string = "{{ ramdisk_volume }}"
	defaultVolString string = "{{ default_volumes }}"

	HostingConfigDefaultFilename = "hosting.config_default"
)

type StorageType int

const (
	ramVolume StorageType = iota
	diskVolume
)

// hostingHostnameMap maps hostnames to StorageType
type hostingHostnameMap map[string]StorageType

type hostingHostname struct {
}

func newHostingHostname(hn string) *hostingHostname {
	return &hostingHostname{}
}

func newHostingConfigurator(options *Options) *HostingConfigurator {
	return &HostingConfigurator{
		options: options,
	}
}

func (h *HostingConfigurator) Do(parsedConfigs []*CustomerConfig, merge bool) ([]ManagedFile, error) {
	if merge {
		return h.get(parsedConfigs, NoCDN)
	}

	// otherwise, group properties by CDN and generate hosting.config for each CDN separately
	grouped := groupCustomerConfigsByCDN(parsedConfigs)

	var managedFiles []ManagedFile
	for cdn, configs := range grouped {
		files, err := h.get(configs, cdn)
		if err != nil {
			return nil, err
		}
		managedFiles = append(managedFiles, files...)
	}

	return managedFiles, nil

}

func (h *HostingConfigurator) get(parsedConfigs []*CustomerConfig, cdn string) ([]ManagedFile, error) {

	hostingHostnames := make(map[string]hostingHostnameMap)
	for _, parsedConfig := range parsedConfigs {
		role := parsedConfig.role
		if hostingHostnames[role] == nil {
			hostingHostnames[role] = make(hostingHostnameMap)
		}
		for _, remap := range parsedConfig.Remaps {
			destURL := remap.OriginURL
			if remap.sourceConfig.RemapOptions.HasOptionSet("origin_host_header") {
				value, err := remap.sourceConfig.RemapOptions.ValueByNameAsString("origin_host_header")
				if err != nil {
					return nil, err
				}
				if value == "alias" {
					destURL = remap.IncomingURL
				}
			}
			u, err := url.Parse(destURL)
			if err != nil {
				return nil, err
			}
			storageConfig := "disk_volume" // default
			if remap.sourceConfig.RemapOptions.HasOptionSet("storage_volume") {
				value, err := remap.sourceConfig.RemapOptions.ValueByNameAsString("storage_volume")
				if err != nil {
					return nil, err
				}
				storageConfig = value
			}
			if storageConfig == "ramdisk_volume" {
				hostingHostnames[role][u.Host] = ramVolume
			} else {
				hostingHostnames[role][u.Host] = diskVolume
			}
		}
	}

	return h.ExpandConfigTemplate(hostingHostnames, cdn)
}

func (h *HostingConfigurator) ExpandConfigTemplate(hostingHostnamesByRole map[string]hostingHostnameMap, cdn string) ([]ManagedFile, error) {
	seen := make(map[string]bool)

	var buf bytes.Buffer
	buf.WriteString(generatedFileBanner)
	buf.WriteString(fmt.Sprintf("{%% if salt.pillar.get(\"%s\") %%}\n", h.options.RamDiskRole))

	var roles []string
	for k := range hostingHostnamesByRole {
		roles = append(roles, k)
	}
	sort.Strings(roles)

	for _, role := range roles {
		hostingHostnames := hostingHostnamesByRole[role]

		var hostnames []string
		for hostname := range hostingHostnames {
			hostnames = append(hostnames, hostname)
		}

		if len(hostnames) == 0 {
			continue
		}

		sort.Strings(hostnames)

		hasRamVolumeHosts := false
		for _, hostname := range hostnames {
			storage := hostingHostnames[hostname]
			if storage == ramVolume {
				hasRamVolumeHosts = true
				break
			}
		}

		if !hasRamVolumeHosts {
			continue
		}

		buf.WriteString(h.startRoleGuard(role))

		for _, hostname := range hostnames {
			if _, exists := seen[hostname]; exists {
				continue
			}

			storage := hostingHostnames[hostname]
			if storage == ramVolume {
				seen[hostname] = true
				buf.WriteString(fmt.Sprintf("hostname=%s %s\n", hostname, ramVolString))
			}
		}
		buf.WriteString(h.endRoleGuard(role))
	}

	buf.WriteString(fmt.Sprintf("hostname=* %s\n", defaultVolString))

	buf.WriteString("{% else %}\n# hosting.config disabled on this host\n{% endif %}\n")

	filename := HostingConfigDefaultFilename
	if len(cdn) > 0 && (cdn != DefaultCDN && cdn != NoCDN) {
		filename = fmt.Sprintf("hosting.config_%s", cdn)
	}

	return []ManagedFile{NewManagedFile(filename, "hosting.config", cdn, "", "", &buf, UnknownLocation)}, nil

}

func (h *HostingConfigurator) startRoleGuard(role string) string {
	if len(role) == 0 {
		return ""
	}

	roleName := role
	if !strings.HasPrefix(role, "roles_") {
		roleName = fmt.Sprintf("roles_%s", role)
	}
	return fmt.Sprintf("{%% if salt.pillar.get('%s') %%}\n", roleName)
}

func (h *HostingConfigurator) endRoleGuard(role string) string {
	if len(role) == 0 {
		return ""
	}
	return "{% endif %}\n\n"
}
