package party

import (
	"errors"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
)

const (
	MaxPartyMembers  = 5
	DefaultInviteTTL = 60 * time.Second
)

var (
	ErrCannotInviteSelf     = errors.New("cannot invite yourself to a party")
	ErrAlreadyInParty       = errors.New("character is already in a party")
	ErrTargetAlreadyInParty = errors.New("target character is already in a party")
	ErrNotInParty           = errors.New("character is not currently in a party")
	ErrNotPartyLeader       = errors.New("only the party leader can perform this action")
	ErrPartyFull            = errors.New("party has reached maximum capacity (5/5 members)")
	ErrInviteNotFound       = errors.New("party invite not found or has expired")
	ErrMemberNotFound       = errors.New("target character is not a member of this party")
)

type MemberInfo struct {
	CharacterID   string  `json:"character_id"`
	CharacterName string  `json:"character_name"`
	Level         int     `json:"level"`
	HP            float64 `json:"hp"`
	MaxHP         float64 `json:"max_hp"`
	Energy        float64 `json:"energy"`
	MaxEnergy     float64 `json:"max_energy"`
	IsLeader      bool    `json:"is_leader"`
}

type InviteView struct {
	InviteID      string `json:"invite_id"`
	PartyID       string `json:"party_id,omitempty"`
	InviterCharID string `json:"inviter_char_id"`
	InviterName   string `json:"inviter_name"`
	TargetCharID  string `json:"target_char_id"`
	TargetName    string `json:"target_name"`
	ExpiresUnix   int64  `json:"expires_unix"`
}

type PartySnapshot struct {
	InParty        bool         `json:"in_party"`
	PartyID        string       `json:"party_id,omitempty"`
	LeaderCharID   string       `json:"leader_char_id,omitempty"`
	Members        []MemberInfo `json:"members"`
	PendingInvites []InviteView `json:"pending_invites"`
	ActionMessage  string       `json:"action_message,omitempty"`
}

type partyState struct {
	id           string
	leaderCharID string
	members      []MemberInfo
}

type inviteRecord struct {
	InviteView
	inviterLv int
	expiresAt time.Time
}

// ComputeSynergyMultiplier returns the NRO per-hit SM/TN multiplier when nearbyPartyMembers
// (including the attacker, within 120m = 12,000 cm) are present:
//   - <=1: 1.00 (base 1.5x Dame)
//   - 2:   1.15 (+15%)
//   - 3:   1.25 (+25%)
//   - 4:   1.35 (+35%)
//   - >=5: 1.45 (+45%)
func ComputeSynergyMultiplier(nearbyPartyMembers int) float64 {
	switch {
	case nearbyPartyMembers <= 1:
		return 1.0
	case nearbyPartyMembers == 2:
		return 1.15
	case nearbyPartyMembers == 3:
		return 1.25
	case nearbyPartyMembers == 4:
		return 1.35
	default:
		return 1.45
	}
}

type Coordinator struct {
	mu          sync.Mutex
	inviteTTL   time.Duration
	parties     map[string]*partyState
	charToParty map[string]string
	vitalsCache map[string]MemberInfo
	invites     map[string]inviteRecord // inviteID -> record
}

func NewCoordinator() *Coordinator {
	return &Coordinator{
		inviteTTL:   DefaultInviteTTL,
		parties:     make(map[string]*partyState),
		charToParty: make(map[string]string),
		vitalsCache: make(map[string]MemberInfo),
		invites:     make(map[string]inviteRecord),
	}
}

func (c *Coordinator) purgeExpiredLocked(now time.Time) {
	for id, inv := range c.invites {
		if !now.Before(inv.expiresAt) {
			delete(c.invites, id)
		}
	}
}

