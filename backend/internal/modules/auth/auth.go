package auth

import (
	"context"
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/infinite-universe/backend/internal/platform/rediskeys"
	goredis "github.com/redis/go-redis/v9"
)

var (
	ErrInvalidToken  = errors.New("invalid or expired token")
	ErrSessionStale  = errors.New("session superseded by a newer login")
	ErrBadHMAC       = errors.New("invalid internal server HMAC signature")
)

const (
	AccessTokenTTL = 15 * time.Minute
)

// Claims represents the verified Ed25519 JWT payload.
type Claims struct {
	Subject   string `json:"sub"` // account UUID
	SessionID string `json:"sid"`
	Username  string `json:"usr"`
	IssuedAt  int64  `json:"iat"`
	ExpiresAt int64  `json:"exp"`
}

// TokenPair is returned on successful Register, Login, or Refresh.
type TokenPair struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresInSec int64  `json:"expires_in_sec"`
	SessionID    string `json:"session_id"`
}

type refreshRecord struct {
	AccountID uuid.UUID `json:"account_id"`
	Username  string    `json:"username"`
	SessionID string    `json:"session_id"`
	ExpiresAt int64     `json:"expires_at"`
}

// Manager issues and verifies Ed25519 JWTs, manages single-active-session locks, and verifies internal Server HMACs.
type Manager struct {
	privKey      ed25519.PrivateKey
	pubKey       ed25519.PublicKey
	serverSecret []byte
	rdb          *goredis.Client

	mu             sync.RWMutex
	activeSessions map[uuid.UUID]string
	refreshTokens  map[string]refreshRecord
}

// NewManager creates an Ed25519 JWT & session manager.
func NewManager(seedHex, pubHex, internalHMACSecret string, rdb *goredis.Client) (*Manager, error) {
	var priv ed25519.PrivateKey
	var pub ed25519.PublicKey

	if strings.TrimSpace(seedHex) != "" {
		seedBytes, err := hex.DecodeString(strings.TrimSpace(seedHex))
		if err != nil || len(seedBytes) != ed25519.SeedSize {
			return nil, fmt.Errorf("invalid IU_JWT_PRIVATE_KEY_SEED_HEX (expected 32-byte hex)")
		}
		priv = ed25519.NewKeyFromSeed(seedBytes)
		pub = priv.Public().(ed25519.PublicKey)
	} else if strings.TrimSpace(pubHex) != "" {
		pubBytes, err := hex.DecodeString(strings.TrimSpace(pubHex))
		if err != nil || len(pubBytes) != ed25519.PublicKeySize {
			return nil, fmt.Errorf("invalid IU_JWT_PUBLIC_KEY_HEX")
		}
		pub = ed25519.PublicKey(pubBytes)
	} else {
		var err error
		pub, priv, err = ed25519.GenerateKey(rand.Reader)
		if err != nil {
			return nil, err
		}
	}

	secret := strings.TrimSpace(internalHMACSecret)
	if secret == "" {
		secret = "iu_step3_internal_server_hmac_2026"
	}

	return &Manager{
		privKey:        priv,
		pubKey:         pub,
		serverSecret:   []byte(secret),
		rdb:            rdb,
		activeSessions: make(map[uuid.UUID]string),
		refreshTokens:  make(map[string]refreshRecord),
	}, nil
}

// IssueSession creates a new active session for accountID (superseding any older session) and returns a TokenPair.
func (m *Manager) IssueSession(ctx context.Context, accountID uuid.UUID, username string) (TokenPair, string, error) {
	if len(m.privKey) == 0 {
		return TokenPair{}, "", errors.New("manager initialized without private signing key")
	}

	sessionID := uuid.NewString()
	now := time.Now().UTC()
	exp := now.Add(AccessTokenTTL)

	accessTok, err := m.signJWT(Claims{
		Subject:   accountID.String(),
		SessionID: sessionID,
		Username:  username,
		IssuedAt:  now.Unix(),
		ExpiresAt: exp.Unix(),
	})
	if err != nil {
		return TokenPair{}, "", err
	}

	rawRefresh := make([]byte, 32)
	if _, err := rand.Read(rawRefresh); err != nil {
		return TokenPair{}, "", err
	}
	refreshStr := base64.RawURLEncoding.EncodeToString(rawRefresh)
	refreshHash := sha256Hex(refreshStr)
	rec := refreshRecord{
		AccountID: accountID,
		Username:  username,
		SessionID: sessionID,
		ExpiresAt: now.Add(rediskeys.RefreshTokenTTL).Unix(),
	}

	var previousSessionID string
	m.mu.Lock()
	previousSessionID = m.activeSessions[accountID]
	m.activeSessions[accountID] = sessionID
	m.refreshTokens[refreshHash] = rec
	m.mu.Unlock()

	if m.rdb != nil {
		sessKey := rediskeys.SessionAccountKey(accountID.String())
		prev, _ := m.rdb.Get(ctx, sessKey).Result()
		if prev != "" {
			previousSessionID = prev
		}
		_ = m.rdb.Set(ctx, sessKey, sessionID, rediskeys.SessionAccountTTL).Err()
		if b, err := json.Marshal(rec); err == nil {
			_ = m.rdb.Set(ctx, rediskeys.RefreshTokenKey(refreshHash), string(b), rediskeys.RefreshTokenTTL).Err()
		}
	}

	return TokenPair{
		AccessToken:  accessTok,
		RefreshToken: refreshStr,
		ExpiresInSec: int64(AccessTokenTTL.Seconds()),
		SessionID:    sessionID,
	}, previousSessionID, nil
}

