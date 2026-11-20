package social

import (
	"errors"
	"math"
	"strings"
	"sync"
	"time"
	"unicode"
)

const (
	ChannelLocal   = "LOCAL"
	ChannelZone    = "ZONE"
	ChannelParty   = "PARTY"
	ChannelGuild   = "GUILD"
	ChannelWhisper = "WHISPER"
	ChannelSystem  = "SYSTEM"

	MaxMessageChars     = 160
	MaxRingBufferSize   = 512
	LocalChatRadiusCm   = 6000.0 // 60m in UE5 centimeters
	DefaultMinChatDelay = 800 * time.Millisecond
)

var (
	ErrEmptyChatMessage  = errors.New("chat message cannot be empty")
	ErrInvalidChannel    = errors.New("invalid chat channel")
	ErrChatRateLimited   = errors.New("chat rate limit exceeded; please wait before sending another message")
	ErrNotInPartyForChat = errors.New("must be in a party to send PARTY chat")
	ErrNotInGuildForChat = errors.New("must be in a guild to send GUILD chat")
	ErrMissingWhisperTgt = errors.New("whisper target character is required")
)

type ChatMessage struct {
	Seq            int64   `json:"seq"`
	Channel        string  `json:"channel"`
	SenderCharID   string  `json:"sender_char_id"`
	SenderName     string  `json:"sender_name"`
	SenderGuildTag string  `json:"sender_guild_tag,omitempty"`
	PartyID        string  `json:"party_id,omitempty"`
	GuildID        string  `json:"guild_id,omitempty"`
	TargetCharID   string  `json:"target_char_id,omitempty"`
	TargetName     string  `json:"target_name,omitempty"`
	ZoneID         string  `json:"zone_id"`
	PosX           float64 `json:"pos_x"`
	PosY           float64 `json:"pos_y"`
	PosZ           float64 `json:"pos_z"`
	Content        string  `json:"content"`
	ShowBubble     bool    `json:"show_bubble"`
	CreatedUnixMs  int64   `json:"created_unix_ms"`
}

type SendChatInput struct {
	Channel        string
	SenderCharID   string
	SenderName     string
	SenderGuildTag string
	PartyID        string
	GuildID        string
	TargetCharID   string
	TargetName     string
	ZoneID         string
	PosX           float64
	PosY           float64
	PosZ           float64
	Content        string
}

type FetchFilter struct {
	AfterSeq      int64
	CharacterID   string
	CharacterName string
	ZoneID        string
	PartyID       string
	GuildID       string
	PosX          float64
	PosY          float64
	PosZ          float64
	HasPosition   bool
	Limit         int
}

type Router struct {
	mu           sync.Mutex
	minInterval  time.Duration
	nextSeq      int64
	messages     []ChatMessage
	lastSentByCh map[string]time.Time
}

func NewRouter() *Router {
	return &Router{
		minInterval:  DefaultMinChatDelay,
		nextSeq:      1,
		messages:     make([]ChatMessage, 0, MaxRingBufferSize),
		lastSentByCh: make(map[string]time.Time),
	}
}

func (r *Router) SetMinInterval(d time.Duration) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.minInterval = d
}

// SanitizeMessage strips ASCII control characters, collapses excessive whitespace, and truncates to MaxMessageChars runes.
func SanitizeMessage(raw string) string {
	var b strings.Builder
	b.Grow(len(raw))
	for _, ch := range raw {
		if ch == '\n' || ch == '\r' || ch == '\t' {
			b.WriteRune(' ')
			continue
		}
		if unicode.IsControl(ch) {
			continue
		}
		b.WriteRune(ch)
	}
	cleaned := strings.TrimSpace(b.String())
	runes := []rune(cleaned)
	if len(runes) > MaxMessageChars {
		runes = runes[:MaxMessageChars]
		cleaned = strings.TrimSpace(string(runes))
	}
	return cleaned
}

