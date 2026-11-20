// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_AutoDemoSubsystem.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
#include "Characters/Enemy/IU_WorldBossCharacter.h"
#include "Core/IU_ZonePortalActor.h"
#include "AI/IU_EnemySpawner.h"
#include "Movement/IU_FlightComponent.h"
#include "Movement/IU_MovementComponent.h"
#include "Combat/IU_CombatComponent.h"
#include "Combat/IU_CombatThreatSubsystem.h"
#include "Combat/IU_TargetingComponent.h"
#include "Combat/IU_DamageComponent.h"
#include "Attributes/IU_AttributeComponent.h"
#include "UI/IU_HUDManager.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "InfiniteUniverse.h"

namespace
{
	static TSet<FString> GClearedDemoFrameDirsInProcess;
	static TMap<FString, int32> GSharedFrameCounterByDir;
}

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

	const TCHAR* Cmd = FCommandLine::Get();
	int32 ParsedNetRole = 0;
	int32 ParsedStep3Phase = 0;
	int32 ParsedStep4Phase = 0;
	int32 ParsedStep5Phase = 0;
	int32 ParsedStep6Phase = 0;
	FParse::Value(Cmd, TEXT("AutoNetDemo="), ParsedNetRole);
	FParse::Value(Cmd, TEXT("AutoStep3Phase="), ParsedStep3Phase);
	FParse::Value(Cmd, TEXT("AutoStep4Phase="), ParsedStep4Phase);
	FParse::Value(Cmd, TEXT("AutoStep5Phase="), ParsedStep5Phase);
	FParse::Value(Cmd, TEXT("AutoStep6Demo="), ParsedStep6Phase);

	FString AutoAuthUser;
	FParse::Value(Cmd, TEXT("AutoAuthUser="), AutoAuthUser);
	// When client boots onto temporary local map while awaiting AutoAuth -> ClientTravel(?Ticket=...), wait for NM_Client world!
	if (!AutoAuthUser.IsEmpty() && (ParsedNetRole > 0 || ParsedStep3Phase > 0 || ParsedStep4Phase > 0 || ParsedStep5Phase > 0 || ParsedStep6Phase > 0) && World->GetNetMode() != NM_Client)
	{
		return false;
	}

	return FParse::Param(Cmd, TEXT("AutoDemoShowcase")) ||
		FParse::Param(Cmd, TEXT("AutoServerTelemetry")) ||
		ParsedNetRole > 0 ||
		ParsedStep3Phase > 0 ||
		ParsedStep4Phase > 0 ||
		ParsedStep5Phase > 0 ||
		ParsedStep6Phase > 0;
}

TStatId UIU_AutoDemoSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UIU_AutoDemoSubsystem, STATGROUP_Tickables);
}

void UIU_AutoDemoSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const TCHAR* Cmd = FCommandLine::Get();
	bSinglePlayerMode = FParse::Param(Cmd, TEXT("AutoDemoShowcase"));
	bServerTelemetryMode = FParse::Param(Cmd, TEXT("AutoServerTelemetry"));
	FParse::Value(Cmd, TEXT("AutoNetDemo="), NetDemoClientRole);
	FParse::Value(Cmd, TEXT("AutoStep3Phase="), Step3PersistencePhase);
	FParse::Value(Cmd, TEXT("AutoStep4Phase="), Step4EconomyPhase);
	FParse::Value(Cmd, TEXT("AutoStep5Phase="), Step5SocialPhase);
	FParse::Value(Cmd, TEXT("AutoStep6Demo="), Step6BossMultiZonePhase);
	if ((Step3PersistencePhase > 0 || Step4EconomyPhase > 0 || Step5SocialPhase > 0 || Step6BossMultiZonePhase > 0) && NetDemoClientRole == 0)
	{
		NetDemoClientRole = 1;
	}

	bRecordFrames = FParse::Param(Cmd, TEXT("RecordDemoFrames"));
	if (Step6BossMultiZonePhase > 0)
	{
		TargetDurationSeconds = 42.0;
		DemoFramesSubdir = FString::Printf(TEXT("Step6Frames_Client%d"), NetDemoClientRole);
		CSVOutputFilename = FString::Printf(TEXT("Step6_Client%dProfile.csv"), NetDemoClientRole);
	}
	else if (Step5SocialPhase > 0)
	{
		TargetDurationSeconds = (Step5SocialPhase == 1) ? 33.5 : 10.5;
		DemoFramesSubdir = FString::Printf(TEXT("Step5Frames_Phase%d_Client%d"), Step5SocialPhase, NetDemoClientRole);
		CSVOutputFilename = FString::Printf(TEXT("Step5_Phase%d_Client%dProfile.csv"), Step5SocialPhase, NetDemoClientRole);
	}
	else if (Step4EconomyPhase > 0)
	{
		TargetDurationSeconds = (Step4EconomyPhase == 1) ? 25.5 : 10.0;
		DemoFramesSubdir = FString::Printf(TEXT("Step4Frames_Phase%d_Client%d"), Step4EconomyPhase, NetDemoClientRole);
		CSVOutputFilename = FString::Printf(TEXT("Step4_Phase%d_Client%dProfile.csv"), Step4EconomyPhase, NetDemoClientRole);
	}
	else if (Step3PersistencePhase > 0)
	{
		TargetDurationSeconds = (Step3PersistencePhase == 1) ? 21.5 : 9.5;
		DemoFramesSubdir = FString::Printf(TEXT("Step3Frames_Phase%d_Client%d"), Step3PersistencePhase, NetDemoClientRole);
		CSVOutputFilename = FString::Printf(TEXT("Step3_Phase%d_Client%dProfile.csv"), Step3PersistencePhase, NetDemoClientRole);
	}
	else if (NetDemoClientRole > 0)
	{
		TargetDurationSeconds = 27.5;
		DemoFramesSubdir = FString::Printf(TEXT("Step2Frames_Client%d"), NetDemoClientRole);
		CSVOutputFilename = FString::Printf(TEXT("Step2_Client%dProfile.csv"), NetDemoClientRole);
	}
	else if (bServerTelemetryMode)
	{
		TargetDurationSeconds = 30.0;
		CSVOutputFilename = TEXT("Step2_DedicatedServerProfile.csv");
	}

	float CustomDur = 0.0f;
	if (FParse::Value(Cmd, TEXT("DemoDuration="), CustomDur) && CustomDur >= 5.0f)
	{
		TargetDurationSeconds = static_cast<double>(CustomDur);
	}
	FString CustomCSV;
	if (FParse::Value(Cmd, TEXT("CSVOutputName="), CustomCSV) && !CustomCSV.IsEmpty())
	{
		CSVOutputFilename = CustomCSV;
	}
	FString CustomFramesDir;
	if (FParse::Value(Cmd, TEXT("DemoFramesDir="), CustomFramesDir) && !CustomFramesDir.IsEmpty())
	{
		DemoFramesSubdir = CustomFramesDir;
	}

	StartPhysicalMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);
	PeakPhysicalMemMB = StartPhysicalMemMB;
	RecordedFrameTimesMs.Reserve(4096);
	PerSecondCSVRows.Add(TEXT("Second,AvgFPS_Or_TickHz,AvgFrameMs,UsedPhysicalRAM_MB,TotalActors,LivingEnemies,OnlinePlayers,NetIn_KBps,NetOut_KBps,PingMs"));

	if (bRecordFrames && !bServerTelemetryMode)
	{
		const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), DemoFramesSubdir);
		if (!GClearedDemoFrameDirsInProcess.Contains(DemoFramesSubdir))
		{
			GClearedDemoFrameDirsInProcess.Add(DemoFramesSubdir);
			GSharedFrameCounterByDir.Add(DemoFramesSubdir, 0);
			IFileManager::Get().DeleteDirectory(*OutDir, false, true);
			IFileManager::Get().MakeDirectory(*OutDir, true);
		}
		else
		{
			CapturedFrameIndex = GSharedFrameCounterByDir.FindRef(DemoFramesSubdir);
		}
	}

	if (InWorld.GetNetMode() != NM_Client)
	{
		for (TActorIterator<AIU_EnemySpawner> It(&InWorld); It; ++It)
		{
			if (AIU_EnemySpawner* Spawner = *It)
			{
				Spawner->RespawnDelaySeconds = 2.0f;
			}
		}
	}

	if (IConsoleVariable* IdleCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground")))
	{
		IdleCVar->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* UnfocusedCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.UnfocusedFrameRateLimit")))
	{
		UnfocusedCVar->Set(0.0f, ECVF_SetByCode);
	}

	UE_LOG(LogInfiniteUniverse, Display,
		TEXT("[AutoDemoSubsystem] Started on map '%s' | NetMode=%d | NetClientRole=%d | ServerTelemetry=%d | Duration=%.1fs | CSV='%s'"),
		*InWorld.GetName(), (int32)InWorld.GetNetMode(), NetDemoClientRole, bServerTelemetryMode ? 1 : 0, TargetDurationSeconds, *CSVOutputFilename);
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

AIU_WorldBossCharacter* UIU_AutoDemoSubsystem::FindWorldBossInArena() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AIU_WorldBossCharacter> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

AIU_PlayerCharacter* UIU_AutoDemoSubsystem::FindOtherPlayerCharacter(const AIU_PlayerCharacter* SelfPlayer) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AIU_PlayerCharacter> It(World); It; ++It)
	{
		AIU_PlayerCharacter* Cand = *It;
		if (IsValid(Cand) && Cand != SelfPlayer)
		{
			return Cand;
		}
	}
	return nullptr;
}