// VerifyAccessToken validates Ed25519 signature, expiration, and single-active-session freshness.
func (m *Manager) VerifyAccessToken(ctx context.Context, token string) (Claims, error) {
	parts := strings.Split(strings.TrimSpace(token), ".")
	if len(parts) != 3 {
		return Claims{}, ErrInvalidToken
	}

	signingInput := parts[0] + "." + parts[1]
	sig, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil || len(sig) != ed25519.SignatureSize {
		return Claims{}, ErrInvalidToken
	}
	if !ed25519.Verify(m.pubKey, []byte(signingInput), sig) {
		return Claims{}, ErrInvalidToken
	}

	payloadBytes, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return Claims{}, ErrInvalidToken
	}

	var claims Claims
	if err := json.Unmarshal(payloadBytes, &claims); err != nil {
		return Claims{}, ErrInvalidToken
	}
	if time.Now().UTC().Unix() > claims.ExpiresAt {
		return Claims{}, ErrInvalidToken
	}

	accID, err := uuid.Parse(claims.Subject)
	if err != nil {
		return Claims{}, ErrInvalidToken
	}

	if m.rdb != nil {
		activeSid, err := m.rdb.Get(ctx, rediskeys.SessionAccountKey(accID.String())).Result()
		if err == nil && activeSid != "" && activeSid != claims.SessionID {
			return Claims{}, ErrSessionStale
		}
	}

	m.mu.RLock()
	activeMemSid, exists := m.activeSessions[accID]
	m.mu.RUnlock()
	if exists && activeMemSid != claims.SessionID {
		return Claims{}, ErrSessionStale
	}

	return claims, nil
}

// Refresh rotates a valid refresh token into a fresh TokenPair.
func (m *Manager) Refresh(ctx context.Context, refreshToken string) (TokenPair, error) {
	refreshHash := sha256Hex(strings.TrimSpace(refreshToken))
	var rec refreshRecord
	found := false

	if m.rdb != nil {
		if raw, err := m.rdb.GetDel(ctx, rediskeys.RefreshTokenKey(refreshHash)).Result(); err == nil && raw != "" {
			if json.Unmarshal([]byte(raw), &rec) == nil {
				found = true
			}
		}
	}

	m.mu.Lock()
	if !found {
		if r, ok := m.refreshTokens[refreshHash]; ok {
			rec = r
			found = true
		}
	}
	delete(m.refreshTokens, refreshHash)
	m.mu.Unlock()

	if !found || time.Now().UTC().Unix() > rec.ExpiresAt {
		return TokenPair{}, ErrInvalidToken
	}

	pair, _, err := m.IssueSession(ctx, rec.AccountID, rec.Username)
	return pair, err
}

// RevokeSession invalidates the active session for accountID on logout.
func (m *Manager) RevokeSession(ctx context.Context, accountID uuid.UUID) {
	m.mu.Lock()
	delete(m.activeSessions, accountID)
	m.mu.Unlock()
	if m.rdb != nil {
		_ = m.rdb.Del(ctx, rediskeys.SessionAccountKey(accountID.String())).Err()
	}
}

// ComputeInternalHMAC computes hex(HMAC-SHA256(body, serverSecret)) for UE5 Dedicated Server <-> Go bridge requests.
func (m *Manager) ComputeInternalHMAC(body []byte) string {
	mac := hmac.New(sha256.New, m.serverSecret)
	mac.Write(body)
	return hex.EncodeToString(mac.Sum(nil))
}

// VerifyInternalHMAC validates X-IU-Server-HMAC in constant time.
func (m *Manager) VerifyInternalHMAC(body []byte, providedHex string) error {
	expected := m.ComputeInternalHMAC(body)
	if !hmac.Equal([]byte(strings.ToLower(strings.TrimSpace(providedHex))), []byte(expected)) {
		return ErrBadHMAC
	}
	return nil
}

func (m *Manager) signJWT(claims Claims) (string, error) {
	header := `{"alg":"EdDSA","typ":"JWT"}`
	payloadBytes, err := json.Marshal(claims)
	if err != nil {
		return "", err
	}
	hEnc := base64.RawURLEncoding.EncodeToString([]byte(header))
	pEnc := base64.RawURLEncoding.EncodeToString(payloadBytes)
	signingInput := hEnc + "." + pEnc
	sig := ed25519.Sign(m.privKey, []byte(signingInput))
	return signingInput + "." + base64.RawURLEncoding.EncodeToString(sig), nil
}

func sha256Hex(s string) string {
	sum := sha256.Sum256([]byte(s))
	return hex.EncodeToString(sum[:])
}
