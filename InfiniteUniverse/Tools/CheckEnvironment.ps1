param(
    [string]$CustomUE5Path = "",
    [switch]$AutoBindUProject
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"
$AllPassed = $true

function Get-UEVersionKey([string]$VerText) {
    $clean = $VerText -replace '^UE_', ''
    try { return [version]($clean + $(if ($clean -notmatch '\.') { '.0' } else { '' })) }
    catch { return [version]"0.0" }
}

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host " INFINITE UNIVERSE - MILESTONE 0 ENVIRONMENT CHECK" -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# 1. Disk Space Check
$driveC = Get-PSDrive C
$driveD = Get-PSDrive D -ErrorAction SilentlyContinue
$freeC_GB = [math]::Round($driveC.Free / 1GB, 1)
$freeD_GB = if ($driveD) { [math]::Round($driveD.Free / 1GB, 1) } else { 0 }

if ($driveD) {
    Write-Host ("[DISK] C: Free = {0} GB | D: Free = {1} GB" -f $freeC_GB, $freeD_GB)
} else {
    Write-Host ("[DISK] C: Free = {0} GB (Single-drive workstation)" -f $freeC_GB)
}

if ($freeC_GB -lt 10.0) {
    Write-Host "  [FAIL] C: drive critically low (< 10 GB free)." -ForegroundColor Red
    $AllPassed = $false
} elseif ($freeC_GB -lt 45.0 -and $driveD) {
    Write-Host "  [WARN] C: drive has < 45 GB free. DerivedDataCache will redirect to D:\UE_DerivedDataCache." -ForegroundColor Yellow
}

# 2. Find Unreal Engine 5 Installation (prefers highest installed 5.x, e.g. 5.8)
$foundUEVersion = $null
$foundUEPath = $null

if ($CustomUE5Path -and (Test-Path (Join-Path $CustomUE5Path "Engine\Build\BatchFiles\Build.bat"))) {
    $foundUEPath = $CustomUE5Path
    $foundUEVersion = (Split-Path -Leaf $CustomUE5Path) -replace '^UE_', ''
}

if (-not $foundUEPath) {
    $launcherDat = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $launcherDat) {
        $ueEntry = Get-Content $launcherDat -Raw | ConvertFrom-Json |
            Select-Object -ExpandProperty InstallationList |
            Where-Object { $_.AppName -like "UE_5*" } |
            Sort-Object { Get-UEVersionKey $_.AppName } -Descending | Select-Object -First 1
        if ($ueEntry -and (Test-Path (Join-Path $ueEntry.InstallLocation "Engine\Build\BatchFiles\Build.bat"))) {
            $foundUEVersion = $ueEntry.AppName.Replace("UE_", "")
            $foundUEPath = $ueEntry.InstallLocation
        }
    }
}

if (-not $foundUEPath) {
    foreach ($dir in @("C:\Program Files\Epic Games", "D:\Epic Games", "D:\UnrealEngine")) {
        if (Test-Path $dir) {
            $cand = Get-ChildItem $dir -Directory -Filter "UE_5*" -ErrorAction SilentlyContinue |
                Where-Object { -not (Test-Path (Join-Path $_.FullName ".egstore\Pending")) } |
                Sort-Object { Get-UEVersionKey $_.Name } -Descending | Select-Object -First 1
            if ($cand -and (Test-Path (Join-Path $cand.FullName "Engine\Build\BatchFiles\Build.bat"))) {
                $foundUEVersion = $cand.Name.Replace("UE_", "")
                $foundUEPath = $cand.FullName
                break
            }
        }
    }
}

