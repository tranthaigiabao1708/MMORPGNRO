# Coding Standards & Scope Guardrails (`Task P0-05`)

**Project:** `InfiniteUniverse` (Step 1: Core Gameplay Prototype)

---

## 1. Unreal Engine 5 C++ Naming Conventions

Every C++ file and symbol in `Source/InfiniteUniverse/` MUST follow Unreal Header Tool (UHT) strict prefixing:

| Symbol Category | File Name Pattern | C++ Type Prefix | Concrete Example |
| :--- | :--- | :---: | :--- |
| **`AActor` / `ACharacter` / `AController`** | `IU_<Name>.h / .cpp` | `AIU_` | `class INFINITEUNIVERSE_API AIU_PlayerCharacter : public ACharacter` |
| **`UObject` / `UActorComponent` / `UUserWidget`** | `IU_<Name>.h / .cpp` | `UIU_` | `class INFINITEUNIVERSE_API UIU_FlightComponent : public UActorComponent` |
| **`USTRUCT`** | Inside `IU_GameplayTypes.h` | `FIU_` | `struct FIU_DamageSpec` |
| **`UENUM`** | Inside `IU_GameplayTypes.h` | `EIU_` | `enum class EIU_PlayerMovementMode : uint8` |
| **`UINTERFACE` / Interface** | `IU_<Name>Interface.h` | `UIU_` / `IIU_` | `class IIU_DamageableInterface` |
| **Boolean Member Variables** | — | `b` | `bool bIsLockedOn = false;` |

---

## 2. Content Browser Asset Naming Conventions

| Asset Type | Mandatory Prefix | Example | Target Directory |
| :--- | :---: | :--- | :--- |
| Blueprint Class | `BP_` | `BP_PlayerCharacter` | `Content/Characters/Player/` |
| Static Mesh | `SM_` | `SM_GreyboxPillar_10m` | `Content/Developer/TestAssets/` |
| Skeletal Mesh | `SK_` | `SK_Player` | `Content/Characters/Player/Meshes/` |
| Material / Master Material | `M_` | `M_AnimeStylized_Master` | `Content/Characters/Player/Materials/` |
| Material Instance | `MI_` | `MI_PlayerBody` | `Content/Characters/Player/Materials/` |
| Texture | `T_` | `T_Player_BaseColor` | `Content/Characters/Player/Textures/` |
| Animation Sequence / Montage | `A_` / `AM_` | `A_Player_FlyForward`, `AM_MeleeCombo` | `Content/Characters/Player/Animations/` |
| Animation Blueprint | `ABP_` | `ABP_Player` | `Content/Characters/Player/Animations/` |
| UMG Widget Blueprint | `WBP_` | `WBP_PlayerHUD` | `Content/UI/` |
| Input Action | `IA_` | `IA_FlightVertical` | `Content/Input/Actions/` |
| Input Mapping Context | `IMC_` | `IMC_Player` | `Content/Input/` |
| Niagara System | `NS_` | `NS_KiBlast` | `Content/Combat/VFX/` |

---

## 3. Core Engineering Rules

1. **Single Responsibility per Component:** `AIU_PlayerCharacter` is an orchestrator only (< 250 lines). It wires input commands from `AIU_PlayerController` to `UIU_MovementComponent`, `UIU_FlightComponent`, `UIU_CombatComponent`, `UIU_TargetingComponent`, and `UIU_AttributeComponent`.
2. **Zero-Tick Default:** Set `PrimaryComponentTick.bCanEverTick = false` by default on all components (`UIU_AttributeComponent`, `UIU_DamageComponent`, `UIU_CombatComponent`, `UIU_HUDManager`). Enable tick ONLY where continuous spatial interpolation is mathematically required (`UIU_MovementComponent`, camera lock-on assist while active).
3. **UI Never Mutates Gameplay State:** `WBP_PlayerHUD` subscribes to `OnHealthChanged` and `OnKiChanged` multicast delegates on `UIU_AttributeComponent`. UI widgets must never modify HP or Ki directly.
4. **Data-Driven `UPROPERTY` Configuration:** Zero magic numbers in `.cpp` files. Speeds, accelerations, cooldowns, Ki costs, detection radii, and damage values must be exposed via `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IU|...")`.
5. **Lightweight Headers:** Prefer forward declarations (`class UIU_CombatComponent;`) in `.h` files; include concrete headers only in `.cpp` files and `IU_GameplayTypes.h`.

---

## 4. Strict Step 1 Forbidden Scope List

AI Coding Agents and contributors MUST NOT create files, stubs, or plugins for:
- Multiplayer, Replication, or Dedicated Server
- Gameplay Ability System (`GameplayAbilities` plugin)
- Backend APIs, Databases, or Account Login
- Inventory, Equipment, Loot Drops, Economy, or Trading
- Quests, Party, Guild, Dungeons, World Boss, or Transformation Trees
