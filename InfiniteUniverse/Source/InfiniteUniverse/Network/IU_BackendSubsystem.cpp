// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Network/IU_BackendSubsystem.h"
#include "InfiniteUniverse.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
	static FORCEINLINE uint32 RotateRight32(uint32 Value, uint32 Bits)
	{
		return (Value >> Bits) | (Value << (32u - Bits));
	}

	static const uint32 SHA256_K[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffaU, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
	};

	static void TransformSHA256Block(uint32 State[8], const uint8 Block[64])
	{
		uint32 W[64];
		for (int32 i = 0; i < 16; ++i)
		{
			W[i] = (static_cast<uint32>(Block[i * 4]) << 24)
				| (static_cast<uint32>(Block[i * 4 + 1]) << 16)
				| (static_cast<uint32>(Block[i * 4 + 2]) << 8)
				| static_cast<uint32>(Block[i * 4 + 3]);
		}
		for (int32 i = 16; i < 64; ++i)
		{
			const uint32 s0 = RotateRight32(W[i - 15], 7) ^ RotateRight32(W[i - 15], 18) ^ (W[i - 15] >> 3);
			const uint32 s1 = RotateRight32(W[i - 2], 17) ^ RotateRight32(W[i - 2], 19) ^ (W[i - 2] >> 10);
			W[i] = W[i - 16] + s0 + W[i - 7] + s1;
		}

		uint32 a = State[0], b = State[1], c = State[2], d = State[3];
		uint32 e = State[4], f = State[5], g = State[6], h = State[7];

		for (int32 i = 0; i < 64; ++i)
		{
			const uint32 S1 = RotateRight32(e, 6) ^ RotateRight32(e, 11) ^ RotateRight32(e, 25);
			const uint32 ch = (e & f) ^ ((~e) & g);
			const uint32 temp1 = h + S1 + ch + SHA256_K[i] + W[i];
			const uint32 S0 = RotateRight32(a, 2) ^ RotateRight32(a, 13) ^ RotateRight32(a, 22);
			const uint32 maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32 temp2 = S0 + maj;

			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}

		State[0] += a; State[1] += b; State[2] += c; State[3] += d;
		State[4] += e; State[5] += f; State[6] += g; State[7] += h;
	}
}

void UIU_BackendSubsystem::ComputeSHA256(const uint8* Data, SIZE_T Length, uint8 OutDigest[32])
{
	uint32 State[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
	};

	SIZE_T Offset = 0;
	while (Offset + 64 <= Length)
	{
		TransformSHA256Block(State, Data + Offset);
		Offset += 64;
	}

	uint8 Tail[128];
	FMemory::Memzero(Tail, sizeof(Tail));
	const SIZE_T Rem = Length - Offset;
	if (Rem > 0 && Data)
	{
		FMemory::Memcpy(Tail, Data + Offset, Rem);
	}
	Tail[Rem] = 0x80u;

	const SIZE_T TotalBlocks = (Rem < 56) ? 1 : 2;
	const uint64 BitLen = static_cast<uint64>(Length) * 8ull;
	const SIZE_T LenPos = TotalBlocks * 64 - 8;
	for (int32 i = 0; i < 8; ++i)
	{
		Tail[LenPos + i] = static_cast<uint8>((BitLen >> ((7 - i) * 8)) & 0xffu);
	}

	TransformSHA256Block(State, Tail);
	if (TotalBlocks == 2)
	{
		TransformSHA256Block(State, Tail + 64);
	}

	for (int32 i = 0; i < 8; ++i)
	{
		OutDigest[i * 4] = static_cast<uint8>((State[i] >> 24) & 0xffu);
		OutDigest[i * 4 + 1] = static_cast<uint8>((State[i] >> 16) & 0xffu);
		OutDigest[i * 4 + 2] = static_cast<uint8>((State[i] >> 8) & 0xffu);
		OutDigest[i * 4 + 3] = static_cast<uint8>(State[i] & 0xffu);
	}
}

FString UIU_BackendSubsystem::ComputeHMACSHA256Hex(const FString& SecretUtf8, const FString& PayloadUtf8)
{
	FTCHARToUTF8 KeyConv(*SecretUtf8);
	FTCHARToUTF8 MsgConv(*PayloadUtf8);

	const uint8* KeyPtr = reinterpret_cast<const uint8*>(KeyConv.Get());
	const SIZE_T KeyLen = static_cast<SIZE_T>(KeyConv.Length());
	const uint8* MsgPtr = reinterpret_cast<const uint8*>(MsgConv.Get());
	const SIZE_T MsgLen = static_cast<SIZE_T>(MsgConv.Length());

	uint8 KBlock[64];
	FMemory::Memzero(KBlock, sizeof(KBlock));
	if (KeyLen > 64)
	{
		ComputeSHA256(KeyPtr, KeyLen, KBlock);
	}
	else if (KeyLen > 0)
	{
		FMemory::Memcpy(KBlock, KeyPtr, KeyLen);
	}

	uint8 IPad[64];
	uint8 OPad[64];
	for (int32 i = 0; i < 64; ++i)
	{
		IPad[i] = KBlock[i] ^ 0x36u;
		OPad[i] = KBlock[i] ^ 0x5cu;
	}

	TArray<uint8> InnerBuf;
	InnerBuf.SetNumUninitialized(static_cast<int32>(64 + MsgLen));
	FMemory::Memcpy(InnerBuf.GetData(), IPad, 64);
	if (MsgLen > 0)
	{
		FMemory::Memcpy(InnerBuf.GetData() + 64, MsgPtr, MsgLen);
	}

	uint8 InnerHash[32];
	ComputeSHA256(InnerBuf.GetData(), static_cast<SIZE_T>(InnerBuf.Num()), InnerHash);

	uint8 OuterBuf[96];
	FMemory::Memcpy(OuterBuf, OPad, 64);
	FMemory::Memcpy(OuterBuf + 64, InnerHash, 32);

	uint8 FinalDigest[32];
	ComputeSHA256(OuterBuf, sizeof(OuterBuf), FinalDigest);

	FString HexOut;
	HexOut.Reserve(64);
	for (int32 i = 0; i < 32; ++i)
	{
		HexOut += FString::Printf(TEXT("%02x"), FinalDigest[i]);
	}
	return HexOut;
}

