param(
    [string]$GameRoot = '',
    [string]$PackageRoot = '',
    [string]$BackupRoot = '',
    [switch]$Restore
)

$ErrorActionPreference = 'Stop'

if (-not $PackageRoot) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'KoDPCNHook.dll')) {
        $PackageRoot = $PSScriptRoot
    } else {
        $PackageRoot = Join-Path $PSScriptRoot '..\release\KoDPCNHook'
    }
}
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path

if (-not $GameRoot) {
    $iniPath = Join-Path $PackageRoot 'KoDPCNHook.ini'
    $gameExeLine = Get-Content -LiteralPath $iniPath -Encoding utf8 |
        Where-Object { $_ -like 'GameExe=*' } |
        Select-Object -First 1
    if (-not $gameExeLine) {
        throw "GameExe was not found in $iniPath"
    }
    $gameExe = $gameExeLine.Substring('GameExe='.Length).Trim()
    $GameRoot = Split-Path -Parent $gameExe
}

if (-not $BackupRoot) {
    $BackupRoot = Join-Path $PackageRoot 'backup\html_original'
}
$BackupRoot = [IO.Path]::GetFullPath($BackupRoot)

$gameKodp = Join-Path $GameRoot 'KoDp'
$overlayKodp = Join-Path $PackageRoot 'overlay\KoDp'

if (-not (Test-Path -LiteralPath $gameKodp -PathType Container)) {
    throw "Game KoDp directory was not found: $gameKodp"
}
if (-not (Test-Path -LiteralPath $overlayKodp -PathType Container)) {
    throw "Translated HTML overlay was not found: $overlayKodp"
}

if ($Restore) {
    if (-not (Test-Path -LiteralPath $BackupRoot -PathType Container)) {
        throw "HTML backup directory was not found: $BackupRoot"
    }
    $restored = 0
    Get-ChildItem -LiteralPath $BackupRoot -File -Recurse -Filter '*.html' |
        ForEach-Object {
            $relative = $_.FullName.Substring($BackupRoot.Length).TrimStart('\')
            $target = Join-Path $GameRoot $relative
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                Copy-Item -LiteralPath $_.FullName -Destination $target -Force
                $restored++
            }
        }
    Write-Output "Restored $restored original HTML files."
    return
}

$copied = 0
$backedUp = 0

function Install-HtmlTree {
    param(
        [string]$SourceDirectory,
        [string]$TargetDirectory,
        [string]$BackupDirectory
    )

    $SourceDirectory = (Resolve-Path -LiteralPath $SourceDirectory).Path
    $TargetDirectory = (Resolve-Path -LiteralPath $TargetDirectory).Path
    $BackupDirectory = [IO.Path]::GetFullPath($BackupDirectory)

    Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse -Filter '*.html' |
        ForEach-Object {
            $relative = $_.FullName.Substring($SourceDirectory.Length).TrimStart('\')
            $target = Join-Path $TargetDirectory $relative
            if (Test-Path -LiteralPath $target -PathType Leaf) {
                $backup = Join-Path $BackupDirectory $relative
                $backupParent = Split-Path -Parent $backup
                if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                    New-Item -ItemType Directory -Path $backupParent -Force | Out-Null
                    Copy-Item -LiteralPath $target -Destination $backup
                    $script:backedUp++
                }

                Copy-Item -LiteralPath $_.FullName -Destination $target -Force
                $script:copied++
            }
        }
}

Install-HtmlTree `
    -SourceDirectory (Join-Path $overlayKodp 'Text') `
    -TargetDirectory (Join-Path $gameKodp 'Text') `
    -BackupDirectory (Join-Path $BackupRoot 'KoDp\Text')

Install-HtmlTree `
    -SourceDirectory (Join-Path $overlayKodp 'Text') `
    -TargetDirectory (Join-Path $gameKodp '_Text') `
    -BackupDirectory (Join-Path $BackupRoot 'KoDp\_Text')

Install-HtmlTree `
    -SourceDirectory (Join-Path $overlayKodp 'Manual') `
    -TargetDirectory (Join-Path $gameKodp 'Manual') `
    -BackupDirectory (Join-Path $BackupRoot 'KoDp\Manual')

Write-Output "Backed up $backedUp original HTML files."
Write-Output "Installed $copied translated HTML files."