void UIU_AutoDemoSubsystem::SteerPlayerToward(
	AIU_PlayerCharacter* Player,
	APlayerController* RawPC,
	const FVector& GoalPos,
	float StopDistCm,
	bool bSprint,
	bool b3DPitch)
{
	if (!Player || !RawPC)
	{
		return;
	}

	const FVector Delta = GoalPos - Player->GetActorLocation();
	const float Dist = b3DPitch ? Delta.Size() : Delta.Size2D();
	if (!Delta.IsNearlyZero(1.0f))
	{
		const FRotator RawRot = Delta.Rotation();
		const float Pitch = b3DPitch ? FMath::Clamp(RawRot.Pitch, -45.0f, 45.0f) : -8.5f;
		RawPC->SetControlRotation(FRotator(Pitch, RawRot.Yaw, 0.0f));
	}

	if (Dist > StopDistCm)
	{
		if (bSprint)
		{
			Player->StartSprintInput();
		}
		else
		{
			Player->StopSprintInput();
		}
		Player->ApplyMovementInput(FVector2D(0.0f, 1.0f));
	}
	else
	{
		Player->StopSprintInput();
		Player->ApplyMovementInput(FVector2D::ZeroVector);
	}
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

	if (ElapsedSeconds > 0.35)
	{
		RecordedFrameTimesMs.Add(ClampedDt * 1000.0f);
	}

	const double CurMemMB = static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0);
	PeakPhysicalMemMB = FMath::Max(PeakPhysicalMemMB, CurMemMB);

	APlayerController* RawPC = (World->GetNetMode() != NM_DedicatedServer)
		? UGameplayStatics::GetPlayerController(World, 0)
		: nullptr;
	AIU_PlayerCharacter* Player = RawPC ? Cast<AIU_PlayerCharacter>(RawPC->GetPawn()) : nullptr;

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

		double InKBps = 0.0;
		double OutKBps = 0.0;
		if (const UNetDriver* ND = World->GetNetDriver())
		{
			int64 CurTotalInBytes = 0;
			int64 CurTotalOutBytes = 0;
			if (const UNetConnection* SrvConn = ND->ServerConnection)
			{
				CurTotalInBytes += SrvConn->InTotalBytes;
				CurTotalOutBytes += SrvConn->OutTotalBytes;
			}
			for (const UNetConnection* CliConn : ND->ClientConnections)
			{
				if (CliConn)
				{
					CurTotalInBytes += CliConn->InTotalBytes;
					CurTotalOutBytes += CliConn->OutTotalBytes;
				}
			}

			const int64 DeltaIn = FMath::Max<int64>(0, CurTotalInBytes - PrevTotalNetInBytes);
			const int64 DeltaOut = FMath::Max<int64>(0, CurTotalOutBytes - PrevTotalNetOutBytes);
			PrevTotalNetInBytes = CurTotalInBytes;
			PrevTotalNetOutBytes = CurTotalOutBytes;

			InKBps = static_cast<double>(DeltaIn) / 1024.0;
			OutKBps = static_cast<double>(DeltaOut) / 1024.0;
			if (InKBps > 0.0 || OutKBps > 0.0)
			{
				SumNetInKBps += InKBps;
				SumNetOutKBps += OutKBps;
				PeakNetInKBps = FMath::Max(PeakNetInKBps, InKBps);
				PeakNetOutKBps = FMath::Max(PeakNetOutKBps, OutKBps);
				++NetBandwidthSamples;
			}
		}

		const AIU_GameState* GS = World->GetGameState<AIU_GameState>();
		const int32 OnlinePlayers = GS ? GS->PlayerArray.Num() : (Player ? 1 : 0);
		float PingMs = 0.0f;
		if (Player && Player->GetPlayerState())
		{
			PingMs = Player->GetPlayerState()->GetPingInMilliseconds();
		}

		const float InstFPS = 1.0f / ClampedDt;
		PerSecondCSVRows.Add(FString::Printf(TEXT("%.0f,%.1f,%.3f,%.1f,%d,%d,%d,%.2f,%.2f,%.1f"),
			NextSecondSampleTime, InstFPS, ClampedDt * 1000.0f, CurMemMB, TotalActors, LivingEnemies, OnlinePlayers, InKBps, OutKBps, PingMs));
		NextSecondSampleTime += 1.0;
	}

	if (bSinglePlayerMode && Player && RawPC)
	{
		TickSinglePlayerShowcase(World, Player, RawPC);
	}
	else if (Step6BossMultiZonePhase > 0 && Player && RawPC)
	{
		ConnectedPlaySeconds += ClampedDt;
		TickStep6BossMultiZoneShowcase(World, Player, RawPC);
	}
	else if (Step5SocialPhase > 0 && Player && RawPC)
	{
		ConnectedPlaySeconds += ClampedDt;
		TickStep5SocialTradeShowcase(World, Player, RawPC);
	}
	else if (Step4EconomyPhase > 0 && Player && RawPC)
	{
		ConnectedPlaySeconds += ClampedDt;
		TickStep4InventoryShowcase(World, Player, RawPC);
	}
	else if (Step3PersistencePhase > 0 && Player && RawPC)
	{
		ConnectedPlaySeconds += ClampedDt;
		TickStep3PersistenceShowcase(World, Player, RawPC);
	}
	else if (NetDemoClientRole > 0 && Player && RawPC)
	{
		ConnectedPlaySeconds += ClampedDt;
		TickMultiplayerNetClient(World, Player, RawPC);
	}

	if (bRecordFrames && !bServerTelemetryMode && ElapsedSeconds >= NextFrameCaptureTime)
	{
		int32& SharedIdx = GSharedFrameCounterByDir.FindOrAdd(DemoFramesSubdir);
		CapturedFrameIndex = SharedIdx++;
		const FString FrameFile = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			DemoFramesSubdir,
			FString::Printf(TEXT("frame_%04d.png"), CapturedFrameIndex));
		FScreenshotRequest::RequestScreenshot(FrameFile, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		NextFrameCaptureTime += (1.0 / 15.0);
	}

	if (ElapsedSeconds >= TargetDurationSeconds)
	{
		if (!bSentCleanNetClose && World->GetNetMode() == NM_Client)
		{
			bSentCleanNetClose = true;
			if (UNetDriver* ND = World->GetNetDriver())
			{
				if (ND->ServerConnection)
				{
					ND->ServerConnection->Close();
					ND->ServerConnection->FlushNet(true);
				}
			}
		}
		WritePerformanceReportCSV();
		FGenericPlatformMisc::RequestExit(false);
	}
}

