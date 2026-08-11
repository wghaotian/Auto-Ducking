# Code signing policy

This policy takes effect once Auto Ducking is accepted into the SignPath Foundation program. Until then, no release artifact is represented as signed by SignPath.

Once approved: **Free code signing provided by SignPath.io, certificate by SignPath Foundation.**

## Team roles

- Committer, reviewer, and approver: [@wghaotian](https://github.com/wghaotian)
- Only the repository owner may approve a release-signing request.

## Release process

- Release artifacts must be built from the Auto Ducking source repository by a GitHub-hosted GitHub Actions workflow.
- Release signing is restricted to reviewed release tags and the protected default branch configured in SignPath.
- Only binaries built from this repository's source and build scripts may be submitted for signing.
- The signing workflow signs the UI executable before assembling the Inno Setup installer, then signs the installer itself.

## Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.

Auto Ducking reads selected application output buffers only to calculate an in-memory peak value. It does not save, decode, upload, or transmit captured audio, and it does not access the microphone.
