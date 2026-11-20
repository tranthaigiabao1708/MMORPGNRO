package world

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/platform/rediskeys"
	goredis "github.com/redis/go-redis/v9"
)

var (
	ErrInvalidOrExpiredTicket = errors.New("invalid, expired, or already-consumed connect ticket")
	ErrWrongTargetZone        = errors.New("ticket target zone mismatch")
	ErrLeaseNotOwned          = errors.New("character lease not owned by requesting server or epoch mismatch")
	ErrChannelHardCapFull     = errors.New("target zone channel has reached hard_cap_ccu (25/25)")
	ErrTargetChannelNotFound  = errors.New("requested zone/channel server is offline")
)

const (
	DefaultLeaseTTL       = 45 * time.Second
	DefaultNodeStaleTTL   = 35 * time.Second
	DefaultSoftCapCCU     = 20
	DefaultHardCapCCU     = 25
)

// Atomic Redis Lua script matching 04_MULTIPLAYER_BACKEND/02_Gateway_Session_and_World_Coordinator.md
const luaClaimTicketAndLease = `
local ticket_raw = redis.call("GETDEL", KEYS[1])
if not ticket_raw then
    return { err = "ERR_INVALID_OR_EXPIRED_TICKET" }
end
local ticket = cjson.decode(ticket_raw)
if ticket.target_zone_id ~= ARGV[2] then
    return { err = "ERR_WRONG_TARGET_ZONE" }
end
redis.call("HSET", KEYS[2],
    "server_id", ARGV[1],
    "epoch", tostring(ARGV[4]),
    "zone_id", ARGV[2],
    "account_id", ticket.account_id
)
redis.call("EXPIRE", KEYS[2], tonumber(ARGV[3]))
return { ticket.character_id, ticket.account_id }
`

const luaRenewCharacterLease = `
local owner = redis.call("HGET", KEYS[1], "server_id")
local epoch = redis.call("HGET", KEYS[1], "epoch")
if owner ~= ARGV[1] or epoch ~= ARGV[2] then
    return 0
end
redis.call("EXPIRE", KEYS[1], tonumber(ARGV[3]))
return 1
`

// ConnectTicket represents a one-time world join or zone-transfer ticket issued to an authenticated client.
type ConnectTicket struct {
	TicketID        string `json:"ticket_id"`
	AccountID       string `json:"account_id"`
	CharacterID     string `json:"character_id"`
	TargetZoneID    string `json:"target_zone_id"`
	TargetChannelID int    `json:"target_channel_id"`
	ServerAddr      string `json:"server_addr"`
	IssuedAtUnix    int64  `json:"issued_at_unix"`
	ExpiresUnix     int64  `json:"expires_unix"`
}

// ServerNodeInfo tracks a live UE5 Dedicated Server Zone/Channel instance for NRO Channel Switching & Multi-Zone Travel.
type ServerNodeInfo struct {
	ServerID    string  `json:"server_id"`
	ZoneID      string  `json:"zone_id"`
	ZoneLabel   string  `json:"zone_label"`
	ChannelID   int     `json:"channel_id"`
	ServerAddr  string  `json:"server_addr"`
	OnlineCCU   int     `json:"online_ccu"`
	SoftCapCCU  int     `json:"soft_cap_ccu"`
	HardCapCCU  int     `json:"hard_cap_ccu"`
	StatusColor string  `json:"status_color"` // GREEN (<15) | YELLOW (15..19) | RED (>=20)
	BossAlive   bool    `json:"boss_alive"`
	BossName    string  `json:"boss_name,omitempty"`
	BossHPRatio float64 `json:"boss_hp_ratio"`
	UpdatedUnix int64   `json:"updated_unix"`
	expiresAt   time.Time
}

// LeaseRecord represents the authoritative single-owner lease held by one UE5 Dedicated Server.
type LeaseRecord struct {
	CharacterID string
	AccountID   string
	ServerID    string
	ZoneID      string
	ChannelID   int
	Epoch       int64
	ExpiresAt   time.Time
}

// LeaseRenewalItem is sent every ~12-15s by a UE5 Dedicated Server to renew its active character leases.
type LeaseRenewalItem struct {
	CharacterID string `json:"character_id"`
	Epoch       int64  `json:"fencing_epoch"`
}

// Coordinator manages one-time connect tickets, duplicate-login lease revocation, multi-zone server registry, and atomic zone transfers.
type Coordinator struct {
	rdb               *goredis.Client
	defaultZoneID     string
	defaultServerAddr string
	leaseTTL          time.Duration

	mu            sync.Mutex
	tickets       map[string]ConnectTicket
	leasesByChar  map[string]LeaseRecord
	charByAccount map[string]string
	nodesByServer map[string]ServerNodeInfo
}

