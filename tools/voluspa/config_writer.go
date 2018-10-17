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
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
)

type FilesystemWriter struct {
	options *Options
}

func NewFilesystemWriter(options *Options) (*FilesystemWriter, error) {
	fw := &FilesystemWriter{options: options}
	err := os.MkdirAll(fw.options.Destination, 0755)
	if err != nil {
		return nil, err
	}
	return fw, nil
}

func (f *FilesystemWriter) WriteFiles(files []ManagedFile) error {
	groups := make(map[string]interface{})
	for _, mf := range files {
		groups[mf.Role] = nil
	}

	for _, managedFile := range files {
		if _, exists := groups[managedFile.Role]; managedFile.Role != "" && !exists {
			continue
		}

		filename := fmt.Sprintf("%s/%s", f.options.Destination, managedFile.Filename)
		if f.options.Verbose {
			log.Printf("Writing %s", filename)
		}

		err := os.MkdirAll(filepath.Dir(filename), 0755)
		if err != nil {
			return err
		}

		err = ioutil.WriteFile(filename, managedFile.Contents.Bytes(), 0644)
		if err != nil {
			return err
		}
	}
	return nil
}

type ConfigExpander struct {
	options *Options
}

func NewConfigExpander(options *Options) *ConfigExpander {
	return &ConfigExpander{options: options}
}

func (c *ConfigExpander) ExpandRemapConf(parsedConfig *CustomerConfig) ([]ManagedFile, error) {
	var managedFiles []ManagedFile

	groups := make(map[string]bool)
	for _, config := range parsedConfig.Remaps {
		groups[config.Group] = true
	}

	configLocations := []ConfigLocation{UnknownLocation}
	if parsedConfig.parentChild {
		configLocations = []ConfigLocation{ChildConfig, ParentConfig}
	}

	for _, configLocation := range configLocations {
		for group := range groups {

			var buf bytes.Buffer
			buf.WriteString(generatedFileBanner)

			var hasContent bool
			for _, config := range parsedConfig.Remaps {
				if group != DefaultGroup && config.Group != group {
					continue
				}

				if configLocation != UnknownLocation && config.ConfigLocation != configLocation {
					continue
				}

				var roleEnabled bool
				if len(config.Role) > 0 {
					roleEnabled = true
					buf.WriteString(fmt.Sprintf("{%% if salt.pillar.get(\"%s\") %%}\n\n", config.Role))
				}

				buf.WriteString(config.asRemapConf())
				buf.WriteString("\n")

				if roleEnabled {
					buf.WriteString("{% endif %}\n\n")
				}

				hasContent = true
			}

			filename := fmt.Sprintf("%s/%s", strings.ToLower(parsedConfig.property), parsedConfig.remapConfigFilename(group, configLocation))

			if !hasContent {
				return nil, fmt.Errorf("Configuration for %s empty. Not writing %s", parsedConfig.property, filename)
			}

			for cdn := range parsedConfig.cdn {
				managedFiles = append(managedFiles, NewManagedFile(filename, filename, cdn, parsedConfig.role, parsedConfig.property, &buf, configLocation))
			}
		}
	}

	return managedFiles, nil
}

func (c *ConfigExpander) ExpandSubConfigs(parsedConfig *CustomerConfig) ([]ManagedFile, error) {
	var managedFiles []ManagedFile

	for _, remap := range parsedConfig.Remaps {
		managedSubFiles, err := c.expandSubConfig(parsedConfig, remap)
		if err != nil {
			return nil, err
		}
		managedFiles = append(managedFiles, managedSubFiles...)
	}
	return managedFiles, nil
}

func (c *ConfigExpander) expandSubConfig(parsedConfig *CustomerConfig, remap Remap) ([]ManagedFile, error) {
	var managedFiles []ManagedFile
	for i := range remap.mappingRules {
		adapter := remap.mappingRules[i]
		value := adapter.SubConfigContent

		if len(adapter.SubConfigFileName) == 0 {
			continue
		}
		if value == nil || value.Len() == 0 {
			continue
		}

		var buf bytes.Buffer
		buf.WriteString(generatedFileBanner)
		buf.Write(value.Bytes())

		// append newline if config does not end with one
		if value.Bytes()[len(value.Bytes())-1] != '\n' {
			buf.WriteByte('\n')
		}

		// TODO new method off of PCC?
		filename := fmt.Sprintf("%s/%s", strings.ToLower(parsedConfig.property), adapter.SubConfigFileName)

		for cdn := range parsedConfig.cdn {
			managedFiles = append(managedFiles, NewManagedFile(filename, filename, cdn, parsedConfig.role, parsedConfig.property, &buf, remap.ConfigLocation))
		}
	}

	return managedFiles, nil
}
