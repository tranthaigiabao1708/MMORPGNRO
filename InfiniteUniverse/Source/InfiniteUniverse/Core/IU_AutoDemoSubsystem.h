// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IU_AutoDemoSubsystem.generated.h"

class AIU_EnemyCharacter;
class AIU_WorldBossCharacter;
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
	void TickStep4InventoryShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC);
	void TickStep5SocialTradeShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC);
	void TickStep6BossMultiZoneShowcase(UWorld* World, AIU_PlayerCharacter* Player, APlayerController* RawPC);
	void SteerPlayerToward(AIU_PlayerCharacter* Player, APlayerController* RawPC, const FVector& GoalPos, float StopDistCm, bool bSprint, bool b3DPitch = false);

	AIU_EnemyCharacter* FindClosestLivingEnemy(const FVector& Origin) const;
	AIU_WorldBossCharacter* FindWorldBossInArena() const;
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
	int32 Step4EconomyPhase = 0; // 1 = StarterEquip -> Raid Loot/Vang -> Elixir/Equip/Enhance +1..+2 -> Logout, 2 = Post-Restart Relogin Proof
	int32 Step5SocialPhase = 0; // 1 = Party + Synergy + Ping + Guild [ZNT] + Bubble Chat + 2-Step P2P Trade -> Logout, 2 = Post-Restart Guild/Trade Proof
	int32 Step6BossMultiZonePhase = 0; // 1 = Khu 1 Raider Patrol Quest + Doi Khu -> Khu 2 (:7778) 3-Phase World Boss + 3D Aerial Quake Dodge + Pity Shop!
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
	bool bStep4EquippedStarterArmor = false;
	bool bStep4UsedElixirAndEquippedLoot = false;
	bool bStep4EnhancedArmorOnce = false;
	bool bStep4EnhancedArmorTwice = false;
	bool bStep5SentPartyAction = false;
	bool bStep5SentPingAndChat = false;
	bool bStep5ExecutedGuildAction = false;
	bool bStep5SentGuildDonateAndChat = false;
	bool bStep5InitiatedOrAcceptedTrade = false;
	bool bStep5UpdatedTradeOffer = false;
	bool bStep5LockedTradeOffer = false;
	bool bStep5ConfirmedTrade = false;
	bool bStep6FormedParty = false;
	bool bStep6ClaimedRaiderQuest = false;
	bool bStep6OpenedChannelList = false;
	bool bStep6TriggeredZoneTransfer = false;
	bool bStep6ClaimedTitanQuest = false;
	bool bStep6RedeemedPityShop = false;
	double Step6BossDefeatedTime = -1.0;
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
