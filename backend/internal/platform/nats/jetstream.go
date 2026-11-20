package nats

import (
	"context"
	"fmt"
	"time"

	natsgo "github.com/nats-io/nats.go"
	"github.com/nats-io/nats.go/jetstream"
)

// Client wraps a NATS connection and JetStream context.
type Client struct {
	Conn      *natsgo.Conn
	JetStream jetstream.JetStream
}

// Connect establishes a NATS connection and ensures core JetStream streams exist.
func Connect(ctx context.Context, url, serviceName string, replicas int) (*Client, error) {
	if url == "" {
		return nil, fmt.Errorf("nats url must not be empty")
	}
	if replicas < 1 {
		replicas = 1
	}

	nc, err := natsgo.Connect(
		url,
		natsgo.Name(serviceName),
		natsgo.Timeout(5*time.Second),
	)
	if err != nil {
		return nil, fmt.Errorf("connect nats %s: %w", url, err)
	}

	js, err := jetstream.New(nc)
	if err != nil {
		nc.Close()
		return nil, fmt.Errorf("init jetstream: %w", err)
	}

	c := &Client{
		Conn:      nc,
		JetStream: js,
	}

	if err := c.EnsureStreams(ctx, replicas); err != nil {
		nc.Close()
		return nil, err
	}

	return c, nil
}

// EnsureStreams creates or updates the canonical JetStream streams.
func (c *Client) EnsureStreams(ctx context.Context, replicas int) error {
	if replicas < 1 {
		replicas = 1
	}

	streams := []jetstream.StreamConfig{
		{
			Name:       "IU_WORLD_STREAM",
			Subjects:   []string{"iu.world.>"},
			Storage:    jetstream.FileStorage,
			Replicas:   replicas,
			MaxAge:     24 * time.Hour,
			Duplicates: 2 * time.Minute,
		},
		{
			Name:       "IU_CHARACTER_STREAM",
			Subjects:   []string{"iu.character.>"},
			Storage:    jetstream.FileStorage,
			Replicas:   replicas,
			MaxAge:     24 * time.Hour,
			Duplicates: 2 * time.Minute,
		},
		{
			Name:       "IU_ECONOMY_STREAM",
			Subjects:   []string{"iu.economy.>"},
			Storage:    jetstream.FileStorage,
			Replicas:   replicas,
			MaxAge:     7 * 24 * time.Hour,
			Duplicates: 5 * time.Minute,
		},
		{
			Name:       "IU_GUILD_STREAM",
			Subjects:   []string{"iu.guild.>"},
			Storage:    jetstream.FileStorage,
			Replicas:   replicas,
			MaxAge:     48 * time.Hour,
			Duplicates: 2 * time.Minute,
		},
		{
			Name:       "IU_TELEMETRY",
			Subjects:   []string{"iu.telemetry.>"},
			Storage:    jetstream.FileStorage,
			Replicas:   replicas,
			MaxAge:     24 * time.Hour,
			Duplicates: 2 * time.Minute,
		},
	}

	ensureCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()

	for _, sc := range streams {
		if _, err := c.JetStream.CreateOrUpdateStream(ensureCtx, sc); err != nil {
			return fmt.Errorf("ensure stream %s: %w", sc.Name, err)
		}
	}
	return nil
}

// Ping verifies NATS connection status and JetStream account availability.
func (c *Client) Ping(ctx context.Context) error {
	if c == nil || c.Conn == nil {
		return fmt.Errorf("nats client not initialized")
	}
	if c.Conn.Status() != natsgo.CONNECTED {
		return fmt.Errorf("nats connection status: %s", c.Conn.Status().String())
	}
	pingCtx, cancel := context.WithTimeout(ctx, 2*time.Second)
	defer cancel()
	if _, err := c.JetStream.AccountInfo(pingCtx); err != nil {
		return fmt.Errorf("jetstream account info: %w", err)
	}
	return nil
}

// Close drains and closes the underlying NATS connection.
func (c *Client) Close() {
	if c == nil || c.Conn == nil {
		return
	}
	if err := c.Conn.Drain(); err != nil {
		c.Conn.Close()
	}
}
