param(
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"
$ArchiveDir = Join-Path $ProjectRoot "Saved\PackagedBuilds"
$UEPath = "C:\Program Files\Epic Games\UE_5.8"
$BuildBat = Join-Path $UEPath "Engine\Build\BatchFiles\Build.bat"
$RunUAT = Join-Path $UEPath "Engine\Build\BatchFiles\RunUAT.bat"

Write-Host "[1/2] Compiling InfiniteUniverse Win64 $Configuration..." -ForegroundColor Cyan
& $BuildBat InfiniteUniverse Win64 $Configuration -Project="$UProjectPath" -WaitMutex -FromMsBuild
if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool failed with exit code $LASTEXITCODE"
}

Write-Host "[2/2] Cooking, Staging, Pak & Archiving Standalone Build to $ArchiveDir..." -ForegroundColor Cyan
& $RunUAT BuildCookRun `
    -project="$UProjectPath" `
    -noP4 `
    -platform=Win64 `
    -clientconfig=$Configuration `
    -skipbuild `
    -cook `
    -map=/Game/Maps/Prototype/Lvl_Greybox_Prototype+/Game/Maps/Prototype/M_Prototype `
    -stage `
    -pak `
    -archive `
    -archivedirectory="$ArchiveDir" `
    -unattended `
    -utf8output
if ($LASTEXITCODE -ne 0) {
    throw "RunUAT BuildCookRun failed with exit code $LASTEXITCODE"
}

Write-Host "[SUCCESS] Standalone Win64 $Configuration packaged at: $ArchiveDir\Windows\InfiniteUniverse.exe" -ForegroundColor Green
