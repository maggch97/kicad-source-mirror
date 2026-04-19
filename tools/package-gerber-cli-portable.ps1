[CmdletBinding()]
param(
    [string]$InstallRoot = "build\install\msvc-win64-release",
    [string]$VcpkgBin = "build\msvc-win64-release\vcpkg_installed\x64-windows\bin",
    [string]$OutputRoot = "out\gerber-cli-portable",
    [switch]$Zip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [switch]$AllowMissing
    )

    if( [System.IO.Path]::IsPathRooted( $Path ) )
    {
        if( $AllowMissing )
        {
            return [System.IO.Path]::GetFullPath( $Path )
        }

        return ( Resolve-Path -LiteralPath $Path ).Path
    }

    $fullPath = Join-Path $PSScriptRoot "..\$Path"

    if( $AllowMissing )
    {
        return [System.IO.Path]::GetFullPath( $fullPath )
    }

    return ( Resolve-Path -LiteralPath $fullPath ).Path
}

function Get-DumpbinPath {
    $dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue

    if( -not $dumpbin )
    {
        throw "dumpbin.exe not found in PATH. Run this from a VS Developer PowerShell/Console."
    }

    return $dumpbin.Source
}

function Get-PeDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string]$DumpbinPath
    )

    $deps = [System.Collections.Generic.List[string]]::new()
    $inDeps = $false

    foreach( $line in & $DumpbinPath /dependents $FilePath )
    {
        if( $line -match "Image has the following dependencies:" )
        {
            $inDeps = $true
            continue
        }

        if( -not $inDeps )
        {
            continue
        }

        if( $line -match "^\s+([A-Za-z0-9._+-]+\.(dll|exe))\s*$" )
        {
            $deps.Add( $matches[1] )
            continue
        }

        if( $line -match "^\s+Summary\s*$" )
        {
            break
        }
    }

    return $deps
}

function Resolve-BinarySource {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FileName,

        [Parameter(Mandatory = $true)]
        [string[]]$SearchDirs
    )

    foreach( $dir in $SearchDirs )
    {
        $candidate = Join-Path $dir $FileName

        if( Test-Path -LiteralPath $candidate )
        {
            return ( Resolve-Path -LiteralPath $candidate ).Path
        }
    }

    return $null
}

function Write-LauncherScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputRoot
    )

    $psLauncherPath = Join-Path $OutputRoot "run-kicad-cli.ps1"
    $psLauncher = @'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PATH = "$root\bin;$env:PATH"
$env:KICAD_STOCK_DATA_HOME = "$root\share\kicad"
$env:KICAD_CONFIG_HOME = "$root\data\config"

& "$root\bin\kicad-cli.exe" @args
exit $LASTEXITCODE
'@

    $cmdLauncherPath = Join-Path $OutputRoot "run-kicad-cli.cmd"
    $cmdLauncher = @'
@echo off
set "ROOT=%~dp0"
set "PATH=%ROOT%bin;%PATH%"
set "KICAD_STOCK_DATA_HOME=%ROOT%share\kicad"
set "KICAD_CONFIG_HOME=%ROOT%data\config"
"%ROOT%bin\kicad-cli.exe" %*
exit /b %ERRORLEVEL%
'@

    Set-Content -LiteralPath $psLauncherPath -Value $psLauncher -Encoding ASCII
    Set-Content -LiteralPath $cmdLauncherPath -Value $cmdLauncher -Encoding ASCII
}

$installRoot = Resolve-RepoPath -Path $InstallRoot
$vcpkgBin = Resolve-RepoPath -Path $VcpkgBin
$outputRoot = Resolve-RepoPath -Path $OutputRoot -AllowMissing
$dumpbinPath = Get-DumpbinPath

$installBin = Join-Path $installRoot "bin"
$stockDataRoot = Join-Path $installRoot "share\kicad"
$destBin = Join-Path $outputRoot "bin"
$destData = Join-Path $outputRoot "share\kicad"
$destConfig = Join-Path $outputRoot "data\config"
$searchDirs = @( $installBin, $vcpkgBin )
$seedFiles = @( "kicad-cli.exe", "_gerbview.dll" )
$dataDirs = @( "resources", "schemas" )

if( Test-Path -LiteralPath $outputRoot )
{
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $destBin -Force | Out-Null
New-Item -ItemType Directory -Path $destData -Force | Out-Null
New-Item -ItemType Directory -Path $destConfig -Force | Out-Null

$queued = [System.Collections.Generic.HashSet[string]]::new( [System.StringComparer]::OrdinalIgnoreCase )
$copied = [System.Collections.Generic.HashSet[string]]::new( [System.StringComparer]::OrdinalIgnoreCase )
$queue = [System.Collections.Generic.Queue[string]]::new()

foreach( $seed in $seedFiles )
{
    $queued.Add( $seed ) | Out-Null
    $queue.Enqueue( $seed )
}

$missingDeps = [System.Collections.Generic.SortedSet[string]]::new( [System.StringComparer]::OrdinalIgnoreCase )

while( $queue.Count -gt 0 )
{
    $fileName = $queue.Dequeue()
    $sourcePath = Resolve-BinarySource -FileName $fileName -SearchDirs $searchDirs

    if( -not $sourcePath )
    {
        throw "Required binary not found: $fileName"
    }

    $destPath = Join-Path $destBin $fileName

    if( -not $copied.Contains( $fileName ) )
    {
        Copy-Item -LiteralPath $sourcePath -Destination $destPath -Force
        $copied.Add( $fileName ) | Out-Null
    }

    foreach( $dep in Get-PeDependencies -FilePath $sourcePath -DumpbinPath $dumpbinPath )
    {
        $depSource = Resolve-BinarySource -FileName $dep -SearchDirs $searchDirs

        if( $depSource )
        {
            if( $queued.Add( $dep ) )
            {
                $queue.Enqueue( $dep )
            }
        }
        else
        {
            $missingDeps.Add( $dep ) | Out-Null
        }
    }
}

foreach( $dirName in $dataDirs )
{
    $srcDir = Join-Path $stockDataRoot $dirName
    $dstDir = Join-Path $destData $dirName

    if( -not ( Test-Path -LiteralPath $srcDir ) )
    {
        throw "Required data directory not found: $srcDir"
    }

    Copy-Item -LiteralPath $srcDir -Destination $dstDir -Recurse -Force
}

Write-LauncherScript -OutputRoot $outputRoot

if( $Zip )
{
    $zipPath = "$outputRoot.zip"

    if( Test-Path -LiteralPath $zipPath )
    {
        Remove-Item -LiteralPath $zipPath -Force
    }

    Compress-Archive -Path ( Join-Path $outputRoot "*" ) -DestinationPath $zipPath
}

Write-Host "Packaged portable gerber CLI to: $outputRoot"
Write-Host "Copied binaries: $($copied.Count)"
Write-Host "Included data dirs: $($dataDirs -join ', ')"

if( $missingDeps.Count -gt 0 )
{
    Write-Host "Dependencies not copied (usually system DLLs):"

    foreach( $dep in $missingDeps )
    {
        Write-Host "  $dep"
    }
}
