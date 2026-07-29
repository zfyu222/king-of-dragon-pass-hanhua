$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $PSScriptRoot "src"
$outputRoot = Join-Path $projectRoot "release\KoDPCNHook"
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual C++ build environment was not found: $vcvars"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$hookSource = Join-Path $sourceRoot "hook.cpp"
$launcherSource = Join-Path $sourceRoot "launcher.cpp"
$hookOutput = Join-Path $outputRoot "KoDPCNHook.dll"
$launcherOutput = Join-Path $outputRoot "KoDPCNLauncher.exe"

$buildCommand = 'call "{0}" x86 >nul && cl.exe /nologo /utf-8 /std:c++17 /O2 /MT /W4 /EHsc /LD "{1}" /link /OUT:"{2}" && cl.exe /nologo /utf-8 /std:c++17 /O2 /MT /W4 /EHsc "{3}" /link /SUBSYSTEM:WINDOWS /OUT:"{4}" shell32.lib user32.lib' -f $vcvars, $hookSource, $hookOutput, $launcherSource, $launcherOutput

& cmd.exe /d /s /c $buildCommand
if ($LASTEXITCODE -ne 0) {
    throw "Hook build failed with exit code $LASTEXITCODE"
}

Write-Host "Built:"
Write-Host "  $hookOutput"
Write-Host "  $launcherOutput"
