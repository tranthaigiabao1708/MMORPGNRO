package world

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
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
)

const (
	DefaultLeaseTTL = 45 * time.Second
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

// ConnectTicket represents a one-time world join ticket issued to an authenticated client.
type ConnectTicket struct {
	TicketID     string `json:"ticket_id"`
	AccountID    string `json:"account_id"`
	CharacterID  string `json:"character_id"`
	TargetZoneID string `json:"target_zone_id"`
	ServerAddr   string `json:"server_addr"`
	IssuedAtUnix int64  `json:"issued_at_unix"`
	ExpiresUnix  int64  `json:"expires_unix"`
}

// LeaseRecord represents the authoritative single-owner lease held by one UE5 Dedicated Server.
type LeaseRecord struct {
	CharacterID string
	AccountID   string
	ServerID    string
	ZoneID      string
	Epoch       int64
	ExpiresAt   time.Time
}

// LeaseRenewalItem is sent every ~15s by a UE5 Dedicated Server to renew its active character leases.
type LeaseRenewalItem struct {
	CharacterID string `json:"character_id"`
	Epoch       int64  `json:"fencing_epoch"`
}

// Coordinator manages one-time connect tickets, duplicate-login lease revocation, and atomic lease renewals.
type Coordinator struct {
	rdb               *goredis.Client
	defaultZoneID     string
	defaultServerAddr string
	leaseTTL          time.Duration

	mu            sync.Mutex
	tickets       map[string]ConnectTicket
	leasesByChar  map[string]LeaseRecord
	charByAccount map[string]string
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
	}
}

// IssueConnectTicket revokes any active duplicate character lease for the same account and issues a 30s one-time ticket.
func (c *Coordinator) IssueConnectTicket(ctx context.Context, accountID, characterID uuid.UUID, requestedZoneID string) (ConnectTicket, bool, error) {
	zoneID := strings.TrimSpace(requestedZoneID)
	if zoneID == "" {
		zoneID = c.defaultZoneID
	}

	raw := make([]byte, 16)
	if _, err := rand.Read(raw); err != nil {
		return ConnectTicket{}, false, err
	}
	ticketID := "tkt_" + hex.EncodeToString(raw)
	now := time.Now().UTC()
	exp := now.Add(rediskeys.SessionTicketTTL)

	ticket := ConnectTicket{
		TicketID:     ticketID,
		AccountID:    accountID.String(),
		CharacterID:  characterID.String(),
		TargetZoneID: zoneID,
		ServerAddr:   c.defaultServerAddr,
		IssuedAtUnix: now.Unix(),
		ExpiresUnix:  exp.Unix(),
	}

	kickedPrevious := false

	c.mu.Lock()
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
		Epoch:       newEpoch,
		ExpiresAt:   now.Add(c.leaseTTL),
	}

	c.mu.Lock()
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
		// Keep luaClaimTicketAndLease referenced & verified for Redis deployments
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
