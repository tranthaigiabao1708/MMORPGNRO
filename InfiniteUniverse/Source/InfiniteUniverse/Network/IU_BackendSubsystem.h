// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IU_BackendSubsystem.generated.h"

/**
 * Authoritative character snapshot returned by Go Backend (/v1/characters, /internal/v1/lease/claim, /internal/v1/character/exp).
 */
USTRUCT(BlueprintType)
struct INFINITEUNIVERSE_API FIU_BackendCharacterSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString CharacterID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString AccountID;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString RaceID = TEXT("race.zenith_warrior");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 CurrentExp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextLevelExp = 100;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 PotentialPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FString ZoneID = TEXT("zone.p01_west_hub");

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	FVector SavedPosition = FVector(0.0f, 0.0f, 112.0f);

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float SavedYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	bool bHasSavedPosition = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	bool bIsAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float HP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float MaxHP = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float Energy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float MaxEnergy = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float AttackPower = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	float AttackPowerMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 HPUpgrades = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 KiUpgrades = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 AtkUpgrades = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextHPCost = 100;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextKiCost = 100;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 NextAtkCost = 150;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 EnemyKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 PvPKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 Deaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 FencingEpoch = 0;

	// Populated on /internal/v1/character/exp & /internal/v1/character/potential responses
	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	bool bLeveledUp = false;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int32 OldLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 ExpGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 PotentialGranted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "IU|Backend")
	int64 PotentialSpent = 0;
};

/**
 * Payload sent from UE5 Dedicated Server to Go Backend for fenced periodic/logout saves.
 */
struct INFINITEUNIVERSE_API FIU_CharacterSavePayload
{
	FString CharacterID;
	int64 FencingEpoch = 0;
	FString ServerID;
	FString SnapshotReason = TEXT("PERIODIC_60S");
	FString ZoneID = TEXT("zone.p01_west_hub");
	FVector Position = FVector::ZeroVector;
	float Yaw = 0.0f;
	bool bIsAlive = true;
	float HP = 100.0f;
	float Energy = 100.0f;
	int32 EnemyKills = 0;
	int32 PvPKills = 0;
	int32 Deaths = 0;
};

struct INFINITEUNIVERSE_API FIU_LeaseRenewalEntry
{
	FString CharacterID;
	int64 FencingEpoch = 0;
};

/**
 * Step 3 Backend Integration Subsystem:
 * - Client Edge: Register / Login (Argon2id + Ed25519 JWT), List/Create Characters, EnterWorld Ticket + ClientTravel.
 * - Dedicated Server Bridge: HMAC-SHA256 authenticated ClaimLease, RenewLeases, SaveCheckpoint, GrantEXP, ReleaseLease.
 * - Gracefully inactive when -UseBackend / -BackendURL is not supplied so all Step 1 & Step 2 offline tests remain 100% green.
 */
