// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Core/IU_AutoDemoSubsystem.h"
#include "Core/IU_PlayerController.h"
#include "Core/IU_GameState.h"
#include "Core/IU_PlayerState.h"
#include "Characters/Player/IU_PlayerCharacter.h"
#include "Characters/Enemy/IU_EnemyCharacter.h"
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
	FParse::Value(Cmd, TEXT("AutoNetDemo="), ParsedNetRole);
	FParse::Value(Cmd, TEXT("AutoStep3Phase="), ParsedStep3Phase);
	FParse::Value(Cmd, TEXT("AutoStep4Phase="), ParsedStep4Phase);

	FString AutoAuthUser;
	FParse::Value(Cmd, TEXT("AutoAuthUser="), AutoAuthUser);
	// When client boots onto temporary local map while awaiting AutoAuth -> ClientTravel(?Ticket=...), wait for NM_Client world!
	if (!AutoAuthUser.IsEmpty() && (ParsedNetRole > 0 || ParsedStep3Phase > 0 || ParsedStep4Phase > 0) && World->GetNetMode() != NM_Client)
	{
		return false;
	}

	return FParse::Param(Cmd, TEXT("AutoDemoShowcase")) ||
		FParse::Param(Cmd, TEXT("AutoServerTelemetry")) ||
		ParsedNetRole > 0 ||
		ParsedStep3Phase > 0 ||
		ParsedStep4Phase > 0;
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
	if ((Step3PersistencePhase > 0 || Step4EconomyPhase > 0) && NetDemoClientRole == 0)
	{
		NetDemoClientRole = 1;
	}

	bRecordFrames = FParse::Param(Cmd, TEXT("RecordDemoFrames"));
	if (Step4EconomyPhase > 0)
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
		IFileManager::Get().DeleteDirectory(*OutDir, false, true);
		IFileManager::Get().MakeDirectory(*OutDir, true);
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
		const FString FrameFile = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			DemoFramesSubdir,
			FString::Printf(TEXT("frame_%04d.png"), CapturedFrameIndex++));
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
			IUPC->Server_RequestUseConsumable(TEXT(""));

			if (PoolTN >= 150)
			{
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
			if (IUPC->IsInventoryPanelVisible() != bShowFinalModal)
			{
				IUPC->SetInventoryPanelVisible(bShowFinalModal, false);
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
