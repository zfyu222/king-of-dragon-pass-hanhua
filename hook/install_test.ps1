param([string]$GameRoot='', [switch]$Restore)
$ErrorActionPreference='Stop'
$package=$PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $package 'TEST_MANIFEST.json') -Raw -Encoding utf8 | ConvertFrom-Json
if(-not $GameRoot){$GameRoot=$manifest.default_game_root}
$GameRoot=(Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\','/')
function SafePath([string]$root,[string]$relative){
    $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $path.StartsWith($root.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw "路径越界：$relative"}
    return $path
}
function Hash([string]$path){
    $algorithm=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($path)
    try {return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
    finally {$stream.Dispose();$algorithm.Dispose()}
}
$s86=SafePath $GameRoot 'King of Dragon Pass.s86'
if((Hash $s86) -ne $manifest.game_s86_sha256){throw '游戏版本不匹配，需要 Steam 2.4.0。'}
foreach($process in @(Get-Process -Name 'King of Dragon Pass' -ErrorAction SilentlyContinue)) {
    if($process.Path -and [IO.Path]::GetDirectoryName($process.Path) -eq $GameRoot){throw '请先退出游戏再安装或恢复。'}
}
$backup=Join-Path $package 'backup\before_test'
$state=Join-Path $package 'backup\test_state.json'
if($Restore){
    if(-not (Test-Path -LiteralPath $state)){throw '没有本测试包的安装记录。'}
    $saved=Get-Content -LiteralPath $state -Raw -Encoding utf8 | ConvertFrom-Json
    if($saved.game_root -ne $GameRoot){throw '备份对应另一游戏目录。'}
    foreach($entry in $manifest.install_files){
        $target=SafePath $GameRoot $entry.game_path
        $old=SafePath $backup $entry.game_path
        if((Hash $old) -ne $entry.before_sha256){throw "备份校验失败：$($entry.game_path)"}
        $current=Hash $target
        if($current -ne $entry.after_sha256 -and $current -ne $entry.before_sha256){throw "文件被其他程序修改，停止恢复：$($entry.game_path)"}
    }
    foreach($entry in $manifest.install_files){Copy-Item -LiteralPath (SafePath $backup $entry.game_path) -Destination (SafePath $GameRoot $entry.game_path) -Force}
    Write-Output '已恢复测试安装前的游戏文件。'
    return
}
# Validate every target and payload before creating backups or changing the game.
foreach($entry in $manifest.support_files){
    if((Hash (SafePath $package $entry.path)) -ne $entry.sha256){throw "运行文件校验失败：$($entry.path)"}
}
foreach($entry in $manifest.install_files){
    $target=SafePath $GameRoot $entry.game_path
    $current=Hash $target
    if($current -eq $entry.after_sha256){
        $old=SafePath $backup $entry.game_path
        if(-not (Test-Path -LiteralPath $state) -or -not (Test-Path -LiteralPath $old) -or (Hash $old) -ne $entry.before_sha256){throw "缺少可恢复备份：$($entry.game_path)"}
    } elseif($current -ne $entry.before_sha256){throw "游戏文件版本或内容已变化：$($entry.game_path)"}
    if((Hash (SafePath $package $entry.payload)) -ne $entry.payload_sha256){throw "载荷校验失败：$($entry.payload)"}
}
New-Item -ItemType Directory -Force -Path $backup | Out-Null
foreach($entry in $manifest.install_files){
    $target=SafePath $GameRoot $entry.game_path
    $old=SafePath $backup $entry.game_path
    if(-not (Test-Path -LiteralPath $old)){
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $old) | Out-Null
        Copy-Item -LiteralPath $target -Destination $old
    }
    if((Hash $old) -ne $entry.before_sha256){throw "备份校验失败：$($entry.game_path)"}
}
$record=@{game_root=$GameRoot;manifest_sha256=(Hash (Join-Path $package 'TEST_MANIFEST.json'));status='installing'}
$record | ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
try {
    foreach($entry in $manifest.install_files){
        $target=SafePath $GameRoot $entry.game_path
        $payload=SafePath $package $entry.payload
        if($entry.mode -eq 'rgba_patch'){
            # Preserve archive length, headers, resource offsets and all other pixels.
            $bytes=[IO.File]::ReadAllBytes($target)
            if((Hash $target) -eq $entry.before_sha256){
                $patch=[IO.File]::ReadAllBytes($payload)
                if($patch.Length -ne $entry.length -or $entry.offset+$entry.length -gt $bytes.Length){throw '贴图补丁边界不匹配。'}
                [Array]::Copy($patch,0,$bytes,[int]$entry.offset,$patch.Length)
                [IO.File]::WriteAllBytes($target,$bytes)
            }
        } else {Copy-Item -LiteralPath $payload -Destination $target -Force}
        if((Hash $target) -ne $entry.after_sha256){throw "安装后校验失败：$($entry.game_path)"}
    }
    $record.status='installed';$record | ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
    Write-Output "测试载荷安装完成：$($manifest.install_files.Count) 个文件。"
} catch {
    foreach($entry in $manifest.install_files){Copy-Item -LiteralPath (SafePath $backup $entry.game_path) -Destination (SafePath $GameRoot $entry.game_path) -Force}
    $record.status='rolled_back';$record | ConvertTo-Json | Set-Content -LiteralPath $state -Encoding utf8
    throw
}
