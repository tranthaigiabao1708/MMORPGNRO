param(
    [ValidateSet("Development", "DebugGame", "Shipping")]
    [string]$Configuration = "Development",
    [string]$CustomUE5Path = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"

# Step 1: Verify environment and auto-bind EngineAssociation
& "$PSScriptRoot\CheckEnvironment.ps1" -CustomUE5Path $CustomUE5Path -AutoBindUProject
if ($LASTEXITCODE -ne 0) {
    throw "Cannot build InfiniteUniverseEditor until environment blockers are resolved."
}

# Step 2: Redirect Unreal Local DerivedDataCache to D:\ to protect C:\ free space
if (Test-Path "D:\") {
    $ddcDir = "D:\UE_DerivedDataCache"
    if (-not (Test-Path $ddcDir)) {
        New-Item -ItemType Directory -Path $ddcDir | Out-Null
    }
    ${env:UE-LocalDataCachePath} = $ddcDir
    Write-Host "[INFO] UE-LocalDataCachePath set to $ddcDir" -ForegroundColor Cyan
}

# Step 3: Locate Build.bat
$ueDir = $CustomUE5Path
if (-not $ueDir) {
    $launcherDat = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $launcherDat) {
        $ueDir = (Get-Content $launcherDat -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -like "UE_5*" } |
            Sort-Object AppName -Descending | Select-Object -First 1 -ExpandProperty InstallLocation
    }
}
if (-not $ueDir) {
    $ueDir = (Get-ChildItem "D:\Epic Games","C:\Program Files\Epic Games" -Directory -Filter "UE_5*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1).FullName
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

Write-Host "[BUILD] Running UnrealBuildTool: InfiniteUniverseEditor Win64 $Configuration..." -ForegroundColor Cyan

if ($suffixArg) {
    & $buildBat InfiniteUniverseEditor Win64 $Configuration -Project="$UProjectPath" $suffixArg -WaitMutex -FromMsBuild
} else {
    & $buildBat InfiniteUniverseEditor Win64 $Configuration -Project="$UProjectPath" -WaitMutex -FromMsBuild
}
if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool failed with exit code $LASTEXITCODE"
}

Write-Host "[SUCCESS] InfiniteUniverseEditor Win64 $Configuration built cleanly!" -ForegroundColor Green