void UIU_BackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const TCHAR* CmdLine = FCommandLine::Get();
	const bool bForceDisabled = FParse::Param(CmdLine, TEXT("NoBackend"));

	FString ParsedURL;
	const bool bHasExplicitURL = FParse::Value(CmdLine, TEXT("BackendURL="), ParsedURL);
	if (bHasExplicitURL && !ParsedURL.IsEmpty())
	{
		SetBackendBaseURL(ParsedURL);
	}

	FString ParsedSecret;
	if (FParse::Value(CmdLine, TEXT("ServerHMACSecret="), ParsedSecret) && !ParsedSecret.IsEmpty())
	{
		InternalHMACSecret = ParsedSecret;
	}

	FString ParsedServerID;
	if (FParse::Value(CmdLine, TEXT("ServerID="), ParsedServerID) && !ParsedServerID.IsEmpty())
	{
		ServerInstanceID = ParsedServerID;
	}

	FString ParsedZoneID;
	if (FParse::Value(CmdLine, TEXT("ZoneID="), ParsedZoneID) && !ParsedZoneID.IsEmpty())
	{
		ServerZoneID = ParsedZoneID;
	}

	bRequireBackendTicket = FParse::Param(CmdLine, TEXT("RequireBackendTicket"));
	const bool bUseBackendFlag = FParse::Param(CmdLine, TEXT("UseBackend"));
	const bool bShowLoginUI = FParse::Param(CmdLine, TEXT("ShowLoginUI"));

	FParse::Value(CmdLine, TEXT("AutoAuthUser="), AutoAuthUsername);
	FParse::Value(CmdLine, TEXT("AutoAuthPass="), AutoAuthPassword);
	FParse::Value(CmdLine, TEXT("AutoCharName="), AutoAuthCharacterName);
	FParse::Value(CmdLine, TEXT("AutoRace="), AutoAuthRaceID);

	if (AutoAuthCharacterName.IsEmpty() && !AutoAuthUsername.IsEmpty())
	{
		AutoAuthCharacterName = AutoAuthUsername;
	}

	bBackendBridgeEnabled = !bForceDisabled &&
		(bUseBackendFlag || bHasExplicitURL || bRequireBackendTicket || bShowLoginUI || !AutoAuthUsername.IsEmpty());

	if (bBackendBridgeEnabled)
	{
		UE_LOG(LogInfiniteUniverse, Log,
			TEXT("[BackendSubsystem] Initialized Step 3 Bridge (URL=%s, ServerID=%s, RequireTicket=%d, AutoAuthUser='%s')"),
			*BackendBaseURL, *ServerInstanceID, bRequireBackendTicket ? 1 : 0, *AutoAuthUsername);
	}
}

void UIU_BackendSubsystem::Deinitialize()
{
	if (BridgeLatencySamplesMs.Num() > 0)
	{
		TArray<double> Sorted = BridgeLatencySamplesMs;
		Sorted.Sort();
		const double P50 = Sorted[Sorted.Num() / 2];
		const double P95 = Sorted[FMath::Clamp(FMath::FloorToInt32(Sorted.Num() * 0.95), 0, Sorted.Num() - 1)];
		UE_LOG(LogInfiniteUniverse, Log,
			TEXT("[BackendSubsystem] Shutdown Telemetry: TotalCalls=%d, Failed=%d, p50=%.2f ms, p95=%.2f ms"),
			TotalBridgeCalls, FailedBridgeCalls, P50, P95);
	}
	Super::Deinitialize();
}

void UIU_BackendSubsystem::SetBackendBaseURL(const FString& InURL)
{
	BackendBaseURL = InURL.TrimStartAndEnd();
	while (BackendBaseURL.EndsWith(TEXT("/")))
	{
		BackendBaseURL.LeftChopInline(1);
	}
}

bool UIU_BackendSubsystem::ParseCharacterSummaryFromJson(const FString& JsonString, FIU_BackendCharacterSummary& OutSummary)
{
	TSharedPtr<FJsonObject> RootObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		return false;
	}

	RootObj->TryGetStringField(TEXT("character_id"), OutSummary.CharacterID);
	RootObj->TryGetStringField(TEXT("account_id"), OutSummary.AccountID);
	RootObj->TryGetStringField(TEXT("character_name"), OutSummary.CharacterName);
	RootObj->TryGetStringField(TEXT("race_id"), OutSummary.RaceID);
	RootObj->TryGetStringField(TEXT("zone_id"), OutSummary.ZoneID);

	double NumVal = 0.0;
	if (RootObj->TryGetNumberField(TEXT("level"), NumVal)) { OutSummary.Level = FMath::Max(1, static_cast<int32>(NumVal)); }
	if (RootObj->TryGetNumberField(TEXT("current_exp"), NumVal)) { OutSummary.CurrentExp = FMath::Max<int64>(0, static_cast<int64>(NumVal)); }
	if (RootObj->TryGetNumberField(TEXT("next_level_exp"), NumVal)) { OutSummary.NextLevelExp = FMath::Max<int64>(0, static_cast<int64>(NumVal)); }
	if (RootObj->TryGetNumberField(TEXT("potential_points"), NumVal)) { OutSummary.PotentialPoints = FMath::Max<int64>(0, static_cast<int64>(NumVal)); }
	if (RootObj->TryGetNumberField(TEXT("standard_coin"), NumVal)) { OutSummary.StandardCoin = FMath::Max<int64>(0, static_cast<int64>(NumVal)); }

	double PosX = 0.0, PosY = 0.0, PosZ = 112.0, Yaw = 0.0;
	RootObj->TryGetNumberField(TEXT("pos_x"), PosX);
	RootObj->TryGetNumberField(TEXT("pos_y"), PosY);
	RootObj->TryGetNumberField(TEXT("pos_z"), PosZ);
	RootObj->TryGetNumberField(TEXT("rot_yaw"), Yaw);
	OutSummary.SavedPosition = FVector(PosX, PosY, PosZ);
	OutSummary.SavedYaw = static_cast<float>(Yaw);
	RootObj->TryGetBoolField(TEXT("has_saved_pos"), OutSummary.bHasSavedPosition);
	RootObj->TryGetBoolField(TEXT("is_alive"), OutSummary.bIsAlive);

	if (RootObj->TryGetNumberField(TEXT("hp"), NumVal)) { OutSummary.HP = static_cast<float>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("max_hp"), NumVal)) { OutSummary.MaxHP = static_cast<float>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("energy"), NumVal)) { OutSummary.Energy = static_cast<float>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("max_energy"), NumVal)) { OutSummary.MaxEnergy = static_cast<float>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("attack_power"), NumVal)) { OutSummary.AttackPower = static_cast<float>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("attack_power_multiplier"), NumVal)) { OutSummary.AttackPowerMultiplier = static_cast<float>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("defense"), NumVal)) { OutSummary.Defense = FMath::Max(0.0f, static_cast<float>(NumVal)); }

	if (RootObj->TryGetNumberField(TEXT("hp_upgrades"), NumVal)) { OutSummary.HPUpgrades = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("ki_upgrades"), NumVal)) { OutSummary.KiUpgrades = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("atk_upgrades"), NumVal)) { OutSummary.AtkUpgrades = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("next_hp_cost"), NumVal)) { OutSummary.NextHPCost = static_cast<int64>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("next_ki_cost"), NumVal)) { OutSummary.NextKiCost = static_cast<int64>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("next_atk_cost"), NumVal)) { OutSummary.NextAtkCost = static_cast<int64>(NumVal); }

	if (RootObj->TryGetNumberField(TEXT("enemy_kills"), NumVal)) { OutSummary.EnemyKills = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("pvp_kills"), NumVal)) { OutSummary.PvPKills = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("deaths"), NumVal)) { OutSummary.Deaths = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("fencing_epoch"), NumVal)) { OutSummary.FencingEpoch = static_cast<int64>(NumVal); }

	RootObj->TryGetBoolField(TEXT("leveled_up"), OutSummary.bLeveledUp);
	if (RootObj->TryGetNumberField(TEXT("old_level"), NumVal)) { OutSummary.OldLevel = static_cast<int32>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("exp_granted"), NumVal)) { OutSummary.ExpGranted = static_cast<int64>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("potential_granted"), NumVal)) { OutSummary.PotentialGranted = static_cast<int64>(NumVal); }
	if (RootObj->TryGetNumberField(TEXT("potential_spent"), NumVal)) { OutSummary.PotentialSpent = static_cast<int64>(NumVal); }

	return !OutSummary.CharacterID.IsEmpty();
}

