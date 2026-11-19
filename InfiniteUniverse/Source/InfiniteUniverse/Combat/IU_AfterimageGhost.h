// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IU_AfterimageGhost.generated.h"

class USceneComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ACharacter;

/**
 * Translucent Phantom Afterimage Decoy ("Bóng mờ / Tàn ảnh" - Black Myth: Wukong style evade).
 * Spawned at origin A when the player evades near an enemy while relocating to destination B.
 * Captures either the exact SkeletalMesh pose (via UPoseableMeshComponent) or prototype silhouette,
 * then smoothly dissolves and self-destructs.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_AfterimageGhost : public AActor
{
	GENERATED_BODY()

public:
	AIU_AfterimageGhost();

	virtual void Tick(float DeltaTime) override;

	/** Copies visual pose/transforms from SourceCharacter at origin A and starts dissolve timer. */
	void InitializeFromCharacter(const ACharacter* SourceCharacter, float InLifetime = 0.65f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|VFX")
	float GhostLifetime = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|VFX")
	float InitialOpacity = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|VFX")
	FLinearColor GhostAuraColor = FLinearColor(0.22f, 0.85f, 1.0f, 1.0f);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|VFX")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|VFX")
	TObjectPtr<UPoseableMeshComponent> PoseableGhostMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|VFX")
	TObjectPtr<UStaticMeshComponent> PlaceholderGhostBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|VFX")
	TObjectPtr<UStaticMeshComponent> PlaceholderGhostVisor;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostDynamicMaterials;

	float ElapsedTime = 0.0f;
	FVector BaseRootScale = FVector::OneVector;
};
