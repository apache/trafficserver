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
)

type ManagedFile struct {
	Filename       string
	RemoteFilename string
	Role           string
	CDN            string
	Property       string
	Contents       *bytes.Buffer
	ConfigType     ConfigLocation
}

func NewManagedFile(filename, remoteFilename, cdn, role, property string, contents *bytes.Buffer, configType ConfigLocation) ManagedFile {
	return ManagedFile{
		Filename:       filename,
		RemoteFilename: remoteFilename,
		Role:           role,
		CDN:            cdn,
		Property:       property,
		Contents:       contents,
		ConfigType:     configType,
	}
}

func (mf ManagedFile) String() string {
	return fmt.Sprintf("Filename=%s RFN=%s Role=%s CDN=%s Property=%s Size=%d Type=%d", mf.Filename, mf.RemoteFilename, mf.Role, mf.CDN, mf.Property, mf.Contents.Len(), mf.ConfigType)
}