void UIU_AutoDemoSubsystem::TickMultiplayerNetClient(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC)
{
	AIU_HUDManager* HUD = Cast<AIU_HUDManager>(RawPC->GetHUD());
	auto SetBanner = [HUD](const TCHAR* Text)
	{
		if (HUD)
		{
			HUD->SetShowcaseStepBanner(Text);
		}
	};

	AIU_PlayerCharacter* OtherPlayer = FindOtherPlayerCharacter(Player);
	AIU_EnemyCharacter* NearestEnemy = FindClosestLivingEnemy(Player->GetActorLocation());
	const double T = FMath::Fmod(ConnectedPlaySeconds, 27.5);
	const bool bIsHeroTeamRole = (NetDemoClientRole % 2 == 1);
	const float LaneY = (NetDemoClientRole == 1) ? -180.0f
		: (NetDemoClientRole == 2) ? 180.0f
		: (NetDemoClientRole == 3) ? -65.0f
		: 65.0f;

	// =========================================================================
	// STAGE 1 (0.0s .. 4.0s): Co-op Courtyard Spawn & Friendly-Fire Authority Guard
	// =========================================================================
	if (T < 4.0)
	{
		if (LastTriggeredStage != 101)
		{
			LastTriggeredStage = 101;
			bTestedCoopFriendlyFireShot = false;
			SetBanner(TEXT("MULTIPLAYER [1/4]: Dedicated Server Spawn & Co-op Friendly-Fire Block"));
		}

		if (OtherPlayer)
		{
			SteerPlayerToward(Player, RawPC, OtherPlayer->GetActorLocation(), 200.0f, false, false);
			if (bIsHeroTeamRole && T > 2.0 && !bTestedCoopFriendlyFireShot)
			{
				bTestedCoopFriendlyFireShot = true;
				Player->KiBlastInput();
			}
			else if (bIsHeroTeamRole && T > 2.8 && T < 3.1 && ElapsedSeconds >= NextActionCooldownTime)
			{
				Player->MeleeAttackInput();
				NextActionCooldownTime = ElapsedSeconds + 0.45;
			}
		}
		else
		{
			Player->ApplyLookInput(FVector2D(0.9f, 0.0f));
		}
	}
	// =========================================================================
	// STAGE 2 (4.0s .. 10.2s): Synchronized Multi-Player Free 3D Flight Formation (FSavedMove)
	// =========================================================================
	else if (T < 10.2)
	{
		if (LastTriggeredStage != 102)
		{
			LastTriggeredStage = 102;
			SetBanner(TEXT("MULTIPLAYER [2/4]: Multi-Player 3D Flight Prediction -> West Martial Arena"));
			Player->StartJumpInput();
		}

		const FVector ArenaSkyWaypoint(-1650.0f, LaneY, 440.0f);

		if (T < 4.30)
		{
			Player->StartJumpInput();
		}
		else if (T < 4.50)
		{
			Player->StopJumpInput();
		}
		else if (T < 7.50)
		{
			if (!Player->GetFlightComponent()->IsFlying())
			{
				Player->StartJumpInput(); // 2nd Space airborne -> Enter CMOVE_FreeFlight!
			}
			else
			{
				// Release Ascend once at target altitude so both fly smoothly in formation!
				if (Player->GetActorLocation().Z > 430.0f)
				{
					Player->StopJumpInput();
				}
				SteerPlayerToward(Player, RawPC, ArenaSkyWaypoint, 120.0f, /*bSprint=*/true, /*b3DPitch=*/true);
			}
		}
		else if (T < 8.40)
		{
			// Synchronized mid-air Hover overlooking the Martial Arena
			Player->StopJumpInput();
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			if (OtherPlayer)
			{
				const FVector ToAlly = OtherPlayer->GetActorLocation() - Player->GetActorLocation();
				if (!ToAlly.IsNearlyZero(5.0f))
				{
					RawPC->SetControlRotation(FRotator(-12.0f, ToAlly.Rotation().Yaw, 0.0f));
				}
			}
		}
		else
		{
			// Controlled Shift Descent & Touchdown inside West Martial Arena
			Player->StopJumpInput();
			Player->StopSprintInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->StartDescendInput();
				SteerPlayerToward(Player, RawPC, FVector(-1720.0f, LaneY, 95.0f), 90.0f, false, false);
			}
			else
			{
				Player->StopDescendInput();
				Player->ApplyMovementInput(FVector2D::ZeroVector);
			}
		}
	}
	// =========================================================================
	// STAGE 3 (10.2s .. 17.4s): 2-Player Co-op Raid vs Server-Authoritative Enemy AI
	// =========================================================================
	else if (T < 17.4)
	{
		if (LastTriggeredStage != 103)
		{
			LastTriggeredStage = 103;
			bTriggeredMirageEvadeInCycle = false;
			Player->StopDescendInput();
			Player->StopJumpInput();
			Player->StopSprintInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}
			SetBanner(TEXT("MULTIPLAYER [3/4]: Co-op Raid vs Replicated Enemy AI & Respawn"));
		}

		if (NearestEnemy && !NearestEnemy->IsDead())
		{
			if (Player->GetTargetingComponent()->GetLockedTarget() != NearestEnemy)
			{
				Player->GetTargetingComponent()->SetLockedTarget(NearestEnemy);
			}

			SteerPlayerToward(Player, RawPC, NearestEnemy->GetActorLocation(), 160.0f, true, false);

			if (NearestEnemy->IsWindingUpAttack() && !bTriggeredMirageEvadeInCycle)
			{
				bTriggeredMirageEvadeInCycle = true;
				const float SideSign = (NetDemoClientRole == 1) ? 1.0f : -1.0f;
				Player->GetCombatComponent()->RequestDodge(FVector(0.0f, SideSign, 0.0f));
			}
			else if (ElapsedSeconds >= NextActionCooldownTime)
			{
				const float DistToE = FVector::Dist2D(Player->GetActorLocation(), NearestEnemy->GetActorLocation());
				if (DistToE > 230.0f)
				{
					Player->KiBlastInput();
					NextActionCooldownTime = ElapsedSeconds + 0.65;
				}
				else
				{
					Player->MeleeAttackInput();
					NextActionCooldownTime = ElapsedSeconds + 0.25;
				}
			}
		}
		else
		{
			SteerPlayerToward(Player, RawPC, FVector(-2250.0f, (NetDemoClientRole == 1) ? -120.0f : 120.0f, 95.0f), 80.0f, true, false);
		}
	}
	// =========================================================================
	// STAGE 4 (17.4s .. 27.5s): Live 2-Player PvP Martial Sparring & Mirage Evade!
	// =========================================================================
	else
	{
		if (LastTriggeredStage != 104)
		{
			LastTriggeredStage = 104;
			bTookCounterHitInCycle = false;
			bTriggeredMirageEvadeInCycle = false;
			SetBanner(TEXT("MULTIPLAYER [4/4]: 2-Player Martial Arena PvP & Wukong Mirage Evade!"));
		}

		// Ensure both players stand inside West Martial Sparring Arena (-4450..-800)
		if (OtherPlayer && !OtherPlayer->GetAttributeComponent()->IsDead() && !Player->GetAttributeComponent()->IsDead())
		{
			if (Player->GetTargetingComponent()->GetLockedTarget() != OtherPlayer)
			{
				Player->GetTargetingComponent()->SetLockedTarget(OtherPlayer);
			}

			// Keep sparring inside West Martial Arena around X = -2300
			const FVector SparAnchor(-2300.0f, 0.0f, 95.0f);
			if (Player->GetActorLocation().X > -1050.0f)
			{
				SteerPlayerToward(Player, RawPC, SparAnchor, 120.0f, true, false);
				return;
			}

			SteerPlayerToward(Player, RawPC, OtherPlayer->GetActorLocation(), 145.0f, true, false);

			if (T < 21.6)
			{
				// Phase 4A: Even-role fighters strike -> Odd-role fighters take 1st PvP hit, then Perfect Evade 2nd PvP attack!
				if (!bIsHeroTeamRole)
				{
					if (ElapsedSeconds >= NextActionCooldownTime)
					{
						const float DistP = FVector::Dist2D(Player->GetActorLocation(), OtherPlayer->GetActorLocation());
						if (DistP > 230.0f)
						{
							Player->KiBlastInput();
							NextActionCooldownTime = ElapsedSeconds + 0.55;
						}
						else
						{
							Player->MeleeAttackInput();
							NextActionCooldownTime = ElapsedSeconds + 0.30;
						}
					}
				}
				else
				{
					if (Player->GetAttributeComponent()->GetHPPercent() < 0.99f)
					{
						bTookCounterHitInCycle = true;
					}

					bool bRivalAttacking = (OtherPlayer->GetCombatComponent() &&
						OtherPlayer->GetCombatComponent()->GetCombatState() == EIU_CombatState::Attacking);
					if (!bRivalAttacking)
					{
						if (UIU_CombatThreatSubsystem* ThreatSub = World->GetSubsystem<UIU_CombatThreatSubsystem>())
						{
							bRivalAttacking = ThreatSub->HasImminentThreatAgainstActor(Player, 0.40f, 35.0f);
						}
					}

					if ((bTookCounterHitInCycle || T > 19.6) && bRivalAttacking && !bTriggeredMirageEvadeInCycle)
					{
						bTriggeredMirageEvadeInCycle = true;
						Player->GetCombatComponent()->RequestDodge(FVector(0.0f, 1.0f, 0.0f));
					}
				}
			}
			else
			{
				// Phase 4B: Odd-role fighters counterattack with 3-Hit Combo + Ki Blast in Arena!
				if (bIsHeroTeamRole && ElapsedSeconds >= NextActionCooldownTime)
				{
					const float DistP = FVector::Dist2D(Player->GetActorLocation(), OtherPlayer->GetActorLocation());
					if (DistP > 220.0f)
					{
						Player->KiBlastInput();
						NextActionCooldownTime = ElapsedSeconds + 0.45;
					}
					else
					{
						Player->MeleeAttackInput();
						NextActionCooldownTime = ElapsedSeconds + 0.24;
					}
				}
			}
		}
		else
		{
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			Player->ApplyLookInput(FVector2D(0.55f, 0.0f));
		}
	}
}