UCLASS()
class INFINITEUNIVERSE_API UIU_BackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========================================================================
	// Pure Cryptographic Primitives (FIPS 180-4 SHA-256 & RFC 2104 HMAC-SHA256)
	// ========================================================================

	static void ComputeSHA256(const uint8* Data, SIZE_T Length, uint8 OutDigest[32]);
	static FString ComputeHMACSHA256Hex(const FString& SecretUtf8, const FString& PayloadUtf8);
	static bool ParseCharacterSummaryFromJson(const FString& JsonString, FIU_BackendCharacterSummary& OutSummary);
	static FString SerializeSavePayloadToJson(const FIU_CharacterSavePayload& Payload);

	// ========================================================================
	// Configuration & Telemetry
	// ========================================================================

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	bool IsBackendBridgeEnabled() const { return bBackendBridgeEnabled; }

	UFUNCTION(BlueprintCallable, Category = "IU|Backend")
	void SetBackendBridgeEnabled(bool bEnabled) { bBackendBridgeEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	bool ShouldRequireBackendTicket() const { return bRequireBackendTicket; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetBackendBaseURL() const { return BackendBaseURL; }

	UFUNCTION(BlueprintCallable, Category = "IU|Backend")
	void SetBackendBaseURL(const FString& InURL);

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetServerInstanceID() const { return ServerInstanceID; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetServerZoneID() const { return ServerZoneID; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	bool IsClientAuthenticated() const { return !ClientAccessToken.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	FString GetLoggedInUsername() const { return LoggedInUsername; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	const TArray<FIU_BackendCharacterSummary>& GetCachedCharacters() const { return CachedCharacters; }

	UFUNCTION(BlueprintPure, Category = "IU|Backend")
	double GetLastBridgeLatencyMs() const { return LastBridgeLatencyMs; }

	// ========================================================================
	// Client Edge REST Operations (/v1/*)
	// ========================================================================

	void RegisterAccount(
		const FString& Username,
		const FString& Email,
		const FString& Password,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback);

	void LoginAccount(
		const FString& Username,
		const FString& Password,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback);

	void FetchCharacters(
		TFunction<void(bool bSuccess, const TArray<FIU_BackendCharacterSummary>& Characters, const FString& ErrorMessage)> Callback);

	void CreateCharacter(
		const FString& CharacterName,
		const FString& RaceID,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& Created, const FString& ErrorMessage)> Callback);

	void RequestEnterWorldAndTravel(
		const FString& CharacterID,
		APlayerController* TravelController,
		TFunction<void(bool bSuccess, const FString& TicketID, const FString& ErrorMessage)> Callback);

	/** Automated one-shot Login-or-Register -> Select-or-Create Character -> EnterWorld ClientTravel (used by -AutoAuthUser= and Quick Play UI). */
	void QuickAuthenticateAndEnterWorld(
		const FString& Username,
		const FString& Password,
		const FString& DesiredCharName,
		const FString& DesiredRaceID,
		APlayerController* TravelController,
		TFunction<void(bool bSuccess, const FString& StatusMessage)> OnStatus);

	/** Invoked by AIU_PlayerController::BeginPlay on local client to trigger -AutoAuthUser= once per process session. */
	void TryTriggerCommandLineAutoAuth(APlayerController* LocalPC);

	// ========================================================================
	// Authoritative Dedicated Server Bridge Operations (/internal/v1/* + HMAC)
	// ========================================================================

	void ClaimCharacterLease(
		const FString& TicketID,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& LoadedState, const FString& ErrorMessage)> Callback);

	void RenewActiveLeases(
		const TArray<FIU_LeaseRenewalEntry>& ActiveLeases,
		TFunction<void(bool bSuccess, const TArray<FString>& RevokedCharacterIDs)> Callback);

	void SaveCharacterCheckpoint(
		const FIU_CharacterSavePayload& Payload,
		TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback = nullptr);

	void GrantCharacterEXP(
		const FString& CharacterID,
		int64 FencingEpoch,
		float OutgoingDamage,
		bool bKilledTarget,
		const FString& IdempotencyKey,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& UpdatedState, const FString& ErrorMessage)> Callback);

	void AllocateCharacterPotential(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FString& StatTypeCode,
		TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& UpdatedState, const FString& ErrorMessage)> Callback);

	void ReleaseCharacterLease(
		const FString& CharacterID,
		int64 FencingEpoch,
		const FIU_CharacterSavePayload* OptionalFinalSave);

private:
	void SendJsonRequest(
		const FString& Verb,
		const FString& EndpointPath,
		const FString& JsonBody,
		bool bUseBearerAuth,
		bool bUseServerHMAC,
		TFunction<void(int32 HttpCode, const FString& ResponseBody, bool bConnected)> Callback);

	static FString ExtractErrorFromJson(const FString& ResponseBody, int32 HttpCode);

	FString BackendBaseURL = TEXT("http://127.0.0.1:8080");
	FString InternalHMACSecret = TEXT("iu_step3_internal_server_hmac_2026");
	FString ServerInstanceID = TEXT("ue5-ded-7777");
	FString ServerZoneID = TEXT("zone.p01_west_hub");

	bool bBackendBridgeEnabled = false;
	bool bRequireBackendTicket = false;
	bool bAutoAuthDispatched = false;

	FString AutoAuthUsername;
	FString AutoAuthPassword = TEXT("Step3HeroPass!2026");
	FString AutoAuthCharacterName;
	FString AutoAuthRaceID = TEXT("race.zenith_warrior");

	FString ClientAccessToken;
	FString ClientRefreshToken;
	FString LoggedInUsername;
	TArray<FIU_BackendCharacterSummary> CachedCharacters;

	double LastBridgeLatencyMs = 0.0;
	int32 TotalBridgeCalls = 0;
	int32 FailedBridgeCalls = 0;
	TArray<double> BridgeLatencySamplesMs;
};