FString UIU_BackendSubsystem::SerializeSavePayloadToJson(const FIU_CharacterSavePayload& Payload)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), Payload.CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(Payload.FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), Payload.ServerID);
	Obj->SetStringField(TEXT("snapshot_reason"), Payload.SnapshotReason);
	Obj->SetStringField(TEXT("zone_id"), Payload.ZoneID);
	Obj->SetNumberField(TEXT("pos_x"), Payload.Position.X);
	Obj->SetNumberField(TEXT("pos_y"), Payload.Position.Y);
	Obj->SetNumberField(TEXT("pos_z"), Payload.Position.Z);
	Obj->SetNumberField(TEXT("rot_yaw"), Payload.Yaw);
	Obj->SetBoolField(TEXT("is_alive"), Payload.bIsAlive);
	Obj->SetNumberField(TEXT("hp"), Payload.HP);
	Obj->SetNumberField(TEXT("energy"), Payload.Energy);
	Obj->SetNumberField(TEXT("enemy_kills"), Payload.EnemyKills);
	Obj->SetNumberField(TEXT("pvp_kills"), Payload.PvPKills);
	Obj->SetNumberField(TEXT("deaths"), Payload.Deaths);

	FString OutJson;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	FJsonSerializer::Serialize(Obj, Writer);
	return OutJson;
}

FString UIU_BackendSubsystem::ExtractErrorFromJson(const FString& ResponseBody, int32 HttpCode)
{
	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		FString Code, Msg;
		Obj->TryGetStringField(TEXT("error_code"), Code);
		Obj->TryGetStringField(TEXT("message"), Msg);
		if (!Code.IsEmpty() || !Msg.IsEmpty())
		{
			return FString::Printf(TEXT("[%d %s] %s"), HttpCode, *Code, *Msg);
		}
	}
	return FString::Printf(TEXT("HTTP %d: %s"), HttpCode, *ResponseBody.Left(140));
}

void UIU_BackendSubsystem::SendJsonRequest(
	const FString& Verb,
	const FString& EndpointPath,
	const FString& JsonBody,
	bool bUseBearerAuth,
	bool bUseServerHMAC,
	TFunction<void(int32 HttpCode, const FString& ResponseBody, bool bConnected)> Callback)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(BackendBaseURL + EndpointPath);
	Req->SetVerb(Verb);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->SetTimeout(5.0f);

	if (bUseBearerAuth && !ClientAccessToken.IsEmpty())
	{
		Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ClientAccessToken));
	}
	if (bUseServerHMAC)
	{
		Req->SetHeader(TEXT("X-IU-Server-HMAC"), ComputeHMACSHA256Hex(InternalHMACSecret, JsonBody));
	}
	if (!JsonBody.IsEmpty())
	{
		Req->SetContentAsString(JsonBody);
	}

	const double StartSec = FPlatformTime::Seconds();
	++TotalBridgeCalls;
	TWeakObjectPtr<UIU_BackendSubsystem> WeakThis(this);

	Req->OnProcessRequestComplete().BindLambda(
		[WeakThis, StartSec, Callback = MoveTemp(Callback)](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			const double ElapsedMs = (FPlatformTime::Seconds() - StartSec) * 1000.0;
			if (UIU_BackendSubsystem* Self = WeakThis.Get())
			{
				Self->LastBridgeLatencyMs = ElapsedMs;
				if (Self->BridgeLatencySamplesMs.Num() < 2048)
				{
					Self->BridgeLatencySamplesMs.Add(ElapsedMs);
				}
				if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() >= 500)
				{
					++Self->FailedBridgeCalls;
				}
			}

			const int32 Code = (bConnectedSuccessfully && Response.IsValid()) ? Response->GetResponseCode() : 0;
			const FString Body = (bConnectedSuccessfully && Response.IsValid()) ? Response->GetContentAsString() : TEXT("");
			if (Callback)
			{
				Callback(Code, Body, bConnectedSuccessfully && Response.IsValid());
			}
		});

	Req->ProcessRequest();
}

void UIU_BackendSubsystem::RegisterAccount(
	const FString& Username,
	const FString& Email,
	const FString& Password,
	TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("username"), Username.TrimStartAndEnd());
	Obj->SetStringField(TEXT("email"), Email.TrimStartAndEnd());
	Obj->SetStringField(TEXT("password"), Password);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	TWeakObjectPtr<UIU_BackendSubsystem> WeakThis(this);
	SendJsonRequest(TEXT("POST"), TEXT("/v1/auth/register"), Body, false, false,
		[WeakThis, Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			if (!bConnected)
			{
				if (Callback) { Callback(false, TEXT("Backend unreachable")); }
				return;
			}
			if (Code == 201 || Code == 200)
			{
				TSharedPtr<FJsonObject> RespObj;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RespBody), RespObj) && RespObj.IsValid())
				{
					if (UIU_BackendSubsystem* Self = WeakThis.Get())
					{
						RespObj->TryGetStringField(TEXT("access_token"), Self->ClientAccessToken);
						RespObj->TryGetStringField(TEXT("refresh_token"), Self->ClientRefreshToken);
						RespObj->TryGetStringField(TEXT("username"), Self->LoggedInUsername);
					}
				}
				if (Callback) { Callback(true, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, ExtractErrorFromJson(RespBody, Code)); }
		});
}

void UIU_BackendSubsystem::LoginAccount(
	const FString& Username,
	const FString& Password,
	TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("username"), Username.TrimStartAndEnd());
	Obj->SetStringField(TEXT("password"), Password);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	TWeakObjectPtr<UIU_BackendSubsystem> WeakThis(this);
	SendJsonRequest(TEXT("POST"), TEXT("/v1/auth/login"), Body, false, false,
		[WeakThis, Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			if (!bConnected)
			{
				if (Callback) { Callback(false, TEXT("Backend unreachable")); }
				return;
			}
			if (Code == 200)
			{
				TSharedPtr<FJsonObject> RespObj;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RespBody), RespObj) && RespObj.IsValid())
				{
					if (UIU_BackendSubsystem* Self = WeakThis.Get())
					{
						RespObj->TryGetStringField(TEXT("access_token"), Self->ClientAccessToken);
						RespObj->TryGetStringField(TEXT("refresh_token"), Self->ClientRefreshToken);
						RespObj->TryGetStringField(TEXT("username"), Self->LoggedInUsername);
					}
				}
				if (Callback) { Callback(true, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, ExtractErrorFromJson(RespBody, Code)); }
		});
}