void UIU_AutoDemoSubsystem::TickSinglePlayerShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC)
{
	AIU_HUDManager* HUD = Cast<AIU_HUDManager>(RawPC->GetHUD());
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
			RawPC->SetControlRotation(FRotator(-8.5f, 180.0f, 0.0f));
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
			Player->StartJumpInput();
		}
		else if (CycleT >= 5.35 && CycleT < 7.2)
		{
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
			Player->StopJumpInput();
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
		}
		else
		{
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

void UIU_AutoDemoSubsystem::TickStep3PersistenceShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC)
{
	AIU_HUDManager* HUD = Cast<AIU_HUDManager>(RawPC->GetHUD());
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(RawPC);
	auto SetBanner = [HUD](const FString& Text)
	{
		if (HUD)
		{
			HUD->SetShowcaseStepBanner(Text);
		}
	};

	const AIU_PlayerState* IUPS = Player->GetPlayerState<AIU_PlayerState>();
	const int32 CurLv = IUPS ? IUPS->GetCharacterLevel() : 1;
	const int64 PoolTN = IUPS ? IUPS->GetPotentialPoints() : 0;
	const FString CharName = (IUPS && !IUPS->GetPersistedCharacterName().IsEmpty()) ? IUPS->GetPersistedCharacterName() : TEXT("Hero");
	const int64 CurEpoch = IUPS ? IUPS->GetActiveFencingEpoch() : 0;

	if (Step3PersistencePhase == 2)
	{
		// Phase 2: Post-Dedicated-Server-Restart State Restoration Verification!
		if (LastTriggeredStage != 302)
		{
			LastTriggeredStage = 302;
			Player->StopSprintInput();
			Player->StopJumpInput();
			Player->StopDescendInput();
		}
		if (IUPC && NetDemoClientRole == 1)
		{
			const bool bShowRestoredPanel = (ConnectedPlaySeconds >= 2.6 && ConnectedPlaySeconds <= 5.6);
			if (IUPC->IsPotentialUpgradePanelVisible() != bShowRestoredPanel)
			{
				IUPC->SetPotentialUpgradePanelVisible(bShowRestoredPanel, false);
			}
		}

		const FVector Loc = Player->GetActorLocation();
		const UIU_AttributeComponent* Attr = Player->GetAttributeComponent();
		SetBanner(FString::Printf(
			TEXT("STEP 3 [RESTORED]: %s Lv.%d | HP %.0f | Ki %.0f | Atk %.0f | TN %lld | Ep#%lld"),
			*CharName,
			CurLv,
			Attr->GetMaxHP(),
			Attr->GetMaxKi(),
			Attr->GetAttackPower(),
			PoolTN,
			CurEpoch));

		if (AIU_PlayerCharacter* Ally = FindOtherPlayerCharacter(Player))
		{
			const FVector ToAlly = Ally->GetActorLocation() - Loc;
			if (!ToAlly.IsNearlyZero(10.0f))
			{
				RawPC->SetControlRotation(FRotator(-9.0f, ToAlly.Rotation().Yaw, 0.0f));
			}
		}
		else
		{
			Player->ApplyLookInput(FVector2D(0.75f, 0.0f));
		}
		return;
	}

	// Phase 1: Register/Login (200 HP/Ki, 100 Atk) -> Fly -> Defeat Raiders (+LV EXP & +350 TN) -> Spend TN (+100 HP, +100 Ki, +10 Atk) -> Logout!
	const double T = ConnectedPlaySeconds;
	const float LaneY = (NetDemoClientRole == 1) ? -220.0f : 320.0f;
	const FVector RaidAnchor = (NetDemoClientRole == 1)
		? FVector(-1850.0f, -200.0f, 95.0f)
		: FVector(-2950.0f, 350.0f, 95.0f);

	if (T < 2.2)
	{
		if (LastTriggeredStage != 201)
		{
			LastTriggeredStage = 201;
			SetBanner(FString::Printf(TEXT("STEP 3 [1/4]: Base 200 HP / 200 Ki / 100 SucDanh (Epoch #%lld)"), CurEpoch));
		}
		if (AIU_PlayerCharacter* Ally = FindOtherPlayerCharacter(Player))
		{
			SteerPlayerToward(Player, RawPC, Ally->GetActorLocation(), 210.0f, false, false);
		}
		else
		{
			Player->ApplyLookInput(FVector2D(0.85f, 0.0f));
		}
	}
	else if (T < 6.8)
	{
		if (LastTriggeredStage != 202)
		{
			LastTriggeredStage = 202;
			SetBanner(TEXT("STEP 3 [2/4]: 3D Flight -> West Martial Arena Raid Zone"));
			Player->StartJumpInput();
		}

		const FVector SkyTarget(-1820.0f, LaneY, 410.0f);
		if (T < 2.45)
		{
			Player->StartJumpInput();
		}
		else if (T < 2.65)
		{
			Player->StopJumpInput();
		}
		else if (T < 5.3)
		{
			if (!Player->GetFlightComponent()->IsFlying())
			{
				Player->StartJumpInput();
			}
			else
			{
				if (Player->GetActorLocation().Z > 400.0f) { Player->StopJumpInput(); }
				SteerPlayerToward(Player, RawPC, SkyTarget, 120.0f, true, true);
			}
		}
		else
		{
			Player->StopJumpInput();
			Player->StopSprintInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->StartDescendInput();
				SteerPlayerToward(Player, RawPC, RaidAnchor, 90.0f, false, false);
			}
			else
			{
				Player->StopDescendInput();
			}
		}
	}
	else if (T < 16.8 && !bDispatchedStep3PotentialAllocations && (CurLv < 2 || PoolTN < 350))
	{
		if (LastTriggeredStage != 203)
		{
			LastTriggeredStage = 203;
			Player->StopDescendInput();
			Player->StopJumpInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}
			SetBanner(TEXT("STEP 3 [3/4]: Hit Monsters -> +1.5x Dame Suc Manh & Tiem Nang Per Hit!"));
		}

		AIU_EnemyCharacter* TargetEnemy = FindClosestLivingEnemy(RaidAnchor);
		if (!TargetEnemy)
		{
			TargetEnemy = FindClosestLivingEnemy(Player->GetActorLocation());
		}

		if (TargetEnemy && !TargetEnemy->IsDead())
		{
			if (Player->GetTargetingComponent()->GetLockedTarget() != TargetEnemy)
			{
				Player->GetTargetingComponent()->SetLockedTarget(TargetEnemy);
			}
			SteerPlayerToward(Player, RawPC, TargetEnemy->GetActorLocation(), 140.0f, true, false);
			if (ElapsedSeconds >= NextActionCooldownTime)
			{
				const float DistE = FVector::Dist2D(Player->GetActorLocation(), TargetEnemy->GetActorLocation());
				if (DistE > 220.0f)
				{
					Player->KiBlastInput();
					NextActionCooldownTime = ElapsedSeconds + 0.55;
				}
				else
				{
					Player->MeleeAttackInput();
					NextActionCooldownTime = ElapsedSeconds + 0.22;
				}
			}
		}
		else
		{
			SteerPlayerToward(Player, RawPC, RaidAnchor, 80.0f, true, false);
		}
	}
	else
	{
		// Allocate Potential Points (+100 HP [200 TN], +100 Ki [200 TN], +10 Atk [250 TN]) & move to Co-op Safe Overlook (-700, +/-180)!
		if (LastTriggeredStage != 204)
		{
			LastTriggeredStage = 204;
			Player->StopDescendInput();
			Player->StopJumpInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}
		}

		if (!bDispatchedStep3PotentialAllocations && IUPC && PoolTN >= 350)
		{
			bDispatchedStep3PotentialAllocations = true;
			Step3PotentialAllocTime = ConnectedPlaySeconds;
			IUPC->Server_RequestAllocatePotential(EIU_PotentialStatType::MaxHP);
			IUPC->Server_RequestAllocatePotential(EIU_PotentialStatType::MaxKi);
			IUPC->Server_RequestAllocatePotential(EIU_PotentialStatType::AttackPower);
		}

		if (IUPC && NetDemoClientRole == 1 && bDispatchedStep3PotentialAllocations)
		{
			const double SinceAlloc = ConnectedPlaySeconds - Step3PotentialAllocTime;
			const bool bShowPanel = (SinceAlloc >= 0.35 && SinceAlloc <= 2.85);
			if (IUPC->IsPotentialUpgradePanelVisible() != bShowPanel)
			{
				IUPC->SetPotentialUpgradePanelVisible(bShowPanel, false);
			}
		}

		// X = -320 is deep inside Co-op Safe Courtyard (far from West Arena Raiders) -> clean logout & untouched Phase 2 spawn!
		const FVector CheckpointSpot(-320.0f, (NetDemoClientRole == 1) ? -180.0f : 180.0f, 112.0f);
		if (FVector::Dist2D(Player->GetActorLocation(), CheckpointSpot) > 105.0f)
		{
			SteerPlayerToward(Player, RawPC, CheckpointSpot, 80.0f, true, false);
		}
		else
		{
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			Player->ApplyLookInput(FVector2D(0.65f, 0.0f));
		}

		const UIU_AttributeComponent* Attr = Player->GetAttributeComponent();
		SetBanner(FString::Printf(
			TEXT("STEP 3 [4/4]: %s Lv.%d | HP %.0f | Ki %.0f | Atk %.0f (TN:%lld) -> LOGOUT!"),
			*CharName, CurLv, Attr->GetMaxHP(), Attr->GetMaxKi(), Attr->GetAttackPower(), PoolTN));
	}
}

