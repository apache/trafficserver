# Security Policy

This is a project of the [Apache Software Foundation](https://apache.org/) and follows the ASF [vulnerability handling process](https://apache.org/security/#vulnerability-handling).

We strongly encourage folks to report such problems to our private security mailing list first, before disclosing them publicly.

# Reporting a Vulnerability

To report a new vulnerability you have discovered please follow the ASF [vulnerability reporting process](https://apache.org/security/#reporting-a-vulnerability).

# Security Model

Administrative users are always considered to be trusted. Reports for vulnerabilities where an attacker already has access to or control over any of the following will be rejected:
- Traffic Server binaries and/or scripts.
- Traffic Server configuration files.

Security-sensitive information may be logged with modified logging configurations, particularly if debug logging is enabled.

Experimental features are known unstable and not supposed to be used on production. We do not consider
vulnerabilities in those as security issues. This explicitly includes HTTP/3 and QUIC support, which remain
experimental. You may report vulnerabilities in those publicly on our public lists or GitHub. However, please
contact us privately, if you believe the vulnerabilities you find are serious, or if you are not sure whether you should report the
vulnerabilities publicly.

Plugins shipped with Traffic Server, including those under `plugins/experimental/`, are in scope for security
reporting. Please report vulnerabilities in those through the private security mailing list following the process above.
