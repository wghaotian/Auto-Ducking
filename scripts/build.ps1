param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

# Some launcher environments inject both `Path` and `PATH`. Older MSBuild
# versions copy environment variables into a case-insensitive dictionary and
# fail on that duplicate, so normalize this process to one spelling.
$originalProcessPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path = $originalProcessPath

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$visualStudio = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $visualStudio) {
    throw 'Visual Studio C++ Build Tools were not found.'
}

$cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'The Visual Studio bundled CMake executable was not found.'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot 'build'

& $cmake -S $repositoryRoot -B $buildDirectory -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& $cmake --build $buildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

& $cmake --build $buildDirectory --config $Configuration --target RUN_TESTS
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

Write-Host "Built CLI: $buildDirectory\$Configuration\auto-mixer-diagnostics.exe"
Write-Host "Built meter: $buildDirectory\$Configuration\auto-mixer-process-meter.exe"
Write-Host "Built UI:  $buildDirectory\$Configuration\auto-mixer-ui.exe"
