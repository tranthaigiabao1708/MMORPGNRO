package redis

import (
	"context"
	"fmt"
	"time"

	goredis "github.com/redis/go-redis/v9"
)

// NewClient initializes a go-redis/v9 client and verifies connectivity with Ping.
func NewClient(ctx context.Context, addr string) (*goredis.Client, error) {
	if addr == "" {
		return nil, fmt.Errorf("redis addr must not be empty")
	}

	client := goredis.NewClient(&goredis.Options{
		Addr:         addr,
		DialTimeout:  3 * time.Second,
		ReadTimeout:  2 * time.Second,
		WriteTimeout: 2 * time.Second,
	})

	pingCtx, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()
	if err := client.Ping(pingCtx).Err(); err != nil {
		_ = client.Close()
		return nil, fmt.Errorf("ping redis %s: %w", addr, err)
	}

	return client, nil
}