if ($foundUEPath) {
    Write-Host ("[PASS] Unreal Engine 5 found: {0} at {1}" -f $foundUEVersion, $foundUEPath) -ForegroundColor Green
    if ($AutoBindUProject -and $foundUEVersion -match "^5\.\d+") {
        $uproj = Get-Content $UProjectPath -Raw | ConvertFrom-Json
        if ($uproj.EngineAssociation -ne $foundUEVersion) {
            $uproj.EngineAssociation = $foundUEVersion
            $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllText($UProjectPath, ($uproj | ConvertTo-Json -Depth 10), $utf8NoBom)
            Write-Host ("  [INFO] Updated InfiniteUniverse.uproject EngineAssociation -> {0}" -f $foundUEVersion) -ForegroundColor Cyan
        }
    }
} else {
    $pendingInstall = Get-ChildItem "C:\Program Files\Epic Games", "D:\Epic Games" -Directory -Filter "UE_5*" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($pendingInstall) {
        Write-Host ("[BLOCKED] Unreal Engine folder '{0}' detected at '{1}', but Build.bat is not ready yet (Epic Games Launcher download/install still in progress)." -f $pendingInstall.Name, $pendingInstall.FullName) -ForegroundColor Yellow
    } else {
        Write-Host "[BLOCKED] Unreal Engine 5 NOT FOUND. Install UE 5.8 via Epic Games Launcher." -ForegroundColor Red
    }
    $AllPassed = $false
}

# 3. MSVC C++ x64 Compiler Check
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcPath = $null
if (Test-Path $vswhere) {
    $vcPath = & $vswhere -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
}
if ($vcPath) {
    $vTools = Get-ChildItem (Join-Path $vcPath "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "^14\.[345]" }
    if ($vTools) {
        Write-Host ("[PASS] MSVC C++ x64 Toolset found at {0} ({1})" -f $vcPath, ($vTools.Name -join ", ")) -ForegroundColor Green
    } else {
        Write-Host ("[WARN] VS C++ installed at {0}, but compatible MSVC 14.x not detected." -f $vcPath) -ForegroundColor Yellow
        $AllPassed = $false
    }
} else {
    Write-Host "[BLOCKED] MSVC C++ x64/x86 Build Tools NOT FOUND. Modify Visual Studio -> Game development with C++." -ForegroundColor Red
    $AllPassed = $false
}

# 4. Windows 10/11 SDK Check
$winSdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
if (Test-Path $winSdkRoot) {
    $sdks = Get-ChildItem $winSdkRoot -Directory | Where-Object { $_.Name -like "10.0.*" } | Select-Object -ExpandProperty Name
    if ($sdks) {
        Write-Host ("[PASS] Windows SDK found: {0}" -f ($sdks -join ", ")) -ForegroundColor Green
    } else {
        Write-Host "[BLOCKED] No 10.0.* version inside Windows Kits\10\Include." -ForegroundColor Red
        $AllPassed = $false
    }
} else {
    Write-Host "[BLOCKED] Windows 10/11 SDK NOT FOUND (Windows Kits\10\Include missing)." -ForegroundColor Red
    $AllPassed = $false
}

# 5. Git & Git LFS Check
if ((Get-Command git -ErrorAction SilentlyContinue) -and (& git lfs version 2>$null)) {
    Write-Host ("[PASS] Git & Git LFS ready ({0})" -f (& git --version)) -ForegroundColor Green
} else {
    Write-Host "[FAIL] Git or Git LFS missing in PATH." -ForegroundColor Red
    $AllPassed = $false
}

# 6. Blender 5.2+ Check
$blenderExe = $null
$pfBlender = Get-ChildItem "C:\Program Files\Blender Foundation" -Directory -Filter "Blender*" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "blender.exe" } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

if ($pfBlender) {
    $blenderExe = $pfBlender
} elseif (Test-Path "D:\blender\blender.exe") {
    $blenderExe = "D:\blender\blender.exe"
} elseif (Get-Command blender -ErrorAction SilentlyContinue) {
    $blenderExe = (Get-Command blender).Source
}

if ($blenderExe) {
    $bVer = (& $blenderExe --version | Select-Object -First 1)
    Write-Host ("[PASS] {0} found at {1}" -f $bVer, $blenderExe) -ForegroundColor Green
} else {
    Write-Host "[WARN] Blender executable not found in C:\Program Files\Blender Foundation, D:\blender, or PATH." -ForegroundColor Yellow
}

Write-Host "==================================================================" -ForegroundColor Cyan
if ($AllPassed) {
    Write-Host " OVERALL STATUS: READY FOR UE5 C++ BUILD (Milestone 0 Unblocked!)" -ForegroundColor Green
    exit 0
} else {
    Write-Host " OVERALL STATUS: WAITING ON ENGINE INSTALL - Resolve items above." -ForegroundColor Yellow
    exit 2
}
