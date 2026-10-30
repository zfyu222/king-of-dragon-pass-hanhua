param([ValidateSet('install','uninstall','launch')][string]$Action='install',[string]$GameRoot='', [string]$SteamRequestFile='')
$ErrorActionPreference='Stop'
$package=Split-Path -Parent $PSScriptRoot
if(-not $GameRoot){$GameRoot=$package}
$GameRoot=(Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\','/')
if($Action -eq 'launch'){& (Join-Path $PSScriptRoot 'launch.ps1') -GameRoot $GameRoot;return}
$manifest=Get-Content -LiteralPath (Join-Path $package 'PATCH_MANIFEST.json') -Raw -Encoding utf8 | ConvertFrom-Json
function Safe([string]$root,[string]$relative){
    $p=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $p.StartsWith($root.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)){throw '文件路径越界。'}
    return $p
}
function Hash([string]$p){
    $h=[Security.Cryptography.SHA256]::Create();$s=[IO.File]::OpenRead($p)
    try{return [BitConverter]::ToString($h.ComputeHash($s)).Replace('-','').ToLowerInvariant()}finally{$s.Dispose();$h.Dispose()}
}
function CopySafe([string]$from,[string]$to){New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null;Copy-Item -LiteralPath $from -Destination $to -Force}
if((Hash (Safe $GameRoot 'King of Dragon Pass.s86')) -ne $manifest.game_s86_sha256){throw '需要 Steam Windows 2.4.0 版游戏。'}
foreach($p in @(Get-Process -Name 'King of Dragon Pass' -ErrorAction SilentlyContinue)){
    if($p.Path -and [IO.Path]::GetDirectoryName($p.Path) -eq $GameRoot){throw '请先退出游戏。'}
}
$backup=Join-Path $GameRoot '汉化工具\backup'
$state=Join-Path $backup 'state.json'
$saved=@{needs_steam=$false}
if(Test-Path -LiteralPath $state){$saved=Get-Content -LiteralPath $state -Raw -Encoding utf8 | ConvertFrom-Json}
if($Action -eq 'uninstall'){
    $restore=@();$steam=[bool]$saved.needs_steam
    foreach($e in $manifest.files){
        if($e.role -ne 'resource'){continue}
        $old=Safe $backup $e.path;$target=Safe $GameRoot $e.path
        if(Test-Path -LiteralPath $old){
            if((Hash $old) -ne $e.original_sha256){throw "恢复文件校验失败：$($e.path)"}
            if((Hash $target) -ne $e.installed_sha256 -and (Hash $target) -ne $e.original_sha256){throw "文件内容已变化：$($e.path)"}
            $restore+=@{old=$old;target=$target}
        }elseif((Hash $target) -ne $e.original_sha256){$steam=$true}
    }
    foreach($e in $restore){CopySafe $e.old $e.target}
    if($steam){
        if($SteamRequestFile){[IO.File]::WriteAllText($SteamRequestFile,'steam://validate/352220')}else{Start-Process 'steam://validate/352220'}
        Write-Output '已恢复可用原件，并调用 Steam 恢复其余游戏文件。'
    }else{Write-Output '已恢复英文游戏文件。'}
    return
}
# Complete all version, content and payload checks before touching any target.
foreach($e in @($manifest.files)+@($manifest.auxiliary)){
    if((Hash (Safe $package $e.archive_path)) -ne $e.sha256){throw "补丁文件校验失败：$($e.archive_path)"}
}
$resources=@($manifest.files | Where-Object {$_.role -eq 'resource'})
foreach($e in $resources){
    $current=Hash (Safe $GameRoot $e.path)
    if($current -ne $e.original_sha256 -and $current -ne $e.installed_sha256 -and $e.legacy_sha256 -notcontains $current){throw "游戏资源内容不匹配：$($e.path)"}
    $old=Safe $backup $e.path
    if(Test-Path -LiteralPath $old){if((Hash $old) -ne $e.original_sha256){throw "原件备份不匹配：$($e.path)"}}
}
$transaction=Join-Path $backup ('transaction_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $transaction | Out-Null
try{
    foreach($e in $resources){
        $target=Safe $GameRoot $e.path;$old=Safe $backup $e.path
        CopySafe $target (Safe $transaction $e.path)
        if(-not (Test-Path -LiteralPath $old)){
            if((Hash $target) -eq $e.original_sha256){CopySafe $target $old}else{$saved.needs_steam=$true}
        }
    }
    foreach($e in $resources){
        $target=Safe $GameRoot $e.path;$payload=Safe $package $e.archive_path
        if($e.mode -eq 'rgba_patch'){
            $bytes=[IO.File]::ReadAllBytes($target);$pixels=[IO.File]::ReadAllBytes($payload)
            if($pixels.Length -ne $e.length -or $e.offset+$e.length -gt $bytes.Length){throw '图片资源边界不匹配。'}
            [Array]::Copy($pixels,0,$bytes,[int]$e.offset,$pixels.Length);[IO.File]::WriteAllBytes($target,$bytes)
        }else{CopySafe $payload $target}
        if((Hash $target) -ne $e.installed_sha256){throw "安装后校验失败：$($e.path)"}
    }
    $saved | ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
}catch{
    foreach($e in $resources){$old=Safe $transaction $e.path;if(Test-Path -LiteralPath $old){CopySafe $old (Safe $GameRoot $e.path)}}
    throw
}finally{
    # Transaction is a freshly-created child of the verified backup directory.
    if([IO.Path]::GetFullPath($transaction).StartsWith([IO.Path]::GetFullPath($backup)+'\',[StringComparison]::OrdinalIgnoreCase)){
        Remove-Item -LiteralPath $transaction -Recurse -Force
    }
}
Write-Output '《龙城之王》汉化安装完成。'
