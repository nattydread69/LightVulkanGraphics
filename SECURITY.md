# Security Policy

## Supported Versions

This project is currently in early development. Security updates will normally be applied to the latest public version only.

| Version        | Supported |
|----------------|-----------|
| latest         | Yes       |
| older versions | No        |

As the project matures and stable releases are made, this policy may be updated to define longer-term support for specific release branches.

## Reporting a Vulnerability

If you discover a security vulnerability, please do **not** open a public GitHub issue.

Instead, please report it privately by emailing:

NathanaelInkson@gmail.com

Please include as much detail as possible, including:

- a description of the vulnerability;
- steps to reproduce the issue;
- affected versions or commits, if known;
- any relevant logs, crash output, screenshots, or proof-of-concept code;
- whether the issue is already public or privately discovered.

## Scope

Security issues may include, but are not limited to:

- memory safety bugs;
- crashes caused by malformed input;
- shader, asset, or file-loading vulnerabilities;
- build-system or dependency-related vulnerabilities;
- unsafe handling of user-provided data;
- undefined behaviour that could plausibly be exploitable.

General bugs, feature requests, build failures, and performance issues should be reported through normal GitHub issues.

## Response Expectations

I will aim to acknowledge valid security reports within a reasonable time. Since this is a small open-source project, response times may vary depending on availability.

Once a report has been reviewed, I will try to:

1. confirm whether the issue is reproducible;
2. assess the severity and affected versions;
3. prepare a fix where appropriate;
4. publish a security advisory or release note if needed.

## Disclosure

Please allow reasonable time for a fix before publicly disclosing the vulnerability.

If the issue is accepted, credit may be given in the release notes or advisory unless you prefer to remain anonymous.

## Dependencies

This project may depend on third-party libraries and tools such as Vulkan SDK components, GLM, CMake, shader compilers, or platform-specific graphics/runtime libraries.

If the vulnerability is in a third-party dependency, please report it to the relevant upstream project as well.