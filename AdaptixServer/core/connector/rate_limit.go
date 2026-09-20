package connector

import (
	"net/http"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

const rateLimitMapPruneThreshold = 4096

type perIPMinuteLimiter struct {
	mu      sync.Mutex
	limit   int
	entries map[string]*minuteBucket
}

type minuteBucket struct {
	windowStart time.Time
	count       int
}

func newPerIPMinuteLimiter(requestsPerMinute int) *perIPMinuteLimiter {
	if requestsPerMinute <= 0 {
		return nil
	}
	return &perIPMinuteLimiter{
		limit:   requestsPerMinute,
		entries: make(map[string]*minuteBucket),
	}
}

func (l *perIPMinuteLimiter) allow(ip string, now time.Time) bool {
	if l == nil {
		return true
	}
	l.mu.Lock()
	defer l.mu.Unlock()

	if len(l.entries) >= rateLimitMapPruneThreshold {
		l.pruneUnlocked(now)
	}

	const window = time.Minute
	b, ok := l.entries[ip]
	if !ok || now.Sub(b.windowStart) >= window {
		l.entries[ip] = &minuteBucket{windowStart: now, count: 1}
		return true
	}
	if b.count >= l.limit {
		return false
	}
	b.count++
	return true
}

func (l *perIPMinuteLimiter) pruneUnlocked(now time.Time) {
	cutoff := now.Add(-2 * time.Minute)
	for ip, b := range l.entries {
		if b.windowStart.Before(cutoff) {
			delete(l.entries, ip)
		}
	}
}

func loginRateLimitMiddleware(limiter *perIPMinuteLimiter) gin.HandlerFunc {
	return func(c *gin.Context) {
		if limiter == nil {
			c.Next()
			return
		}
		ip := c.ClientIP()
		if ip == "" {
			ip = "unknown"
		}
		if !limiter.allow(ip, time.Now()) {
			c.AbortWithStatusJSON(http.StatusTooManyRequests, gin.H{"error": "too many requests"})
			return
		}
		c.Next()
	}
}