func NewCoordinator(rdb *goredis.Client, defaultZoneID, defaultServerAddr string) *Coordinator {
	if strings.TrimSpace(defaultZoneID) == "" {
		defaultZoneID = "zone.p01_west_hub"
	}
	if strings.TrimSpace(defaultServerAddr) == "" {
		defaultServerAddr = "127.0.0.1:7777"
	}
	return &Coordinator{
		rdb:               rdb,
		defaultZoneID:     defaultZoneID,
		defaultServerAddr: defaultServerAddr,
		leaseTTL:          DefaultLeaseTTL,
		tickets:           make(map[string]ConnectTicket),
		leasesByChar:      make(map[string]LeaseRecord),
		charByAccount:     make(map[string]string),
		nodesByServer:     make(map[string]ServerNodeInfo),
	}
}

func computeChannelStatusColor(ccu, softCap int) string {
	if softCap <= 0 {
		softCap = DefaultSoftCapCCU
	}
	if ccu >= softCap {
		return "RED"
	}
	if ccu >= (softCap*3)/4 {
		return "YELLOW"
	}
	return "GREEN"
}

func formatZoneLabel(zoneID string) string {
	switch strings.TrimSpace(zoneID) {
	case "zone.p01_west_hub":
		return "Tram Trung Tam Tay Do"
	case "zone.p01_highland_01":
		return "Cao Nguyen Titan (Boss)"
	default:
		return zoneID
	}
}

func (c *Coordinator) pruneStaleNodesLocked(now time.Time) {
	for sid, n := range c.nodesByServer {
		if now.After(n.expiresAt) {
			delete(c.nodesByServer, sid)
		}
	}
}

// UpsertServerHeartbeat registers or refreshes a live UE5 Dedicated Server Zone/Channel node.
func (c *Coordinator) UpsertServerHeartbeat(in ServerNodeInfo) ServerNodeInfo {
	now := time.Now().UTC()
	in.ServerID = strings.TrimSpace(in.ServerID)
	if in.ServerID == "" {
		in.ServerID = "ue5-ded-7777"
	}
	in.ZoneID = strings.TrimSpace(in.ZoneID)
	if in.ZoneID == "" {
		in.ZoneID = c.defaultZoneID
	}
	in.ZoneLabel = formatZoneLabel(in.ZoneID)
	if in.ChannelID < 1 {
		in.ChannelID = 1
	}
	in.ServerAddr = strings.TrimSpace(in.ServerAddr)
	if in.ServerAddr == "" {
		in.ServerAddr = c.defaultServerAddr
	}
	if in.SoftCapCCU <= 0 {
		in.SoftCapCCU = DefaultSoftCapCCU
	}
	if in.HardCapCCU < in.SoftCapCCU {
		in.HardCapCCU = DefaultHardCapCCU
	}
	if in.OnlineCCU < 0 {
		in.OnlineCCU = 0
	}
	in.StatusColor = computeChannelStatusColor(in.OnlineCCU, in.SoftCapCCU)
	in.UpdatedUnix = now.Unix()
	in.expiresAt = now.Add(DefaultNodeStaleTTL)

	c.mu.Lock()
	c.pruneStaleNodesLocked(now)
	c.nodesByServer[in.ServerID] = in
	c.mu.Unlock()
	return in
}

// ListActiveChannels returns all live Dedicated Server channels sorted by ZoneID and ChannelID.
func (c *Coordinator) ListActiveChannels() []ServerNodeInfo {
	now := time.Now().UTC()
	c.mu.Lock()
	c.pruneStaleNodesLocked(now)
	out := make([]ServerNodeInfo, 0, max(1, len(c.nodesByServer)))
	for _, n := range c.nodesByServer {
		out = append(out, n)
	}
	if len(out) == 0 {
		// Provide default Channel 1 entry when running single-server without explicit heartbeat
		activeCount := 0
		for _, lr := range c.leasesByChar {
			if now.Before(lr.ExpiresAt) {
				activeCount++
			}
		}
		out = append(out, ServerNodeInfo{
			ServerID:    "ue5-ded-default",
			ZoneID:      c.defaultZoneID,
			ZoneLabel:   formatZoneLabel(c.defaultZoneID),
			ChannelID:   1,
			ServerAddr:  c.defaultServerAddr,
			OnlineCCU:   activeCount,
			SoftCapCCU:  DefaultSoftCapCCU,
			HardCapCCU:  DefaultHardCapCCU,
			StatusColor: computeChannelStatusColor(activeCount, DefaultSoftCapCCU),
			UpdatedUnix: now.Unix(),
		})
	}
	c.mu.Unlock()

	sort.Slice(out, func(i, j int) bool {
		if out[i].ChannelID != out[j].ChannelID {
			return out[i].ChannelID < out[j].ChannelID
		}
		return out[i].ZoneID < out[j].ZoneID
	})
	return out
}

