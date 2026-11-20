// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IU_AutoDemoSubsystem.generated.h"

class AIU_EnemyCharacter;
class AIU_PlayerCharacter;
class APlayerController;

/**
 * Opt-in Live D3D12 Core Gameplay Loop, Multi-Process Multiplayer & Dedicated Server Profiler.
 * Dormant (never created) in normal gameplay and unit tests unless one of the CLI flags is supplied:
 *   -AutoDemoShowcase        : Step 1 single-player loop showcase
 *   -AutoNetDemo=1 / =2      : Step 2 live UDP Client 1 & Client 2 choreography (pure Client inputs & RPCs)
 *   -AutoServerTelemetry     : Step 2 headless Dedicated Server tick/bandwidth/RAM CSV profiler
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_AutoDemoSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void TickSinglePlayerShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC);
	void TickMultiplayerNetClient(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC);
	void TickStep3PersistenceShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC);
	void SteerPlayerToward(AIU_PlayerCharacter* Player, APlayerController* RawPC, const FVector& GoalPos, float StopDistCm, bool bSprint, bool b3DPitch = false);

	AIU_EnemyCharacter* FindClosestLivingEnemy(const FVector& Origin) const;
	AIU_PlayerCharacter* FindOtherPlayerCharacter(const AIU_PlayerCharacter* SelfPlayer) const;
	void WritePerformanceReportCSV() const;

	double ElapsedSeconds = 0.0;
	double ConnectedPlaySeconds = 0.0;
	double TargetDurationSeconds = 22.5;
	double NextFrameCaptureTime = 0.25;
	double NextSecondSampleTime = 1.0;

	bool bSinglePlayerMode = false;
	bool bServerTelemetryMode = false;
	int32 NetDemoClientRole = 0; // 1 = Client 1 (Recorder / Hero), 2 = Client 2 (Co-op Ally & PvP Sparring Partner)
	int32 Step3PersistencePhase = 0; // 1 = Login -> Raid -> LevelUp -> Save/Disconnect, 2 = Post-Server-Restart Relogin Proof
	bool bSentCleanNetClose = false;

	bool bRecordFrames = false;
	FString DemoFramesSubdir = TEXT("DemoFrames");
	FString CSVOutputFilename = TEXT("Step1_LiveD3D12Profile.csv");
	int32 CapturedFrameIndex = 0;

	int32 LastTriggeredStage = -1;
	bool bTookCounterHitInCycle = false;
	bool bTriggeredMirageEvadeInCycle = false;
	bool bTestedCoopFriendlyFireShot = false;
	bool bDispatchedStep3PotentialAllocations = false;
	double Step3PotentialAllocTime = 0.0;
	double NextActionCooldownTime = 0.0;

	double StartPhysicalMemMB = 0.0;
	double PeakPhysicalMemMB = 0.0;
	double SumNetInKBps = 0.0;
	double SumNetOutKBps = 0.0;
	double PeakNetInKBps = 0.0;
	double PeakNetOutKBps = 0.0;
	int64 PrevTotalNetInBytes = 0;
	int64 PrevTotalNetOutBytes = 0;
	int32 NetBandwidthSamples = 0;

	TArray<float> RecordedFrameTimesMs;
	TArray<FString> PerSecondCSVRows;
};
