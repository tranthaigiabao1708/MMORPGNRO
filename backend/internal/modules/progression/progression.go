package progression

import "math"

const (
	MaxCharacterLevel = 200

	// NRO Per-Hit Progression Multiplier:
	// Both Điểm Sức mạnh (LV EXP) and Điểm Tiềm năng (Potential Points) are awarded on EVERY valid hit
	// (not only on kill!), equal to 1.5x the outgoing damage dealt!
	HitRewardDamageMultiplier = 1.5

	DefaultHitEXP       = int64(30) // Fallback for 20.0 baseline damage * 1.5
	DefaultHitPotential = int64(30)

	// Base starting stats per NRO-style Step 3 design: 200 HP, 200 Ki, 100 Attack Power.
	// Điểm Sức mạnh (LV EXP) determines Character Level ONLY and NEVER grants automatic stat increases!
	BaseCharacterHP = 200.0
	BaseCharacterKi = 200.0
	BaseAttackPower = 100.0

	// Per-allocation increments when spending Potential Points (Điểm Tiềm năng)
	StepUpgradeHP  = 100.0
	StepUpgradeKi  = 100.0
	StepUpgradeAtk = 10.0

	StatTypeHP  = "hp"
	StatTypeKi  = "ki"
	StatTypeAtk = "atk"
)

// ScaledStats represents the authoritative combat stats compiled strictly from allocated Potential upgrades.
type ScaledStats struct {
	MaxHP                 float64 `json:"max_hp"`
	MaxEnergy             float64 `json:"max_energy"`
	AttackPower           float64 `json:"attack_power"`
	PhysAtk               float64 `json:"phys_atk"`
	KiAtk                 float64 `json:"ki_atk"`
	Defense               float64 `json:"defense"`
	AttackPowerMultiplier float64 `json:"attack_power_multiplier"`
	NextHPCost            int64   `json:"next_hp_cost"`
	NextKiCost            int64   `json:"next_ki_cost"`
	NextAtkCost           int64   `json:"next_atk_cost"`
}

// ComputeHitProgressionReward computes Điểm Sức mạnh (powerExpGain) and Điểm Tiềm năng (potentialGain)
// earned immediately after each valid combat hit: Reward = Round(1.5 * outgoingDamage).
func ComputeHitProgressionReward(outgoingDamage float64) (powerExpGain int64, potentialGain int64) {
	if outgoingDamage <= 0 || math.IsNaN(outgoingDamage) || math.IsInf(outgoingDamage, 0) {
		return 0, 0
	}
	reward := int64(math.Round(outgoingDamage * HitRewardDamageMultiplier))
	if reward < 1 {
		reward = 1
	}
	return reward, reward
}

// ExpRequiredForNextLevel returns the Điểm Sức mạnh (LV EXP) needed to advance from `level` to `level + 1`.
// Level 1 -> 2 requires 100; Level 2 -> 3 requires 225; Level 3 -> 4 requires 400.
func ExpRequiredForNextLevel(level int) int64 {
	if level < 1 {
		level = 1
	}
	if level >= MaxCharacterLevel {
		return 0
	}
	l := int64(level)
	return 100*l + 25*(l-1)*(l-1)
}

// ApplyExpGain adds Điểm Sức mạnh (expGain) to (level, currentExp) and resolves single or multi-level ups.
// Level ups do NOT modify HP, Ki, or Attack Power.
func ApplyExpGain(level int, currentExp int64, expGain int64) (newLevel int, newExp int64, leveledUp bool) {
	if level < 1 {
		level = 1
	}
	if currentExp < 0 {
		currentExp = 0
	}
	if expGain <= 0 || level >= MaxCharacterLevel {
		return level, currentExp, false
	}

	newLevel = level
	newExp = currentExp + expGain
	for newLevel < MaxCharacterLevel {
		req := ExpRequiredForNextLevel(newLevel)
		if req <= 0 || newExp < req {
			break
		}
		newExp -= req
		newLevel++
		leveledUp = true
	}
	if newLevel >= MaxCharacterLevel {
		newExp = 0
	}
	return newLevel, newExp, leveledUp
}

// PotentialUpgradeCost returns the Potential Points required for the next upgrade of a specific stat track.
// Each stat track escalates independently based on how many upgrades have been invested into THAT stat:
// - HP  (+100 HP):  100, 200, 300, 400, ... (100 * (hpUpgrades + 1))
// - Ki  (+100 Ki):  100, 200, 300, 400, ... (100 * (kiUpgrades + 1))
// - Atk (+10 Atk):  150, 300, 450, 600, ... (150 * (atkUpgrades + 1))
func PotentialUpgradeCost(statType string, currentUpgrades int32) int64 {
	if currentUpgrades < 0 {
		currentUpgrades = 0
	}
	u := int64(currentUpgrades)
	switch statType {
	case StatTypeHP:
		return 100 * (u + 1)
	case StatTypeKi:
		return 100 * (u + 1)
	case StatTypeAtk:
		return 150 * (u + 1)
	default:
		return 0
	}
}

// CompileCharacterStats computes authoritative combat stats strictly from allocated Potential upgrades.
// Starting baseline (0 upgrades): 200 HP, 200 Ki, 100 AttackPower (1.00x AttackPowerMultiplier).
func CompileCharacterStats(hpUpgrades, kiUpgrades, atkUpgrades int32) ScaledStats {
	if hpUpgrades < 0 {
		hpUpgrades = 0
	}
	if kiUpgrades < 0 {
		kiUpgrades = 0
	}
	if atkUpgrades < 0 {
		atkUpgrades = 0
	}

	maxHP := BaseCharacterHP + float64(hpUpgrades)*StepUpgradeHP
	maxKi := BaseCharacterKi + float64(kiUpgrades)*StepUpgradeKi
	atkPower := BaseAttackPower + float64(atkUpgrades)*StepUpgradeAtk
	atkMul := atkPower / BaseAttackPower

	return ScaledStats{
		MaxHP:                 maxHP,
		MaxEnergy:             maxKi,
		AttackPower:           atkPower,
		PhysAtk:               atkPower,
		KiAtk:                 atkPower,
		Defense:               20.0,
		AttackPowerMultiplier: atkMul,
		NextHPCost:            PotentialUpgradeCost(StatTypeHP, hpUpgrades),
		NextKiCost:            PotentialUpgradeCost(StatTypeKi, kiUpgrades),
		NextAtkCost:           PotentialUpgradeCost(StatTypeAtk, atkUpgrades),
	}
}
