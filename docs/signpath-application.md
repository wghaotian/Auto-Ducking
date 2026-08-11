# SignPath Foundation application and first-release checklist

## Before applying

- [x] Publish source code in a public GitHub repository.
- [x] Add an OSI-approved license: MIT.
- [x] Document application functionality in `README.md`.
- [x] Add a [Code signing policy](../CODE_SIGNING_POLICY.md).
- [ ] Enable two-factor authentication for `@wghaotian` and every future repository maintainer.
- [ ] Enable a protected default branch and require review before release changes are merged.
- [ ] Create a public first release in the same format that will later be signed.

## First unsigned release

SignPath requires a project to be released before it can receive free signing. Create a GitHub Release from a reviewed tag, for example `v0.2.0`.

Attach only release assets built from the current source:

```text
Auto-Ducking-Setup-v0.2.0-x64.exe
SHA256SUMS-Auto-Ducking-Setup-v0.2.0-x64.txt
```

If the installer is unsigned, state that clearly in the release notes and tell users to verify the SHA-256 checksum. Never upload files with the `-unsigned-test` suffix as a public release.

## Apply to SignPath Foundation

Open <https://signpath.org/apply.html> and provide:

- the public repository URL;
- the public release URL;
- the MIT license URL;
- the README URL;
- the [Code signing policy](../CODE_SIGNING_POLICY.md) URL;
- confirmation that `@wghaotian` owns and maintains the source and release process.

SignPath Foundation will review the project. If approved, install the SignPath GitHub App for this repository and create the project, signing policy, artifact configurations, and API token in SignPath.

## Configure GitHub after approval

Add the following **Actions secrets** to the repository; do not commit them to source control:

```text
SIGNPATH_API_TOKEN
SIGNPATH_ORGANIZATION_ID
SIGNPATH_PROJECT_SLUG
SIGNPATH_RELEASE_POLICY_SLUG
SIGNPATH_UI_ARTIFACT_CONFIGURATION
SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION
```

Then update `.github/workflows/signpath-release.yml` with the approved release tag policy and enable the workflow. The workflow submits two artifacts: first the UI executable, then the Inno Setup installer that embeds the signed UI executable.

For each signed release, verify the Authenticode signatures and upload only the signed installer and its new SHA-256 checksum to GitHub Releases.
