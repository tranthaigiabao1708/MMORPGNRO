// Copyright InfiniteUniverse Dev. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/IU_GameplayTypes.h"
#include "IU_KiProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UNiagaraSystem;

/**
 * Energy projectile spawned by Ki Blast (Milestone 3: P3-05).
 * Validates collision, ignores instigator & dead targets, and routes damage through UIU_DamageComponent.
 */
UCLASS()
class INFINITEUNIVERSE_API AIU_KiProjectile : public AActor
{
	GENERATED_BODY()

public:
	AIU_KiProjectile();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initializes velocity, instigator exclusion, and damage amount immediately after spawning. */
	void InitializeProjectile(AActor* InShooter, const FVector& ShootDirection, float InDamage, float InSpeed);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|KiBlast", meta = (ClampMin = "1.0", ClampMax = "5000.0"))
	float ProjectileDamage = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|KiBlast", meta = (ClampMin = "200.0", ClampMax = "15000.0"))
	float ProjectileSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|KiBlast", meta = (ClampMin = "0.2", ClampMax = "15.0"))
	float MaxLifetimeSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|KiBlast", meta = (ClampMin = "5.0", ClampMax = "150.0"))
	float CollisionRadius = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IU|KiBlast")
	TObjectPtr<UNiagaraSystem> ImpactVFX;

	UFUNCTION(BlueprintPure, Category = "IU|KiBlast")
	USphereComponent* GetCollisionSphere() const { return CollisionSphere; }

	UFUNCTION(BlueprintPure, Category = "IU|KiBlast")
	UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }

	UFUNCTION(BlueprintPure, Category = "IU|KiBlast")
	AActor* GetShooterActor() const { return ShooterActor.Get(); }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|KiBlast")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|KiBlast")
	TObjectPtr<UStaticMeshComponent> OrbMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IU|KiBlast")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

private:
	UFUNCTION()
	void HandleSphereOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSphereHit(
		UPrimitiveComponent* HitComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	void ProcessImpact(AActor* TargetActor, const FVector& ImpactPoint);

	TWeakObjectPtr<AActor> ShooterActor;
	bool bHasImpacted = false;
};