void UIU_AutoDemoSubsystem::TickStep4InventoryShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC)
{
	AIU_HUDManager* HUD = Cast<AIU_HUDManager>(RawPC->GetHUD());
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(RawPC);
	auto SetBanner = [HUD](const FString& Text)
	{
		if (HUD)
		{
			HUD->SetShowcaseStepBanner(Text);
		}
	};

	const AIU_PlayerState* IUPS = Player->GetPlayerState<AIU_PlayerState>();
	const int32 CurLv = IUPS ? IUPS->GetCharacterLevel() : 1;
	const int64 PoolTN = IUPS ? IUPS->GetPotentialPoints() : 0;
	const int64 CoinBal = IUPS ? IUPS->GetStandardCoin() : 0;
	const int32 EqCount = IUPS ? IUPS->GetEquippedItemCount() : 0;
	const FString CharName = (IUPS && !IUPS->GetPersistedCharacterName().IsEmpty()) ? IUPS->GetPersistedCharacterName() : TEXT("Hero");
	const int64 CurEpoch = IUPS ? IUPS->GetActiveFencingEpoch() : 0;
	const UIU_AttributeComponent* Attr = Player->GetAttributeComponent();

	// =========================================================================
	// PHASE 2: Post-Dedicated-Server-Restart Relogin Verification (Wallet + Bag + 8-Slot Equip + Enhance!)
	// =========================================================================
	if (Step4EconomyPhase == 2)
	{
		if (LastTriggeredStage != 402)
		{
			LastTriggeredStage = 402;
			Player->StopSprintInput();
			Player->StopJumpInput();
			Player->StopDescendInput();
			if (IUPC)
			{
				IUPC->Server_RequestRefreshInventory();
			}
		}

		if (IUPC && NetDemoClientRole == 1)
		{
			const bool bShowInvModal = (ConnectedPlaySeconds >= 2.0 && ConnectedPlaySeconds <= 7.8);
			if (IUPC->IsInventoryPanelVisible() != bShowInvModal)
			{
				IUPC->SetInventoryPanelVisible(bShowInvModal, false);
			}
		}

		SetBanner(FString::Printf(
			TEXT("STEP 4 [RESTORED]: %s Lv.%d | Vang:%lld | Equip:%d/8 | HP %.0f | Atk %.0f | Giap %.0f | Ep#%lld"),
			*CharName,
			CurLv,
			CoinBal,
			EqCount,
			Attr ? Attr->GetMaxHP() : 200.0f,
			Attr ? Attr->GetAttackPower() : 100.0f,
			Attr ? Attr->GetDefense() : 20.0f,
			CurEpoch));

		if (AIU_PlayerCharacter* Ally = FindOtherPlayerCharacter(Player))
		{
			const FVector ToAlly = Ally->GetActorLocation() - Player->GetActorLocation();
			if (!ToAlly.IsNearlyZero(10.0f))
			{
				RawPC->SetControlRotation(FRotator(-9.0f, ToAlly.Rotation().Yaw, 0.0f));
			}
		}
		else
		{
			Player->ApplyLookInput(FVector2D(0.70f, 0.0f));
		}
		return;
	}

	// =========================================================================
	// PHASE 1: Starter Armor Equip -> Raid Monsters (+CSPRNG Vang & Loot) -> Dau Than [4] -> Equip Loot & Enhance +1/+2 -> Safe Logout
	// =========================================================================
	const double T = ConnectedPlaySeconds;
	const float LaneY = (NetDemoClientRole == 1) ? -220.0f : 320.0f;
	const FVector RaidAnchor = (NetDemoClientRole == 1)
		? FVector(-1850.0f, -200.0f, 95.0f)
		: FVector(-2950.0f, 350.0f, 95.0f);

	if (T < 3.0)
	{
		if (LastTriggeredStage != 411)
		{
			LastTriggeredStage = 411;
			if (IUPC)
			{
				IUPC->Server_RequestRefreshInventory();
			}
			SetBanner(FString::Printf(TEXT("STEP 4 [1/5]: Login & Equip Starter Armor (+80 HP, +8 Giap | Ep#%lld)"), CurEpoch));
		}

		if (IUPC && !bStep4EquippedStarterArmor && T >= 0.9 && IUPC->HasCachedInventorySnapshot())
		{
			for (const FIU_InventoryItemView& Item : IUPC->GetCachedInventorySnapshot().BagItems)
			{
				if (Item.Category == TEXT("EQUIPMENT") && Item.RequiredLevel <= CurLv)
				{
					bStep4EquippedStarterArmor = true;
					IUPC->Server_RequestEquipItem(Item.InstanceID);
					break;
				}
			}
		}

		if (IUPC && NetDemoClientRole == 1)
		{
			const bool bOpenStarterBag = (T >= 1.2 && T <= 2.7);
			if (IUPC->IsInventoryPanelVisible() != bOpenStarterBag)
			{
				IUPC->SetInventoryPanelVisible(bOpenStarterBag, false);
			}
		}

		if (AIU_PlayerCharacter* Ally = FindOtherPlayerCharacter(Player))
		{
			SteerPlayerToward(Player, RawPC, Ally->GetActorLocation(), 210.0f, false, false);
		}
		else
		{
			Player->ApplyLookInput(FVector2D(0.80f, 0.0f));
		}
	}
	else if (T < 7.2)
	{
		if (LastTriggeredStage != 412)
		{
			LastTriggeredStage = 412;
			if (IUPC && IUPC->IsInventoryPanelVisible())
			{
				IUPC->SetInventoryPanelVisible(false, false);
			}
			SetBanner(TEXT("STEP 4 [2/5]: 3D Flight -> West Raid Zone (CSPRNG Loot & Vang Hunt)"));
			Player->StartJumpInput();
		}

		const FVector SkyTarget(-1820.0f, LaneY, 410.0f);
		if (T < 3.25)
		{
			Player->StartJumpInput();
		}
		else if (T < 3.45)
		{
			Player->StopJumpInput();
		}
		else if (T < 5.7)
		{
			if (!Player->GetFlightComponent()->IsFlying())
			{
				Player->StartJumpInput();
			}
			else
			{
				if (Player->GetActorLocation().Z > 400.0f) { Player->StopJumpInput(); }
				SteerPlayerToward(Player, RawPC, SkyTarget, 120.0f, true, true);
			}
		}
		else
		{
			Player->StopJumpInput();
			Player->StopSprintInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->StartDescendInput();
				SteerPlayerToward(Player, RawPC, RaidAnchor, 90.0f, false, false);
			}
			else
			{
				Player->StopDescendInput();
			}
		}
	}
	else if (T < 16.5 && (CoinBal < 260 || CurLv < 2))
	{
		if (LastTriggeredStage != 413)
		{
			LastTriggeredStage = 413;
			Player->StopDescendInput();
			Player->StopJumpInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}
			SetBanner(TEXT("STEP 4 [3/5]: Defeat Raiders -> CSPRNG Vang + Personal Loot + 1.5x Dame SM/TN!"));
		}

		AIU_EnemyCharacter* TargetEnemy = FindClosestLivingEnemy(RaidAnchor);
		if (!TargetEnemy)
		{
			TargetEnemy = FindClosestLivingEnemy(Player->GetActorLocation());
		}

		if (TargetEnemy && !TargetEnemy->IsDead())
		{
			if (Player->GetTargetingComponent()->GetLockedTarget() != TargetEnemy)
			{
				Player->GetTargetingComponent()->SetLockedTarget(TargetEnemy);
			}
			SteerPlayerToward(Player, RawPC, TargetEnemy->GetActorLocation(), 140.0f, true, false);
			if (ElapsedSeconds >= NextActionCooldownTime)
			{
				const float DistE = FVector::Dist2D(Player->GetActorLocation(), TargetEnemy->GetActorLocation());
				if (DistE > 220.0f)
				{
					Player->KiBlastInput();
					NextActionCooldownTime = ElapsedSeconds + 0.50;
				}
				else
				{
					Player->MeleeAttackInput();
					NextActionCooldownTime = ElapsedSeconds + 0.21;
				}
			}
		}
		else
		{
			SteerPlayerToward(Player, RawPC, RaidAnchor, 80.0f, true, false);
		}
	}
	else if (T < 20.6)
	{
		if (LastTriggeredStage != 414)
		{
			LastTriggeredStage = 414;
			Player->StopDescendInput();
			Player->StopJumpInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}
			SetBanner(TEXT("STEP 4 [4/5]: Use Dau Than [4], Equip Looted Gear & Enhance +1/+2!"));
		}

		if (IUPC && !bStep4UsedElixirAndEquippedLoot)
		{
			bStep4UsedElixirAndEquippedLoot = true;
			IUPC->TriggerQuickUseElixirFromHUD();

			if (PoolTN >= 150)
			{
				IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::KyNang, false);
				IUPC->Server_RequestAllocatePotential(EIU_PotentialStatType::AttackPower);
			}

			if (IUPC->HasCachedInventorySnapshot())
			{
				TSet<FString> EquippedSlotsSeen;
				for (const FIU_InventoryItemView& Eq : IUPC->GetCachedInventorySnapshot().EquippedItems)
				{
					EquippedSlotsSeen.Add(Eq.EquippedSlotID);
				}
				for (const FIU_InventoryItemView& BagItem : IUPC->GetCachedInventorySnapshot().BagItems)
				{
					if (BagItem.Category == TEXT("EQUIPMENT") && BagItem.RequiredLevel <= CurLv && !EquippedSlotsSeen.Contains(BagItem.EquipSlot))
					{
						EquippedSlotsSeen.Add(BagItem.EquipSlot);
						IUPC->Server_RequestEquipItem(BagItem.InstanceID);
					}
				}
			}
		}

		if (IUPC && !bStep4EnhancedArmorOnce && T >= 18.2 && IUPC->HasCachedInventorySnapshot() && CoinBal >= 100)
		{
			for (const FIU_InventoryItemView& Eq : IUPC->GetCachedInventorySnapshot().EquippedItems)
			{
				if (Eq.MaxEnhanceLevel > 0 && Eq.EnhanceLevel < Eq.MaxEnhanceLevel)
				{
					bStep4EnhancedArmorOnce = true;
					IUPC->Server_RequestEnhanceItem(Eq.InstanceID);
					break;
				}
			}
		}

		if (IUPC && bStep4EnhancedArmorOnce && !bStep4EnhancedArmorTwice && T >= 19.4 && IUPC->HasCachedInventorySnapshot() && CoinBal >= 150)
		{
			for (const FIU_InventoryItemView& Eq : IUPC->GetCachedInventorySnapshot().EquippedItems)
			{
				if (Eq.MaxEnhanceLevel > 0 && Eq.EnhanceLevel < Eq.MaxEnhanceLevel)
				{
					bStep4EnhancedArmorTwice = true;
					IUPC->Server_RequestEnhanceItem(Eq.InstanceID);
					break;
				}
			}
		}

		if (IUPC && NetDemoClientRole == 1 && T >= 19.0)
		{
			if (IUPC->IsInventoryPanelVisible())
			{
				IUPC->SetInventoryPanelVisible(false, false);
			}
		}

		const FVector CheckpointSpot(-320.0f, (NetDemoClientRole == 1) ? -180.0f : 180.0f, 112.0f);
		SteerPlayerToward(Player, RawPC, CheckpointSpot, 80.0f, true, false);
	}
	else
	{
		if (LastTriggeredStage != 415)
		{
			LastTriggeredStage = 415;
		}

		if (IUPC && NetDemoClientRole == 1)
		{
			const bool bShowFinalModal = (T >= 21.0 && T <= 24.8);
			if (bShowFinalModal && !IUPC->IsInventoryPanelVisible())
			{
				IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::HanhTrang, false);
			}
			else if (!bShowFinalModal && IUPC->IsInventoryPanelVisible())
			{
				IUPC->SetInventoryPanelVisible(false, false);
			}
		}

		const FVector CheckpointSpot(-320.0f, (NetDemoClientRole == 1) ? -180.0f : 180.0f, 112.0f);
		if (FVector::Dist2D(Player->GetActorLocation(), CheckpointSpot) > 105.0f)
		{
			SteerPlayerToward(Player, RawPC, CheckpointSpot, 80.0f, true, false);
		}
		else
		{
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			Player->ApplyLookInput(FVector2D(0.55f, 0.0f));
		}

		SetBanner(FString::Printf(
			TEXT("STEP 4 [5/5]: %s Lv.%d | Vang:%lld | Equip:%d/8 | HP %.0f | Atk %.0f | Giap %.0f -> LOGOUT!"),
			*CharName,
			CurLv,
			CoinBal,
			EqCount,
			Attr ? Attr->GetMaxHP() : 200.0f,
			Attr ? Attr->GetAttackPower() : 100.0f,
			Attr ? Attr->GetDefense() : 20.0f));
	}
}

