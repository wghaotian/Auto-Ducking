[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.2.0',

    [string]$CertificateThumbprint,

    [string]$TimestampUrl = 'https://timestamp.digicert.com',

    [switch]$SkipSigning
)

$ErrorActionPreference = 'Stop'

if ($SkipSigning -and $CertificateThumbprint) {
    throw 'Use either -SkipSigning or -CertificateThumbprint, not both.'
}
if (-not $SkipSigning -and -not $CertificateThumbprint) {
    throw 'A trusted code-signing certificate thumbprint is required. Use -SkipSigning only for local installer testing.'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$installerScript = Join-Path $repositoryRoot 'installer\AutoDucking.iss'
$releaseDirectory = Join-Path $repositoryRoot 'dist'
$uiExecutable = Join-Path $repositoryRoot 'build\Release\auto-ducking-ui.exe'
$assetBaseName = if ($SkipSigning) {
    "Auto-Ducking-Setup-v$Version-x64-unsigned-test"
} else {
    "Auto-Ducking-Setup-v$Version-x64"
}
$installerExecutable = Join-Path $releaseDirectory "$assetBaseName.exe"
$checksums = Join-Path $releaseDirectory "SHA256SUMS-$assetBaseName.txt"
$innoCompiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
$signTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (-not (Test-Path -LiteralPath $innoCompiler)) {
    throw 'Inno Setup 6 (ISCC.exe) was not found.'
}
if (-not (Test-Path -LiteralPath $installerScript)) {
    throw 'installer\AutoDucking.iss was not found.'
}
if (-not $SkipSigning -and -not $signTool) {
    throw 'Windows SDK SignTool.exe was not found.'
}

if (-not $SkipSigning) {
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint" -ErrorAction SilentlyContinue
    if ($null -eq $certificate -or -not $certificate.HasPrivateKey) {
        throw 'The supplied CurrentUser\\My certificate was not found or has no accessible private key.'
    }
}

if (-not (Test-Path -LiteralPath $releaseDirectory)) {
    New-Item -ItemType Directory -Path $releaseDirectory | Out-Null
}
if (Test-Path -LiteralPath $installerExecutable) {
    throw "Release asset already exists: $installerExecutable. Choose a new version or remove it deliberately."
}
if (Test-Path -LiteralPath $checksums) {
    throw "Checksum file already exists: $checksums. Choose a new version or remove it deliberately."
}

& $buildScript
if ($LASTEXITCODE -ne 0) {
    throw 'Application build or tests failed.'
}
if (-not (Test-Path -LiteralPath $uiExecutable)) {
    throw 'The Release UI executable was not produced.'
}

function Sign-ReleaseFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    & $signTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 /d 'Auto Ducking' $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Signing failed: $Path"
    }
    & $signTool verify /pa $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Signature verification failed: $Path"
    }
}

if (-not $SkipSigning) {
    Sign-ReleaseFile -Path $uiExecutable
}

& $innoCompiler "/DAppVersion=$Version" "/DOutputBaseFilename=$assetBaseName" $installerScript
if ($LASTEXITCODE -ne 0) {
    throw 'Inno Setup compilation failed.'
}
if (-not (Test-Path -LiteralPath $installerExecutable)) {
    throw 'The installer was not produced.'
}

if (-not $SkipSigning) {
    Sign-ReleaseFile -Path $installerExecutable
}

@(
    Get-FileHash -Algorithm SHA256 -LiteralPath $installerExecutable |
        ForEach-Object { "{0} *{1}" -f $_.Hash, (Split-Path -Leaf $_.Path) }
) | Set-Content -Encoding ascii -LiteralPath $checksums

Write-Host "Installer: $installerExecutable"
Write-Host "Checksums: $checksums"
if ($SkipSigning) {
    Write-Warning 'Unsigned test installer created. Do not upload it as a public release asset.'
} else {
    Write-Host 'Release binaries were signed and verified.'
}
