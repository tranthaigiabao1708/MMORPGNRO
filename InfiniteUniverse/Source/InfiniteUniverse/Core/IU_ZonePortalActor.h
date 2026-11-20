// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IU_ZonePortalActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * Step 6 NRO Capsule Spaceship / Inter-Zone Warp Gate (AIU_ZonePortalActor).
 * Triggers server-authoritative atomic ZONE_TRANSFER handoff (:7777 <-> :7778) when a living player enters the gate.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_ZonePortalActor : public AActor
{
	GENERATED_BODY()

public:
	AIU_ZonePortalActor();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|ZoneTransfer")
	FString PortalTitle = TEXT("TAU VU TRU -> CAO NGUYEN TITAN (KHU 2)");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|ZoneTransfer")
	FString TargetZoneID = TEXT("zone.p01_highland_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "IU|ZoneTransfer")
	int32 TargetChannelID = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|ZoneTransfer")
	bool bEnableOverlapAutoTransfer = true;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|ZoneTransfer")
	TObjectPtr<USphereComponent> TriggerSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|ZoneTransfer")
	TObjectPtr<UStaticMeshComponent> PortalPedestalMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|ZoneTransfer")
	TObjectPtr<UStaticMeshComponent> PortalEnergyPodMesh;

private:
	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	TMap<TWeakObjectPtr<AActor>, double> RecentActorCooldowns;
};
