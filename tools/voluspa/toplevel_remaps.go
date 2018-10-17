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
	"sort"
	"strings"
)

type TopLevelRemapGenerator struct {
	options *Options
}

func newTopLevelRemapGenerator(options *Options) *TopLevelRemapGenerator {
	return &TopLevelRemapGenerator{options: options}
}

func (tlrg *TopLevelRemapGenerator) formatDescriptionComment(config *CustomerConfig) string {
	var domain string
	if strings.HasPrefix(config.Remaps[0].IncomingURL, "http") {
		parts := strings.Split(config.Remaps[0].IncomingURL, "/")
		if len(parts) >= 2 {
			domain = parts[2]
		}
	}

	var buf bytes.Buffer
	buf.WriteString("############################################################################\n")
	buf.WriteString(fmt.Sprintf("# Host: %s\n", domain))
	buf.WriteString(fmt.Sprintf("# Owner: %s\n", config.owner))
	if len(config.reference) > 0 {
		buf.WriteString(fmt.Sprintf("# Reference: %s\n", config.reference))
	}
	buf.WriteString("############################################################################\n")
	return buf.String()
}

func (tlrg *TopLevelRemapGenerator) filenameForRole(role, cdn, parentOrChild string) string {
	var buf bytes.Buffer
	if cdn == DefaultCDN || cdn == NoCDN {
		buf.WriteString("remap_")
	} else {
		buf.WriteString(fmt.Sprintf("%s_remap_", cdn))
	}

	if len(role) > 0 {
		buf.WriteString(fmt.Sprintf("%s_", role))
	}

	buf.WriteString(fmt.Sprintf("%s_include.config", parentOrChild))

	return buf.String()
}

// groupedRemaps returns true if a Group has been explicitly set
func groupedRemaps(remaps []Remap) bool {
	for _, remap := range remaps {
		if remap.Group != DefaultGroup {
			return true
		}
	}
	return false
}

func (tlrg *TopLevelRemapGenerator) Do(parsedConfigs []*CustomerConfig, merge bool) ([]ManagedFile, error) {
	if merge {
		return tlrg.get(parsedConfigs, NoRole, NoCDN)
	}

	if len(parsedConfigs) == 0 {
		return nil, ErrMinimumConfigsNotMet
	}

	// Generating the include file is next to useless for 1 file
	if len(parsedConfigs) == 1 {
		return nil, nil
	}

	// group properties by CDN and role and generate includeable files for each role/parent/child combo
	grouped := make(map[string]map[string][]*CustomerConfig)

	for _, config := range parsedConfigs {
		if config.lifecycle == Retired {
			continue
		}

		for cdn := range config.cdn {

			if _, exists := grouped[cdn]; !exists {
				grouped[cdn] = make(map[string][]*CustomerConfig)
			}

			role := config.role
			grouped[cdn][role] = append(grouped[cdn][role], config)
		}
	}
	var managedFiles []ManagedFile
	for cdn, cdnGrouped := range grouped {
		for role, configs := range cdnGrouped {
			sort.Stable(sort.Reverse(byQPSAndName(configs)))

			remaps, err := tlrg.get(configs, role, cdn)
			if err != nil {
				return nil, err
			}
			managedFiles = append(managedFiles, remaps...)
		}
	}

	return managedFiles, nil

}

func (tlrg *TopLevelRemapGenerator) get(configs []*CustomerConfig, role, cdn string) ([]ManagedFile, error) {
	var managedFiles []ManagedFile

	for _, parentOrChild := range []string{Child, Parent} {

		var buf bytes.Buffer
		buf.WriteString("# START voluspa-generated config\n")

		if len(role) == 0 {
			buf.WriteString("\n")
		} else {
			buf.WriteString(fmt.Sprintf("# ROLE: %s\n\n", role))
		}

		for _, parsedConfig := range configs {
			if _, exists := parsedConfig.cdn[cdn]; !exists && cdn != NoCDN {
				continue
			}

			// skip grouped remap sets as the output cannot be used
			// without knowledge beyond the scope of voluspa
			// (feature used by cdn-icloud-content-uat)
			if groupedRemaps(parsedConfig.Remaps) {
				continue
			}

			property := strings.ToLower(parsedConfig.property)
			buf.WriteString(tlrg.formatDescriptionComment(parsedConfig))

			filename := property

			if parentOrChild == Child || !parsedConfig.parentChild {
				buf.WriteString(fmt.Sprintf(".include %s/%s.config\n", property, filename))
			} else {
				buf.WriteString(fmt.Sprintf(".include %s/%s_parent.config\n", property, filename))
			}
			buf.WriteString("\n")
		}
		buf.WriteString("# END voluspa-generated config\n\n")

		fileName := tlrg.filenameForRole(role, cdn, parentOrChild)

		var remoteFilename string
		if len(role) > 0 {
			remoteFilename = fmt.Sprintf("remap_voluspa_%s.config", role)
		} else {
			remoteFilename = "remap_voluspa.config"
		}

		managedFiles = append(managedFiles, NewManagedFile(fileName, remoteFilename, cdn, role, "", &buf, tlrg.configType(parentOrChild)))
	}

	return managedFiles, nil
}

func (tlrg *TopLevelRemapGenerator) configType(val string) ConfigLocation {
	switch val {
	case Child:
		return ChildConfig
	case Parent:
		return ParentConfig
	default:
		panic(fmt.Errorf("Unhandled configType '%s'", val))
	}
}
