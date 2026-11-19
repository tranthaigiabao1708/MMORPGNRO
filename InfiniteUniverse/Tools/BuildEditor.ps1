param(
    [ValidateSet("Development", "DebugGame", "Shipping")]
    [string]$Configuration = "Development",
    [string]$CustomUE5Path = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"

function Get-UEVersionKey([string]$VerText) {
    $clean = $VerText -replace '^UE_', ''
    try { return [version]($clean + $(if ($clean -notmatch '\.') { '.0' } else { '' })) }
    catch { return [version]"0.0" }
}

# Step 1: Verify environment and auto-bind EngineAssociation (e.g. 5.8)
& "$PSScriptRoot\CheckEnvironment.ps1" -CustomUE5Path $CustomUE5Path -AutoBindUProject
if ($LASTEXITCODE -ne 0) {
    throw "Cannot build InfiniteUniverseEditor until environment blockers are resolved."
}

# Step 2: Only redirect Unreal Local DerivedDataCache to D:\ if C:\ is tight (< 80 GB free) and D:\ exists
$freeC = (Get-PSDrive C).Free
if (($freeC -lt 80GB) -and (Test-Path "D:\")) {
    $ddcDir = "D:\UE_DerivedDataCache"
    if (-not (Test-Path $ddcDir)) {
        New-Item -ItemType Directory -Path $ddcDir | Out-Null
    }
    ${env:UE-LocalDataCachePath} = $ddcDir
    Write-Host "[INFO] Low C: space detected; UE-LocalDataCachePath redirected to $ddcDir" -ForegroundColor Cyan
}

# Step 3: Locate Build.bat (prefers latest UE_5.*, e.g. UE_5.8)
$ueDir = $CustomUE5Path
if (-not $ueDir) {
    $launcherDat = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $launcherDat) {
        $ueDir = (Get-Content $launcherDat -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -like "UE_5*" } |
            Sort-Object { Get-UEVersionKey $_.AppName } -Descending |
            Select-Object -First 1 -ExpandProperty InstallLocation
    }
}
if (-not $ueDir -or -not (Test-Path (Join-Path $ueDir "Engine\Build\BatchFiles\Build.bat"))) {
    $ueDir = (Get-ChildItem "C:\Program Files\Epic Games","D:\Epic Games" -Directory -Filter "UE_5*" -ErrorAction SilentlyContinue |
        Where-Object { (Test-Path (Join-Path $_.FullName "Engine\Build\BatchFiles\Build.bat")) -and (-not (Test-Path (Join-Path $_.FullName ".egstore\Pending"))) } |
        Sort-Object { Get-UEVersionKey $_.Name } -Descending |
        Select-Object -First 1).FullName
}

# Clean up any leftover headless UnrealEditor-Cmd test workers holding DLL locks (leaves GUI UnrealEditor untouched)
Get-Process -Name "UnrealEditor-Cmd" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$buildBat = Join-Path $ueDir "Engine\Build\BatchFiles\Build.bat"
$dllPath = Join-Path $ProjectRoot "Binaries\Win64\UnrealEditor-InfiniteUniverse.dll"
$suffixArg = ""

if (Test-Path $dllPath) {
    try {
        $fs = [System.IO.File]::Open($dllPath, 'Open', 'ReadWrite', 'None')
        $fs.Close()
    } catch {
        $randId = Get-Random -Minimum 1000 -Maximum 9999
        $suffixArg = "-ModuleWithSuffix=InfiniteUniverse,$randId"
        Write-Host "[INFO] Base DLL is locked by an active Editor process; using UE5 Hot-Reload suffix ($suffixArg)" -ForegroundColor Yellow
    }
}

Write-Host "[BUILD] Running UnrealBuildTool ($ueDir): InfiniteUniverseEditor Win64 $Configuration..." -ForegroundColor Cyan

if ($suffixArg) {
    & $buildBat InfiniteUniverseEditor Win64 $Configuration -Project="$UProjectPath" $suffixArg -WaitMutex -FromMsBuild
} else {
    & $buildBat InfiniteUniverseEditor Win64 $Configuration -Project="$UProjectPath" -WaitMutex -FromMsBuild
}
if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool failed with exit code $LASTEXITCODE"
}

Write-Host "[SUCCESS] InfiniteUniverseEditor Win64 $Configuration built cleanly!" -ForegroundColor Green
