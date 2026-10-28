param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$native=Join-Path $root 'build\native'
New-Item -ItemType Directory -Force $native | Out-Null
$code=Get-Content -LiteralPath (Join-Path $root 'src\hook.cpp') -Raw -Encoding utf8
$values=@('"Play"','"Settings"')
foreach($m in [regex]::Matches($code,'const char\* (\w+Probe)\s*=\s*([\s\S]*?);')) {
    if($m.Groups[1].Value -ne 'partiallyTranslatedClanProbe') { $values+=$m.Groups[2].Value }
}
$header="std::vector<const char*> probes = {`n"+($values -join ",`n")+"`n};"
[IO.File]::WriteAllText((Join-Path $native 'runtime_probes.inc'),$header,[Text.UTF8Encoding]::new($false))
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
$cmd='call "{0}" x86 >nul && cl /nologo /utf-8 /std:c++17 /O2 /MT /EHsc "{1}\src\runtime_check.cpp" /Fo"{2}\runtime_check.obj" /link /OUT:"{2}\runtime_check.exe" /IMPLIB:"{2}\runtime_check.lib"' -f $vc,$root,$native
& cmd.exe /d /s /c $cmd
if($LASTEXITCODE -ne 0){throw 'Runtime checker compilation failed'}
& (Join-Path $native 'runtime_check.exe') $PackageRoot
if($LASTEXITCODE -ne 0){throw 'Runtime lookup check failed'}
$tail=(Get-Content -LiteralPath (Join-Path $PackageRoot 'KoDPCNHook.log') -Tail 2) -join "`n"
if($tail -notmatch 'runtime composite translation self-test passed'){throw 'Embedded composite checks did not pass'}
