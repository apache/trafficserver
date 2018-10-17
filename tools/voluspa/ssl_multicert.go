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

var (
	// defaultCertNames is a set of cert names to be specified as the default
	defaultCertNames = map[string]interface{}{
		"/secret/edgecdn:images.domain.com":                             nil,
		"/secret/images.domain.com.rsa":                                 nil,
		"/secret/images.domain.com.rsa,/secret/images.domain.com.ecdsa": nil,
	}

	SSLMulticertDefaultFilename = "ssl_multicert.config_default"
	SSLMulticertFilename        = "ssl_multicert.config"
)

type sslCert struct {
	filename string
	comment  string
}

type SSLMulticertConfigurator struct {
	options *Options
}

func newSSLMulticertConfigurator(options *Options) *SSLMulticertConfigurator {
	return &SSLMulticertConfigurator{options: options}
}

func initCertsMap(parsedConfigs []*CustomerConfig) map[string]map[string]sslCert {
	// a map of roles to an array of sslCerts
	certs := make(map[string]map[string]sslCert)

	sort.Stable(sort.Reverse(byQPSAndName(parsedConfigs)))

	for _, parsedConfig := range parsedConfigs {
		if len(parsedConfig.sslCertNames) == 0 {
			continue
		}
		if parsedConfig.lifecycle == Retired {
			continue
		}

		role := parsedConfig.role

		if _, exists := certs[role]; !exists {
			certs[role] = make(map[string]sslCert)
		}

		sort.Strings(parsedConfig.sslCertNames)

		for _, sslCertName := range parsedConfig.sslCertNames {
			certs[role][sslCertName] = sslCert{
				filename: sslCertName,
				comment:  parsedConfig.reference,
			}
		}
	}

	return certs
}

func (s *SSLMulticertConfigurator) Do(parsedConfigs []*CustomerConfig, merge bool) ([]ManagedFile, error) {
	if merge {
		return s.get(parsedConfigs, NoCDN)
	}

	// otherwise, group properties by CDN and generate ssl_multicert.config for each CDN
	grouped := groupCustomerConfigsByCDN(parsedConfigs)
	var managedFiles []ManagedFile
	for cdn, configs := range grouped {
		files, err := s.get(configs, cdn)
		if err != nil {
			return nil, err
		}
		managedFiles = append(managedFiles, files...)
	}

	return managedFiles, nil
}

func (s *SSLMulticertConfigurator) get(configs []*CustomerConfig, cdn string) ([]ManagedFile, error) {
	certs := initCertsMap(configs)
	if len(certs) == 0 {
		return nil, nil
	}

	var buf bytes.Buffer
	buf.WriteString(generatedFileBanner)
	var roles []string
	for role := range certs {
		roles = append(roles, role)
	}
	sort.Strings(roles)

	for _, role := range roles {
		err := s.expandConfigTemplate(certs, role, &buf)
		if err != nil {
			return nil, err
		}
	}

	fileName := SSLMulticertDefaultFilename
	if len(cdn) > 0 && (cdn != DefaultCDN && cdn != NoCDN) {
		fileName = fmt.Sprintf("ssl_multicert.config_%s", cdn)
	}

	return []ManagedFile{
		NewManagedFile(fileName, SSLMulticertFilename, cdn, "", "", &buf, UnknownLocation),
	}, nil
}

func (s *SSLMulticertConfigurator) startRoleGuard(role string) string {
	if len(role) == 0 {
		if s.options.PromoteRolesToCDN {
			return ""
		}

		return "{% if not salt.pillar.get('roles_uat') %}\n\n"
	}

	roleName := role
	if !strings.HasPrefix(role, "roles_") {
		roleName = fmt.Sprintf("roles_%s", role)
	}
	return fmt.Sprintf("{%% if salt.pillar.get('%s') %%}\n\n", roleName)
}

func (s *SSLMulticertConfigurator) endRoleGuard(role string) string {
	if len(role) == 0 && s.options.PromoteRolesToCDN {
		return ""
	}
	return "{% endif %}\n\n"
}

func (s *SSLMulticertConfigurator) expandConfigTemplate(certsMap map[string]map[string]sslCert, role string, buf *bytes.Buffer) error {
	certs, ok := certsMap[role]
	if !ok {
		return fmt.Errorf("role %q not found", role)
	}

	if len(certs) == 0 {
		return nil
	}

	var certFilenames []string
	for certFilename := range certs {
		certFilenames = append(certFilenames, certFilename)
	}
	sort.Strings(certFilenames)

	buf.WriteString(s.startRoleGuard(role))

	var sawDefaultCert bool
	for _, certFilename := range certFilenames {
		cert := certs[certFilename]
		if len(cert.comment) > 0 {
			buf.WriteString(fmt.Sprintf("# %s\n", cert.comment))
		}
		if _, found := defaultCertNames[certFilename]; found {
			if sawDefaultCert {
				return fmt.Errorf("more than one default cert found for role")
			}

			buf.WriteString("dest_ip=* ")
			sawDefaultCert = true
		}

		buf.WriteString(fmt.Sprintf("ssl_cert_name=%s\n\n", certFilename))
	}

	buf.WriteString(s.endRoleGuard(role))

	return nil
}