func (r *Router) SendMessage(in SendChatInput) (ChatMessage, error) {
	ch := strings.ToUpper(strings.TrimSpace(in.Channel))
	if ch == "" {
		ch = ChannelLocal
	}
	switch ch {
	case ChannelLocal, ChannelZone, ChannelParty, ChannelGuild, ChannelWhisper, ChannelSystem:
	default:
		return ChatMessage{}, ErrInvalidChannel
	}

	content := SanitizeMessage(in.Content)
	if content == "" {
		return ChatMessage{}, ErrEmptyChatMessage
	}

	if ch == ChannelParty && strings.TrimSpace(in.PartyID) == "" {
		return ChatMessage{}, ErrNotInPartyForChat
	}
	if ch == ChannelGuild && strings.TrimSpace(in.GuildID) == "" {
		return ChatMessage{}, ErrNotInGuildForChat
	}
	if ch == ChannelWhisper && strings.TrimSpace(in.TargetCharID) == "" && strings.TrimSpace(in.TargetName) == "" {
		return ChatMessage{}, ErrMissingWhisperTgt
	}

	zoneID := strings.TrimSpace(in.ZoneID)
	if zoneID == "" {
		zoneID = "zone.p01_west_hub"
	}

	now := time.Now()
	r.mu.Lock()
	defer r.mu.Unlock()

	senderID := strings.TrimSpace(in.SenderCharID)
	if ch != ChannelSystem && senderID != "" && r.minInterval > 0 {
		if last, ok := r.lastSentByCh[senderID]; ok && now.Sub(last) < r.minInterval {
			return ChatMessage{}, ErrChatRateLimited
		}
		r.lastSentByCh[senderID] = now
	}

	showBubble := (ch == ChannelLocal || ch == ChannelParty || ch == ChannelGuild)
	msg := ChatMessage{
		Seq:            r.nextSeq,
		Channel:        ch,
		SenderCharID:   senderID,
		SenderName:     strings.TrimSpace(in.SenderName),
		SenderGuildTag: strings.TrimSpace(in.SenderGuildTag),
		PartyID:        strings.TrimSpace(in.PartyID),
		GuildID:        strings.TrimSpace(in.GuildID),
		TargetCharID:   strings.TrimSpace(in.TargetCharID),
		TargetName:     strings.TrimSpace(in.TargetName),
		ZoneID:         zoneID,
		PosX:           in.PosX,
		PosY:           in.PosY,
		PosZ:           in.PosZ,
		Content:        content,
		ShowBubble:     showBubble,
		CreatedUnixMs:  now.UnixMilli(),
	}
	r.nextSeq++

	if len(r.messages) >= MaxRingBufferSize {
		copy(r.messages, r.messages[1:])
		r.messages[len(r.messages)-1] = msg
	} else {
		r.messages = append(r.messages, msg)
	}
	return msg, nil
}

func (r *Router) FetchMessages(f FetchFilter) ([]ChatMessage, int64) {
	limit := f.Limit
	if limit <= 0 || limit > 100 {
		limit = 50
	}
	zoneID := strings.TrimSpace(f.ZoneID)
	if zoneID == "" {
		zoneID = "zone.p01_west_hub"
	}
	charID := strings.TrimSpace(f.CharacterID)
	charName := strings.TrimSpace(f.CharacterName)
	partyID := strings.TrimSpace(f.PartyID)
	guildID := strings.TrimSpace(f.GuildID)

	r.mu.Lock()
	defer r.mu.Unlock()

	latestSeq := r.nextSeq - 1
	out := make([]ChatMessage, 0, min(limit, len(r.messages)))

	for _, m := range r.messages {
		if m.Seq <= f.AfterSeq {
			continue
		}
		visible := false
		switch m.Channel {
		case ChannelSystem:
			visible = (m.ZoneID == "" || m.ZoneID == zoneID)
		case ChannelZone:
			visible = (m.ZoneID == "" || m.ZoneID == zoneID)
		case ChannelLocal:
			if m.ZoneID == zoneID {
				if !f.HasPosition || m.SenderCharID == charID {
					visible = true
				} else {
					dx := m.PosX - f.PosX
					dy := m.PosY - f.PosY
					dz := m.PosZ - f.PosZ
					dist := math.Sqrt(dx*dx + dy*dy + dz*dz)
					visible = dist <= LocalChatRadiusCm
				}
			}
		case ChannelParty:
			visible = (partyID != "" && m.PartyID == partyID)
		case ChannelGuild:
			visible = (guildID != "" && m.GuildID == guildID)
		case ChannelWhisper:
			visible = (charID != "" && (m.SenderCharID == charID || m.TargetCharID == charID)) ||
				(charName != "" && strings.EqualFold(m.TargetName, charName))
		}
		if visible {
			out = append(out, m)
		}
	}

	if len(out) > limit {
		out = out[len(out)-limit:]
	}
	return out, latestSeq
}