void UIU_BackendSubsystem::FetchCharacters(
	TFunction<void(bool bSuccess, const TArray<FIU_BackendCharacterSummary>& Characters, const FString& ErrorMessage)> Callback)
{
	TWeakObjectPtr<UIU_BackendSubsystem> WeakThis(this);
	SendJsonRequest(TEXT("GET"), TEXT("/v1/characters"), TEXT(""), true, false,
		[WeakThis, Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			TArray<FIU_BackendCharacterSummary> ParsedList;
			if (!bConnected || Code != 200)
			{
				if (Callback) { Callback(false, ParsedList, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
				return;
			}

			TSharedPtr<FJsonObject> RootObj;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RespBody), RootObj) && RootObj.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrPtr = nullptr;
				if (RootObj->TryGetArrayField(TEXT("characters"), ArrPtr) && ArrPtr)
				{
					for (const TSharedPtr<FJsonValue>& Val : *ArrPtr)
					{
						if (Val.IsValid() && Val->Type == EJson::Object)
						{
							FString SubJson;
							const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> SubWriter =
								TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SubJson);
							FJsonSerializer::Serialize(Val->AsObject().ToSharedRef(), SubWriter);

							FIU_BackendCharacterSummary Item;
							if (ParseCharacterSummaryFromJson(SubJson, Item))
							{
								ParsedList.Add(Item);
							}
						}
					}
				}
			}

			if (UIU_BackendSubsystem* Self = WeakThis.Get())
			{
				Self->CachedCharacters = ParsedList;
			}
			if (Callback) { Callback(true, ParsedList, TEXT("")); }
		});
}

void UIU_BackendSubsystem::CreateCharacter(
	const FString& CharacterName,
	const FString& RaceID,
	TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& Created, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_name"), CharacterName.TrimStartAndEnd());
	Obj->SetStringField(TEXT("race_id"), RaceID.IsEmpty() ? TEXT("race.zenith_warrior") : RaceID);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	SendJsonRequest(TEXT("POST"), TEXT("/v1/characters"), Body, true, false,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_BackendCharacterSummary Created;
			if (bConnected && (Code == 201 || Code == 200) && ParseCharacterSummaryFromJson(RespBody, Created))
			{
				if (Callback) { Callback(true, Created, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Created, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::RequestEnterWorldAndTravel(
	const FString& CharacterID,
	APlayerController* TravelController,
	TFunction<void(bool bSuccess, const FString& TicketID, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	TWeakObjectPtr<APlayerController> WeakPC(TravelController);
	SendJsonRequest(TEXT("POST"), TEXT("/v1/world/enter"), Body, true, false,
		[WeakPC, Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			if (!bConnected || Code != 200)
			{
				if (Callback) { Callback(false, TEXT(""), bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
				return;
			}

			FString TicketID, ServerAddr;
			TSharedPtr<FJsonObject> RespObj;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RespBody), RespObj) && RespObj.IsValid())
			{
				RespObj->TryGetStringField(TEXT("ticket_id"), TicketID);
				RespObj->TryGetStringField(TEXT("server_addr"), ServerAddr);
			}
			if (TicketID.IsEmpty())
			{
				if (Callback) { Callback(false, TEXT(""), TEXT("Missing ticket_id in EnterWorld response")); }
				return;
			}
			if (ServerAddr.IsEmpty())
			{
				ServerAddr = TEXT("127.0.0.1:7777");
			}

			const FString TravelURL = FString::Printf(TEXT("%s?Ticket=%s"), *ServerAddr, *TicketID);
			UE_LOG(LogInfiniteUniverse, Log, TEXT("[BackendSubsystem] Received One-Time World Ticket '%s' -> ClientTravel('%s')"),
				*TicketID.Left(16), *ServerAddr);

			if (APlayerController* PC = WeakPC.Get())
			{
				PC->ClientTravel(TravelURL, TRAVEL_Absolute);
			}
			if (Callback) { Callback(true, TicketID, TEXT("")); }
		});
}

void UIU_BackendSubsystem::QuickAuthenticateAndEnterWorld(
	const FString& Username,
	const FString& Password,
	const FString& DesiredCharName,
	const FString& DesiredRaceID,
	APlayerController* TravelController,
	TFunction<void(bool bSuccess, const FString& StatusMessage)> OnStatus)
{
	bBackendBridgeEnabled = true;
	TWeakObjectPtr<UIU_BackendSubsystem> WeakThis(this);
	TWeakObjectPtr<APlayerController> WeakPC(TravelController);
	const FString CleanUser = Username.TrimStartAndEnd();
	const FString CleanChar = DesiredCharName.TrimStartAndEnd().IsEmpty() ? CleanUser : DesiredCharName.TrimStartAndEnd();
	const FString CleanRace = DesiredRaceID.TrimStartAndEnd().IsEmpty() ? TEXT("race.zenith_warrior") : DesiredRaceID.TrimStartAndEnd();

	auto ProceedAfterAuth = [WeakThis, WeakPC, CleanChar, CleanRace, OnStatus]()
	{
		UIU_BackendSubsystem* Self = WeakThis.Get();
		if (!Self) { return; }

		Self->FetchCharacters([WeakThis, WeakPC, CleanChar, CleanRace, OnStatus](bool bListed, const TArray<FIU_BackendCharacterSummary>& Chars, const FString& ListErr)
		{
			UIU_BackendSubsystem* SelfInner = WeakThis.Get();
			if (!SelfInner || !bListed)
			{
				if (OnStatus) { OnStatus(false, ListErr); }
				return;
			}

			// Match by requested name first, or fall back to first character on the account
			FString SelectedID;
			for (const FIU_BackendCharacterSummary& C : Chars)
			{
				if (C.CharacterName.Equals(CleanChar, ESearchCase::IgnoreCase))
				{
					SelectedID = C.CharacterID;
					break;
				}
			}
			if (SelectedID.IsEmpty() && Chars.Num() > 0)
			{
				SelectedID = Chars[0].CharacterID;
			}

			if (!SelectedID.IsEmpty())
			{
				SelfInner->RequestEnterWorldAndTravel(SelectedID, WeakPC.Get(),
					[OnStatus](bool bEntered, const FString&, const FString& EnterErr)
					{
						if (OnStatus) { OnStatus(bEntered, bEntered ? TEXT("Entering World...") : EnterErr); }
					});
				return;
			}

			SelfInner->CreateCharacter(CleanChar, CleanRace,
				[WeakThis, WeakPC, OnStatus](bool bCreated, const FIU_BackendCharacterSummary& NewChar, const FString& CreateErr)
				{
					UIU_BackendSubsystem* SelfCreate = WeakThis.Get();
					if (!SelfCreate || !bCreated)
					{
						if (OnStatus) { OnStatus(false, CreateErr); }
						return;
					}
					SelfCreate->RequestEnterWorldAndTravel(NewChar.CharacterID, WeakPC.Get(),
						[OnStatus](bool bEntered, const FString&, const FString& EnterErr)
						{
							if (OnStatus) { OnStatus(bEntered, bEntered ? TEXT("Entering World...") : EnterErr); }
						});
				});
		});
	};

	LoginAccount(CleanUser, Password,
		[WeakThis, CleanUser, Password, ProceedAfterAuth, OnStatus](bool bLoggedIn, const FString& LoginErr)
		{
			if (bLoggedIn)
			{
				ProceedAfterAuth();
				return;
			}
			UIU_BackendSubsystem* Self = WeakThis.Get();
			if (!Self) { return; }

			const FString SynthEmail = FString::Printf(TEXT("%s@iu.local"), *CleanUser.ToLower());
			Self->RegisterAccount(CleanUser, SynthEmail, Password,
				[ProceedAfterAuth, OnStatus, LoginErr](bool bRegistered, const FString& RegErr)
				{
					if (bRegistered)
					{
						ProceedAfterAuth();
					}
					else if (OnStatus)
					{
						OnStatus(false, FString::Printf(TEXT("Login failed (%s); Register failed (%s)"), *LoginErr, *RegErr));
					}
				});
		});
}

void UIU_BackendSubsystem::TryTriggerCommandLineAutoAuth(APlayerController* LocalPC)
{
	if (bAutoAuthDispatched || AutoAuthUsername.IsEmpty() || !LocalPC || !LocalPC->IsLocalController())
	{
		return;
	}
	bAutoAuthDispatched = true;

	UE_LOG(LogInfiniteUniverse, Log, TEXT("[BackendSubsystem] AutoAuth triggered for user '%s' (Character '%s')"),
		*AutoAuthUsername, *AutoAuthCharacterName);

	QuickAuthenticateAndEnterWorld(
		AutoAuthUsername,
		AutoAuthPassword,
		AutoAuthCharacterName,
		AutoAuthRaceID,
		LocalPC,
		[](bool bSuccess, const FString& Msg)
		{
			if (bSuccess)
			{
				UE_LOG(LogInfiniteUniverse, Log, TEXT("[BackendSubsystem] AutoAuth succeeded: %s"), *Msg);
			}
			else
			{
				UE_LOG(LogInfiniteUniverse, Error, TEXT("[BackendSubsystem] AutoAuth FAILED: %s"), *Msg);
			}
		});
}

void UIU_BackendSubsystem::ClaimCharacterLease(
	const FString& TicketID,
	TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& LoadedState, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("ticket_id"), TicketID);
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("zone_id"), ServerZoneID);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/lease/claim"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_BackendCharacterSummary Summary;
			if (bConnected && Code == 200 && ParseCharacterSummaryFromJson(RespBody, Summary))
			{
				if (Callback) { Callback(true, Summary, TEXT("")); }
				return;
			}
			if (Callback)
			{
				Callback(false, Summary, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable during ClaimLease"));
			}
		});
}

void UIU_BackendSubsystem::RenewActiveLeases(
	const TArray<FIU_LeaseRenewalEntry>& ActiveLeases,
	TFunction<void(bool bSuccess, const TArray<FString>& RevokedCharacterIDs)> Callback)
{
	if (ActiveLeases.Num() == 0)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> LeaseArr;
	for (const FIU_LeaseRenewalEntry& Entry : ActiveLeases)
	{
		TSharedRef<FJsonObject> ItemObj = MakeShared<FJsonObject>();
		ItemObj->SetStringField(TEXT("character_id"), Entry.CharacterID);
		ItemObj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(Entry.FencingEpoch));
		LeaseArr.Add(MakeShared<FJsonValueObject>(ItemObj));
	}

	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("server_id"), ServerInstanceID);
	RootObj->SetArrayField(TEXT("leases"), LeaseArr);
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(RootObj, Writer);

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/lease/renew"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			TArray<FString> Revoked;
			if (!bConnected || Code != 200)
			{
				if (Callback) { Callback(false, Revoked); }
				return;
			}

			TSharedPtr<FJsonObject> RespObj;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RespBody), RespObj) && RespObj.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* RevokedArr = nullptr;
				if (RespObj->TryGetArrayField(TEXT("revoked_character_ids"), RevokedArr) && RevokedArr)
				{
					for (const TSharedPtr<FJsonValue>& V : *RevokedArr)
					{
						if (V.IsValid() && V->Type == EJson::String)
						{
							Revoked.Add(V->AsString());
						}
					}
				}
			}
			if (Callback) { Callback(true, Revoked); }
		});
}

