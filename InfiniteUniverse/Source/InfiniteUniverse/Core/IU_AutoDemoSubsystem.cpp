// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_AutoDemoSubsystem.h"
#include "Core/IU_PlayerController.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "AI/IU_EnemySpawner.h"
#include "Movement/IU_FlightComponent.h"
#include "Movement/IU_MovementComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "UI/IU_HUDManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "InfiniteUniverse.h"

bool UIU_AutoDemoSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World || !World->IsGameWorld())
	{
		return false;
	}
	return FParse::Param(FCommandLine::Get(), TEXT("AutoDemoShowcase"));
}

TStatId UIU_AutoDemoSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UIU_AutoDemoSubsystem, STATGROUP_Tickables);
}

void UIU_AutoDemoSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bRecordFrames = FParse::Param(FCommandLine::Get(), TEXT("RecordDemoFrames"));
	float CustomDur = 22.5f;
	if (FParse::Value(FCommandLine::Get(), TEXT("DemoDuration="), CustomDur) && CustomDur >= 5.0f)
	{
		TargetDurationSeconds = static_cast<double>(CustomDur);
	}

	StartPhysicalMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);
	PeakPhysicalMemMB = StartPhysicalMemMB;
	RecordedFrameTimesMs.Reserve(4096);
	PerSecondCSVRows.Add(TEXT("Second,AvgFPS,AvgFrameMs,UsedPhysicalRAM_MB,TotalActors,LivingEnemies"));

	if (bRecordFrames)
	{
		const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DemoFrames"));
		IFileManager::Get().DeleteDirectory(*OutDir, false, true);
		IFileManager::Get().MakeDirectory(*OutDir, true);
	}

	// Shorten spawner respawn delay slightly so respawn is clearly visible within the 22.5s video loop
	for (TActorIterator<AIU_EnemySpawner> It(&InWorld); It; ++It)
	{
		if (AIU_EnemySpawner* Spawner = *It)
		{
			Spawner->RespawnDelaySeconds = 1.8f;
		}
	}

	UE_LOG(LogInfiniteUniverse, Display, TEXT("[AutoDemoShowcase] Started on map '%s' (Duration=%.1fs, RecordFrames=%d)"),
		*InWorld.GetName(), TargetDurationSeconds, bRecordFrames ? 1 : 0);
}

AIU_EnemyCharacter* UIU_AutoDemoSubsystem::FindClosestLivingEnemy(const FVector& Origin) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AIU_EnemyCharacter* Best = nullptr;
	double BestDistSq = DBL_MAX;
	for (TActorIterator<AIU_EnemyCharacter> It(World); It; ++It)
	{
		AIU_EnemyCharacter* E = *It;
		if (IsValid(E) && !E->IsDead() && !E->bCanFlyChase)
		{
			const double D = FVector::DistSquared(Origin, E->GetActorLocation());
			if (D < BestDistSq)
			{
				BestDistSq = D;
				Best = E;
			}
		}
	}
	return Best;
}

