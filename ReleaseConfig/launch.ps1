param([string]$GameRoot='', [switch]$CheckOnly)
$ErrorActionPreference='Stop'
function FileHash([string]$Path){
    $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($Path)
    try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$sha.Dispose()}
}
# Windows PowerShell 5.1's Process.Modules can omit WOW64 modules when
# called from a 64-bit host. Toolhelp SNAPMODULE32 gives the real DLL path.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class KoDPTestModules {
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
    struct Entry {
        public uint size,moduleId,processId,globalCount,processCount;
        public IntPtr address;
        public uint bytes;
        public IntPtr module;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string name;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)] public string path;
    }
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags,uint pid);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool Module32FirstW(IntPtr snapshot,ref Entry entry);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool Module32NextW(IntPtr snapshot,ref Entry entry);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static string[] Paths(uint pid) {
        IntPtr snapshot=CreateToolhelp32Snapshot(0x18,pid);
        if(snapshot==new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            var entry=new Entry();entry.size=(uint)Marshal.SizeOf(typeof(Entry));
            var paths=new List<string>();
            if(!Module32FirstW(snapshot,ref entry)) throw new Win32Exception(Marshal.GetLastWin32Error());
            do {if(String.Equals(entry.name,"KoDPCNHook.dll",StringComparison.OrdinalIgnoreCase)) paths.Add(entry.path);}
            while(Module32NextW(snapshot,ref entry));
            return paths.ToArray();
        } finally {CloseHandle(snapshot);}
    }
}
'@
$package=Split-Path -Parent $PSScriptRoot
if(-not $GameRoot){$GameRoot=$package}
$game=(Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\','/')
$expected=Join-Path $package '汉化文件\KoDPCNHook\KoDPCNHook.dll'
$manifest=Get-Content -LiteralPath (Join-Path $package 'PATCH_MANIFEST.json') -Raw -Encoding utf8 | ConvertFrom-Json
foreach($entry in @($manifest.files | Where-Object {$_.role -eq 'runtime'})){
    $path=Join-Path $package $entry.archive_path
    if((FileHash $path) -ne $entry.sha256){throw "测试载荷校验失败：$path"}
}
$running=@(Get-Process -Name 'King of Dragon Pass' -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $game 'King of Dragon Pass.exe')})
$loaded=@(foreach($process in $running){[KoDPTestModules]::Paths($process.Id)})
$installed=$true
foreach($entry in @($manifest.files | Where-Object {$_.role -eq 'resource'})){
    $target=Join-Path $game $entry.path
    if(-not (Test-Path -LiteralPath $target -PathType Leaf) -or (FileHash $target) -ne $entry.installed_sha256){$installed=$false;break}
}
if($CheckOnly){
    @{package=$package;expected_dll=$expected;running_game_ids=@($running.Id);loaded_dlls=$loaded;payload_hashes='passed';installed=$installed;game_launched=$false} | ConvertTo-Json -Depth 4
    return
}
if($running.Count){throw '请先保存并退出当前游戏，再使用这个入口。此脚本不会关闭游戏。'}
# The previous watcher can survive its game for 20 seconds. Let it finish
# before the new game starts, so it cannot inject an older DLL first.
foreach($watcher in @(Get-Process -Name 'KoDPCNLauncher' -ErrorAction SilentlyContinue)){
    if($watcher.Path){
        Write-Output '正在等待旧汉化启动器自行退出……'
        if(-not $watcher.WaitForExit(30000)){throw '旧汉化启动器尚未退出，请稍后重试。'}
    }
}
Write-Output ('当前汉化补丁：'+$package)
if(-not $installed){& (Join-Path $package '汉化工具\install.ps1') -Action install -GameRoot $game}
Start-Process -WindowStyle Hidden -FilePath (Join-Path $package '汉化文件/KoDPCNHook/KoDPCNLauncher.exe') -ArgumentList ('"'+(Join-Path $game 'King of Dragon Pass.exe')+'"')
$deadline=[DateTime]::UtcNow.AddSeconds(30)
do{
    foreach($process in @(Get-Process -Name 'King of Dragon Pass' -ErrorAction SilentlyContinue)){
        if($process.Path -ne (Join-Path $game 'King of Dragon Pass.exe')){continue}
        foreach($modulePath in @([KoDPTestModules]::Paths($process.Id))){
            if($modulePath -ne $expected){throw ('当前游戏加载了其他补丁：'+$modulePath)}
            Write-Output ('已确认加载新版汉化DLL：'+$modulePath)
            return
        }
    }
    Start-Sleep -Milliseconds 500
}while([DateTime]::UtcNow -lt $deadline)
throw '未能确认新版DLL已加载，请保留此窗口信息。'
