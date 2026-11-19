// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IU_AutoDemoSubsystem.generated.h"

class AIU_EnemyCharacter;

/**
 * Opt-in Live D3D12 Core Gameplay Loop Showcase & Hardware Profiler (-AutoDemoShowcase).
 * Dormant (never created) in normal gameplay and unit tests unless -AutoDemoShowcase is on the CLI.
 * Supports -RecordDemoFrames (captures UI+3D frames for MP4 encoding) and -DemoDuration=<sec> (e.g. 1800 for 30-min soak).
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
	AIU_EnemyCharacter* FindClosestLivingEnemy(const FVector& Origin) const;
	void WritePerformanceReportCSV() const;

	double ElapsedSeconds = 0.0;
	double TargetDurationSeconds = 22.5;
	double NextFrameCaptureTime = 0.25;
	double NextSecondSampleTime = 1.0;

	bool bRecordFrames = false;
	int32 CapturedFrameIndex = 0;

	int32 LastTriggeredStage = -1;
	bool bTookCounterHitInCycle = false;
	bool bTriggeredMirageEvadeInCycle = false;
	double NextActionCooldownTime = 0.0;

	double StartPhysicalMemMB = 0.0;
	double PeakPhysicalMemMB = 0.0;
	TArray<float> RecordedFrameTimesMs;
	TArray<FString> PerSecondCSVRows;
};