void UIU_AutoDemoSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float ClampedDt = FMath::Clamp(DeltaTime, 0.0005f, 0.25f);
	ElapsedSeconds += ClampedDt;

	// Skip initial shader/level-up hitch on frame 0-3 when recording steady-state frame stats
	if (ElapsedSeconds > 0.35)
	{
		RecordedFrameTimesMs.Add(ClampedDt * 1000.0f);
	}

	const double CurMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);
	PeakPhysicalMemMB = FMath::Max(PeakPhysicalMemMB, CurMemMB);

	if (ElapsedSeconds >= NextSecondSampleTime)
	{
		int32 TotalActors = 0;
		int32 LivingEnemies = 0;
		for (FActorIterator It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++TotalActors;
				if (const AIU_EnemyCharacter* E = Cast<AIU_EnemyCharacter>(*It))
				{
					if (!E->IsDead())
					{
						++LivingEnemies;
					}
				}
			}
		}
		const float InstFPS = 1.0f / ClampedDt;
		PerSecondCSVRows.Add(FString::Printf(TEXT("%.0f,%.1f,%.3f,%.1f,%d,%d"),
			NextSecondSampleTime, InstFPS, ClampedDt * 1000.0f, CurMemMB, TotalActors, LivingEnemies));
		NextSecondSampleTime += 1.0;
	}

	APlayerController* RawPC = UGameplayStatics::GetPlayerController(World, 0);
	AIU_PlayerCharacter* Player = RawPC ? Cast<AIU_PlayerCharacter>(RawPC->GetPawn()) : nullptr;
	AIU_HUDManager* HUD = RawPC ? Cast<AIU_HUDManager>(RawPC->GetHUD()) : nullptr;

	if (Player && RawPC)
	{
		// Cycle time modulo 22.5s so both 22.5s Demo Video and 30-min Soak execute the full Core Loop repeatedly!
		const double CycleT = FMath::Fmod(ElapsedSeconds, 22.5);
		AIU_EnemyCharacter* NearestEnemy = FindClosestLivingEnemy(Player->GetActorLocation());

		auto SetBanner = [HUD](const TCHAR* Text)
		{
			if (HUD)
			{
				HUD->SetShowcaseStepBanner(Text);
			}
		};

		if (CycleT < 2.0)
		{
			if (LastTriggeredStage != 1)
			{
				LastTriggeredStage = 1;
				bTookCounterHitInCycle = false;
				bTriggeredMirageEvadeInCycle = false;
				Player->StopSprintInput();
				Player->StopJumpInput();
				Player->StopDescendInput();
				SetBanner(TEXT("CORE LOOP [1/7]: Spawn Anime Fighter & 360 Camera Orbit"));
			}
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			Player->ApplyLookInput(FVector2D(1.35f, 0.0f));
		}
		else if (CycleT < 4.8)
		{
			if (LastTriggeredStage != 2)
			{
				LastTriggeredStage = 2;
				RawPC->SetControlRotation(FRotator(-8.5f, 180.0f, 0.0f)); // Face -X West Combat Arena
				Player->StartSprintInput();
				SetBanner(TEXT("CORE LOOP [2/7]: Ground Locomotion (Walk -> Sprint -> Jump)"));
			}
			Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
			if (CycleT > 3.6 && CycleT < 3.75 && !Player->GetCharacterMovement()->IsFalling())
			{
				Player->StartJumpInput();
			}
			else if (CycleT >= 3.85)
			{
				Player->StopJumpInput();
			}
		}
		else if (CycleT < 9.2)
		{
			if (LastTriggeredStage != 3)
			{
				LastTriggeredStage = 3;
				SetBanner(TEXT("CORE LOOP [3/7]: Free 3D Flight (Takeoff -> Boost -> Hover -> Land)"));
				Player->StartJumpInput();
			}

			if (CycleT < 5.15)
			{
				Player->StopJumpInput();
			}
			else if (CycleT < 5.35 && !Player->GetFlightComponent()->IsFlying())
			{
				Player->StartJumpInput(); // 2nd Space airborne -> Enter Free 3D Flight!
			}
			else if (CycleT >= 5.35 && CycleT < 7.2)
			{
				// Ascend + 2x Flight Boost toward Enemy Arena
				Player->StartSprintInput();
				if (NearestEnemy)
				{
					const FVector ToEnemy = (NearestEnemy->GetActorLocation() + FVector(0.0f, 0.0f, 220.0f) - Player->GetActorLocation()).GetSafeNormal();
					RawPC->SetControlRotation(FRotator(-10.0f, ToEnemy.Rotation().Yaw, 0.0f));
				}
				Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
			}
			else if (CycleT >= 7.2 && CycleT < 8.1)
			{
				// Mid-air stationary Hover
				Player->StopJumpInput();
				Player->StopSprintInput();
				Player->ApplyMovementInput(FVector2D::ZeroVector);
			}
			else
			{
				// Controlled Shift Descent & Touchdown near Enemy
				if (NearestEnemy && FVector::Dist2D(Player->GetActorLocation(), NearestEnemy->GetActorLocation()) > 520.0f)
				{
					const FVector ApproachSpot = NearestEnemy->GetActorLocation() + FVector(380.0f, 0.0f, 120.0f);
					Player->SetActorLocation(ApproachSpot);
				}
				Player->StartDescendInput();
			}
		}
		else if (CycleT < 12.4)
		{
			if (LastTriggeredStage != 4)
			{
				LastTriggeredStage = 4;
				Player->StopDescendInput();
				Player->StopJumpInput();
				if (Player->GetFlightComponent()->IsFlying())
				{
					Player->GetFlightComponent()->ExitFlight(true);
				}
				SetBanner(TEXT("CORE LOOP [4/7]: Target Lock-On & Luminous Ki Blast"));
				if (NearestEnemy)
				{
					NearestEnemy->GetAttributeComponent()->MaxHP = 95.0f;
					NearestEnemy->GetAttributeComponent()->ResetAttributesToFull();
					Player->GetTargetingComponent()->SetLockedTarget(NearestEnemy);
				}
			}

			if (ElapsedSeconds >= NextActionCooldownTime)
			{
				Player->KiBlastInput();
				NextActionCooldownTime = ElapsedSeconds + 0.95;
			}
		}
		else if (CycleT < 16.2)
		{
			if (LastTriggeredStage != 5)
			{
				LastTriggeredStage = 5;
				SetBanner(TEXT("CORE LOOP [5/7]: Enemy Counterattack & Wukong Mirage Evade"));
				if (NearestEnemy)
				{
					Player->SetActorLocation(NearestEnemy->GetActorLocation() + FVector(165.0f, 0.0f, 0.0f));
					Player->GetTargetingComponent()->SetLockedTarget(NearestEnemy);
				}
			}

			if (NearestEnemy && NearestEnemy->IsWindingUpAttack())
			{
				// Let 1st strike hit Player so HUD HP visibly drops, then Perfect Evade the 2nd strike!
				if (Player->GetAttributeComponent()->GetHPPercent() < 0.98f)
				{
					bTookCounterHitInCycle = true;
				}
				if (bTookCounterHitInCycle && !bTriggeredMirageEvadeInCycle)
				{
					Player->GetCombatComponent()->RequestDodge(FVector(0.0f, 1.0f, 0.0f));
					bTriggeredMirageEvadeInCycle = true;
				}
			}
		}
		else if (CycleT < 19.2)
		{
			if (LastTriggeredStage != 6)
			{
				LastTriggeredStage = 6;
				SetBanner(TEXT("CORE LOOP [6/7]: 3-Hit Melee Finisher & Enemy Defeated!"));
			}

			if (NearestEnemy && !NearestEnemy->IsDead())
			{
				Player->GetDamageComponent()->ResetDamageState();
				Player->SetActorLocation(NearestEnemy->GetActorLocation() - NearestEnemy->GetActorForwardVector() * -135.0f);
				Player->GetTargetingComponent()->SetLockedTarget(NearestEnemy);
				if (ElapsedSeconds >= NextActionCooldownTime)
				{
					Player->MeleeAttackInput();
					NextActionCooldownTime = ElapsedSeconds + 0.24;
				}
			}
		}
		else
		{
			if (LastTriggeredStage != 7)
			{
				LastTriggeredStage = 7;
				SetBanner(TEXT("CORE LOOP [7/7]: Spawner Auto-Respawn & Loop Ready!"));
			}
			if (NearestEnemy && !Player->GetTargetingComponent()->IsLockedOn())
			{
				Player->GetTargetingComponent()->SetLockedTarget(NearestEnemy);
			}
			Player->ApplyLookInput(FVector2D(0.55f, 0.0f));
		}
	}

	if (bRecordFrames && ElapsedSeconds >= NextFrameCaptureTime)
	{
		const FString FrameFile = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("DemoFrames"),
			FString::Printf(TEXT("frame_%04d.png"), CapturedFrameIndex++));
		FScreenshotRequest::RequestScreenshot(FrameFile, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		NextFrameCaptureTime += (1.0 / 15.0);
	}

	if (ElapsedSeconds >= TargetDurationSeconds)
	{
		WritePerformanceReportCSV();
		FGenericPlatformMisc::RequestExit(false);
	}
}