func (c *Coordinator) resolveTargetNodeLocked(now time.Time, requestedZoneID string, requestedChannelID int) (string, int, string, error) {
	c.pruneStaleNodesLocked(now)
	zoneID := strings.TrimSpace(requestedZoneID)
	if zoneID == "" {
		zoneID = c.defaultZoneID
	}
	chanID := max(1, requestedChannelID)

	if len(c.nodesByServer) == 0 {
		return zoneID, chanID, c.defaultServerAddr, nil
	}

	// 1. Exact match on (ZoneID, ChannelID)
	for _, n := range c.nodesByServer {
		if n.ZoneID == zoneID && (requestedChannelID <= 0 || n.ChannelID == chanID) {
			if n.OnlineCCU >= n.HardCapCCU {
				return "", 0, "", ErrChannelHardCapFull
			}
			return n.ZoneID, n.ChannelID, n.ServerAddr, nil
		}
	}

	// 2. Match by ChannelID alone (NRO "Doi Khu 1..N" across cluster)
	if requestedChannelID > 0 {
		for _, n := range c.nodesByServer {
			if n.ChannelID == chanID {
				if n.OnlineCCU >= n.HardCapCCU {
					return "", 0, "", ErrChannelHardCapFull
				}
				return n.ZoneID, n.ChannelID, n.ServerAddr, nil
			}
		}
	}

	// 3. Any non-full node in requested ZoneID
	var foundInZone bool
	for _, n := range c.nodesByServer {
		if n.ZoneID == zoneID {
			foundInZone = true
			if n.OnlineCCU < n.HardCapCCU {
				return n.ZoneID, n.ChannelID, n.ServerAddr, nil
			}
		}
	}
	if foundInZone {
		return "", 0, "", ErrChannelHardCapFull
	}

	// Fallback to defaultServerAddr if zone isn't registered in nodesByServer yet (e.g. unit test)
	if requestedChannelID <= 1 && zoneID == c.defaultZoneID {
		return zoneID, 1, c.defaultServerAddr, nil
	}
	return "", 0, "", ErrTargetChannelNotFound
}

// IssueConnectTicket revokes any active duplicate character lease for the same account and issues a 30s one-time ticket.
func (c *Coordinator) IssueConnectTicket(ctx context.Context, accountID, characterID uuid.UUID, requestedZoneID string) (ConnectTicket, bool, error) {
	raw := make([]byte, 16)
	if _, err := rand.Read(raw); err != nil {
		return ConnectTicket{}, false, err
	}
	ticketID := "tkt_" + hex.EncodeToString(raw)
	now := time.Now().UTC()
	exp := now.Add(rediskeys.SessionTicketTTL)

	kickedPrevious := false

	c.mu.Lock()
	resolvedZone, resolvedChan, resolvedAddr, resolveErr := c.resolveTargetNodeLocked(now, requestedZoneID, 0)
	if resolveErr != nil {
		// If character was saved in a zone whose server is currently offline, fall back safely to default hub!
		resolvedZone, resolvedChan, resolvedAddr, resolveErr = c.resolveTargetNodeLocked(now, c.defaultZoneID, 1)
		if resolveErr != nil {
			c.mu.Unlock()
			return ConnectTicket{}, false, resolveErr
		}
	}

	ticket := ConnectTicket{
		TicketID:        ticketID,
		AccountID:       accountID.String(),
		CharacterID:     characterID.String(),
		TargetZoneID:    resolvedZone,
		TargetChannelID: resolvedChan,
		ServerAddr:      resolvedAddr,
		IssuedAtUnix:    now.Unix(),
		ExpiresUnix:     exp.Unix(),
	}

	if prevCharID, ok := c.charByAccount[accountID.String()]; ok {
		if existing, hasLease := c.leasesByChar[prevCharID]; hasLease && now.Before(existing.ExpiresAt) {
			delete(c.leasesByChar, prevCharID)
			kickedPrevious = true
			if c.rdb != nil {
				_ = c.rdb.Del(ctx, rediskeys.CharLeaseKey(prevCharID)).Err()
			}
		}
		delete(c.charByAccount, accountID.String())
	}
	c.tickets[ticketID] = ticket
	c.mu.Unlock()

	if c.rdb != nil {
		if payload, err := json.Marshal(ticket); err == nil {
			_ = c.rdb.Set(ctx, rediskeys.SessionTicketKey(ticketID), string(payload), rediskeys.SessionTicketTTL).Err()
		}
	}

	return ticket, kickedPrevious, nil
}

