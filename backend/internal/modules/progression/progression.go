package progression

const (
	MaxCharacterLevel = 200
	DefaultMobKillEXP = int64(55)
)

// ScaledStats represents the authoritative combat stats compiled for a character's Race + Level.
type ScaledStats struct {
	MaxHP                 float64 `json:"max_hp"`
	MaxEnergy             float64 `json:"max_energy"`
	PhysAtk               float64 `json:"phys_atk"`
	KiAtk                 float64 `json:"ki_atk"`
	Defense               float64 `json:"defense"`
	AttackPowerMultiplier float64 `json:"attack_power_multiplier"`
}

// ExpRequiredForNextLevel returns the EXP needed to advance from `level` to `level + 1`.
// Level 1 -> 2 requires 100 EXP; Level 2 -> 3 requires 225 EXP; Level 3 -> 4 requires 400 EXP.
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

// ApplyExpGain adds expGain to (level, currentExp) and resolves single or multi-level ups.
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

// CompileCharacterStats computes authoritative Step 3 combat stats aligned with UE5 UIU_AttributeComponent.
// At Level 1, baseline matches Step 1/Step 2 (100 MaxHP, 100 MaxKi, 1.0x AttackPowerMultiplier) for zero regression!
func CompileCharacterStats(raceID string, level int) ScaledStats {
	if level < 1 {
		level = 1
	}
	if level > MaxCharacterLevel {
		level = MaxCharacterLevel
	}

	lvBonus := float64(level - 1)
	hpPerLv := 25.0
	kiPerLv := 15.0
	atkMulPerLv := 0.14

	switch raceID {
	case "race.zenith_warrior":
		atkMulPerLv = 0.16
	case "race.mystic_elder":
		kiPerLv = 20.0
	case "race.frost_archon":
		hpPerLv = 28.0
	}

	maxHP := 100.0 + lvBonus*hpPerLv
	maxKi := 100.0 + lvBonus*kiPerLv
	atkMul := 1.0 + lvBonus*atkMulPerLv

	return ScaledStats{
		MaxHP:                 maxHP,
		MaxEnergy:             maxKi,
		PhysAtk:               45.0 * atkMul,
		KiAtk:                 45.0 * atkMul,
		Defense:               20.0 + lvBonus*4.0,
		AttackPowerMultiplier: atkMul,
	}
}