void UIU_AutoDemoSubsystem::WritePerformanceReportCSV() const
{
	const FString CSVPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("Step1_LiveD3D12Profile.csv"));
	FString OutText = FString::Join(PerSecondCSVRows, TEXT("\n")) + TEXT("\n");

	if (!RecordedFrameTimesMs.IsEmpty())
	{
		TArray<float> Sorted = RecordedFrameTimesMs;
		Sorted.Sort();
		double SumMs = 0.0;
		for (float V : Sorted)
		{
			SumMs += V;
		}
		const double AvgMs = SumMs / Sorted.Num();
		const double AvgFPS = 1000.0 / FMath::Max(AvgMs, 0.01);
		const int32 P99Idx = FMath::Clamp(FMath::FloorToInt(Sorted.Num() * 0.99f), 0, Sorted.Num() - 1);
		const double OnePercentLowFPS = 1000.0 / FMath::Max((double)Sorted[P99Idx], 0.01);
		const double EndMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);

		const FString SummaryLine = FString::Printf(
			TEXT("# SUMMARY: Frames=%d, AvgFPS=%.1f, 1PctLowFPS=%.1f, AvgFrameMs=%.3f, StartRAM_MB=%.1f, PeakRAM_MB=%.1f, EndRAM_MB=%.1f\n"),
			Sorted.Num(), AvgFPS, OnePercentLowFPS, AvgMs, StartPhysicalMemMB, PeakPhysicalMemMB, EndMemMB);
		OutText += SummaryLine;
		UE_LOG(LogInfiniteUniverse, Display, TEXT("[AutoDemoShowcase] %s"), *SummaryLine);
	}

	FFileHelper::SaveStringToFile(OutText, *CSVPath);
}