void UIU_AutoDemoSubsystem::TickStep5SocialTradeShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC)
{
	AIU_HUDManager* HUD = Cast<AIU_HUDManager>(RawPC->GetHUD());
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(RawPC);
	auto SetBanner = [HUD](const FString& Text)
	{
		if (HUD)
		{
			HUD->SetShowcaseStepBanner(Text);
		}
	};

	const AIU_PlayerState* IUPS = Player->GetPlayerState<AIU_PlayerState>();
	const int32 CurLv = IUPS ? IUPS->GetCharacterLevel() : 1;
	const int64 CoinBal = IUPS ? IUPS->GetStandardCoin() : 0;
	const FString CharName = (IUPS && !IUPS->GetPersistedCharacterName().IsEmpty()) ? IUPS->GetPersistedCharacterName() : TEXT("Hero");
	const FString GuildTag = IUPS ? IUPS->GetGuildTag() : TEXT("");
	const int64 CurEpoch = IUPS ? IUPS->GetActiveFencingEpoch() : 0;
	AIU_PlayerCharacter* Ally = FindOtherPlayerCharacter(Player);

	// =========================================================================
	// PHASE 2: Post-Dedicated-Server-Restart Guild [ZNT], Traded Gear & Ledger Vang Verification!
	// =========================================================================
	if (Step5SocialPhase == 2)
	{
		if (LastTriggeredStage != 502)
		{
			LastTriggeredStage = 502;
			Player->StopSprintInput();
			Player->StopJumpInput();
			Player->StopDescendInput();
			if (IUPC)
			{
				IUPC->Server_RequestRefreshInventory();
				IUPC->Server_RequestRefreshGuild();
			}
		}

		if (IUPC && NetDemoClientRole == 1)
		{
			if (ConnectedPlaySeconds >= 1.8 && ConnectedPlaySeconds < 5.4)
			{
				if (!IUPC->IsInventoryPanelVisible() ||
					(IUPC->GetActiveCharacterProfileWidget() && IUPC->GetActiveCharacterProfileWidget()->GetActiveProfileTab() != EIU_CharacterProfileTab::BangHoi))
				{
					IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::BangHoi, false);
				}
			}
			else if (ConnectedPlaySeconds >= 5.4 && ConnectedPlaySeconds < 8.8)
			{
				if (!IUPC->IsInventoryPanelVisible() ||
					(IUPC->GetActiveCharacterProfileWidget() && IUPC->GetActiveCharacterProfileWidget()->GetActiveProfileTab() != EIU_CharacterProfileTab::HanhTrang))
				{
					IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::HanhTrang, false);
				}
			}
			else if (IUPC->IsInventoryPanelVisible())
			{
				IUPC->SetInventoryPanelVisible(false, false);
			}
		}

		if (Ally)
		{
			const FVector ToAlly = Ally->GetActorLocation() - Player->GetActorLocation();
			if (!ToAlly.IsNearlyZero(10.0f))
			{
				RawPC->SetControlRotation(FRotator(-9.0f, ToAlly.Rotation().Yaw, 0.0f));
			}
		}
		else
		{
			Player->ApplyLookInput(FVector2D(0.60f, 0.0f));
		}

		SetBanner(FString::Printf(
			TEXT("STEP 5 [RESTORED]: [%s] %s Lv.%d | Vang:%lld | Ep#%lld (Bang Hoi & Do Giao Dich Bao Toan 100%%)"),
			GuildTag.IsEmpty() ? TEXT("---") : *GuildTag,
			*CharName,
			CurLv,
			CoinBal,
			CurEpoch));
		return;
	}

	// =========================================================================
	// PHASE 1: Party + 3D Ping + Speech Bubble -> Co-op Raid (+15% Synergy) -> Guild [ZNT] -> 2-Step P2P Trade -> Logout
	// =========================================================================
	const double T = ConnectedPlaySeconds;
	const FVector RaidAnchor(-1850.0f, (NetDemoClientRole == 1) ? -140.0f : 140.0f, 95.0f);
	const FVector CourtyardSpot(-320.0f, (NetDemoClientRole == 1) ? -105.0f : 105.0f, 112.0f);

	if (T < 4.5)
	{
		if (LastTriggeredStage != 511)
		{
			LastTriggeredStage = 511;
			if (IUPC)
			{
				IUPC->Server_RequestRefreshInventory();
			}
			SetBanner(TEXT("STEP 5 [1/5]: Lap Party 5 Nguoi [Y], Bong Bong Chat 3D & Ping Khong Chien [Alt+LMB]"));
		}

		if (Ally)
		{
			SteerPlayerToward(Player, RawPC, Ally->GetActorLocation(), 210.0f, false, false);
		}

		if (IUPC && !bStep5SentPartyAction)
		{
			if (NetDemoClientRole == 1 && T >= 1.2 && Ally)
			{
				bStep5SentPartyAction = true;
				IUPC->Server_RequestPartyInvite(TEXT(""));
			}
			else if (NetDemoClientRole == 2 && T >= 2.2)
			{
				bStep5SentPartyAction = true;
				IUPC->Server_RequestPartyRespond(TEXT(""), true);
			}
		}

		if (IUPC && !bStep5SentPingAndChat && T >= 2.8)
		{
			bStep5SentPingAndChat = true;
			if (NetDemoClientRole == 1)
			{
				IUPC->Server_RequestPlaceTacticalPing(FVector(-620.0f, 0.0f, 112.0f));
				IUPC->Server_RequestSendChat(TEXT("LOCAL"), TEXT("Anh em vao Party san Raider nhan +15% SM/TN!"), TEXT(""));
			}
			else
			{
				IUPC->Server_RequestSendChat(TEXT("LOCAL"), TEXT("San sang! Tien ve khu Raider thoi!"), TEXT(""));
			}
		}
	}
	else if (T < 16.2 && (CoinBal < 260 || CurLv < 2))
	{
		if (LastTriggeredStage != 512)
		{
			LastTriggeredStage = 512;
			SetBanner(TEXT("STEP 5 [2/5]: Party Co-op Raid (1.5x Dame * 1.15 Party Synergy + Personal Party Loot)"));
		}

		AIU_EnemyCharacter* TargetEnemy = FindClosestLivingEnemy(RaidAnchor);
		if (!TargetEnemy)
		{
			TargetEnemy = FindClosestLivingEnemy(Player->GetActorLocation());
		}

		if (TargetEnemy && !TargetEnemy->IsDead())
		{
			if (Player->GetTargetingComponent()->GetLockedTarget() != TargetEnemy)
			{
				Player->GetTargetingComponent()->SetLockedTarget(TargetEnemy);
			}
			SteerPlayerToward(Player, RawPC, TargetEnemy->GetActorLocation(), 145.0f, true, false);
			if (ElapsedSeconds >= NextActionCooldownTime)
			{
				const float DistE = FVector::Dist2D(Player->GetActorLocation(), TargetEnemy->GetActorLocation());
				if (DistE > 220.0f)
				{
					Player->KiBlastInput();
					NextActionCooldownTime = ElapsedSeconds + 0.48;
				}
				else
				{
					Player->MeleeAttackInput();
					NextActionCooldownTime = ElapsedSeconds + 0.21;
				}
			}
		}
		else
		{
			SteerPlayerToward(Player, RawPC, RaidAnchor, 90.0f, true, false);
		}
	}
	else if (T < 21.4)
	{
		if (LastTriggeredStage != 513)
		{
			LastTriggeredStage = 513;
			if (IUPC)
			{
				IUPC->Server_RequestRefreshInventory();
			}
			SetBanner(TEXT("STEP 5 [3/5]: Tab [Bang Hoi] -> Lap Bang [ZNT] (-200 Vang), Gia Nhap & Gop Quy Bang!"));
		}

		if (FVector::Dist2D(Player->GetActorLocation(), CourtyardSpot) > 90.0f)
		{
			SteerPlayerToward(Player, RawPC, CourtyardSpot, 70.0f, true, false);
		}
		else
		{
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			if (Ally)
			{
				const FVector ToAlly = Ally->GetActorLocation() - Player->GetActorLocation();
				if (!ToAlly.IsNearlyZero(5.0f))
				{
					RawPC->SetControlRotation(FRotator(-8.5f, ToAlly.Rotation().Yaw, 0.0f));
				}
			}
		}

		if (IUPC && !bStep5ExecutedGuildAction)
		{
			if (NetDemoClientRole == 1 && T >= 16.6)
			{
				bStep5ExecutedGuildAction = true;
				IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::BangHoi, false);
				IUPC->Server_RequestCreateGuild(TEXT("Zenith Vanguards"), TEXT("ZNT"), TEXT("Bang hoi Chien Binh Vu Tru!"));
			}
			else if (NetDemoClientRole == 2 && T >= 17.8)
			{
				IUPC->Server_RequestRefreshGuild();
				if (IUPC->HasCachedGuildSnapshot() && IUPC->GetCachedGuildSnapshot().Directory.Num() > 0)
				{
					FString TargetGuildID;
					for (const FIU_GuildDirectoryEntry& DirEntry : IUPC->GetCachedGuildSnapshot().Directory)
					{
						if (DirEntry.GuildTag.Equals(TEXT("ZNT"), ESearchCase::IgnoreCase))
						{
							TargetGuildID = DirEntry.GuildID;
							break;
						}
					}
					if (!TargetGuildID.IsEmpty())
					{
						bStep5ExecutedGuildAction = true;
						IUPC->Server_RequestJoinGuild(TargetGuildID);
					}
				}
			}
		}

		if (IUPC && bStep5ExecutedGuildAction && !bStep5SentGuildDonateAndChat && T >= 19.3)
		{
			bStep5SentGuildDonateAndChat = true;
			if (NetDemoClientRole == 2 && CoinBal >= 50)
			{
				IUPC->Server_RequestDonateGuildCoins(50);
				IUPC->Server_RequestSendChat(TEXT("GUILD"), TEXT("Da vao Bang [ZNT] va gop 50 Vang vao Quy Bang!"), TEXT(""));
			}
			else if (NetDemoClientRole == 1)
			{
				IUPC->Server_RequestRefreshGuild();
				IUPC->Server_RequestSendChat(TEXT("GUILD"), TEXT("Chao mung thanh vien moi cua Bang [ZNT]!"), TEXT(""));
			}
		}
	}
	else if (T < 30.2)
	{
		if (LastTriggeredStage != 514)
		{
			LastTriggeredStage = 514;
			if (IUPC && IUPC->IsInventoryPanelVisible())
			{
				IUPC->SetInventoryPanelVisible(false, false);
			}
			SetBanner(TEXT("STEP 5 [4/5]: Giao Dich P2P 2 Buoc (Khoa Do & 100 Vang -> Dem Nguoc 3.0s -> Xac Nhan!)"));
		}

		// Keep both players standing within 210cm (<= 600cm trade range) facing each other!
		if (FVector::Dist2D(Player->GetActorLocation(), CourtyardSpot) > 85.0f)
		{
			SteerPlayerToward(Player, RawPC, CourtyardSpot, 65.0f, true, false);
		}
		else
		{
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
			if (Ally)
			{
				const FVector ToAlly = Ally->GetActorLocation() - Player->GetActorLocation();
				if (!ToAlly.IsNearlyZero(5.0f))
				{
					RawPC->SetControlRotation(FRotator(-8.5f, ToAlly.Rotation().Yaw, 0.0f));
				}
			}
		}

		if (IUPC && !bStep5InitiatedOrAcceptedTrade)
		{
			if (NetDemoClientRole == 1 && T >= 21.6 && Ally && FVector::Dist(Player->GetActorLocation(), Ally->GetActorLocation()) <= 580.0f)
			{
				bStep5InitiatedOrAcceptedTrade = true;
				IUPC->Server_RequestTradeWithCharacter(TEXT(""));
			}
			else if (NetDemoClientRole == 2 && T >= 22.4 && IUPC->HasCachedTradeSnapshot() && IUPC->GetCachedTradeSnapshot().bHasActiveTrade)
			{
				bStep5InitiatedOrAcceptedTrade = true;
				IUPC->Server_RequestTradeRespond(IUPC->GetCachedTradeSnapshot().TradeID, true);
			}
		}

		if (IUPC && bStep5InitiatedOrAcceptedTrade && !bStep5UpdatedTradeOffer && T >= 23.4 &&
			IUPC->HasCachedTradeSnapshot() && IUPC->GetCachedTradeSnapshot().State == TEXT("OPEN"))
		{
			bStep5UpdatedTradeOffer = true;
			if (NetDemoClientRole == 1)
			{
				const int64 OfferCoins = FMath::Min<int64>(100, FMath::Max<int64>(20, CoinBal));
				IUPC->Server_RequestTradeUpdateOffer(OfferCoins, TArray<FString>());
			}
			else
			{
				TArray<FString> UnboundOffer;
				if (IUPC->HasCachedInventorySnapshot())
				{
					for (const FIU_InventoryItemView& It : IUPC->GetCachedInventorySnapshot().BagItems)
					{
						if (!It.bIsBound && !It.bIsPlayerLocked)
						{
							UnboundOffer.Add(It.InstanceID);
							break;
						}
					}
				}
				IUPC->Server_RequestTradeUpdateOffer(0, UnboundOffer);
			}
		}

		if (IUPC && bStep5UpdatedTradeOffer && !bStep5LockedTradeOffer && T >= 24.6)
		{
			bStep5LockedTradeOffer = true;
			IUPC->Server_RequestTradeLock();
		}

		// Wait full > 3.2s after Lock (24.6s -> 28.2s) so 3.0s Anti-Scam Inspection Countdown completes visibly!
		if (IUPC && bStep5LockedTradeOffer && !bStep5ConfirmedTrade && T >= 28.3)
		{
			bStep5ConfirmedTrade = true;
			IUPC->Server_RequestTradeConfirm();
		}
	}
	else
	{
		if (LastTriggeredStage != 515)
		{
			LastTriggeredStage = 515;
			if (IUPC && NetDemoClientRole == 1)
			{
				IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::HanhTrang, false);
			}
		}
		if (IUPC && NetDemoClientRole == 1 && T >= 32.8 && IUPC->IsInventoryPanelVisible())
		{
			IUPC->SetInventoryPanelVisible(false, false);
		}
		Player->StopSprintInput();
		Player->ApplyMovementInput(FVector2D::ZeroVector);
		SetBanner(FString::Printf(
			TEXT("STEP 5 [5/5]: [%s] %s Lv.%d | Vang:%lld -> Giao Dich & Bang Hoi Hoan Tat -> LOGOUT!"),
			GuildTag.IsEmpty() ? TEXT("ZNT") : *GuildTag,
			*CharName,
			CurLv,
			CoinBal));
	}
}