func (c *Coordinator) SyncMemberVitals(charID, charName string, level int, hp, maxHP, energy, maxEnergy float64) {
	charID = strings.TrimSpace(charID)
	if charID == "" {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()

	info := c.vitalsCache[charID]
	info.CharacterID = charID
	if strings.TrimSpace(charName) != "" {
		info.CharacterName = strings.TrimSpace(charName)
	}
	if level > 0 {
		info.Level = level
	}
	if maxHP > 0 {
		info.HP = hp
		info.MaxHP = maxHP
	}
	if maxEnergy > 0 {
		info.Energy = energy
		info.MaxEnergy = maxEnergy
	}
	c.vitalsCache[charID] = info

	if pid, ok := c.charToParty[charID]; ok {
		if p, exists := c.parties[pid]; exists {
			for i := range p.members {
				if p.members[i].CharacterID == charID {
					if info.CharacterName != "" {
						p.members[i].CharacterName = info.CharacterName
					}
					if info.Level > 0 {
						p.members[i].Level = info.Level
					}
					if info.MaxHP > 0 {
						p.members[i].HP = info.HP
						p.members[i].MaxHP = info.MaxHP
					}
					if info.MaxEnergy > 0 {
						p.members[i].Energy = info.Energy
						p.members[i].MaxEnergy = info.MaxEnergy
					}
					break
				}
			}
		}
	}
}

func (c *Coordinator) snapshotLocked(charID string, msg string) PartySnapshot {
	now := time.Now()
	c.purgeExpiredLocked(now)

	snap := PartySnapshot{
		Members:        []MemberInfo{},
		PendingInvites: []InviteView{},
		ActionMessage:  msg,
	}

	if pid, ok := c.charToParty[charID]; ok {
		if p, exists := c.parties[pid]; exists {
			snap.InParty = true
			snap.PartyID = p.id
			snap.LeaderCharID = p.leaderCharID
			snap.Members = make([]MemberInfo, len(p.members))
			for i, m := range p.members {
				m.IsLeader = (m.CharacterID == p.leaderCharID)
				snap.Members[i] = m
			}
		}
	}

	for _, inv := range c.invites {
		if inv.TargetCharID == charID {
			snap.PendingInvites = append(snap.PendingInvites, inv.InviteView)
		}
	}
	return snap
}

func (c *Coordinator) GetSnapshot(charID string) PartySnapshot {
	charID = strings.TrimSpace(charID)
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.snapshotLocked(charID, "")
}

func (c *Coordinator) GetPartyID(charID string) string {
	charID = strings.TrimSpace(charID)
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.charToParty[charID]
}

func (c *Coordinator) Invite(inviterID, inviterName string, inviterLv int, targetID, targetName string) (PartySnapshot, error) {
	inviterID = strings.TrimSpace(inviterID)
	targetID = strings.TrimSpace(targetID)
	inviterName = strings.TrimSpace(inviterName)
	targetName = strings.TrimSpace(targetName)

	if inviterID == "" || targetID == "" || inviterID == targetID {
		return PartySnapshot{}, ErrCannotInviteSelf
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	now := time.Now()
	c.purgeExpiredLocked(now)

	if _, inParty := c.charToParty[targetID]; inParty {
		return PartySnapshot{}, ErrTargetAlreadyInParty
	}

	var existingPartyID string
	if pid, inParty := c.charToParty[inviterID]; inParty {
		p := c.parties[pid]
		if p.leaderCharID != inviterID {
			return PartySnapshot{}, ErrNotPartyLeader
		}
		if len(p.members) >= MaxPartyMembers {
			return PartySnapshot{}, ErrPartyFull
		}
		existingPartyID = pid
	}

	if inviterName == "" {
		if cached, ok := c.vitalsCache[inviterID]; ok && cached.CharacterName != "" {
			inviterName = cached.CharacterName
		} else {
			inviterName = "Warrior"
		}
	}
	if inviterLv <= 0 {
		if cached, ok := c.vitalsCache[inviterID]; ok && cached.Level > 0 {
			inviterLv = cached.Level
		} else {
			inviterLv = 1
		}
	}

	// Replace any older invite from the same inviter to the same target
	for id, inv := range c.invites {
		if inv.InviterCharID == inviterID && inv.TargetCharID == targetID {
			delete(c.invites, id)
		}
	}

	invID := uuid.NewString()
	exp := now.Add(c.inviteTTL)
	c.invites[invID] = inviteRecord{
		InviteView: InviteView{
			InviteID:      invID,
			PartyID:       existingPartyID,
			InviterCharID: inviterID,
			InviterName:   inviterName,
			TargetCharID:  targetID,
			TargetName:    targetName,
			ExpiresUnix:   exp.Unix(),
		},
		inviterLv: inviterLv,
		expiresAt: exp,
	}

	return c.snapshotLocked(inviterID, "Da gui loi moi to doi!"), nil
}

func (c *Coordinator) RespondInvite(targetID, targetName string, targetLv int, inviteID string, accept bool) (PartySnapshot, error) {
	targetID = strings.TrimSpace(targetID)
	inviteID = strings.TrimSpace(inviteID)
	targetName = strings.TrimSpace(targetName)

	c.mu.Lock()
	defer c.mu.Unlock()

	now := time.Now()
	c.purgeExpiredLocked(now)

	var chosen inviteRecord
	var found bool
	if inviteID != "" {
		rec, ok := c.invites[inviteID]
		if ok && rec.TargetCharID == targetID {
			chosen = rec
			found = true
		}
	} else {
		for _, rec := range c.invites {
			if rec.TargetCharID == targetID {
				if !found || rec.expiresAt.After(chosen.expiresAt) {
					chosen = rec
					found = true
				}
			}
		}
	}
	if !found {
		return PartySnapshot{}, ErrInviteNotFound
	}

	delete(c.invites, chosen.InviteID)
	if !accept {
		return c.snapshotLocked(targetID, "Da tu choi loi moi to doi."), nil
	}

	if _, already := c.charToParty[targetID]; already {
		return PartySnapshot{}, ErrAlreadyInParty
	}

	var p *partyState
	if pid, ok := c.charToParty[chosen.InviterCharID]; ok {
		p = c.parties[pid]
		if p.leaderCharID != chosen.InviterCharID {
			return PartySnapshot{}, ErrNotPartyLeader
		}
		if len(p.members) >= MaxPartyMembers {
			return PartySnapshot{}, ErrPartyFull
		}
	} else {
		// Create new party with inviter as leader
		invInfo := c.vitalsCache[chosen.InviterCharID]
		if invInfo.CharacterID == "" {
			invInfo = MemberInfo{
				CharacterID:   chosen.InviterCharID,
				CharacterName: chosen.InviterName,
				Level:         max(1, chosen.inviterLv),
				HP:            200,
				MaxHP:         200,
				Energy:        200,
				MaxEnergy:     200,
			}
		}
		invInfo.IsLeader = true
		pid := uuid.NewString()
		p = &partyState{
			id:           pid,
			leaderCharID: chosen.InviterCharID,
			members:      []MemberInfo{invInfo},
		}
		c.parties[pid] = p
		c.charToParty[chosen.InviterCharID] = pid
	}

	tgtInfo := c.vitalsCache[targetID]
	if tgtInfo.CharacterID == "" {
		if targetName == "" {
			targetName = "Member"
		}
		tgtInfo = MemberInfo{
			CharacterID:   targetID,
			CharacterName: targetName,
			Level:         max(1, targetLv),
			HP:            200,
			MaxHP:         200,
			Energy:        200,
			MaxEnergy:     200,
		}
	} else {
		if targetName != "" {
			tgtInfo.CharacterName = targetName
		}
		if targetLv > 0 {
			tgtInfo.Level = targetLv
		}
	}
	tgtInfo.IsLeader = false

	p.members = append(p.members, tgtInfo)
	c.charToParty[targetID] = p.id

	// Clear any remaining pending invites targeting targetID
	for id, inv := range c.invites {
		if inv.TargetCharID == targetID {
			delete(c.invites, id)
		}
	}

	return c.snapshotLocked(targetID, "Da gia nhap to doi!"), nil
}

func (c *Coordinator) removeMemberLocked(p *partyState, charID string) {
	delete(c.charToParty, charID)
	next := p.members[:0]
	for _, m := range p.members {
		if m.CharacterID != charID {
			next = append(next, m)
		}
	}
	p.members = next

	if len(p.members) <= 1 {
		for _, rem := range p.members {
			delete(c.charToParty, rem.CharacterID)
		}
		delete(c.parties, p.id)
		return
	}

	if p.leaderCharID == charID {
		p.leaderCharID = p.members[0].CharacterID
		p.members[0].IsLeader = true
	}
}

func (c *Coordinator) Leave(charID string) (PartySnapshot, error) {
	charID = strings.TrimSpace(charID)
	c.mu.Lock()
	defer c.mu.Unlock()

	pid, ok := c.charToParty[charID]
	if !ok {
		return PartySnapshot{}, ErrNotInParty
	}
	p := c.parties[pid]
	c.removeMemberLocked(p, charID)
	return c.snapshotLocked(charID, "Da roi to doi."), nil
}

func (c *Coordinator) Kick(leaderID, targetID string) (PartySnapshot, error) {
	leaderID = strings.TrimSpace(leaderID)
	targetID = strings.TrimSpace(targetID)
	c.mu.Lock()
	defer c.mu.Unlock()

	pid, ok := c.charToParty[leaderID]
	if !ok {
		return PartySnapshot{}, ErrNotInParty
	}
	p := c.parties[pid]
	if p.leaderCharID != leaderID {
		return PartySnapshot{}, ErrNotPartyLeader
	}
	if targetID == leaderID || c.charToParty[targetID] != pid {
		return PartySnapshot{}, ErrMemberNotFound
	}
	c.removeMemberLocked(p, targetID)
	return c.snapshotLocked(leaderID, "Da truc xuat thanh vien khoi to doi."), nil
}

func (c *Coordinator) PromoteLeader(leaderID, targetID string) (PartySnapshot, error) {
	leaderID = strings.TrimSpace(leaderID)
	targetID = strings.TrimSpace(targetID)
	c.mu.Lock()
	defer c.mu.Unlock()

	pid, ok := c.charToParty[leaderID]
	if !ok {
		return PartySnapshot{}, ErrNotInParty
	}
	p := c.parties[pid]
	if p.leaderCharID != leaderID {
		return PartySnapshot{}, ErrNotPartyLeader
	}
	if c.charToParty[targetID] != pid {
		return PartySnapshot{}, ErrMemberNotFound
	}
	p.leaderCharID = targetID
	for i := range p.members {
		p.members[i].IsLeader = (p.members[i].CharacterID == targetID)
	}
	return c.snapshotLocked(leaderID, "Da nhuong quyen doi truong!"), nil
}

func (c *Coordinator) CleanupOnDisconnect(charID string) {
	charID = strings.TrimSpace(charID)
	if charID == "" {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()

	if pid, ok := c.charToParty[charID]; ok {
		if p, exists := c.parties[pid]; exists {
			c.removeMemberLocked(p, charID)
		}
	}
	delete(c.vitalsCache, charID)
	for id, inv := range c.invites {
		if inv.InviterCharID == charID || inv.TargetCharID == charID {
			delete(c.invites, id)
		}
	}
}
