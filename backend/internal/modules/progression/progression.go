package progression

const (
	MaxCharacterLevel      = 200
	DefaultMobKillEXP      = int64(55)
	DefaultMobKillPotential = int64(350)

	// Base starting stats per NRO-style Step 3 design: 200 HP, 200 Ki, 100 Attack Power.
	// Level (LV EXP) determines Character Level ONLY and NEVER grants automatic stat increases!
	BaseCharacterHP  = 200.0
	BaseCharacterKi  = 200.0
	BaseAttackPower  = 100.0

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

// ExpRequiredForNextLevel returns the LV EXP needed to advance from `level` to `level + 1`.
// Level 1 -> 2 requires 100 LV EXP; Level 2 -> 3 requires 225 LV EXP; Level 3 -> 4 requires 400 LV EXP.
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

// ApplyExpGain adds LV expGain to (level, currentExp) and resolves single or multi-level ups.
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
// - HP  (+100 HP):  200, 300, 400, 500, ... (200 + 100 * hpUpgrades)
// - Ki  (+100 Ki):  200, 300, 400, 500, ... (200 + 100 * kiUpgrades)
// - Atk (+10 Atk):  250, 400, 550, 700, ... (250 + 150 * atkUpgrades)
func PotentialUpgradeCost(statType string, currentUpgrades int32) int64 {
	if currentUpgrades < 0 {
		currentUpgrades = 0
	}
	u := int64(currentUpgrades)
	switch statType {
	case StatTypeHP:
		return 200 + 100*u
	case StatTypeKi:
		return 200 + 100*u
	case StatTypeAtk:
		return 250 + 150*u
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