void UIU_AutoDemoSubsystem::TickStep6BossMultiZoneShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC)
{
	AIU_HUDManager* HUD = Cast<AIU_HUDManager>(RawPC->GetHUD());
	AIU_PlayerController* IUPC = Cast<AIU_PlayerController>(RawPC);
	auto SetBanner = [HUD](const FString& Text)
	{
		if (HUD)
		{
			HUD->SetShowcaseStepBanner(Text);
		}
	};

	const AIU_PlayerState* IUPS = Player->GetPlayerState<AIU_PlayerState>();
	const int32 CurLv = IUPS ? IUPS->GetCharacterLevel() : 1;
	const int64 CoinBal = IUPS ? IUPS->GetStandardCoin() : 0;
	const int32 TitanHC = IUPS ? IUPS->GetPityTitanTokens() : 0;
	const int64 CurEpoch = IUPS ? IUPS->GetActiveFencingEpoch() : 0;
	const FString CharName = (IUPS && !IUPS->GetPersistedCharacterName().IsEmpty()) ? IUPS->GetPersistedCharacterName() : TEXT("Hero");
	const UIU_AttributeComponent* Attr = Player->GetAttributeComponent();

	AIU_WorldBossCharacter* Boss = FindWorldBossInArena();
	const bool bInBossZone = (Boss != nullptr) ||
		(IUPS && (IUPS->GetCurrentZoneID() == TEXT("zone.p01_highland_01") || IUPS->GetCurrentChannelID() >= 2));
	const double T = ConnectedPlaySeconds;

	// =========================================================================
	// STAGE A (SERVER #1 :7777 — zone.p01_west_hub / KHU 1):
	// Form Party -> Defeat 3 Raiders -> Claim Quest -> Open Channel List -> Warp via Tau Vu Tru to :7778!
	// =========================================================================
	if (!bInBossZone)
	{
		AIU_PlayerCharacter* Ally = FindOtherPlayerCharacter(Player);
		const FVector RaidAnchor = (NetDemoClientRole == 1)
			? FVector(-1850.0f, -200.0f, 95.0f)
			: FVector(-2950.0f, 350.0f, 95.0f);

		if (T < 2.6)
		{
			if (LastTriggeredStage != 601)
			{
				LastTriggeredStage = 601;
				if (IUPC)
				{
					IUPC->Server_RequestRefreshInventory();
					IUPC->Server_RequestRefreshQuests();
					IUPC->Server_RequestRefreshChannels();
				}
				SetBanner(FString::Printf(TEXT("STEP 6 [1/6 - KHU 1 :7777]: Lap Party (+15%% Synergy) & Nhan Nhiem Vu Tuan Tra (Ep#%lld)"), CurEpoch));
			}

			if (Ally)
			{
				SteerPlayerToward(Player, RawPC, Ally->GetActorLocation(), 210.0f, false, false);
			}

			if (IUPC && !bStep6FormedParty)
			{
				if (NetDemoClientRole == 1 && T >= 0.9 && Ally)
				{
					bStep6FormedParty = true;
					IUPC->Server_RequestPartyInvite(TEXT(""));
					IUPC->Server_RequestSendChat(TEXT("LOCAL"), TEXT("Diet 3 Raider xong bay Tau Vu Tru sang Khu 2 san Boss Titan!"), TEXT(""));
				}
				else if (NetDemoClientRole == 2 && T >= 1.7)
				{
					bStep6FormedParty = true;
					IUPC->Server_RequestPartyRespond(TEXT(""), true);
				}
			}
		}
		else if (T < 11.4)
		{
			if (LastTriggeredStage != 602)
			{
				LastTriggeredStage = 602;
				SetBanner(TEXT("STEP 6 [2/6 - KHU 1 :7777]: Diet 3 Raider hoan thanh Nhiem Vu Tuan Tra Cao Nguyen!"));
			}

			AIU_EnemyCharacter* TargetEnemy = FindClosestLivingEnemy(RaidAnchor);
			if (!TargetEnemy)
			{
				TargetEnemy = FindClosestLivingEnemy(Player->GetActorLocation());
			}

			if (TargetEnemy && !TargetEnemy->IsDead())
			{
				if (Player->GetTargetingComponent()->GetLockedTarget() != TargetEnemy)
				{
					Player->GetTargetingComponent()->SetLockedTarget(TargetEnemy);
				}
				SteerPlayerToward(Player, RawPC, TargetEnemy->GetActorLocation(), 140.0f, true, false);
				if (ElapsedSeconds >= NextActionCooldownTime)
				{
					const float DistE = FVector::Dist2D(Player->GetActorLocation(), TargetEnemy->GetActorLocation());
					if (DistE > 220.0f)
					{
						Player->KiBlastInput();
						NextActionCooldownTime = ElapsedSeconds + 0.48;
					}
					else
					{
						Player->MeleeAttackInput();
						NextActionCooldownTime = ElapsedSeconds + 0.20;
					}
				}
			}
			else
			{
				SteerPlayerToward(Player, RawPC, RaidAnchor, 85.0f, true, false);
			}
		}
		else if (T < 14.2)
		{
			if (LastTriggeredStage != 603)
			{
				LastTriggeredStage = 603;
				Player->StopSprintInput();
				Player->ApplyMovementInput(FVector2D::ZeroVector);
				if (IUPC)
				{
					IUPC->Server_RequestRefreshQuests();
					if (NetDemoClientRole == 1)
					{
						IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::NhiemVu, false);
					}
				}
				SetBanner(TEXT("STEP 6 [3/6 - KHU 1]: Tab [Nhiem Vu] -> Tra Nhiem Vu Tuan Tra (+300 Vang, +1 Huy Chuong)!"));
			}

			if (IUPC && !bStep6ClaimedRaiderQuest && T >= 12.2)
			{
				bStep6ClaimedRaiderQuest = true;
				IUPC->Server_RequestClaimQuest(TEXT("quest.nro_raider_patrol_01"));
			}
		}
		else if (T < 16.8)
		{
			if (LastTriggeredStage != 604)
			{
				LastTriggeredStage = 604;
				if (IUPC && !bStep6OpenedChannelList)
				{
					bStep6OpenedChannelList = true;
					IUPC->Server_RequestRefreshChannels();
					if (NetDemoClientRole == 1)
					{
						IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::ChucNang, false);
						if (UIU_InventoryEquipmentWidget* W = IUPC->GetActiveCharacterProfileWidget())
						{
							W->SetActiveFuncSubMode(0);
						}
					}
				}
				SetBanner(TEXT("STEP 6 [4/6 - KHU 1]: Tab [Chuc Nang -> Doi Khu] Phat Hien BOSS TITAN tai KHU 2 (:7778)!"));
			}
			Player->StopSprintInput();
			Player->ApplyMovementInput(FVector2D::ZeroVector);
		}
		else
		{
			if (LastTriggeredStage != 605)
			{
				LastTriggeredStage = 605;
				if (IUPC && IUPC->IsInventoryPanelVisible())
				{
					IUPC->SetInventoryPanelVisible(false, false);
				}
				SetBanner(TEXT("STEP 6 [4/6]: Bay Vao Tau Vu Tru -> Chuyen Vung Lien Server (:7777 -> :7778 KHU 2)!"));
			}

			const FVector GatePos(460.0f, 0.0f, 118.0f);
			SteerPlayerToward(Player, RawPC, GatePos, 40.0f, true, false);

			const double TransferTriggerT = (NetDemoClientRole == 1) ? 18.0 : 18.35;
			if (IUPC && !bStep6TriggeredZoneTransfer && T >= TransferTriggerT)
			{
				bStep6TriggeredZoneTransfer = true;
				IUPC->Server_RequestZoneTransfer(TEXT("zone.p01_highland_01"), 2);
			}
		}
		return;
	}

	// =========================================================================
	// STAGE B (SERVER #2 :7778 — zone.p01_highland_01 / KHU 2):
	// 3-Phase World Boss Highland Titan + 3D Aerial Quake Dodge (>4.5m) + Poise Stagger + Quest & Pity Shop!
	// =========================================================================
	if (T < 2.2)
	{
		if (LastTriggeredStage != 611)
		{
			LastTriggeredStage = 611;
			if (IUPC)
			{
				IUPC->Server_RequestRefreshInventory();
				IUPC->Server_RequestRefreshParty();
				IUPC->Server_RequestRefreshQuests();
				IUPC->Server_RequestRefreshPityShop();
			}
			SetBanner(FString::Printf(TEXT("STEP 6 [5/6 - KHU 2 :7778]: Da Den Cao Nguyen Titan (Epoch #%lld) -> Tiep Can BOSS!"), CurEpoch));
		}
		if (Boss && !Boss->IsDead())
		{
			Player->GetTargetingComponent()->SetLockedTarget(Boss);
			SteerPlayerToward(Player, RawPC, Boss->GetActorLocation(), 320.0f, true, false);
		}
		return;
	}

	if (Boss && !Boss->IsDead() && T < 21.0)
	{
		if (Player->GetTargetingComponent()->GetLockedTarget() != Boss)
		{
			Player->GetTargetingComponent()->SetLockedTarget(Boss);
		}

		// Use Dau Than automatically if HP drops below 65% during Phase 2/3 Boss attacks!
		if (IUPC && Attr && Attr->GetHPPercent() < 0.65f)
		{
			IUPC->TriggerQuickUseElixirFromHUD();
		}

		const bool bQuakeTelegraphActive = (Boss->GetActiveSpecialAction() == EIU_BossSpecialAction::GroundQuake) ||
			(T >= 3.4 && T <= 5.8);

		if (bQuakeTelegraphActive)
		{
			if (LastTriggeredStage != 612)
			{
				LastTriggeredStage = 612;
				SetBanner(TEXT("STEP 6 [5/6 - KHU 2]: DIA CHAN 360 DO! Bay Vu Tru > 4.5m Ne 100% Sat Thuong & Ban Ki!"));
			}

			// 3D Aerial Quake Counterplay: Ascend to Z >= 640 cm (> 450cm above ground) and rain Ki Blasts!
			if (!Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}
			if (Player->GetActorLocation().Z < 650.0f)
			{
				Player->StartJumpInput();
			}
			else
			{
				Player->StopJumpInput();
			}

			const FVector AerialOrbit = Boss->GetActorLocation() + FVector(380.0f, (NetDemoClientRole == 1) ? -140.0f : 140.0f, 520.0f);
			SteerPlayerToward(Player, RawPC, AerialOrbit, 90.0f, true, true);
			if (ElapsedSeconds >= NextActionCooldownTime)
			{
				Player->KiBlastInput();
				NextActionCooldownTime = ElapsedSeconds + 0.44;
			}
		}
		else
		{
			Player->StopJumpInput();
			Player->StopDescendInput();
			if (Player->GetFlightComponent()->IsFlying())
			{
				Player->ToggleFlightInput();
			}

			const int32 BPhase = Boss->GetBossPhase();
			const bool bStag = Boss->IsBossStaggered();
			SetBanner(FString::Printf(
				TEXT("STEP 6 [5/6 - KHU 2]: Boss Phase %d/3 | %s | Co-op Don Dame!"),
				BPhase,
				bStag ? TEXT("STAGGER CHOANG 1.25x SAT THUONG!") : TEXT("Pha Thanh Poise Super Armor")));
			LastTriggeredStage = 613;

			const FVector FlankGoal = Boss->GetActorLocation() + FVector(190.0f, (NetDemoClientRole == 1) ? -95.0f : 95.0f, 0.0f);
			SteerPlayerToward(Player, RawPC, FlankGoal, 85.0f, true, false);
			if (ElapsedSeconds >= NextActionCooldownTime)
			{
				const float DistB = FVector::Dist2D(Player->GetActorLocation(), Boss->GetActorLocation());
				if (DistB > 300.0f)
				{
					Player->KiBlastInput();
					NextActionCooldownTime = ElapsedSeconds + 0.44;
				}
				else
				{
					Player->MeleeAttackInput();
					NextActionCooldownTime = ElapsedSeconds + 0.19;
				}
			}
		}
		return;
	}

	// World Boss Defeated -> Post-Boss Quest Claim + Pity Shop Redemption -> Clean Disconnect!
	if (Step6BossDefeatedTime < 0.0)
	{
		Step6BossDefeatedTime = T;
		Player->StopJumpInput();
		Player->StopDescendInput();
		Player->StopSprintInput();
		if (Player->GetFlightComponent()->IsFlying())
		{
			Player->ToggleFlightInput();
		}
		Player->ApplyMovementInput(FVector2D::ZeroVector);
	}

	const double PostKill = T - Step6BossDefeatedTime;
	Player->ApplyMovementInput(FVector2D::ZeroVector);

	if (PostKill < 3.6)
	{
		if (LastTriggeredStage != 614)
		{
			LastTriggeredStage = 614;
			if (IUPC)
			{
				IUPC->Server_RequestRefreshQuests();
				IUPC->Server_RequestRefreshPityShop();
				if (NetDemoClientRole == 1)
				{
					IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::NhiemVu, false);
				}
			}
			SetBanner(TEXT("STEP 6 [6/6 - KHU 2]: Ha Guc HIGHLAND TITAN -> Nhan Thuong Nhiem Vu (+1000V, +3 HC)!"));
		}

		if (IUPC && !bStep6ClaimedTitanQuest && PostKill >= 1.3)
		{
			bStep6ClaimedTitanQuest = true;
			IUPC->Server_RequestClaimQuest(TEXT("quest.nro_titan_slayer_01"));
		}
	}
	else if (PostKill < 7.4)
	{
		if (LastTriggeredStage != 615)
		{
			LastTriggeredStage = 615;
			if (IUPC && NetDemoClientRole == 1)
			{
				IUPC->OpenCharacterProfileOnTab(EIU_CharacterProfileTab::ChucNang, false);
				if (UIU_InventoryEquipmentWidget* W = IUPC->GetActiveCharacterProfileWidget())
				{
					W->SetActiveFuncSubMode(1);
				}
			}
			SetBanner(TEXT("STEP 6 [6/6 - KHU 2]: Shop Bao Hiem Boss -> Doi Huy Chuong Titan Lay Da Cuong Hoa Do!"));
		}

		if (IUPC && !bStep6RedeemedPityShop && PostKill >= 4.6)
		{
			bStep6RedeemedPityShop = true;
			IUPC->Server_RequestRedeemPityEntry(TEXT("pity.mat_crimson_ore_t1"));
		}
	}
	else if (PostKill < 9.2)
	{
		if (LastTriggeredStage != 616)
		{
			LastTriggeredStage = 616;
			if (IUPC && IUPC->IsInventoryPanelVisible())
			{
				IUPC->SetInventoryPanelVisible(false, false);
			}
		}
		SetBanner(FString::Printf(
			TEXT("STEP 6 HOAN TAT: %s Lv.%d | Vang:%lld | HuyChuong:%d | Khu 2 Ep#%lld -> LOGOUT!"),
			*CharName, CurLv, CoinBal, TitanHC, CurEpoch));
	}
	else
	{
		TargetDurationSeconds = ElapsedSeconds;
	}
}

