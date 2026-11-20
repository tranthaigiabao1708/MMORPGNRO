package rediskeys_test

import (
	"strings"
	"testing"
	"time"

	"github.com/infinite-universe/backend/internal/platform/rediskeys"
)

func TestCanonicalPrimaryPrefixes(t *testing.T) {
	if rediskeys.SessionTicketPrefix != "session:ticket:" {
		t.Fatalf("unexpected SessionTicketPrefix: %q", rediskeys.SessionTicketPrefix)
	}
	if rediskeys.CharLeasePrefix != "char:lease:" {
		t.Fatalf("unexpected CharLeasePrefix: %q", rediskeys.CharLeasePrefix)
	}
	if rediskeys.ZoneRegistryPrefix != "zone:registry:" {
		t.Fatalf("unexpected ZoneRegistryPrefix: %q", rediskeys.ZoneRegistryPrefix)
	}

	if got := rediskeys.SessionTicketKey("tkt_123"); got != "session:ticket:tkt_123" {
		t.Fatalf("SessionTicketKey mismatch: got %q", got)
	}
	if got := rediskeys.CharLeaseKey("char_456"); got != "char:lease:char_456" {
		t.Fatalf("CharLeaseKey mismatch: got %q", got)
	}
	if got := rediskeys.ZoneRegistryKey("zone.p01_highland_01", "srv-1"); got != "zone:registry:zone.p01_highland_01:srv-1" {
		t.Fatalf("ZoneRegistryKey mismatch: got %q", got)
	}
}

func TestAllKeyBuilders(t *testing.T) {
	tests := []struct {
		name string
		got  string
		want string
	}{
		{name: "SessionTicketKey", got: rediskeys.SessionTicketKey(" abc "), want: "session:ticket:abc"},
		{name: "SessionAccountKey", got: rediskeys.SessionAccountKey("acc-1"), want: "session:account:acc-1"},
		{name: "RefreshTokenKey", got: rediskeys.RefreshTokenKey("sha256hex"), want: "refresh_tok:sha256hex"},
		{name: "CharLeaseKey", got: rediskeys.CharLeaseKey("c-1"), want: "char:lease:c-1"},
		{name: "CharEpochKey", got: rediskeys.CharEpochKey("c-1"), want: "char:epoch:c-1"},
		{name: "ZoneRegistryKey", got: rediskeys.ZoneRegistryKey("zone.p01_highland_01", "inst-1"), want: "zone:registry:zone.p01_highland_01:inst-1"},
		{name: "ZoneInstancesKey", got: rediskeys.ZoneInstancesKey("zone.p01_highland_01"), want: "zone:instances:zone.p01_highland_01"},
		{name: "ZoneChannelsKey", got: rediskeys.ZoneChannelsKey("zone.p01_highland_01"), want: "zone:channels:zone.p01_highland_01"},
		{name: "SlotReservationKey", got: rediskeys.SlotReservationKey("srv-1"), want: "slot:resv:srv-1"},
		{name: "HandoffCharKey", got: rediskeys.HandoffCharKey("c-1"), want: "handoff:char:c-1"},
		{name: "TradeSessionKey", got: rediskeys.TradeSessionKey("tr-1"), want: "trade:session:tr-1"},
		{name: "TradeActiveCharKey", got: rediskeys.TradeActiveCharKey("c-1"), want: "trade:active_char:c-1"},
		{name: "PartyStateKey", got: rediskeys.PartyStateKey("p-1"), want: "party:state:p-1"},
		{name: "PartyCharKey", got: rediskeys.PartyCharKey("c-1"), want: "party:char:c-1"},
		{name: "GuildOnlineKey", got: rediskeys.GuildOnlineKey("g-1"), want: "guild:online:g-1"},
		{name: "PresenceCharKey", got: rediskeys.PresenceCharKey("c-1"), want: "presence:char:c-1"},
		{name: "KillIdempotencyKey", got: rediskeys.KillIdempotencyKey("evt-1"), want: "idem:kill:evt-1"},
		{name: "KillRateLimitKey", got: rediskeys.KillRateLimitKey("c-1"), want: "ratelimit:kill:c-1"},
		{name: "GatewayRateLimitKey", got: rediskeys.GatewayRateLimitKey("ip:127.0.0.1"), want: "rl:gw:ip:127.0.0.1"},
		{name: "EventSeenKey", got: rediskeys.EventSeenKey("gateway", "evt-9"), want: "evt:seen:gateway:evt-9"},
	}

	seen := make(map[string]struct{}, len(tests))
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			if tc.got != tc.want {
				t.Fatalf("%s: got %q, want %q", tc.name, tc.got, tc.want)
			}
			if strings.ContainsAny(tc.got, " \t\n\r") {
				t.Fatalf("%s contains whitespace: %q", tc.name, tc.got)
			}
			if _, dup := seen[tc.got]; dup {
				t.Fatalf("duplicate key produced: %q", tc.got)
			}
			seen[tc.got] = struct{}{}
		})
	}
}

func TestTTLConstants(t *testing.T) {
	ttls := map[string]time.Duration{
		"SessionTicketTTL":            rediskeys.SessionTicketTTL,
		"SessionAccountTTL":           rediskeys.SessionAccountTTL,
		"RefreshTokenTTL":             rediskeys.RefreshTokenTTL,
		"CharLeaseTTL":                rediskeys.CharLeaseTTL,
		"CharLeaseHeartbeatCadence":   rediskeys.CharLeaseHeartbeatCadence,
		"ZoneRegistryTTL":             rediskeys.ZoneRegistryTTL,
		"ZoneHeartbeatCadence":        rediskeys.ZoneHeartbeatCadence,
		"SlotReservationTTL":          rediskeys.SlotReservationTTL,
		"HandoffCharTTL":              rediskeys.HandoffCharTTL,
		"TradeSessionTTL":             rediskeys.TradeSessionTTL,
		"TradeActiveCharTTL":          rediskeys.TradeActiveCharTTL,
		"PartyStateTTL":               rediskeys.PartyStateTTL,
		"KillIdempotencyTTL":          rediskeys.KillIdempotencyTTL,
		"KillRateLimitWindow":         rediskeys.KillRateLimitWindow,
		"GatewayLoginRateLimitWindow": rediskeys.GatewayLoginRateLimitWindow,
		"GatewayChatRateLimitWindow":  rediskeys.GatewayChatRateLimitWindow,
		"EventSeenTTL":                rediskeys.EventSeenTTL,
	}

	for name, d := range ttls {
		if d <= 0 {
			t.Fatalf("TTL constant %s must be positive, got %v", name, d)
		}
	}

	if rediskeys.CharLeaseHeartbeatCadence >= rediskeys.CharLeaseTTL {
		t.Fatalf("CharLeaseHeartbeatCadence (%v) must be < CharLeaseTTL (%v)", rediskeys.CharLeaseHeartbeatCadence, rediskeys.CharLeaseTTL)
	}
	if rediskeys.ZoneHeartbeatCadence >= rediskeys.ZoneRegistryTTL {
		t.Fatalf("ZoneHeartbeatCadence (%v) must be < ZoneRegistryTTL (%v)", rediskeys.ZoneHeartbeatCadence, rediskeys.ZoneRegistryTTL)
	}
}
