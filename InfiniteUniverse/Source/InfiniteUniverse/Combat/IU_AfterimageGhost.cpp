// Copyright InfiniteUniverse Dev. All Rights Reserved.

#include "Combat/IU_AfterimageGhost.h"
#include "Components/SceneComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AIU_AfterimageGhost::AIU_AfterimageGhost()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PoseableGhostMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PoseableGhostMesh"));
	PoseableGhostMesh->SetupAttachment(SceneRoot);
	PoseableGhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableGhostMesh->SetGenerateOverlapEvents(false);
	PoseableGhostMesh->CastShadow = false;
	PoseableGhostMesh->SetVisibility(false);

	PlaceholderGhostBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderGhostBody"));
	PlaceholderGhostBody->SetupAttachment(SceneRoot);
	PlaceholderGhostBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderGhostBody->SetGenerateOverlapEvents(false);
	PlaceholderGhostBody->CastShadow = false;
	PlaceholderGhostBody->SetRelativeLocation(FVector(0.0f, 0.0f, -5.0f));
	PlaceholderGhostBody->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.65f));

	PlaceholderGhostVisor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderGhostVisor"));
	PlaceholderGhostVisor->SetupAttachment(PlaceholderGhostBody);
	PlaceholderGhostVisor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderGhostVisor->SetGenerateOverlapEvents(false);
	PlaceholderGhostVisor->CastShadow = false;
	PlaceholderGhostVisor->SetRelativeLocation(FVector(42.0f, 0.0f, 32.0f));
	PlaceholderGhostVisor->SetRelativeScale3D(FVector(0.45f, 0.70f, 0.12f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderAsset.Succeeded())
	{
		PlaceholderGhostBody->SetStaticMesh(CylinderAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		PlaceholderGhostVisor->SetStaticMesh(CubeAsset.Object);
	}
}

void AIU_AfterimageGhost::InitializeFromCharacter(const ACharacter* SourceCharacter, float InLifetime)
{
	GhostLifetime = FMath::Max(0.10f, InLifetime);
	ElapsedTime = 0.0f;
	BaseRootScale = GetActorScale3D();

	UMaterialInterface* BaseGhostMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Combat/VFX/M_AfterimageGhost.M_AfterimageGhost"));
	if (!BaseGhostMat)
	{
		BaseGhostMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	auto ApplyGhostMaterialToMesh = [this, BaseGhostMat](UMeshComponent* MeshComp)
	{
		if (!MeshComp || !BaseGhostMat)
		{
			return;
		}
		const int32 NumMats = FMath::Max(1, MeshComp->GetNumMaterials());
		for (int32 Idx = 0; Idx < NumMats; ++Idx)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseGhostMat, this))
			{
				MID->SetVectorParameterValue(TEXT("GhostColor"), GhostAuraColor);
				MID->SetVectorParameterValue(TEXT("Color"), GhostAuraColor);
				MID->SetScalarParameterValue(TEXT("GhostOpacity"), InitialOpacity);
				MeshComp->SetMaterial(Idx, MID);
				GhostDynamicMaterials.Add(MID);
			}
		}
	};

	bool bUsedSkeletalPose = false;
	if (SourceCharacter)
	{
		if (const USkeletalMeshComponent* SrcSkel = SourceCharacter->GetMesh())
		{
			if (SrcSkel->GetSkeletalMeshAsset() != nullptr)
			{
				PoseableGhostMesh->SetSkinnedAssetAndUpdate(SrcSkel->GetSkeletalMeshAsset());
				PoseableGhostMesh->SetRelativeTransform(SrcSkel->GetRelativeTransform());
				PoseableGhostMesh->CopyPoseFromSkeletalComponent(const_cast<USkeletalMeshComponent*>(SrcSkel));
				PoseableGhostMesh->SetVisibility(true);
				PlaceholderGhostBody->SetVisibility(false, true);
				ApplyGhostMaterialToMesh(PoseableGhostMesh);
				bUsedSkeletalPose = true;
			}
		}

		if (!bUsedSkeletalPose)
		{
			// Match exact crouch/lean/scale of prototype body silhouette at origin A
			TInlineComponentArray<UStaticMeshComponent*> StaticComps(SourceCharacter);
			for (const UStaticMeshComponent* Comp : StaticComps)
			{
				if (Comp && Comp->GetName() == TEXT("PlaceholderBodyMesh"))
				{
					PlaceholderGhostBody->SetRelativeTransform(Comp->GetRelativeTransform());
					break;
				}
			}
		}
	}

	if (!bUsedSkeletalPose)
	{
		PlaceholderGhostBody->SetVisibility(true, true);
		ApplyGhostMaterialToMesh(PlaceholderGhostBody);
		ApplyGhostMaterialToMesh(PlaceholderGhostVisor);
	}

	SetLifeSpan(GhostLifetime + 0.05f);
}

void AIU_AfterimageGhost::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;
	const float NormalizedAge = FMath::Clamp(ElapsedTime / FMath::Max(GhostLifetime, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	// Hold vivid silhouette briefly for first 25% of lifetime, then dissolve smoothly like Wukong's mirage
	const float FadeCurve = 1.0f - FMath::InterpEaseIn(0.0f, 1.0f, NormalizedAge, 1.8f);
	const float CurrentOpacity = InitialOpacity * FadeCurve;

	for (UMaterialInstanceDynamic* MID : GhostDynamicMaterials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("GhostOpacity"), CurrentOpacity);
		}
	}

	// Subtle ethereal spirit expansion as the phantom dissipates
	const float SpiritScale = FMath::Lerp(1.0f, 1.045f, NormalizedAge);
	SetActorScale3D(BaseRootScale * SpiritScale);

	if (NormalizedAge >= 1.0f)
	{
		Destroy();
	}
}
