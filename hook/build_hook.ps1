param([string]$OutputRoot = '')
$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $PSScriptRoot "src"
if (-not $OutputRoot) { $OutputRoot = Join-Path $projectRoot "release\KoDPCNHook" }
$outputRoot = [IO.Path]::GetFullPath($OutputRoot)
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual C++ build environment was not found: $vcvars"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$hookSource = Join-Path $sourceRoot "hook.cpp"
$launcherSource = Join-Path $sourceRoot "launcher.cpp"
$hookOutput = Join-Path $outputRoot "KoDPCNHook.dll"
$launcherOutput = Join-Path $outputRoot "KoDPCNLauncher.exe"
$htmlInstaller = Join-Path $PSScriptRoot "deploy_html.ps1"

$buildCommand = 'call "{0}" x86 >nul && cl.exe /nologo /utf-8 /std:c++17 /O2 /MT /W4 /EHsc /LD "{1}" /link /OUT:"{2}" && cl.exe /nologo /utf-8 /std:c++17 /O2 /MT /W4 /EHsc "{3}" /link /SUBSYSTEM:WINDOWS /OUT:"{4}" shell32.lib user32.lib' -f $vcvars, $hookSource, $hookOutput, $launcherSource, $launcherOutput

$buildRoot = Join-Path $PSScriptRoot 'build\native'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
Push-Location $buildRoot
try { & cmd.exe /d /s /c $buildCommand } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) {
    throw "Hook build failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath $htmlInstaller `
    -Destination (Join-Path $outputRoot "InstallHtmlTranslation.ps1") -Force

Write-Host "Built:"
Write-Host "  $hookOutput"
Write-Host "  $launcherOutput"
Write-Host "  $(Join-Path $outputRoot 'InstallHtmlTranslation.ps1')"