void UIU_BackendSubsystem::SaveCharacterCheckpoint(
	const FIU_CharacterSavePayload& Payload,
	TFunction<void(bool bSuccess, const FString& ErrorMessage)> Callback)
{
	FIU_CharacterSavePayload Normalized = Payload;
	if (Normalized.ServerID.IsEmpty()) { Normalized.ServerID = ServerInstanceID; }
	if (Normalized.ZoneID.IsEmpty()) { Normalized.ZoneID = ServerZoneID; }

	const FString Body = SerializeSavePayloadToJson(Normalized);
	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/character/save"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			if (bConnected && Code == 200)
			{
				if (Callback) { Callback(true, TEXT("")); }
				return;
			}
			if (Callback)
			{
				Callback(false, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable during SaveCheckpoint"));
			}
		});
}

void UIU_BackendSubsystem::GrantCharacterEXP(
	const FString& CharacterID,
	int64 FencingEpoch,
	float OutgoingDamage,
	bool bKilledTarget,
	const FString& IdempotencyKey,
	TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& UpdatedState, const FString& ErrorMessage)> Callback)
{
	const int64 ComputedReward = FMath::Max<int64>(1, FMath::RoundToInt64(static_cast<double>(OutgoingDamage) * 1.5));
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetNumberField(TEXT("outgoing_damage"), static_cast<double>(OutgoingDamage));
	Obj->SetNumberField(TEXT("exp_gain"), static_cast<double>(ComputedReward));
	Obj->SetNumberField(TEXT("potential_gain"), static_cast<double>(ComputedReward));
	Obj->SetBoolField(TEXT("killed_target"), bKilledTarget);
	Obj->SetStringField(TEXT("idempotency_key"), IdempotencyKey);

	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/character/exp"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_BackendCharacterSummary Updated;
			if (bConnected && Code == 200 && ParseCharacterSummaryFromJson(RespBody, Updated))
			{
				if (Callback) { Callback(true, Updated, TEXT("")); }
				return;
			}
			if (Callback)
			{
				Callback(false, Updated, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable during GrantEXP"));
			}
		});
}

void UIU_BackendSubsystem::AllocateCharacterPotential(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& StatTypeCode,
	TFunction<void(bool bSuccess, const FIU_BackendCharacterSummary& UpdatedState, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("stat_type"), StatTypeCode);

	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Obj, Writer);

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/character/potential"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_BackendCharacterSummary Updated;
			if (bConnected && Code == 200 && ParseCharacterSummaryFromJson(RespBody, Updated))
			{
				if (Callback) { Callback(true, Updated, TEXT("")); }
				return;
			}
			if (Callback)
			{
				Callback(false, Updated, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable during AllocatePotential"));
			}
		});
}

