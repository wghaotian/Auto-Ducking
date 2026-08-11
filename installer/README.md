# Auto Ducking installer release workflow

`AutoDucking.iss` installs the UI for the current user, creates a Start menu shortcut, offers an optional desktop shortcut, and registers an uninstaller. It packages only `auto-ducking-ui.exe` and `README.md`.

## Signed public release

1. Install a trusted code-signing certificate with an accessible private key in `Cert:\CurrentUser\My`.
2. Record its thumbprint. Do not commit a `.pfx` file or its password to this repository.
3. Build, sign, verify, package, sign the installer, verify it, and create a SHA-256 checksum with:

   ```powershell
   .\scripts\package-release.ps1 -Version 0.2.0 -CertificateThumbprint '<thumbprint>'
   ```

The signed release assets are written to `dist\`:

```text
Auto-Ducking-Setup-v0.2.0-x64.exe
SHA256SUMS-Auto-Ducking-Setup-v0.2.0-x64.txt
```

Use a new version for every release. The script deliberately refuses to overwrite an existing release asset.

## Local installer test only

To test installer construction without a certificate:

```powershell
.\scripts\package-release.ps1 -Version 0.2.0 -SkipSigning
```

This creates files ending in `-unsigned-test`. Do not publish them.

## 中文说明

`AutoDucking.iss` 会为当前用户安装 UI、创建开始菜单快捷方式、提供可选桌面快捷方式并注册卸载程序。安装包只包含 `auto-ducking-ui.exe` 和 `README.md`。

发布正式版本前，请将带私钥的可信代码签名证书安装到 `Cert:\CurrentUser\My`，然后使用证书指纹运行上述“Signed public release”命令。请勿将 `.pfx` 文件或密码提交到仓库。脚本会依次构建、签名主程序、验证签名、制作安装包、签名安装包、验证签名并生成 SHA-256 校验文件。

`-SkipSigning` 只用于本地验证安装包，生成的 `-unsigned-test` 文件不能上传到 GitHub Release。每个版本只能生成一次同名正式资产；脚本会拒绝覆盖已有发布文件。
