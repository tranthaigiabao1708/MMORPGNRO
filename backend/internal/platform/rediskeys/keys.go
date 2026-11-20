package rediskeys

import (
	"strings"
	"time"
)

// Canonical Redis keyspace prefixes.
const (
	SessionTicketPrefix   = "session:ticket:"
	SessionAccountPrefix  = "session:account:"
	RefreshTokenPrefix    = "refresh_tok:"
	CharLeasePrefix       = "char:lease:"
	CharEpochPrefix       = "char:epoch:"
	ZoneRegistryPrefix    = "zone:registry:"
	ZoneInstancesPrefix   = "zone:instances:"
	ZoneChannelsPrefix    = "zone:channels:"
	SlotReservationPrefix = "slot:resv:"
	HandoffCharPrefix     = "handoff:char:"
	TradeSessionPrefix    = "trade:session:"
	TradeActiveCharPrefix = "trade:active_char:"
	PartyStatePrefix      = "party:state:"
	PartyCharPrefix       = "party:char:"
	GuildOnlinePrefix     = "guild:online:"
	PresenceCharPrefix    = "presence:char:"
	KillIdempotencyPrefix = "idem:kill:"
	KillRateLimitPrefix   = "ratelimit:kill:"
	GatewayRateLimitPrefix = "rl:gw:"
	EventSeenPrefix       = "evt:seen:"
)

// Authoritative TTL and heartbeat cadence constants.
const (
	SessionTicketTTL           = 30 * time.Second
	SessionAccountTTL          = 3600 * time.Second
	RefreshTokenTTL            = 7 * 24 * time.Hour
	CharLeaseTTL               = 15 * time.Second
	CharLeaseHeartbeatCadence  = 5 * time.Second
	ZoneRegistryTTL            = 10 * time.Second
	ZoneHeartbeatCadence       = 3 * time.Second
	SlotReservationTTL         = 15 * time.Second
	HandoffCharTTL             = 15 * time.Second
	TradeSessionTTL            = 180 * time.Second
	TradeActiveCharTTL         = 180 * time.Second
	PartyStateTTL              = 7200 * time.Second
	KillIdempotencyTTL         = 15 * time.Minute
	KillRateLimitWindow        = 60 * time.Second
	GatewayLoginRateLimitWindow = 60 * time.Second
	GatewayChatRateLimitWindow = 1 * time.Second
	EventSeenTTL               = 600 * time.Second
)

func sanitizeSegment(s string) string {
	return strings.TrimSpace(s)
}

// SessionTicketKey builds session:ticket:<ticketID>.
func SessionTicketKey(ticketID string) string {
	return SessionTicketPrefix + sanitizeSegment(ticketID)
}

// SessionAccountKey builds session:account:<accountID>.
func SessionAccountKey(accountID string) string {
	return SessionAccountPrefix + sanitizeSegment(accountID)
}

// RefreshTokenKey builds refresh_tok:<tokenHash>.
func RefreshTokenKey(tokenHash string) string {
	return RefreshTokenPrefix + sanitizeSegment(tokenHash)
}

// CharLeaseKey builds char:lease:<charID>.
func CharLeaseKey(charID string) string {
	return CharLeasePrefix + sanitizeSegment(charID)
}

// CharEpochKey builds char:epoch:<charID>.
func CharEpochKey(charID string) string {
	return CharEpochPrefix + sanitizeSegment(charID)
}

// ZoneRegistryKey builds zone:registry:<zoneID>:<instanceID>.
func ZoneRegistryKey(zoneID, instanceID string) string {
	return ZoneRegistryPrefix + sanitizeSegment(zoneID) + ":" + sanitizeSegment(instanceID)
}

// ZoneInstancesKey builds zone:instances:<zoneID>.
func ZoneInstancesKey(zoneID string) string {
	return ZoneInstancesPrefix + sanitizeSegment(zoneID)
}

// ZoneChannelsKey builds zone:channels:<zoneID>.
func ZoneChannelsKey(zoneID string) string {
	return ZoneChannelsPrefix + sanitizeSegment(zoneID)
}

// SlotReservationKey builds slot:resv:<serverID>.
func SlotReservationKey(serverID string) string {
	return SlotReservationPrefix + sanitizeSegment(serverID)
}

// HandoffCharKey builds handoff:char:<charID>.
func HandoffCharKey(charID string) string {
	return HandoffCharPrefix + sanitizeSegment(charID)
}

// TradeSessionKey builds trade:session:<tradeID>.
func TradeSessionKey(tradeID string) string {
	return TradeSessionPrefix + sanitizeSegment(tradeID)
}

// TradeActiveCharKey builds trade:active_char:<charID>.
func TradeActiveCharKey(charID string) string {
	return TradeActiveCharPrefix + sanitizeSegment(charID)
}

// PartyStateKey builds party:state:<partyID>.
func PartyStateKey(partyID string) string {
	return PartyStatePrefix + sanitizeSegment(partyID)
}

// PartyCharKey builds party:char:<charID>.
func PartyCharKey(charID string) string {
	return PartyCharPrefix + sanitizeSegment(charID)
}

// GuildOnlineKey builds guild:online:<guildID>.
func GuildOnlineKey(guildID string) string {
	return GuildOnlinePrefix + sanitizeSegment(guildID)
}

// PresenceCharKey builds presence:char:<charID>.
func PresenceCharKey(charID string) string {
	return PresenceCharPrefix + sanitizeSegment(charID)
}

// KillIdempotencyKey builds idem:kill:<eventID>.
func KillIdempotencyKey(eventID string) string {
	return KillIdempotencyPrefix + sanitizeSegment(eventID)
}

// KillRateLimitKey builds ratelimit:kill:<charID>.
func KillRateLimitKey(charID string) string {
	return KillRateLimitPrefix + sanitizeSegment(charID)
}

// GatewayRateLimitKey builds rl:gw:<clientKey>.
func GatewayRateLimitKey(clientKey string) string {
	return GatewayRateLimitPrefix + sanitizeSegment(clientKey)
}

// EventSeenKey builds evt:seen:<consumerGroup>:<eventID>.
func EventSeenKey(consumerGroup, eventID string) string {
	return EventSeenPrefix + sanitizeSegment(consumerGroup) + ":" + sanitizeSegment(eventID)
}