void UIU_BackendSubsystem::ReleaseCharacterLease(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FIU_CharacterSavePayload* OptionalFinalSave)
{
	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("character_id"), CharacterID);
	RootObj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	RootObj->SetStringField(TEXT("server_id"), ServerInstanceID);

	if (OptionalFinalSave)
	{
		FIU_CharacterSavePayload FinalCopy = *OptionalFinalSave;
		FinalCopy.SnapshotReason = TEXT("LOGOUT");
		if (FinalCopy.ServerID.IsEmpty()) { FinalCopy.ServerID = ServerInstanceID; }
		if (FinalCopy.ZoneID.IsEmpty()) { FinalCopy.ZoneID = ServerZoneID; }

		TSharedPtr<FJsonObject> SaveObj;
		const FString SaveJson = SerializeSavePayloadToJson(FinalCopy);
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SaveJson), SaveObj) && SaveObj.IsValid())
		{
			RootObj->SetObjectField(TEXT("final_save"), SaveObj);
		}
	}

	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(RootObj, Writer);

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/lease/release"), Body, false, true,
		[CharacterID, FencingEpoch](int32 Code, const FString& RespBody, bool bConnected)
		{
			if (bConnected && Code == 200)
			{
				UE_LOG(LogInfiniteUniverse, Log, TEXT("[BackendSubsystem] Released lease & saved LOGOUT checkpoint for '%s' (Epoch #%lld)"),
					*CharacterID, FencingEpoch);
			}
			else
			{
				UE_LOG(LogInfiniteUniverse, Warning, TEXT("[BackendSubsystem] ReleaseLease warning for '%s' (HTTP %d): %s"),
					*CharacterID, Code, *RespBody.Left(120));
			}
		});
}

FString UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(const FString& InStr)
{
	if (InStr.IsEmpty())
	{
		return InStr;
	}

	auto MapVietnameseChar = [](TCHAR Ch) -> TCHAR
	{
		if (Ch < 128)
		{
			return Ch;
		}
		switch (Ch)
		{
		case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x0102:
		case 0x1EA0: case 0x1EA2: case 0x1EA4: case 0x1EA6: case 0x1EA8: case 0x1EAA: case 0x1EAC:
		case 0x1EAE: case 0x1EB0: case 0x1EB2: case 0x1EB4: case 0x1EB6:
			return TEXT('A');
		case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x0103:
		case 0x1EA1: case 0x1EA3: case 0x1EA5: case 0x1EA7: case 0x1EA9: case 0x1EAB: case 0x1EAD:
		case 0x1EAF: case 0x1EB1: case 0x1EB3: case 0x1EB5: case 0x1EB7:
			return TEXT('a');

		case 0x0110: return TEXT('D');
		case 0x0111: return TEXT('d');

		case 0x00C8: case 0x00C9: case 0x00CA:
		case 0x1EB8: case 0x1EBA: case 0x1EBC: case 0x1EBE: case 0x1EC0: case 0x1EC2: case 0x1EC4: case 0x1EC6:
			return TEXT('E');
		case 0x00E8: case 0x00E9: case 0x00EA:
		case 0x1EB9: case 0x1EBB: case 0x1EBD: case 0x1EBF: case 0x1EC1: case 0x1EC3: case 0x1EC5: case 0x1EC7:
			return TEXT('e');

		case 0x00CC: case 0x00CD: case 0x0128: case 0x1EC8: case 0x1ECA:
			return TEXT('I');
		case 0x00EC: case 0x00ED: case 0x0129: case 0x1EC9: case 0x1ECB:
			return TEXT('i');

		case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x01A0:
		case 0x1ECC: case 0x1ECE: case 0x1ED0: case 0x1ED2: case 0x1ED4: case 0x1ED6: case 0x1ED8:
		case 0x1EDA: case 0x1EDC: case 0x1EDE: case 0x1EE0: case 0x1EE2:
			return TEXT('O');
		case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x01A1:
		case 0x1ECD: case 0x1ECF: case 0x1ED1: case 0x1ED3: case 0x1ED5: case 0x1ED7: case 0x1ED9:
		case 0x1EDB: case 0x1EDD: case 0x1EDF: case 0x1EE1: case 0x1EE3:
			return TEXT('o');

		case 0x00D9: case 0x00DA: case 0x0168: case 0x01AF:
		case 0x1EE4: case 0x1EE6: case 0x1EE8: case 0x1EEA: case 0x1EEC: case 0x1EEE: case 0x1EF0:
			return TEXT('U');
		case 0x00F9: case 0x00FA: case 0x0169: case 0x01B0:
		case 0x1EE5: case 0x1EE7: case 0x1EE9: case 0x1EEB: case 0x1EED: case 0x1EEF: case 0x1EF1:
			return TEXT('u');

		case 0x00DD: case 0x1EF2: case 0x1EF4: case 0x1EF6: case 0x1EF8:
			return TEXT('Y');
		case 0x00FD: case 0x1EF3: case 0x1EF5: case 0x1EF7: case 0x1EF9:
			return TEXT('y');

		default:
			return TEXT(' ');
		}
	};

	FString Out;
	Out.Reserve(InStr.Len());
	for (TCHAR C : InStr)
	{
		Out.AppendChar(MapVietnameseChar(C));
	}
	return Out;
}

namespace
{
	static FString FormatAttrPairForUI(const FString& AttrID, double Val)
	{
		if (AttrID == TEXT("attr.max_hp")) { return FString::Printf(TEXT("+%.0f HP"), Val); }
		if (AttrID == TEXT("attr.max_energy")) { return FString::Printf(TEXT("+%.0f Ki"), Val); }
		if (AttrID == TEXT("attr.phys_atk")) { return FString::Printf(TEXT("+%.0f SucDanh"), Val); }
		if (AttrID == TEXT("attr.ki_atk")) { return FString::Printf(TEXT("+%.0f KiAtk"), Val); }
		if (AttrID == TEXT("attr.def")) { return FString::Printf(TEXT("+%.0f Giap"), Val); }
		if (AttrID == TEXT("attr.phys_dmg_pct") || AttrID == TEXT("attr.ki_dmg_pct")) { return FString::Printf(TEXT("+%.1f%% ST"), Val * 100.0); }
		if (AttrID == TEXT("attr.crit_rate")) { return FString::Printf(TEXT("+%.1f%% Crit"), Val * 100.0); }
		if (AttrID == TEXT("attr.crit_dmg")) { return FString::Printf(TEXT("+%.0f%% CritST"), Val * 100.0); }
		if (AttrID == TEXT("attr.move_spd_pct")) { return FString::Printf(TEXT("+%.0f%% TocChay"), Val * 100.0); }
		return FString::Printf(TEXT("%s +%.1f"), *AttrID, Val);
	}