// IssueZoneTransferTicket atomically verifies & revokes the source server's lease (Epoch #N)
// and issues a one-time transfer ticket to the destination Dedicated Server (which will claim Epoch #(N+1)).
func (c *Coordinator) IssueZoneTransferTicket(
	ctx context.Context,
	accountID uuid.UUID,
	characterID uuid.UUID,
	fromServerID string,
	fromEpoch int64,
	targetZoneID string,
	targetChannelID int,
) (ConnectTicket, error) {
	raw := make([]byte, 16)
	if _, err := rand.Read(raw); err != nil {
		return ConnectTicket{}, err
	}
	ticketID := "tkt_xfer_" + hex.EncodeToString(raw)
	now := time.Now().UTC()
	exp := now.Add(rediskeys.SessionTicketTTL)
	charStr := characterID.String()

	c.mu.Lock()
	rec, ok := c.leasesByChar[charStr]
	if !ok || rec.ServerID != fromServerID || rec.Epoch != fromEpoch || now.After(rec.ExpiresAt) {
		c.mu.Unlock()
		return ConnectTicket{}, ErrLeaseNotOwned
	}

	resolvedZone, resolvedChan, resolvedAddr, err := c.resolveTargetNodeLocked(now, targetZoneID, targetChannelID)
	if err != nil {
		c.mu.Unlock()
		return ConnectTicket{}, err
	}

	// Revoke source lease immediately so no further Epoch #N mutations can be accepted!
	delete(c.leasesByChar, charStr)
	if c.charByAccount[accountID.String()] == charStr {
		delete(c.charByAccount, accountID.String())
	}

	ticket := ConnectTicket{
		TicketID:        ticketID,
		AccountID:       accountID.String(),
		CharacterID:     charStr,
		TargetZoneID:    resolvedZone,
		TargetChannelID: resolvedChan,
		ServerAddr:      resolvedAddr,
		IssuedAtUnix:    now.Unix(),
		ExpiresUnix:     exp.Unix(),
	}
	c.tickets[ticketID] = ticket
	c.mu.Unlock()

	if c.rdb != nil {
		_ = c.rdb.Del(ctx, rediskeys.CharLeaseKey(charStr)).Err()
		if payload, mErr := json.Marshal(ticket); mErr == nil {
			_ = c.rdb.Set(ctx, rediskeys.SessionTicketKey(ticketID), string(payload), rediskeys.SessionTicketTTL).Err()
		}
	}

	return ticket, nil
}

