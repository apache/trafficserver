#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
"""
Centralized documentation data for LSP functionality.

This module contains comprehensive documentation for functions, keywords, and other
language elements that are used for hover information and completion details.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final

from hrw4u.types import SuffixGroup


@dataclass(slots=True, frozen=True)
class DocumentationInfo:
    """Unified documentation structure for all HRW4U language elements."""
    name: str
    description: str
    context: str | None = None
    usage: str = "Used in expression evaluation."
    maps_to: str | None = None
    available_items: list[str] | None = None
    examples: list[str] | None = None
    default_value: str | None = None
    possible_values: list[str] | None = None

    def create_hover_markdown(self, expression: str, is_interpolation: bool = False) -> str:
        """Create standardized hover markdown for this documentation."""
        prefix = f"**{{{expression}}}**" if is_interpolation else f"**{expression}**"
        suffix = " Interpolation" if is_interpolation else ""

        sections = [f"{prefix} - {self.name}{suffix}"]

        if self.context:
            sections.extend(["", f"**Context:** {self.context}"])

        sections.extend(["", f"**Description:** {self.description}"])

        if self.maps_to:
            sections.extend(["", f"**Maps to:** `{self.maps_to}`"])

        if self.available_items:
            sections.extend(["", f"**Available items:** {', '.join(self.available_items)}"])

        if self.default_value:
            sections.extend(["", f"**Default Value:** `{self.default_value}`"])

        if self.possible_values:
            sections.extend(["", f"**Possible Values:** {', '.join(self.possible_values)}"])

        sections.extend(["", f"**Usage:** {self.usage}"])

        if self.examples:
            sections.extend(["", "**Examples:**"])
            for example in self.examples:
                sections.append(f"```hrw4u\n{example}\n```")

        return "\n".join(sections)


# Legacy alias for backward compatibility
FieldInfo = DocumentationInfo


@dataclass(slots=True, frozen=True)
class IPPatternInfo:
    """Specialized field info for IP patterns with additional metadata."""
    name: str
    description: str
    maps_to: str
    usage: str

    def create_hover_markdown(self, expression: str) -> str:
        """Create hover markdown for IP patterns."""
        return (
            f"**{expression}** - {self.name}\n\n"
            f"**Context:** {self.description}\n\n"
            f"**Maps to:** `{self.maps_to}`\n\n"
            f"{self.usage}")


@dataclass(slots=True, frozen=True)
class FunctionDoc:
    """Documentation for a function."""
    name: str
    category: str
    description: str
    syntax: str
    maps_to: str
    usage_context: str
    parameters: list[ParameterDoc] | None = None
    examples: list[str] | None = None
    return_values: list[str] | None = None


@dataclass(slots=True, frozen=True)
class ParameterDoc:
    """Documentation for a function parameter."""
    name: str
    type: str
    description: str


# Connection Field Documentation
# URL Component Descriptions
URL_COMPONENTS: Final[dict[str, FieldInfo]] = {
    'host': FieldInfo('Hostname', 'The hostname part of the URL', usage='Used for hostname-based routing and filtering.'),
    'port': FieldInfo('Port Number', 'The port number of the URL', usage='Used for port-based routing and filtering.'),
    'path':
        FieldInfo(
            'URL Path',
            'The path portion of the URL (everything after the hostname/port)',
            usage='Used for path-based routing and filtering.'),
    'query':
        FieldInfo(
            'Query String',
            'The query string parameters (everything after ?)',
            usage='Used for query parameter analysis and filtering.'),
    'scheme': FieldInfo('URL Scheme', 'The URL scheme (http, https, etc.)', usage='Used for protocol-based routing decisions.'),
    'fragment': FieldInfo('URL Fragment', 'The URL fragment (everything after #)', usage='Used for fragment-based processing.')
}

# ID Field Descriptions
ID_FIELDS: Final[dict[str, FieldInfo]] = {
    'UNIQUE':
        FieldInfo(
            'Unique Identifier', 'Globally unique identifier for this transaction', '%{ID:UNIQUE}',
            'Used for transaction tracking and correlation across systems.'),
    'REQUEST':
        FieldInfo(
            'Request ID', 'Unique identifier for this HTTP request', '%{ID:REQUEST}', 'Used for request tracking and debugging.'),
    'PROCESS':
        FieldInfo('Process ID', 'ATS process identifier', '%{ID:PROCESS}', 'Used for process-level debugging and monitoring.'),
    'THREAD':
        FieldInfo(
            'Thread ID', 'ATS thread identifier', '%{ID:THREAD}', 'Used for thread-level debugging and performance analysis.'),
    'SESSION': FieldInfo('Session ID', 'Client session identifier', '%{ID:SESSION}', 'Used for session tracking and management.'),
    'SSN':
        FieldInfo(
            'Session ID (alias)', 'Alias for SESSION - client session identifier', '%{ID:SSN}',
            'Used for session tracking and management (short form).')
}

# Time Field Descriptions
TIME_FIELDS: Final[dict[str, FieldInfo]] = {
    'YEAR':
        FieldInfo(
            'Current Year', '4-digit year (e.g., 2023)', '%{NOW:YEAR}',
            'Used for timestamp generation and time-based conditional logic.'),
    'MONTH': FieldInfo('Current Month', 'Month number (1-12)', '%{NOW:MONTH}', 'Used for month-based routing and time analysis.'),
    'DAY': FieldInfo('Current Day', 'Day of month (1-31)', '%{NOW:DAY}', 'Used for day-based routing and scheduling.'),
    'HOUR':
        FieldInfo(
            'Current Hour', 'Hour in 24-hour format (0-23)', '%{NOW:HOUR}',
            'Used for time-of-day based routing and load balancing.'),
    'MINUTE': FieldInfo('Current Minute', 'Minute (0-59)', '%{NOW:MINUTE}', 'Used for fine-grained time-based decisions.'),
    'SECOND': FieldInfo('Current Second', 'Second (0-59)', '%{NOW:SECOND}', 'Used for precise timing and debugging.'),
    'WEEKDAY':
        FieldInfo(
            'Day of Week', 'Day of week (0=Sunday, 6=Saturday)', '%{NOW:WEEKDAY}',
            'Used for weekday-based routing and scheduling.'),
    'YEARDAY': FieldInfo('Day of Year', 'Day of year (1-366)', '%{NOW:YEARDAY}', 'Used for annual scheduling and time analysis.')
}

# Geographic Field Descriptions
GEO_FIELDS: Final[dict[str, FieldInfo]] = {
    'COUNTRY':
        FieldInfo(
            'Country Code', 'ISO 3166-1 alpha-2 country code (e.g., US, GB, DE)', '%{GEO:COUNTRY}',
            'Used for geographic routing, content localization, and compliance.'),
    'COUNTRY_ISO':
        FieldInfo(
            'Country ISO Code', 'ISO 3166-1 alpha-3 country code (e.g., USA, GBR, DEU)', '%{GEO:COUNTRY-ISO}',
            'Used for geographic routing with extended country codes.'),
    'ASN':
        FieldInfo(
            'Autonomous System Number', 'ASN of the client IP address (e.g., 15169 for Google)', '%{GEO:ASN}',
            'Used for network-based routing and traffic analysis.'),
    'ASN-NAME':
        FieldInfo(
            'AS Organization', 'Name of the Autonomous System organization (e.g., "Google LLC")', '%{GEO:ASN-NAME}',
            'Used for organization-based routing and analytics.')
}

# Certificate Field Descriptions
CERT_FIELDS: Final[dict[str, FieldInfo]] = {
    'PEM':
        FieldInfo(
            'PEM-encoded Certificate',
            'The PEM-encoded certificate, as a string',
            context='X.509 Certificate Field',
            usage='Used to extract the full certificate in PEM format for validation and analysis.'),
    'SIG':
        FieldInfo(
            'Certificate Signature',
            'The signature of the certificate',
            context='X.509 Certificate Field',
            usage='Used for certificate signature verification and validation.'),
    'SUBJECT':
        FieldInfo(
            'Certificate Subject',
            'The subject of the certificate',
            context='X.509 Certificate Field',
            usage='Used for certificate identity verification and routing.'),
    'ISSUER':
        FieldInfo(
            'Certificate Issuer',
            'The issuer of the certificate',
            context='X.509 Certificate Field',
            usage='Used for certificate authority validation and trust decisions.'),
    'SERIAL':
        FieldInfo(
            'Serial Number',
            'The serial number of the certificate',
            context='X.509 Certificate Field',
            usage='Used for certificate identification and revocation checking.'),
    'NOT_BEFORE':
        FieldInfo(
            'Valid From Date',
            'The date and time when the certificate becomes valid',
            context='X.509 Certificate Field',
            usage='Used for certificate validity period checking.'),
    'NOT_AFTER':
        FieldInfo(
            'Expiration Date',
            'The date and time when the certificate expires',
            context='X.509 Certificate Field',
            usage='Used for certificate expiration monitoring and validation.'),
    'VERSION':
        FieldInfo(
            'Certificate Version',
            'The version of the certificate',
            context='X.509 Certificate Field',
            usage='Used for certificate format compatibility checking.')
}

# Subject Alternative Name Field Descriptions
SAN_FIELDS: Final[dict[str, FieldInfo]] = {
    'DNS':
        FieldInfo(
            'DNS Names',
            'The Subject Alternative Name (SAN) DNS entries',
            context='Certificate SAN Field',
            usage='Used for hostname validation and multi-domain certificate handling.'),
    'IP':
        FieldInfo(
            'IP Addresses',
            'The Subject Alternative Name (SAN) IP addresses',
            context='Certificate SAN Field',
            usage='Used for IP-based certificate validation and routing.'),
    'EMAIL':
        FieldInfo(
            'Email Addresses',
            'The Subject Alternative Name (SAN) email addresses',
            context='Certificate SAN Field',
            usage='Used for email-based certificate validation and user identification.'),
    'URI':
        FieldInfo(
            'URIs',
            'The Subject Alternative Name (SAN) URIs',
            context='Certificate SAN Field',
            usage='Used for URI-based certificate validation and service identification.')
}

# IP Pattern Information
IP_PATTERNS: Final[dict[str, IPPatternInfo]] = {
    'inbound.ip':
        IPPatternInfo(
            'Client IP Address', 'IP address of the connecting client', '%{IP:CLIENT}',
            'Used for access control, logging, and geographic routing.'),
    'outbound.ip':
        IPPatternInfo(
            'Server IP Address', 'IP address of the destination server', '%{IP:SERVER}',
            'Used for server selection and routing decisions.'),
    'inbound.server':
        IPPatternInfo(
            'Inbound Server IP', 'IP address of the ATS inbound interface', '%{IP:INBOUND}',
            'Used for multi-homed server configurations.'),
    'outbound.server':
        IPPatternInfo(
            'Outbound Server IP', 'IP address of the ATS outbound interface', '%{IP:OUTBOUND}',
            'Used for source IP selection and routing.')
}

# Connection Field Descriptions
CONN_FIELDS: Final[dict[str, FieldInfo]] = {
    'DSCP':
        FieldInfo(
            'Differentiated Services Code Point', 'Traffic classification and QoS marking (0-63)', 'set-conn-dscp',
            'Used for Quality of Service (QoS) traffic classification and prioritization.'),
    'MARK':
        FieldInfo(
            'Connection Mark', 'Netfilter connection mark for traffic shaping (32-bit integer)', 'set-conn-mark',
            'Used for advanced traffic shaping and firewall integration.')
}

LSP_CONNECTION_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "TLS":
        DocumentationInfo(
            name="TLS Protocol",
            description="The TLS protocol version if the connection is over TLS, otherwise empty string.",
            maps_to="%{INBOUND:TLS}",
            context="Connection Security Information",
            usage="Used to detect TLS connections and make security-based routing decisions.",
            examples=[
                "if inbound.conn.TLS {\n    // Handle TLS connection\n}", "inbound.conn.TLS != \"\" && inbound.method == \"GET\""
            ]),
    "H2":
        DocumentationInfo(
            name="HTTP/2 Protocol",
            description="The string 'h2' if the connection is HTTP/2, otherwise empty string.",
            maps_to="%{INBOUND:H2}",
            context="HTTP Protocol Information",
            usage="Used to detect HTTP/2 connections and apply protocol-specific handling.",
            examples=["if inbound.conn.H2 {\n    // Handle HTTP/2 connection\n}", "inbound.conn.H2 == \"h2\""]),
    "IPV4":
        DocumentationInfo(
            name="IPv4 Connection",
            description="The string 'ipv4' if the connection is IPv4, otherwise empty string.",
            maps_to="%{INBOUND:IPV4}",
            context="IP Version Information",
            usage="Used to detect IPv4 connections for version-specific processing.",
            examples=["if inbound.conn.IPV4 {\n    // Handle IPv4 connection\n}"]),
    "IPV6":
        DocumentationInfo(
            name="IPv6 Connection",
            description="The string 'ipv6' if the connection is IPv6, otherwise empty string.",
            maps_to="%{INBOUND:IPV6}",
            context="IP Version Information",
            usage="Used to detect IPv6 connections for version-specific processing.",
            examples=["if inbound.conn.IPV6 {\n    // Handle IPv6 connection\n}"]),
    "IP-FAMILY":
        DocumentationInfo(
            name="IP Address Family",
            description="The IP family: either 'ipv4' or 'ipv6'.",
            maps_to="%{INBOUND:IP-FAMILY}",
            context="IP Version Information",
            usage="Used to determine the IP version family for network routing decisions.",
            examples=["if inbound.conn.IP-FAMILY == \"ipv6\" {\n    // IPv6-specific handling\n}"]),
    "STACK":
        DocumentationInfo(
            name="Protocol Stack",
            description="The full protocol stack separated by commas (e.g., 'ipv4,tcp,tls,h2').",
            maps_to="%{INBOUND:STACK}",
            context="Protocol Stack Information",
            usage="Used for comprehensive protocol analysis and debugging.",
            examples=["if inbound.conn.STACK ~ /tls/ {\n    // TLS is in the stack\n}"]),
    "LOCAL-ADDR":
        DocumentationInfo(
            name="Local Address",
            description="The local (ATS) IP address for the connection.",
            maps_to="%{INBOUND:LOCAL-ADDR}",
            context="Connection Address Information",
            usage="Used for multi-homed server configurations and routing decisions.",
            examples=["inbound.resp.X-Server-IP = \"{inbound.conn.LOCAL-ADDR}\""]),
    "LOCAL-PORT":
        DocumentationInfo(
            name="Local Port",
            description="The local (ATS) port number for the connection.",
            maps_to="%{INBOUND:LOCAL-PORT}",
            context="Connection Address Information",
            usage="Used for port-based routing and service identification.",
            examples=["if inbound.conn.LOCAL-PORT == 8080 {\n    // Handle specific port\n}"]),
    "REMOTE-ADDR":
        DocumentationInfo(
            name="Remote Address",
            description="The client IP address for the connection.",
            maps_to="%{INBOUND:REMOTE-ADDR}",
            context="Connection Address Information",
            usage="Used for client identification, access control, and geographic routing.",
            examples=["inbound.resp.X-Client-IP = \"{inbound.conn.REMOTE-ADDR}\""]),
    "REMOTE-PORT":
        DocumentationInfo(
            name="Remote Port",
            description="The client port number for the connection.",
            maps_to="%{INBOUND:REMOTE-PORT}",
            context="Connection Address Information",
            usage="Used for client connection analysis and debugging.",
            examples=["if inbound.conn.REMOTE-PORT > 1024 {\n    // Non-privileged port\n}"])
}


def _build_certificate_field_description(cert_type: str) -> str:
    """Build certificate field description dynamically from type definitions."""
    cert_fields = {field.upper() for field in SuffixGroup.CERT_FIELDS.value}
    san_fields = {field.upper() for field in SuffixGroup.SAN_FIELDS.value}

    cert_field_list = ", ".join(sorted(cert_fields))
    san_field_list = ", ".join(f"SAN.{field}" for field in sorted(san_fields))

    base_desc = f"Access to {cert_type} certificate fields from {'mTLS connections' if cert_type == 'client' else 'TLS handshake'}."
    field_desc = f"Available fields include {cert_field_list}, {san_field_list}, etc."

    return f"{base_desc} {field_desc}"


LSP_CERTIFICATE_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "client-cert":
        DocumentationInfo(
            name="Client Certificate",
            description=_build_certificate_field_description("client"),
            context="X.509 Client Certificate",
            usage="Used for client authentication, authorization, and certificate-based routing in mTLS scenarios.",
            examples=[
                "if inbound.conn.client-cert.SUBJECT ~ /CN=admin/ {\n    // Admin client detected\n}",
                "inbound.resp.X-Client-Cert = \"{inbound.conn.client-cert.SERIAL}\""
            ]),
    "server-cert":
        DocumentationInfo(
            name="Server Certificate",
            description=_build_certificate_field_description("server"),
            context="X.509 Server Certificate",
            usage="Used for server certificate validation, monitoring, and certificate-based routing decisions.",
            examples=[
                "if inbound.conn.server-cert.NOT_AFTER < now.timestamp {\n    // Certificate expired\n}",
                "inbound.resp.X-Server-Cert-CN = \"{inbound.conn.server-cert.SUBJECT}\""
            ])
}


@dataclass(slots=True, frozen=True)
class CertificatePattern:
    """Table-driven certificate pattern parsing."""

    @staticmethod
    def parse_certificate_expression(expression: str, is_interpolation: bool = False) -> dict[str, str] | None:
        """Parse certificate expressions using table-driven approach."""
        parts = expression.split('.')

        if len(parts) < 4:
            return None

        direction, conn_part, cert_type = parts[0], parts[1], parts[2]

        # Validate structure
        if conn_part != 'conn' or cert_type not in LSP_CERTIFICATE_DOCUMENTATION:
            return None

        cert_doc = LSP_CERTIFICATE_DOCUMENTATION[cert_type]
        direction_name = direction.title()
        cert_prefix = "CLIENT-CERT" if cert_type == "client-cert" else "SERVER-CERT"

        # Handle SAN fields
        if len(parts) >= 5 and parts[3].upper() == 'SAN':
            san_field = parts[4].upper() if len(parts) > 4 else ''
            san_fields_upper = {field.upper() for field in SuffixGroup.SAN_FIELDS.value}
            if san_field in san_fields_upper:
                field_name = f"SAN {san_field}"
                maps_to = f"%{{{direction.upper()}:{cert_prefix}:SAN:{san_field}}}"
                description = f"{cert_doc.description}. Accessing {field_name} entries from certificate Subject Alternative Names."
                usage = "Used to extract Subject Alternative Name fields from X.509 certificates. Available exclusively on TLS sessions for certificate introspection."

                return {
                    "field_name": field_name,
                    "cert_name": cert_doc.name,
                    "direction_name": direction_name,
                    "maps_to": maps_to,
                    "description": description,
                    "usage": usage,
                    "context": f"{direction_name} Connection {cert_doc.name} SAN Field"
                }

        # Handle regular certificate fields
        elif len(parts) == 4:
            cert_field = parts[3].upper()
            cert_fields_upper = {field.upper() for field in SuffixGroup.CERT_FIELDS.value}
            if cert_field in cert_fields_upper:
                maps_to = f"%{{{direction.upper()}:{cert_prefix}:{cert_field}}}"
                description = f"{cert_doc.description}. Accessing the {cert_field} field from the certificate."
                usage = "Used to extract certificate fields from X.509 certificates. Available exclusively on TLS sessions for certificate introspection."

                return {
                    "field_name": cert_field,
                    "cert_name": cert_doc.name,
                    "direction_name": direction_name,
                    "maps_to": maps_to,
                    "description": description,
                    "usage": usage,
                    "context": f"{direction_name} Connection {cert_doc.name}"
                }

        return None

    @staticmethod
    def create_certificate_hover(expression: str, parsed_data: dict[str, str], is_interpolation: bool = False) -> str:
        """Create hover documentation from parsed certificate data."""
        prefix = f"**{{{expression}}}**" if is_interpolation else f"**{expression}**"
        suffix = " Interpolation" if is_interpolation else ""

        return (
            f"{prefix} - {parsed_data['cert_name']} {parsed_data['field_name']}{suffix}\n\n"
            f"**Context:** {parsed_data['context']}\n\n"
            f"**Description:** {parsed_data['description']}\n\n"
            f"**Maps to:** `{parsed_data['maps_to']}`\n\n"
            f"{parsed_data['usage']}")


@dataclass(slots=True, frozen=True)
class URLPattern:
    """Table-driven URL pattern parsing."""

    @staticmethod
    def parse_url_expression(expression: str, is_interpolation: bool = False) -> dict[str, str] | None:
        """Parse URL expressions using table-driven approach."""
        import re

        url_pattern_match = re.match(r'^([^.]+)\.url\.([^.]+)$', expression)
        if not url_pattern_match:
            return None

        prefix, url_component = url_pattern_match.groups()

        if url_component.lower() not in URL_COMPONENTS:
            return None

        component_info = URL_COMPONENTS[url_component.lower()]
        direction_name = prefix.replace('-', ' ').title()

        return {
            "expression": expression,
            "prefix": prefix,
            "component": url_component,
            "direction_name": direction_name,
            "component_name": component_info.name,
            "description": component_info.description,
            "usage": component_info.usage
        }

    @staticmethod
    def create_url_hover(parsed_data: dict[str, str], is_interpolation: bool = False) -> str:
        """Create hover documentation from parsed URL data."""
        expr = parsed_data["expression"]
        prefix = f"**{{{expr}}}**" if is_interpolation else f"**{expr}**"
        suffix = " Interpolation" if is_interpolation else ""

        return (
            f"{prefix} - {parsed_data['direction_name']} URL {parsed_data['component'].title()}{suffix}\n\n"
            f"**Context:** {parsed_data['direction_name']} URL Component\n\n"
            f"**Description:** {parsed_data['description']}\n\n"
            f"**Usage:** {parsed_data['usage']}")


@dataclass(slots=True, frozen=True)
class CapturePattern:
    """Table-driven capture group parsing."""

    @staticmethod
    def parse_capture_expression(expression: str, is_interpolation: bool = False) -> dict[str, str] | None:
        """Parse capture group expressions using table-driven approach."""
        if not expression.startswith('capture.'):
            return None

        capture_ref = expression[8:]  # Remove 'capture.' prefix

        try:
            group_number = int(capture_ref)
            return {
                "expression": expression,
                "group_number": group_number,
                "is_numeric": True,
                "maps_to": f"${{CAPTURE:{group_number}}}",
                "description": f"References capture group #{group_number} from the most recently evaluated regex condition",
                "usage":
                    "Used to insert captured substrings from regex matches into string values. The capture group must be defined in a preceding regex condition (e.g., using `//` matches).",
                "note": "Capture groups are numbered starting from 1. Group 0 represents the entire match."
            }
        except ValueError:
            return {
                "expression": expression,
                "group_number": None,
                "is_numeric": False,
                "maps_to": None,
                "description": f"References a capture element '{capture_ref}' from the most recently evaluated regex condition",
                "usage": "Used to insert captured substrings from regex matches into string values.",
                "note": None
            }

    @staticmethod
    def create_capture_hover(parsed_data: dict[str, str], is_interpolation: bool = False) -> str:
        """Create hover documentation from parsed capture data."""
        expr = parsed_data["expression"]
        prefix = f"**{{{expr}}}**" if is_interpolation else f"**{expr}**"
        suffix = " Interpolation" if is_interpolation else ""

        if parsed_data["is_numeric"]:
            result = (
                f"{prefix} - Regex Capture Group {parsed_data['group_number']}{suffix}\n\n"
                f"**Context:** Regular Expression Capture Group\n\n"
                f"**Description:** {parsed_data['description']}\n\n"
                f"**Maps to:** `{parsed_data['maps_to']}`\n\n"
                f"**Usage:** {parsed_data['usage']}")

            if parsed_data["note"]:
                result += f"\n\n**Note:** {parsed_data['note']}"
        else:
            result = (
                f"{prefix} - Regex Capture Reference{suffix}\n\n"
                f"**Context:** Regular Expression Capture\n\n"
                f"**Description:** {parsed_data['description']}\n\n"
                f"**Usage:** {parsed_data['usage']}")

        return result


LSP_METHOD_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "method":
        DocumentationInfo(
            name="HTTP Request Method",
            description="The HTTP method (verb) used by the client for this request. Common methods include GET (retrieve data), POST (submit data), PUT (update resource), DELETE (remove resource), HEAD (get headers only), OPTIONS (check capabilities), PATCH (partial update), CONNECT (establish tunnel), and TRACE (diagnostic loopback).",
            context="HTTP Request Method",
            usage="Used for method-based routing, access control, HTTP semantics enforcement, and RESTful API handling. Essential for implementing proper HTTP behavior and security policies.",
            examples=[
                "if inbound.method == \"POST\" {\n    // Handle data submission\n}",
                "inbound.method in [\"GET\", \"HEAD\"] && inbound.url.path ~ /api/",
                "if inbound.method == \"OPTIONS\" {\n    // Handle CORS preflight\n}",
                "inbound.method in [\"PUT\", \"PATCH\", \"DELETE\"] // Mutating operations"
            ])
}

# Cookie field documentation
LSP_COOKIE_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "inbound.cookie":
        DocumentationInfo(
            name="Inbound Request Cookies",
            description="Access to HTTP cookies sent by the client in the request. Cookies are key-value pairs used for session management, user preferences, authentication tokens, and client state tracking. Each cookie can be accessed by name (e.g., inbound.cookie.sessionid).",
            context="Client Request Cookie Access",
            usage="Used for session management, user authentication, personalization, A/B testing, and maintaining client state across requests. Essential for web application functionality.",
            examples=[
                "if inbound.cookie.session == \"admin\" {\n    // Admin session detected\n}",
                "inbound.cookie.role = \"guest\"; // Set default role", "inbound.cookie.tracking = \"\"; // Delete tracking cookie",
                "if inbound.cookie.consent == \"accepted\" {\n    // User accepted cookies\n}"
            ]),
    "outbound.cookie":
        DocumentationInfo(
            name="Outbound Response Cookies",
            description="Set HTTP cookies in the response that will be sent to the client. These cookies will be stored by the client's browser and sent back in future requests. Use for session management, user preferences, and client state.",
            context="Server Response Cookie Setting",
            usage="Used to set session cookies, authentication tokens, user preferences, tracking identifiers, and other client state that needs to persist across requests.",
            examples=[
                "outbound.cookie.sessionid = \"{id.UNIQUE}\"; // Set session ID",
                "outbound.cookie.theme = \"dark\"; // Set user preference",
                "outbound.cookie.auth_token = \"{auth_token}\"; // Set auth token",
                "outbound.cookie.old_cookie = \"\"; // Delete old cookie"
            ])
}

# Regular Expression documentation - no longer used as a dataclass


class RegexPattern:
    """Parser and documentation provider for regular expression patterns."""

    # PCRE flag documentation
    FLAG_DOCUMENTATION: Final[dict[str, str]] = {
        'i': 'Case-insensitive matching - ignore letter case differences',
        'g': 'Global matching - find all matches, not just the first',
        'm': 'Multiline mode - ^ and $ match line boundaries',
        's': 'Single-line mode - . matches newline characters',
        'x': 'Extended mode - ignore whitespace and allow comments',
    }

    # Common PCRE syntax reference
    PCRE_SYNTAX_REFERENCE: Final[list[tuple[str, str]]] = [
        ('\\d', 'digit (0-9)'),
        ('\\w', 'word character (a-zA-Z0-9_)'),
        ('\\s', 'whitespace character'),
        ('+', 'one or more'),
        ('*', 'zero or more'),
        ('?', 'zero or one (optional)'),
        ('()', 'capture group'),
        ('(?:)', 'non-capturing group'),
        ('^', 'start of string/line'),
        ('$', 'end of string/line'),
        ('[abc]', 'character class'),
        ('|', 'alternation (OR)'),
    ]

    @staticmethod
    def detect_regex_pattern(line: str, character: int) -> dict[str, str] | None:
        """Detect if cursor is inside a regex pattern and extract components."""
        import re as regex_module

        # Find all potential regex boundaries
        slash_positions = []
        i = 0
        while i < len(line):
            if line[i] == '/':
                before_context = line[:i].strip()
                if (before_context.endswith('~') or ' ~ ' in before_context or '(' in before_context or
                        before_context.endswith('(') or 'if ' in before_context):
                    slash_positions.append(i)
            i += 1

        for i in range(0, len(slash_positions) - 1, 2):
            start_pos = slash_positions[i]
            end_pos = slash_positions[i + 1]

            if start_pos <= character <= end_pos:
                regex_content = line[start_pos + 1:end_pos]
                flags_match = regex_module.match(r'([gimsx]*)', line[end_pos + 1:])
                flags = flags_match.group(1) if flags_match else ""

                return {
                    'pattern': regex_content,
                    'flags': flags,
                    'full_regex': f"/{regex_content}/{flags}",
                    'start_pos': start_pos,
                    'end_pos': end_pos
                }

        return None

    @staticmethod
    def create_regex_hover(regex_data: dict[str, str], brief: bool = False) -> str:
        """Create hover documentation for regex patterns.

        Args:
            regex_data: Dictionary containing pattern, flags, full_regex, etc.
            brief: If True, show condensed documentation suitable for hover
        """
        pattern = regex_data['pattern']
        flags = regex_data['flags']
        full_regex = regex_data['full_regex']

        if brief:
            # Brief version for LSP hover - most important info first
            doc_parts = [
                f"**Regular Expression Pattern**", "", f"**Pattern:** `{full_regex}`", "",
                "**Context:** PCRE-compatible regex for string matching"
            ]

            if flags:
                doc_parts.extend(["", "**Flags:**"])
                for flag in flags:
                    if flag in RegexPattern.FLAG_DOCUMENTATION:
                        doc_parts.append(f"- `{flag}`: {RegexPattern.FLAG_DOCUMENTATION[flag]}")

            syntax_help = []
            for syntax, description in RegexPattern.PCRE_SYNTAX_REFERENCE[:6]:  # First 6 most common
                if syntax.replace('\\', '\\') in pattern:
                    syntax_help.append(f"- `{syntax}` - {description}")

            if syntax_help:
                doc_parts.extend(["", "**Pattern Elements:**"] + syntax_help)

        else:
            # Full documentation version
            doc_parts = [
                f"**Regular Expression Pattern**", "", f"**Pattern:** `{full_regex}`", "",
                "**Context:** PCRE-compatible regular expression for string matching", "",
                "**Description:** This pattern uses Perl Compatible Regular Expression (PCRE) syntax for matching strings. Common in conditions with the `~` operator for pattern matching."
            ]

            if flags:
                doc_parts.extend(["", "**Flags:**"])
                for flag in flags:
                    if flag in RegexPattern.FLAG_DOCUMENTATION:
                        doc_parts.append(f"- `{flag}`: {RegexPattern.FLAG_DOCUMENTATION[flag]}")

            # Add complete PCRE syntax help
            doc_parts.extend(["", "**Common PCRE Syntax:**"])
            for syntax, description in RegexPattern.PCRE_SYNTAX_REFERENCE:
                doc_parts.append(f"- `{syntax}` - {description}")

            doc_parts.extend(
                [
                    "", "**Usage Examples:**", "```hrw4u", "if inbound.req.User-Agent ~ /(?i)bot|crawler/ {",
                    "    // Match bot user agents case-insensitively", "}", "", "if inbound.url.path ~ /^/api/v\\d+/ {",
                    "    // Match API versioned paths", "}", "```"
                ])

        return "\n".join(doc_parts)


LSP_STATUS_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "status":
        DocumentationInfo(
            name="HTTP Status Code",
            description="The HTTP status code of the response (200, 404, 500, etc.). Can be used for both inbound and outbound contexts.",
            context="HTTP Response Status",
            usage="Used for status-based routing, error handling, and response transformation.",
            examples=[
                "if outbound.status >= 400 {\n    // Handle error response\n}",
                "if inbound.status == 200 {\n    // Success response\n}", "outbound.status in [301, 302, 307, 308]"
            ])
}

LSP_CACHE_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "cache":
        DocumentationInfo(
            name="Cache Status Function",
            description="The cache lookup result status indicating whether the requested object was found in cache and its freshness.",
            context="Cache Lookup Status",
            usage="Used for cache-based routing, debugging, and performance optimization decisions.",
            possible_values=["none", "miss", "hit-stale", "hit-fresh", "skipped"],
            examples=[
                "if cache() == \"hit-fresh\" {\n    // Serve from fresh cache\n}",
                "cache() in [\"miss\", \"hit-stale\"] && inbound.method == \"GET\""
            ])
}

LSP_HTTP_CONTROL_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "LOGGING":
        DocumentationInfo(
            name="Transaction Logging Control",
            description="Controls whether this transaction should be logged.",
            context="HTTP Transaction Control",
            default_value="on",
            usage="Used to selectively disable logging for specific transactions (e.g., health checks).",
            examples=["http.cntl.LOGGING = false", "if http.cntl.LOGGING {\n    // Logging is enabled\n}"]),
    "INTERCEPT_RETRY":
        DocumentationInfo(
            name="Intercept Retry Control",
            description="Allows intercepts to be retried on failure.",
            context="HTTP Transaction Control",
            default_value="off",
            usage="Used to enable retry logic for intercepted requests.",
            examples=["http.cntl.INTERCEPT_RETRY = true"]),
    "RESP_CACHEABLE":
        DocumentationInfo(
            name="Response Cacheable Control",
            description="Forces the response to be cacheable regardless of cache headers.",
            context="HTTP Transaction Control",
            default_value="off",
            usage="Used to override response caching decisions for specific transactions.",
            examples=["http.cntl.RESP_CACHEABLE = true"]),
    "REQ_CACHEABLE":
        DocumentationInfo(
            name="Request Cacheable Control",
            description="Forces the request to be cacheable.",
            context="HTTP Transaction Control",
            default_value="off",
            usage="Used to override request caching decisions.",
            examples=["http.cntl.REQ_CACHEABLE = true"]),
    "SERVER_NO_STORE":
        DocumentationInfo(
            name="Server No Store Control",
            description="Prevents the response from being written to cache storage.",
            context="HTTP Transaction Control",
            default_value="off",
            usage="Used to prevent specific responses from being cached.",
            examples=["http.cntl.SERVER_NO_STORE = true"]),
    "TXN_DEBUG":
        DocumentationInfo(
            name="Transaction Debug Control",
            description="Enables detailed debugging for this transaction.",
            context="HTTP Transaction Control",
            default_value="off",
            usage="Used to enable debugging output for specific transactions.",
            examples=["http.cntl.TXN_DEBUG = true", "if inbound.req.X-Debug == \"on\" {\n    http.cntl.TXN_DEBUG = true;\n}"]),
    "SKIP_REMAP":
        DocumentationInfo(
            name="Skip Remap Control",
            description="Skips remap processing for this transaction (enables open proxy mode).",
            context="HTTP Transaction Control",
            default_value="off",
            usage="Used to bypass remap rules for specific requests.",
            examples=["http.cntl.SKIP_REMAP = true"])
}

LSP_FUNCTION_DOCUMENTATION: Final[dict[str, FunctionDoc]] = {
    "access":
        FunctionDoc(
            name="Access Control Function",
            category="Security Function",
            description="Evaluates access control rules based on client information and request properties. Returns true if access should be granted, false otherwise.",
            syntax="access(rule_name)",
            maps_to="%{ACCESS:rule_name}",
            usage_context="Used in conditional expressions for access control and security filtering",
            parameters=[ParameterDoc("rule_name", "string", "Name of the access control rule to evaluate")],
            examples=[
                "if access(\"admin_only\") {\n    // Allow admin access\n}", "access(\"geo_filter\") && inbound.method == \"GET\""
            ]),
    "cache":
        FunctionDoc(
            name="Cache Status Function",
            category="Cache Function",
            description="Checks the current cache status for the request. Returns information about cache hits, misses, and cache policies.",
            syntax="cache()",
            maps_to="%{CACHE}",
            usage_context="Used in conditional expressions for cache-based routing and debugging",
            examples=["if cache() {\n    // Handle cached response\n}", "!cache() && inbound.method == \"GET\""]),
    "cidr":
        FunctionDoc(
            name="CIDR Network Function",
            category="Network Function",
            description="Generates CIDR network ranges for IP-based filtering. Creates network masks for IPv4 and IPv6 addresses.",
            syntax="cidr(ipv4_bits, ipv6_bits)",
            maps_to="%{CIDR:ipv4_bits,ipv6_bits}",
            usage_context="Used in conditional expressions for network-based access control",
            parameters=[
                ParameterDoc("ipv4_bits", "integer", "IPv4 network mask bits (1-32)"),
                ParameterDoc("ipv6_bits", "integer", "IPv6 network mask bits (1-128)")
            ],
            examples=["if inbound.ip in cidr(24, 64) {\n    // Handle local network\n}", "cidr(16, 48) && geo.country == \"US\""]),
    "internal":
        FunctionDoc(
            name="Internal Transaction Function",
            category="Transaction Function",
            description="Checks if the current transaction is internal to ATS (generated internally rather than from external clients).",
            syntax="internal()",
            maps_to="%{INTERNAL-TRANSACTION}",
            usage_context="Used in conditional expressions to distinguish internal vs external requests",
            examples=["if internal() {\n    // Handle internal request\n}", "!internal() && inbound.method == \"POST\""]),
    "random":
        FunctionDoc(
            name="Random Number Function",
            category="Utility Function",
            description="Generates a random number for load balancing, sampling, or probabilistic routing decisions.",
            syntax="random(max_value)",
            maps_to="%{RANDOM:max_value}",
            usage_context="Used in conditional expressions for probabilistic routing and load distribution",
            parameters=[ParameterDoc("max_value", "integer", "Maximum random value (32-bit unsigned integer)")],
            examples=["if random(100) < 10 {\n    // 10% sampling rate\n}", "random(1000) > 500 && inbound.method == \"GET\""]),
    "ssn-txn-count":
        FunctionDoc(
            name="Session Transaction Count Function",
            category="Session Function",
            description="Returns the number of transactions processed in the current client session.",
            syntax="ssn-txn-count()",
            maps_to="%{SSN-TXN-COUNT}",
            usage_context="Used in conditional expressions for session-based routing and rate limiting",
            examples=[
                "if ssn-txn-count() > 100 {\n    // Rate limit high-activity sessions\n}",
                "ssn-txn-count() == 1 && inbound.method == \"GET\""
            ]),
    "txn-count":
        FunctionDoc(
            name="Transaction Count Function",
            category="Transaction Function",
            description="Returns the total number of transactions processed by this ATS instance.",
            syntax="txn-count()",
            maps_to="%{TXN-COUNT}",
            usage_context="Used in conditional expressions for load balancing and health monitoring",
            examples=["if txn-count() > 1000000 {\n    // High load condition\n}", "txn-count() % 1000 == 0"]),
    "skip-remap":
        FunctionDoc(
            name="Skip Remap Function",
            category="Statement Function",
            description="Skips remap processing for the current transaction, enabling open proxy mode. When called with true, ATS will forward the request to the target host without applying remap rules.",
            syntax="skip-remap(boolean)",
            maps_to="set-transaction-hook(skip-remap)",
            usage_context="Used as a statement in code blocks to bypass remap processing",
            parameters=[ParameterDoc("enabled", "boolean", "Whether to skip remap processing (true/false)")],
            examples=["if inbound.req.X-Bypass == \"1\" {\n    skip-remap(true);\n}", "skip-remap(inbound.method == \"CONNECT\")"]),
    "set-plugin-cntl":
        FunctionDoc(
            name="Set Plugin Control Function",
            category="Statement Function",
            description="Sets plugin control parameters that affect plugin behavior. Used to configure plugin-specific settings at runtime.",
            syntax="set-plugin-cntl(parameter_name, value)",
            maps_to="set-plugin-control",
            usage_context="Used as a statement in code blocks to configure plugin behavior",
            parameters=[
                ParameterDoc("parameter_name", "string", "Name of the plugin control parameter"),
                ParameterDoc("value", "string", "Value to set for the parameter")
            ],
            examples=["set-plugin-cntl(\"TIMEZONE\", \"GMT\");", "set-plugin-cntl(\"INBOUND_IP_SOURCE\", \"PEER\");"]),
    "set-config":
        FunctionDoc(
            name="Set Configuration Function",
            category="Statement Function",
            description="Sets ATS configuration parameters at runtime for the current transaction. Allows dynamic configuration changes without restarting ATS.",
            syntax="set-config(config_name, value)",
            maps_to="set-configuration",
            usage_context="Used as a statement in code blocks to modify ATS configuration",
            parameters=[
                ParameterDoc("config_name", "string", "Name of the ATS configuration parameter"),
                ParameterDoc("value", "string|integer", "Value to set for the configuration parameter")
            ],
            examples=[
                "set-config(\"proxy.config.http.cache.http\", 0);", "set-config(\"proxy.config.http.allow_multi_range\", 1);"
            ]),
    "no-op":
        FunctionDoc(
            name="No Operation Function",
            category="Statement Function",
            description="Performs no operation. Used as a placeholder or to explicitly indicate that no action should be taken in a code block.",
            syntax="no-op()",
            maps_to="no-operation",
            usage_context="Used as a statement in code blocks for explicit no-action cases",
            examples=[
                "if inbound.method == \"OPTIONS\" {\n    no-op();\n} else {\n    // Handle other methods\n}",
                "no-op(); // Explicit no-action placeholder"
            ]),
    "set-redirect":
        FunctionDoc(
            name="Set Redirect Function",
            category="Statement Function",
            description="Sets up an HTTP redirect response with the specified status code and target URL. Terminates normal request processing and sends a redirect response to the client.",
            syntax="set-redirect(status_code, target_url)",
            maps_to="set-redirect-response",
            usage_context="Used as a statement in code blocks to redirect clients",
            parameters=[
                ParameterDoc("status_code", "integer", "HTTP redirect status code (301, 302, 307, 308)"),
                ParameterDoc("target_url", "string", "Target URL for the redirect")
            ],
            examples=[
                "set-redirect(302, \"https://example.com/new-path\");",
                "set-redirect(301, \"https://secure.example.com{inbound.url.path}\");"
            ]),
    "set-body-from":
        FunctionDoc(
            name="Set Body From URL Function",
            category="Statement Function",
            description="Sets the response body by fetching content from the specified URL. Used to replace the response body with content from an external source.",
            syntax="set-body-from(source_url)",
            maps_to="set-response-body-from-url",
            usage_context="Used as a statement in code blocks to replace response content",
            parameters=[ParameterDoc("source_url", "string", "URL to fetch the response body content from")],
            examples=[
                "set-body-from(\"https://errors.example.com/500.html\");", "set-body-from(\"http://content.example.com/body.txt\");"
            ]),
    "run-plugin":
        FunctionDoc(
            name="Run Plugin Function",
            category="Statement Function",
            description="Executes a specified plugin with optional arguments. Allows dynamic plugin execution based on runtime conditions.",
            syntax="run-plugin(plugin_path, ...args)",
            maps_to="run-plugin-with-args",
            usage_context="Used as a statement in code blocks to conditionally execute plugins",
            parameters=[
                ParameterDoc("plugin_path", "string", "Path to the plugin library (.so file)"),
                ParameterDoc("args", "string", "Optional arguments to pass to the plugin")
            ],
            examples=[
                "run-plugin(\"regex_remap.so\", \"in:^/foo/(.*)\", \"out:/bar/$1\");",
                "run-plugin(\"/opt/ats/libexec/trafficserver/rate_limit.so\", \"--limit=300\");"
            ]),
    "counter":
        FunctionDoc(
            name="Counter Function",
            category="Statement Function",
            description="Increments a named counter for monitoring and statistics. Counters can be used for debugging, monitoring, and operational visibility.",
            syntax="counter(counter_name)",
            maps_to="increment-counter",
            usage_context="Used as a statement in code blocks for monitoring and statistics",
            parameters=[ParameterDoc("counter_name", "string", "Name of the counter to increment")],
            examples=["counter(\"plugin.header_rewrite.redirects\");", "counter(\"custom.api.requests\");"]),
    "keep_query":
        FunctionDoc(
            name="Keep Query Parameters Function",
            category="Statement Function",
            description="Preserves only the specified query parameters in the URL, removing all others. Used for URL cleanup and parameter filtering.",
            syntax="keep_query(parameter_list)",
            maps_to="filter-query-parameters",
            usage_context="Used as a statement in code blocks for URL parameter management",
            parameters=[ParameterDoc("parameter_list", "string", "Comma-separated list of query parameters to keep")],
            examples=["keep_query(\"id,utm_campaign\");", "keep_query(\"page,limit,sort\");"]),
    "remove_query":
        FunctionDoc(
            name="Remove Query Parameters Function",
            category="Statement Function",
            description="Removes the specified query parameters from the URL, keeping all others. Used for URL cleanup and parameter filtering.",
            syntax="remove_query(parameter_list)",
            maps_to="remove-query-parameters",
            usage_context="Used as a statement in code blocks for URL parameter management",
            parameters=[ParameterDoc("parameter_list", "string", "Comma-separated list of query parameters to remove")],
            examples=["remove_query(\"debug,trace\");", "remove_query(\"utm_source,utm_medium\");"])
}

LSP_STRING_LITERAL_INFO: Final[dict[str, str]] = {
    "name": "String Literal",
    "description":
        "String values in HRW4U support variable interpolation using {variable} syntax. Variables and function calls can be embedded within strings for dynamic content generation. Strings must be enclosed in double quotes when they contain spaces or special characters."
}

# Remove redundant LSP_HEADER_CONTEXTS - now covered by LSP_SUB_NAMESPACE_DOCUMENTATION

# Unified field documentation using DocumentationInfo
LSP_FIELD_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "inbound.resp.body":
        DocumentationInfo(
            name="Inbound Response Body",
            description="Sets the response body content that will be sent to the client. Can be used to provide custom response content.",
            context="Response Body Assignment",
            usage="Used to set custom response body content in response processing sections",
            examples=["inbound.resp.body = \"Custom error message\";", "inbound.resp.body = \"I am a teapot, rewritten\";"]),
    "outbound.resp.body":
        DocumentationInfo(
            name="Outbound Response Body",
            description="Sets the response body content received from upstream servers before forwarding to clients.",
            context="Response Body Assignment",
            usage="Used to modify upstream response body content before client delivery",
            examples=["outbound.resp.body = \"Modified upstream content\";", "outbound.resp.body = template_content;"]),
    "http.status":
        DocumentationInfo(
            name="HTTP Status Code",
            description="Sets the HTTP status code for the response. Can be used to override the default or upstream status code.",
            context="HTTP Status Control",
            usage="Used to set custom HTTP status codes in response processing",
            examples=["http.status = 404;", "http.status = 200;"]),
    "http.status.reason":
        DocumentationInfo(
            name="HTTP Status Reason",
            description="Sets the HTTP status reason phrase that accompanies the status code in the response line.",
            context="HTTP Status Control",
            usage="Used to set custom HTTP status reason phrases",
            examples=["http.status.reason = \"Not Found\";", "http.status.reason = \"Go Away\";"]),
    "break":
        DocumentationInfo(
            name="Break Statement",
            description="Terminates execution of the current rule block, similar to 'last' in Apache mod_rewrite. Prevents further rule processing.",
            context="Control Flow",
            usage="Used to stop processing additional rules in the current section",
            examples=[
                "if inbound.req.X-Bypass == \"1\" {\n    skip-remap(true);\n    break;\n}", "break; // Stop processing more rules"
            ])
}

# Unified modifier documentation using DocumentationInfo
LSP_MODIFIER_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "NOCASE":
        DocumentationInfo(
            name="Case Insensitive Matching",
            description="Performs case-insensitive string matching, ignoring differences between uppercase and lowercase letters.",
            context="String Comparison Modifier",
            usage="Used in string equality and regex matching conditions to ignore case differences.",
            examples=[
                "if (inbound.req.User-Agent == \"mozilla\" with NOCASE) {\n    // Matches Mozilla, MOZILLA, mozilla, etc.\n}",
                "inbound.cookie.session == \"ADMIN\" with NOCASE",
                "if (inbound.url.host ~ /example\\.com/ with NOCASE) {\n    // Case-insensitive regex match\n}"
            ]),
    "MID":
        DocumentationInfo(
            name="Middle String Matching",
            description="Matches if the pattern appears anywhere within the target string (substring matching).",
            context="String Matching Modifier",
            usage="Used to find patterns that occur anywhere within a string, not just at the beginning or end.",
            examples=[
                "if (inbound.req.User-Agent ~ /mobile/ with MID) {\n    // Matches if 'mobile' appears anywhere\n}",
                "inbound.url.path == \"api\" with MID  // Matches /some/api/path",
                "if (inbound.req.Referer == \"google\" with MID,NOCASE) {\n    // Case-insensitive substring match\n}"
            ]),
    "PRE":
        DocumentationInfo(
            name="Prefix String Matching",
            description="Matches if the target string starts with the specified pattern (prefix matching).",
            context="String Matching Modifier",
            usage="Used to match patterns that must appear at the beginning of a string.",
            examples=[
                "if (inbound.url.path == \"/api\" with PRE) {\n    // Matches /api/v1/users, /api/data, etc.\n}",
                "inbound.req.User-Agent == \"Mozilla\" with PRE",
                "if (inbound.url.host == \"api\" with PRE,NOCASE) {\n    // Case-insensitive prefix match\n}"
            ]),
    "SUF":
        DocumentationInfo(
            name="Suffix String Matching",
            description="Matches if the target string ends with the specified pattern (suffix matching).",
            context="String Matching Modifier",
            usage="Used to match patterns that must appear at the end of a string.",
            examples=[
                "if (inbound.url.path == \".json\" with SUF) {\n    // Matches /data.json, /api/users.json, etc.\n}",
                "inbound.req.Accept == \"json\" with SUF",
                "if (inbound.url.host == \".com\" with SUF,NOCASE) {\n    // Case-insensitive suffix match\n}"
            ]),
    "EXT":
        DocumentationInfo(
            name="File Extension Matching",
            description="Matches file extensions in the target string, such as .php, .cc, .html, etc.",
            context="File Extension Matching Modifier",
            usage="Used to match file extensions in URLs, paths, or filenames.",
            examples=[
                "if (inbound.url.path == \".php\" with EXT) {\n    // Matches PHP files\n}",
                "inbound.url.path == \".html\" with EXT",
                "if (inbound.url.path == \".CC\" with EXT,NOCASE) {\n    // Case-insensitive extension match\n}"
            ]),
}


class ModifierPattern:
    """Parser and documentation provider for condition modifiers."""

    @staticmethod
    def detect_modifier_list(line: str, character: int) -> dict[str, str] | None:
        """Detect if cursor is within a modifier list and extract components."""
        import re

        # Find 'with' keyword and following modifiers - more flexible ending
        with_pattern = r'\bwith\s+([A-Z,\s]+?)(?=\s*[\)\{\s]|$)'

        for match in re.finditer(with_pattern, line, re.IGNORECASE):
            start_pos = match.start(1)
            end_pos = match.end(1)

            if start_pos <= character <= end_pos:
                modifier_text = match.group(1).strip()
                modifiers = [m.strip().upper() for m in modifier_text.split(',')]

                # Find which specific modifier the cursor is on
                current_pos = start_pos
                for modifier in modifiers:
                    modifier_end = current_pos + len(modifier)
                    if current_pos <= character <= modifier_end:
                        return {
                            'current_modifier': modifier,
                            'all_modifiers': modifiers,
                            'modifier_text': modifier_text,
                            'start_pos': start_pos,
                            'end_pos': end_pos
                        }
                    # Move past this modifier and comma
                    current_pos = modifier_end + 1
                    while current_pos < len(line) and line[current_pos] in ', ':
                        current_pos += 1

        return None

    @staticmethod
    def create_modifier_hover(modifier_data: dict[str, str]) -> str:
        """Create hover documentation for condition modifiers."""
        current_modifier = modifier_data['current_modifier']
        all_modifiers = modifier_data['all_modifiers']

        if current_modifier in LSP_MODIFIER_DOCUMENTATION:
            mod_doc = LSP_MODIFIER_DOCUMENTATION[current_modifier]

            sections = [
                f"**{current_modifier}** - {mod_doc.name}", "", f"**Context:** {mod_doc.context}", "",
                f"**Description:** {mod_doc.description}", "", f"**Usage:** {mod_doc.usage}"
            ]

            if mod_doc.examples:
                sections.extend(["", "**Examples:**"])
                for example in mod_doc.examples:
                    sections.append(f"```hrw4u\n{example}\n```")

            # Add info about combined modifiers if multiple are present
            if len(all_modifiers) > 1:
                sections.extend(["", f"**Combined with:** {', '.join([m for m in all_modifiers if m != current_modifier])}"])

            return "\n".join(sections)

        else:
            return (
                f"**{current_modifier}** - Condition Modifier\n\n"
                f"**Context:** HRW4U Condition Modifier\n\n"
                f"Used with the 'with' keyword to modify condition behavior.\n\n"
                f"**Available modifiers:** {', '.join(LSP_MODIFIER_DOCUMENTATION.keys())}")


# Unified Namespace Documentation using DocumentationInfo
LSP_NAMESPACE_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "geo":
        DocumentationInfo(
            name="Geographic Information Namespace",
            context="Client Geographic Data",
            description="Access geographic information about the client's location based on IP geolocation. "
            "Provides country codes, ASN information and other location-based data for routing and compliance decisions.",
            available_items=list(GEO_FIELDS.keys()),
            usage="Used for geographic routing, content localization, and compliance with regional regulations.",
            examples=["if geo.COUNTRY == \"US\" {\n    // Handle US traffic\n}", "geo.ASN == 15169  // Google's ASN"]),
    "id":
        DocumentationInfo(
            name="Transaction Identifier Namespace",
            context="Transaction and Session Identifiers",
            description="Access unique identifiers for transactions, requests, processes, threads, and sessions. "
            "Essential for tracking, debugging, and correlation across distributed systems.",
            available_items=list(ID_FIELDS.keys()),
            usage="Used for transaction tracking, debugging, logging correlation, and session management.",
            examples=["inbound.resp.X-Request-ID = \"{id.REQUEST}\"", "if id.SESSION {\n    // Session-based processing\n}"]),
    "now":
        DocumentationInfo(
            name="Current Time Namespace",
            context="Current Date and Time Information",
            description="Access current time components for time-based routing, scheduling, and conditional logic. "
            "All values are based on the server's current time when the request is processed.",
            available_items=list(TIME_FIELDS.keys()),
            usage="Used for time-based routing, load balancing, maintenance windows, and temporal logic.",
            examples=["if now.HOUR >= 22 || now.HOUR < 6 {\n    // Night time processing\n}", "now.WEEKDAY == 0  // Sunday"]),
    "inbound":
        DocumentationInfo(
            name="Inbound Request Context",
            context="Client Request Processing",
            description="Access to incoming request data from clients. This includes request headers, "
            "URL components, HTTP method, cookies, connection information, and client certificates.",
            available_items=["req", "resp", "method", "status", "url", "cookie", "conn", "ip"],
            usage="Used for request analysis, routing decisions, authentication, and client behavior processing.",
            examples=["inbound.method == \"POST\"", "inbound.req.User-Agent ~ /mobile/",
                      "inbound.url.host == \"api.example.com\""]),
    "outbound":
        DocumentationInfo(
            name="Outbound Request Context",
            context="Upstream Server Communication",
            description="Access to outbound request data sent to upstream servers and responses received back. "
            "This includes request/response headers, status codes, and server connection information.",
            available_items=["req", "resp", "status", "ip", "conn"],
            usage="Used for upstream communication control, response processing, and server selection.",
            examples=[
                "outbound.req.Authorization = \"Bearer {token}\"", "if outbound.status >= 500 {\n    // Handle server errors\n}"
            ]),
    "client":
        DocumentationInfo(
            name="Client Information Namespace",
            context="Client Connection and Certificate Data",
            description="Access to client-specific information including client certificates in mTLS scenarios, "
            "client IP addresses, and other client connection metadata.",
            available_items=["cert", "ip", "conn"],
            usage="Used for client authentication, authorization, certificate-based routing, and client identification.",
            examples=["if client.cert.SUBJECT ~ /CN=admin/ {\n    // Admin client detected\n}", "client.ip == \"192.168.1.100\""]),
    "http":
        DocumentationInfo(
            name="HTTP Transaction Control Namespace",
            context="HTTP Transaction Processing Control",
            description="Access to HTTP transaction control fields and status information. "
            "This includes transaction control settings, HTTP status codes, and processing directives.",
            available_items=["cntl", "status"],
            usage="Used for fine-grained control of HTTP transaction processing, status manipulation, and ATS behavior control.",
            examples=["http.cntl.LOGGING = false", "http.status = 418", "http.status.reason = \"I'm a teapot\""])
}

# Combined namespace and sub-namespace documentation using unified structure
LSP_SUB_NAMESPACE_DOCUMENTATION: Final[dict[str, DocumentationInfo]] = {
    "inbound.conn":
        DocumentationInfo(
            name="Inbound Connection Properties",
            context="Client Connection Information",
            description="Access to properties of the client connection including protocol information (TLS, HTTP/2), "
            "IP family details (IPv4/IPv6), address information (local/remote), port numbers, and certificate data. "
            "Essential for connection-based routing and security decisions.",
            available_items=[
                "TLS", "H2", "IPV4", "IPV6", "IP-FAMILY", "STACK", "LOCAL-ADDR", "LOCAL-PORT", "REMOTE-ADDR", "REMOTE-PORT",
                "client-cert", "server-cert", "DSCP", "MARK"
            ],
            usage="Used for protocol-specific routing, security policy enforcement, certificate validation, "
            "and connection analysis in client request processing.",
            examples=[
                "if inbound.conn.TLS {\n    // Handle TLS connections\n}", "inbound.conn.H2 == \"h2\" && inbound.method == \"GET\"",
                "inbound.resp.X-Client-IP = \"{inbound.conn.REMOTE-ADDR}\""
            ]),
    "outbound.conn":
        DocumentationInfo(
            name="Outbound Connection Properties",
            context="Upstream Server Connection Information",
            description="Access to properties of the upstream server connection including protocol stack, "
            "address information, and connection characteristics. Used for server selection decisions "
            "and upstream communication control.",
            available_items=[
                "TLS", "H2", "IPV4", "IPV6", "IP-FAMILY", "STACK", "LOCAL-ADDR", "LOCAL-PORT", "REMOTE-ADDR", "REMOTE-PORT",
                "server-cert", "DSCP", "MARK"
            ],
            usage="Used for upstream server routing, protocol negotiation, load balancing decisions, "
            "and server connection analysis.",
            examples=[
                "if outbound.conn.TLS {\n    // Secure upstream connection\n}", "outbound.conn.REMOTE-ADDR == \"10.0.1.100\"",
                "outbound.req.X-Forwarded-Proto = outbound.conn.TLS ? \"https\" : \"http\""
            ]),
    "inbound.req":
        DocumentationInfo(
            name="Inbound Request Headers",
            context="Client Request Header Access",
            description="Access to HTTP headers sent by the client in the incoming request. "
            "Headers contain client-provided information such as User-Agent, Accept, Authorization, "
            "Content-Type, custom headers, and other request metadata essential for routing and processing.",
            available_items=[
                "User-Agent", "Accept", "Authorization", "Content-Type", "Content-Length", "Host", "Referer", "Cookie",
                "X-Forwarded-For", "Accept-Language", "Accept-Encoding"
            ],
            usage="Used for request routing, client identification, authentication, content negotiation, "
            "and request analysis based on client-provided header information.",
            examples=[
                "if inbound.req.User-Agent ~ /mobile/ {\n    // Mobile client detected\n}", "inbound.req.Authorization ~ /Bearer/",
                "inbound.req.X-API-Key != \"\""
            ]),
    "inbound.resp":
        DocumentationInfo(
            name="Inbound Response Headers",
            context="Client Response Header Control",
            description="Control HTTP headers that will be sent back to the client in the response. "
            "These headers control client behavior, caching policies, security settings, CORS, "
            "and custom response metadata. Essential for client communication and security.",
            available_items=[
                "Content-Type", "Content-Length", "Cache-Control", "Set-Cookie", "Location", "Access-Control-Allow-Origin",
                "X-Frame-Options", "Content-Security-Policy", "X-Custom-Header", "Strict-Transport-Security"
            ],
            usage="Used to set response headers for cache control, security policies, CORS configuration, "
            "custom response information, and client behavior control.",
            examples=[
                "inbound.resp.X-Powered-By = \"Apache Traffic Server\"", "inbound.resp.Access-Control-Allow-Origin = \"*\"",
                "inbound.resp.Cache-Control = \"max-age=3600\""
            ]),
    "outbound.req":
        DocumentationInfo(
            name="Outbound Request Headers",
            context="Upstream Request Header Control",
            description="Control HTTP headers sent to upstream servers when forwarding requests. "
            "Used to modify, add, or remove headers before sending requests to backend services. "
            "Essential for upstream authentication, load balancing hints, and server communication.",
            available_items=[
                "User-Agent", "Authorization", "Content-Type", "Content-Length", "Host", "X-Forwarded-For", "X-Forwarded-Proto",
                "X-Real-IP", "X-Request-ID", "Accept"
            ],
            usage="Used for upstream authentication forwarding, request modification, load balancing, "
            "backend communication control, and request header transformation.",
            examples=[
                "outbound.req.Authorization = \"Bearer {api_token}\"",
                "outbound.req.X-Forwarded-For = \"{inbound.conn.REMOTE-ADDR}\"", "outbound.req.User-Agent = \"ATS-Proxy/9.0\""
            ]),
    "outbound.resp":
        DocumentationInfo(
            name="Outbound Response Headers",
            context="Upstream Response Header Processing",
            description="Access and modify HTTP headers received from upstream servers before forwarding "
            "to clients. Used for response transformation, header filtering, security header injection, "
            "and backend response processing before client delivery.",
            available_items=[
                "Content-Type", "Content-Length", "Cache-Control", "Set-Cookie", "Location", "Server", "X-Powered-By", "Expires",
                "Last-Modified", "ETag"
            ],
            usage="Used for response header transformation, security header filtering, cache control modification, "
            "and upstream response processing before forwarding to clients.",
            examples=[
                "outbound.resp.Server = \"\"  // Remove server header",
                "if outbound.resp.Cache-Control == \"\" {\n    outbound.resp.Cache-Control = \"no-cache\";\n}",
                "outbound.resp.X-Backend-Server = \"{outbound.conn.REMOTE-ADDR}\""
            ]),
    "inbound.url":
        DocumentationInfo(
            name="Inbound URL Components",
            context="Client Request URL Analysis",
            description="Access components of the incoming request URL including hostname, port, path, "
            "query parameters, scheme, and fragment. Essential for URL-based routing, path analysis, "
            "and request processing decisions.",
            available_items=["host", "port", "path", "query", "scheme", "fragment"],
            usage="Used for hostname-based routing, path analysis, query parameter processing, "
            "protocol detection, and URL-based conditional logic.",
            examples=[
                "if inbound.url.host == \"api.example.com\" {\n    // API subdomain routing\n}",
                "inbound.url.path ~ /^\\/api\\/v[0-9]+\\//", "inbound.url.query ~ /debug=true/"
            ]),
    "outbound.url":
        DocumentationInfo(
            name="Outbound URL Components",
            context="Upstream Request URL Control",
            description="Control components of the URL sent to upstream servers. Used for URL rewriting, "
            "path transformation, query parameter modification, and upstream routing decisions.",
            available_items=["host", "port", "path", "query", "scheme", "fragment"],
            usage="Used for upstream URL rewriting, path transformation, server selection, "
            "query parameter manipulation, and backend routing control.",
            examples=[
                "outbound.url.host = \"backend.internal.com\"", "outbound.url.path = \"/v2\" + inbound.url.path",
                "outbound.url.query = inbound.url.query + \"&source=ats\""
            ]),
    "inbound.cookie":
        DocumentationInfo(
            name="Inbound Request Cookies",
            context="Client Cookie Access",
            description="Access HTTP cookies sent by the client in the request. Cookies are key-value pairs "
            "used for session management, user preferences, authentication tokens, and client state tracking. "
            "Each cookie can be accessed by name (e.g., inbound.cookie.sessionid).",
            available_items=["sessionid", "auth_token", "user_pref", "tracking_id", "consent", "theme", "language"],
            usage="Used for session management, user authentication, personalization, A/B testing, "
            "and maintaining client state across requests.",
            examples=[
                "if inbound.cookie.session == \"admin\" {\n    // Admin session detected\n}", "inbound.cookie.tracking_id != \"\"",
                "if inbound.cookie.consent == \"accepted\" {\n    // User accepted cookies\n}"
            ]),
    "outbound.cookie":
        DocumentationInfo(
            name="Outbound Response Cookies",
            context="Client Cookie Setting",
            description="Set HTTP cookies in the response that will be sent to the client. "
            "These cookies will be stored by the client's browser and sent back in future requests. "
            "Use for session management, user preferences, and client state persistence.",
            available_items=["sessionid", "auth_token", "user_pref", "tracking_id", "theme", "language", "consent"],
            usage="Used to set session cookies, authentication tokens, user preferences, tracking identifiers, "
            "and other client state that needs to persist across requests.",
            examples=[
                "outbound.cookie.sessionid = \"{id.UNIQUE}\"", "outbound.cookie.theme = \"dark\"",
                "outbound.cookie.auth_token = \"{generated_token}\""
            ])
}