void UIU_AutoDemoSubsystem::WritePerformanceReportCSV() const
{
	const FString CSVPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), CSVOutputFilename);
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
		const double AvgInKBps = (NetBandwidthSamples > 0) ? (SumNetInKBps / NetBandwidthSamples) : 0.0;
		const double AvgOutKBps = (NetBandwidthSamples > 0) ? (SumNetOutKBps / NetBandwidthSamples) : 0.0;

		const FString SummaryLine = FString::Printf(
			TEXT("# SUMMARY: Frames=%d, AvgFPS_Or_TickHz=%.1f, 1PctLowFPS=%.1f, AvgFrameMs=%.3f, StartRAM_MB=%.1f, PeakRAM_MB=%.1f, EndRAM_MB=%.1f, AvgNetIn_KBps=%.2f, PeakNetIn_KBps=%.2f, AvgNetOut_KBps=%.2f, PeakNetOut_KBps=%.2f\n"),
			Sorted.Num(), AvgFPS, OnePercentLowFPS, AvgMs, StartPhysicalMemMB, PeakPhysicalMemMB, EndMemMB, AvgInKBps, PeakNetInKBps, AvgOutKBps, PeakNetOutKBps);
		OutText += SummaryLine;
		UE_LOG(LogInfiniteUniverse, Display, TEXT("[AutoDemoSubsystem] %s"), *SummaryLine);
	}

	FFileHelper::SaveStringToFile(OutText, *CSVPath);
}