// ConsumeTicket atomically validates and deletes a one-time connect ticket (GETDEL semantics: 2nd use always fails!).
func (c *Coordinator) ConsumeTicket(ctx context.Context, ticketID, expectedZoneID string) (ConnectTicket, error) {
	ticketID = strings.TrimSpace(ticketID)
	if ticketID == "" {
		return ConnectTicket{}, ErrInvalidOrExpiredTicket
	}

	now := time.Now().UTC()

	if c.rdb != nil {
		raw, err := c.rdb.GetDel(ctx, rediskeys.SessionTicketKey(ticketID)).Result()
		if err != nil || raw == "" {
			return ConnectTicket{}, ErrInvalidOrExpiredTicket
		}
		var t ConnectTicket
		if err := json.Unmarshal([]byte(raw), &t); err != nil {
			return ConnectTicket{}, ErrInvalidOrExpiredTicket
		}
		c.mu.Lock()
		delete(c.tickets, ticketID)
		c.mu.Unlock()
		if now.Unix() > t.ExpiresUnix {
			return ConnectTicket{}, ErrInvalidOrExpiredTicket
		}
		if expectedZoneID != "" && t.TargetZoneID != expectedZoneID {
			return ConnectTicket{}, ErrWrongTargetZone
		}
		return t, nil
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	t, ok := c.tickets[ticketID]
	delete(c.tickets, ticketID)
	if !ok || now.Unix() > t.ExpiresUnix {
		return ConnectTicket{}, ErrInvalidOrExpiredTicket
	}
	if expectedZoneID != "" && t.TargetZoneID != expectedZoneID {
		return ConnectTicket{}, ErrWrongTargetZone
	}
	return t, nil
}

// ActivateLease binds (charID, newEpoch) exclusively to serverID after PostgreSQL increments fencing_epoch.
func (c *Coordinator) ActivateLease(ctx context.Context, accountID, charID, serverID, zoneID string, newEpoch int64) {
	now := time.Now().UTC()
	rec := LeaseRecord{
		CharacterID: charID,
		AccountID:   accountID,
		ServerID:    serverID,
		ZoneID:      zoneID,
		ChannelID:   1,
		Epoch:       newEpoch,
		ExpiresAt:   now.Add(c.leaseTTL),
	}

	c.mu.Lock()
	if node, ok := c.nodesByServer[serverID]; ok {
		rec.ChannelID = node.ChannelID
	}
	if prevChar, ok := c.charByAccount[accountID]; ok && prevChar != charID {
		delete(c.leasesByChar, prevChar)
	}
	c.leasesByChar[charID] = rec
	c.charByAccount[accountID] = charID
	c.mu.Unlock()

	if c.rdb != nil {
		leaseKey := rediskeys.CharLeaseKey(charID)
		_ = c.rdb.HSet(ctx, leaseKey,
			"server_id", serverID,
			"epoch", strconv.FormatInt(newEpoch, 10),
			"zone_id", zoneID,
			"account_id", accountID,
		).Err()
		_ = c.rdb.Expire(ctx, leaseKey, c.leaseTTL).Err()
		_ = luaClaimTicketAndLease
	}
}

// RenewLeases renews active character leases owned by serverID and reports any revoked/stolen character IDs!
func (c *Coordinator) RenewLeases(ctx context.Context, serverID string, items []LeaseRenewalItem) (renewed []string, revoked []string) {
	now := time.Now().UTC()
	ttlSec := int(c.leaseTTL.Seconds())

	for _, item := range items {
		charID := strings.TrimSpace(item.CharacterID)
		if charID == "" {
			continue
		}

		if c.rdb != nil {
			res, err := c.rdb.Eval(ctx, luaRenewCharacterLease,
				[]string{rediskeys.CharLeaseKey(charID)},
				serverID, strconv.FormatInt(item.Epoch, 10), ttlSec,
			).Int()
			if err != nil || res != 1 {
				revoked = append(revoked, charID)
				continue
			}
		}

		c.mu.Lock()
		rec, ok := c.leasesByChar[charID]
		if !ok || rec.ServerID != serverID || rec.Epoch != item.Epoch || now.After(rec.ExpiresAt) {
			c.mu.Unlock()
			revoked = append(revoked, charID)
			continue
		}
		rec.ExpiresAt = now.Add(c.leaseTTL)
		c.leasesByChar[charID] = rec
		c.mu.Unlock()
		renewed = append(renewed, charID)
	}
	return renewed, revoked
}

// ValidateActiveLease verifies that (serverID, epoch) still owns the active lease for charID.
func (c *Coordinator) ValidateActiveLease(ctx context.Context, charID, serverID string, epoch int64) error {
	now := time.Now().UTC()
	if c.rdb != nil {
		vals, err := c.rdb.HMGet(ctx, rediskeys.CharLeaseKey(charID), "server_id", "epoch").Result()
		if err != nil || len(vals) != 2 || vals[0] == nil || vals[1] == nil {
			return ErrLeaseNotOwned
		}
		if vals[0].(string) != serverID || vals[1].(string) != strconv.FormatInt(epoch, 10) {
			return ErrLeaseNotOwned
		}
		return nil
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	rec, ok := c.leasesByChar[charID]
	if !ok || rec.ServerID != serverID || rec.Epoch != epoch || now.After(rec.ExpiresAt) {
		return ErrLeaseNotOwned
	}
	return nil
}

// ReleaseLease removes the lease only if (serverID, epoch) currently owns it.
func (c *Coordinator) ReleaseLease(ctx context.Context, charID, serverID string, epoch int64) {
	c.mu.Lock()
	if rec, ok := c.leasesByChar[charID]; ok && rec.ServerID == serverID && rec.Epoch == epoch {
		delete(c.leasesByChar, charID)
		if c.charByAccount[rec.AccountID] == charID {
			delete(c.charByAccount, rec.AccountID)
		}
	}
	c.mu.Unlock()

	if c.rdb != nil {
		const luaRelease = `
if redis.call("HGET", KEYS[1], "server_id") == ARGV[1] and redis.call("HGET", KEYS[1], "epoch") == ARGV[2] then
    return redis.call("DEL", KEYS[1])
end
return 0`
		_ = c.rdb.Eval(ctx, luaRelease, []string{rediskeys.CharLeaseKey(charID)}, serverID, strconv.FormatInt(epoch, 10)).Err()
	}
}
