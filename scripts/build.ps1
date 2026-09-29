[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release",

    # Which product to build. Both come from this one tree; see the Brand
    # property in SZK.vcxproj.
    [ValidateSet("numbanine", "less")]
    [string] $Brand = "numbanine",

    [switch] $Rebuild,
    [switch] $Run,
    [switch] $StopRunning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot "SZK.sln"
$targetName = if ($Brand -eq "less") { "Less" } else { "numbanine" }
$applicationPath = Join-Path $repositoryRoot "$Configuration\$targetName.exe"
$layoutVerifier = Join-Path $PSScriptRoot "verify-source-layout.ps1"
$translationVerifier = Join-Path $PSScriptRoot "verify-translations.py"
$uiStringVerifier = Join-Path $PSScriptRoot "verify-ui-strings.py"

function Find-MSBuild {
    $command = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $installerRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    $vswhere = Join-Path $installerRoot "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installation) {
            $candidate = Join-Path $installation.Trim() "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw "MSBuild.exe was not found. Install Visual Studio 2022 with Desktop development with C++."
}

if ($StopRunning -and (Test-Path -LiteralPath $applicationPath)) {
    $resolvedApplication = [IO.Path]::GetFullPath($applicationPath)
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ExecutablePath -and
            [IO.Path]::GetFullPath($_.ExecutablePath) -ieq $resolvedApplication
        } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
}

$msbuild = Find-MSBuild
$target = if ($Rebuild) { "Rebuild" } else { "Build" }

Write-Host "Verifying source layout..."
& $layoutVerifier

# Python is not required to build, so a machine without it skips this rather
# than failing: the check is a lint over the translation table, not a step the
# compiler depends on.
$python = @("python", "python3", "py") |
    ForEach-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if ($python) {
    Write-Host "Verifying translations..."
    & $python.Source $translationVerifier $repositoryRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Translation table has keys nothing looks up."
    }

    & $python.Source $uiStringVerifier $repositoryRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Text is drawn to screen without a translation."
    }
}
else {
    Write-Host "Skipping translation check (no Python on PATH)."
}

# keyauth_secrets.h is pulled in through __has_include. If keyauth.cpp was ever
# compiled while the file was absent, MSBuild records no dependency on it, so
# adding or restoring it later never triggers a recompile and the exe keeps
# saying "no licence server configured". Drop the object whenever the secrets
# file is newer than it, or whenever it is missing from a stale build.
$secretsPath = Join-Path $repositoryRoot "src\backend\keyauth_secrets.h"
$keyauthObject = Join-Path $repositoryRoot "$Configuration\obj\$Brand\keyauth.obj"
if ((Test-Path $secretsPath) -and (Test-Path $keyauthObject) -and
    ((Get-Item $secretsPath).LastWriteTimeUtc -gt (Get-Item $keyauthObject).LastWriteTimeUtc -or
     -not (Select-String -Path $keyauthObject -Pattern "keyauth_secrets" -SimpleMatch -Quiet))) {
    Remove-Item -LiteralPath $keyauthObject -Force
}

Write-Host "Building $targetName ($Configuration|x64)..."
& $msbuild $solutionPath "/t:$target" "/p:Configuration=$Configuration" "/p:Platform=x64" `
    "/p:Brand=$Brand" "/m" "/nologo" "/v:minimal"
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if ($Run) {
    if (-not (Test-Path -LiteralPath $applicationPath)) {
        throw "The application executable was not produced: $applicationPath"
    }
    Start-Process -FilePath $applicationPath -WorkingDirectory $repositoryRoot
}

Write-Host "Build completed successfully."