	static bool ParseSingleItemViewFromJsonObj(const TSharedPtr<FJsonObject>& Obj, FIU_InventoryItemView& OutView)
	{
		if (!Obj.IsValid())
		{
			return false;
		}
		Obj->TryGetStringField(TEXT("instance_id"), OutView.InstanceID);
		Obj->TryGetStringField(TEXT("item_def_id"), OutView.ItemDefID);

		FString RawName;
		Obj->TryGetStringField(TEXT("display_name"), RawName);
		OutView.DisplayName = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(RawName.IsEmpty() ? OutView.ItemDefID : RawName);

		Obj->TryGetStringField(TEXT("category"), OutView.Category);
		Obj->TryGetStringField(TEXT("equip_slot"), OutView.EquipSlot);
		Obj->TryGetStringField(TEXT("equipped_slot_id"), OutView.EquippedSlotID);
		Obj->TryGetStringField(TEXT("rarity"), OutView.Rarity);

		double Num = 0.0;
		if (Obj->TryGetNumberField(TEXT("required_level"), Num)) { OutView.RequiredLevel = static_cast<int32>(Num); }
		if (Obj->TryGetNumberField(TEXT("bag_slot_index"), Num)) { OutView.BagSlotIndex = static_cast<int32>(Num); }
		if (Obj->TryGetNumberField(TEXT("stack_count"), Num)) { OutView.StackCount = FMath::Max(1, static_cast<int32>(Num)); }
		if (Obj->TryGetNumberField(TEXT("max_stack"), Num)) { OutView.MaxStack = FMath::Max(1, static_cast<int32>(Num)); }
		if (Obj->TryGetNumberField(TEXT("enhance_level"), Num)) { OutView.EnhanceLevel = FMath::Max(0, static_cast<int32>(Num)); }
		if (Obj->TryGetNumberField(TEXT("max_enhance_level"), Num)) { OutView.MaxEnhanceLevel = FMath::Max(0, static_cast<int32>(Num)); }
		if (Obj->TryGetNumberField(TEXT("enhance_pity"), Num)) { OutView.EnhancePity = FMath::Max(0, static_cast<int32>(Num)); }
		if (Obj->TryGetNumberField(TEXT("sell_price_coin"), Num)) { OutView.SellPriceCoin = FMath::Max<int64>(0, static_cast<int64>(Num)); }
		if (Obj->TryGetNumberField(TEXT("next_enhance_coin"), Num)) { OutView.NextEnhanceCoin = FMath::Max<int64>(0, static_cast<int64>(Num)); }
		if (Obj->TryGetNumberField(TEXT("next_enhance_mat"), Num)) { OutView.NextEnhanceMat = FMath::Max(0, static_cast<int32>(Num)); }
		if (Obj->TryGetNumberField(TEXT("next_enhance_bp"), Num)) { OutView.NextEnhanceBP = FMath::Max(0, static_cast<int32>(Num)); }

		Obj->TryGetBoolField(TEXT("is_bound"), OutView.bIsBound);
		Obj->TryGetBoolField(TEXT("is_player_locked"), OutView.bIsPlayerLocked);

		TArray<FString> StatParts;
		const TSharedPtr<FJsonObject>* BaseStatsObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("base_stats"), BaseStatsObj) && BaseStatsObj && BaseStatsObj->IsValid())
		{
			// Stable canonical ordering for display
			static const TCHAR* OrderedKeys[] = {
				TEXT("attr.phys_atk"), TEXT("attr.ki_atk"), TEXT("attr.max_hp"), TEXT("attr.max_energy"), TEXT("attr.def")
			};
			const bool bHasPhys = (*BaseStatsObj)->HasField(TEXT("attr.phys_atk"));
			for (const TCHAR* K : OrderedKeys)
			{
				double V = 0.0;
				if ((*BaseStatsObj)->TryGetNumberField(K, V) && !FMath::IsNearlyZero(V))
				{
					if (FCString::Strcmp(K, TEXT("attr.ki_atk")) == 0 && bHasPhys)
					{
						continue;
					}
					StatParts.Add(FormatAttrPairForUI(K, V));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* AffixArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("affixes"), AffixArr) && AffixArr)
		{
			for (const TSharedPtr<FJsonValue>& AfVal : *AffixArr)
			{
				if (AfVal.IsValid() && AfVal->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> AfObj = AfVal->AsObject();
					FString AttrID;
					double V = 0.0;
					AfObj->TryGetStringField(TEXT("attr_id"), AttrID);
					AfObj->TryGetNumberField(TEXT("value"), V);
					if (!AttrID.IsEmpty())
					{
						StatParts.Add(FString::Printf(TEXT("[Af]%s"), *FormatAttrPairForUI(AttrID, V)));
					}
				}
			}
		}

		if (StatParts.Num() > 0)
		{
			OutView.StatSummaryText = FString::Join(StatParts, TEXT(", "));
		}
		else if (OutView.ItemDefID == TEXT("item.consumable_verdant_elixir_s"))
		{
			OutView.StatSummaryText = TEXT("+150 HP & +150 Ki (CD 3s)");
		}
		else if (OutView.ItemDefID == TEXT("item.mat_crimson_ore_t1"))
		{
			OutView.StatSummaryText = TEXT("Da Cuong Hoa +1..+10");
		}

		return !OutView.InstanceID.IsEmpty();
	}

	static bool ParseInventorySnapshotObject(const TSharedPtr<FJsonObject>& RootObj, FIU_InventorySnapshot& OutSnap)
	{
		if (!RootObj.IsValid())
		{
			return false;
		}
		OutSnap = FIU_InventorySnapshot();
		RootObj->TryGetStringField(TEXT("character_id"), OutSnap.CharacterID);

		FString RawAction;
		if (RootObj->TryGetStringField(TEXT("action_result"), RawAction))
		{
			OutSnap.ActionResult = UIU_BackendSubsystem::SanitizeVietnameseForSlateASCII(RawAction);
		}

		bool bEnhBool = false;
		if (RootObj->TryGetBoolField(TEXT("enhance_succeeded"), bEnhBool))
		{
			OutSnap.bHasEnhanceOutcome = true;
			OutSnap.bEnhanceSucceeded = bEnhBool;
		}

		double Num = 0.0;
		if (RootObj->TryGetNumberField(TEXT("level"), Num)) { OutSnap.Level = FMath::Max(1, static_cast<int32>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("standard_coin"), Num)) { OutSnap.StandardCoin = FMath::Max<int64>(0, static_cast<int64>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("hp"), Num)) { OutSnap.HP = static_cast<float>(Num); }
		if (RootObj->TryGetNumberField(TEXT("max_hp"), Num)) { OutSnap.MaxHP = static_cast<float>(Num); }
		if (RootObj->TryGetNumberField(TEXT("energy"), Num)) { OutSnap.Energy = static_cast<float>(Num); }
		if (RootObj->TryGetNumberField(TEXT("max_energy"), Num)) { OutSnap.MaxEnergy = static_cast<float>(Num); }
		if (RootObj->TryGetNumberField(TEXT("attack_power"), Num)) { OutSnap.AttackPower = static_cast<float>(Num); }
		if (RootObj->TryGetNumberField(TEXT("attack_power_multiplier"), Num)) { OutSnap.AttackPowerMultiplier = static_cast<float>(Num); }
		if (RootObj->TryGetNumberField(TEXT("defense"), Num)) { OutSnap.Defense = FMath::Max(0.0f, static_cast<float>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("used_bag_slots"), Num)) { OutSnap.UsedBagSlots = FMath::Max(0, static_cast<int32>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("max_bag_slots"), Num)) { OutSnap.MaxBagSlots = FMath::Max(1, static_cast<int32>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("elixir_count"), Num)) { OutSnap.ElixirCount = FMath::Max(0, static_cast<int32>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("crimson_ore_count"), Num)) { OutSnap.CrimsonOreCount = FMath::Max(0, static_cast<int32>(Num)); }
		if (RootObj->TryGetNumberField(TEXT("coins_delta"), Num)) { OutSnap.CoinsDelta = static_cast<int64>(Num); }

		const TArray<TSharedPtr<FJsonValue>>* BagArr = nullptr;
		if (RootObj->TryGetArrayField(TEXT("bag_items"), BagArr) && BagArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *BagArr)
			{
				if (V.IsValid() && V->Type == EJson::Object)
				{
					FIU_InventoryItemView Item;
					if (ParseSingleItemViewFromJsonObj(V->AsObject(), Item))
					{
						OutSnap.BagItems.Add(MoveTemp(Item));
					}
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* EqArr = nullptr;
		if (RootObj->TryGetArrayField(TEXT("equipped_items"), EqArr) && EqArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *EqArr)
			{
				if (V.IsValid() && V->Type == EJson::Object)
				{
					FIU_InventoryItemView Item;
					if (ParseSingleItemViewFromJsonObj(V->AsObject(), Item))
					{
						OutSnap.EquippedItems.Add(MoveTemp(Item));
					}
				}
			}
		}

		return !OutSnap.CharacterID.IsEmpty();
	}
}

