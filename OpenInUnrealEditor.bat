@echo off
title Launching InfiniteUniverse in Unreal Editor 5.5
set "UE-LocalDataCachePath=D:\UE_DerivedDataCache"
if not exist "D:\UE_DerivedDataCache" mkdir "D:\UE_DerivedDataCache"

echo Starting Unreal Editor 5.5 with DerivedDataCache on D:\UE_DerivedDataCache...
start "" "D:\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0InfiniteUniverse.uproject"
