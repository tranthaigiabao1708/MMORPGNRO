@echo off
setlocal
title Launching InfiniteUniverse in Unreal Editor 5.x

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$projRoot = '%~dp0'.TrimEnd('\');" ^
  "$uproj = Join-Path $projRoot 'InfiniteUniverse.uproject';" ^
  "& (Join-Path $projRoot 'Tools\CheckEnvironment.ps1') -AutoBindUProject;" ^
  "if ($LASTEXITCODE -ne 0) { Read-Host 'Press Enter to exit'; exit $LASTEXITCODE };" ^
  "if (((Get-PSDrive C).Free -lt 80GB) -and (Test-Path 'D:\')) {" ^
  "    $ddc = 'D:\UE_DerivedDataCache'; if (-not (Test-Path $ddc)) { New-Item -ItemType Directory -Path $ddc | Out-Null };" ^
  "    ${env:UE-LocalDataCachePath} = $ddc;" ^
  "};" ^
  "$ueDir = $null;" ^
  "$dat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat';" ^
  "if (Test-Path $dat) {" ^
  "    $ueDir = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList | Where-Object { $_.AppName -like 'UE_5*' } | Sort-Object { [version](($_.AppName -replace '^UE_','') + '.0') } -Descending | Select-Object -First 1 -ExpandProperty InstallLocation;" ^
  "};" ^
  "if (-not $ueDir -or -not (Test-Path (Join-Path $ueDir 'Engine\Binaries\Win64\UnrealEditor.exe'))) {" ^
  "    $ueDir = (Get-ChildItem 'C:\Program Files\Epic Games','D:\Epic Games' -Directory -Filter 'UE_5*' -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName 'Engine\Binaries\Win64\UnrealEditor.exe') } | Sort-Object Name -Descending | Select-Object -First 1).FullName;" ^
  "};" ^
  "$editorExe = Join-Path $ueDir 'Engine\Binaries\Win64\UnrealEditor.exe';" ^
  "Write-Host ('Starting Unreal Editor from: ' + $editorExe) -ForegroundColor Green;" ^
  "Start-Process -FilePath $editorExe -ArgumentList ('\"' + $uproj + '\"')"