bool UIU_BackendSubsystem::ParseInventorySnapshotFromJson(const FString& JsonString, FIU_InventorySnapshot& OutSnapshot)
{
	TSharedPtr<FJsonObject> RootObj;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), RootObj) || !RootObj.IsValid())
	{
		return false;
	}
	return ParseInventorySnapshotObject(RootObj, OutSnapshot);
}

bool UIU_BackendSubsystem::ParseMobLootResponseFromJson(
	const FString& JsonString,
	FIU_MobLootDropSummary& OutLoot,
	FIU_InventorySnapshot& OutSnapshot)
{
	OutLoot = FIU_MobLootDropSummary();
	TSharedPtr<FJsonObject> RootObj;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), RootObj) || !RootObj.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* LootObjPtr = nullptr;
	if (RootObj->TryGetObjectField(TEXT("loot"), LootObjPtr) && LootObjPtr && LootObjPtr->IsValid())
	{
		double Num = 0.0;
		if ((*LootObjPtr)->TryGetNumberField(TEXT("coins_granted"), Num)) { OutLoot.CoinsGranted = FMath::Max<int64>(0, static_cast<int64>(Num)); }
		if ((*LootObjPtr)->TryGetNumberField(TEXT("standard_coin"), Num)) { OutLoot.NewCoinBalance = FMath::Max<int64>(0, static_cast<int64>(Num)); }
		(*LootObjPtr)->TryGetBoolField(TEXT("bag_full_skipped"), OutLoot.bBagFullSkipped);

		const TArray<TSharedPtr<FJsonValue>>* DroppedArr = nullptr;
		if ((*LootObjPtr)->TryGetArrayField(TEXT("items_dropped"), DroppedArr) && DroppedArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *DroppedArr)
			{
				if (V.IsValid() && V->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> DObj = V->AsObject();
					FString RawName, Rarity;
					double QtyNum = 1.0;
					DObj->TryGetStringField(TEXT("display_name"), RawName);
					DObj->TryGetStringField(TEXT("rarity"), Rarity);
					DObj->TryGetNumberField(TEXT("quantity"), QtyNum);
					const FString CleanName = SanitizeVietnameseForSlateASCII(RawName);
					const int32 Qty = FMath::Max(1, static_cast<int32>(QtyNum));
					OutLoot.DroppedItemLabels.Add(
						Qty > 1
							? FString::Printf(TEXT("%s x%d"), *CleanName, Qty)
							: FString::Printf(TEXT("%s [%s]"), *CleanName, *Rarity));
				}
			}
		}
	}

	const TSharedPtr<FJsonObject>* InvObjPtr = nullptr;
	if (RootObj->TryGetObjectField(TEXT("inventory"), InvObjPtr) && InvObjPtr && InvObjPtr->IsValid())
	{
		return ParseInventorySnapshotObject(*InvObjPtr, OutSnapshot);
	}
	return false;
}

void UIU_BackendSubsystem::FetchCharacterInventory(
	const FString& CharacterID,
	int64 FencingEpoch,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/inventory/get"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::EquipInventoryItem(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& ItemInstanceID,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("item_instance_id"), ItemInstanceID);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/inventory/equip"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::UnequipEquipmentSlot(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& SlotID,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("slot_id"), SlotID);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/inventory/unequip"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::UseInventoryConsumable(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& OptionalItemInstanceID,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("item_instance_id"), OptionalItemInstanceID);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/inventory/use"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::ToggleInventoryItemLock(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& ItemInstanceID,
	bool bLocked,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("item_instance_id"), ItemInstanceID);
	Obj->SetBoolField(TEXT("locked"), bLocked);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/inventory/lock"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::SellInventoryItem(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& ItemInstanceID,
	int32 Quantity,
	const FString& IdempotencyKey,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("item_instance_id"), ItemInstanceID);
	Obj->SetNumberField(TEXT("quantity"), Quantity);
	Obj->SetStringField(TEXT("idempotency_key"), IdempotencyKey);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/inventory/sell"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::EnhanceInventoryEquipment(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& ItemInstanceID,
	const FString& IdempotencyKey,
	TFunction<void(bool bSuccess, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("item_instance_id"), ItemInstanceID);
	Obj->SetStringField(TEXT("idempotency_key"), IdempotencyKey);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/economy/enhance"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseInventorySnapshotFromJson(RespBody, Snap))
			{
				if (Callback) { Callback(true, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}

void UIU_BackendSubsystem::RollMonsterLoot(
	const FString& CharacterID,
	int64 FencingEpoch,
	const FString& SourceEntityID,
	const FString& IdempotencyKey,
	TFunction<void(bool bSuccess, const FIU_MobLootDropSummary& LootResult, const FIU_InventorySnapshot& Snapshot, const FString& ErrorMessage)> Callback)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("character_id"), CharacterID);
	Obj->SetNumberField(TEXT("fencing_epoch"), static_cast<double>(FencingEpoch));
	Obj->SetStringField(TEXT("server_id"), ServerInstanceID);
	Obj->SetStringField(TEXT("source_entity_id"), SourceEntityID.IsEmpty() ? TEXT("mob.prototype_raider") : SourceEntityID);
	Obj->SetStringField(TEXT("zone_id"), ServerZoneID);
	Obj->SetStringField(TEXT("idempotency_key"), IdempotencyKey);
	FString Body;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body));

	SendJsonRequest(TEXT("POST"), TEXT("/internal/v1/loot/roll-mob"), Body, false, true,
		[Callback = MoveTemp(Callback)](int32 Code, const FString& RespBody, bool bConnected)
		{
			FIU_MobLootDropSummary LootRes;
			FIU_InventorySnapshot Snap;
			if (bConnected && Code == 200 && ParseMobLootResponseFromJson(RespBody, LootRes, Snap))
			{
				if (Callback) { Callback(true, LootRes, Snap, TEXT("")); }
				return;
			}
			if (Callback) { Callback(false, LootRes, Snap, bConnected ? ExtractErrorFromJson(RespBody, Code) : TEXT("Backend unreachable")); }
		});
}
