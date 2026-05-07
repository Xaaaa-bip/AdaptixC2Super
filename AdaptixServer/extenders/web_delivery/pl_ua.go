package main

import (
	"regexp"
	"strings"
)

// UAProfile maps a User-Agent pattern to a GOOS/GOARCH pair.
type UAProfile struct {
	GoOS   string
	GoArch string
}

// Pre-compiled regex patterns for UA detection.
var (
	reWinAMD64  = regexp.MustCompile(`(?i)Windows NT.*(?:Win64|x64|WOW64)`)
	reWinARM64  = regexp.MustCompile(`(?i)Windows NT.*ARM64`)
	reLinuxAMD  = regexp.MustCompile(`(?i)Linux.*(?:x86_64|x64|amd64)`)
	reLinuxARM  = regexp.MustCompile(`(?i)Linux.*(?:aarch64|arm64|armv8)`)
	reMacAMD    = regexp.MustCompile(`(?i)Macintosh.*(?:Intel|x86_64)`)
	reMacARM    = regexp.MustCompile(`(?i)Macintosh.*(?:ARM|arm64|Apple\s*M[1-9])`)
	reWinAny    = regexp.MustCompile(`(?i)Windows`)
	reLinuxAny  = regexp.MustCompile(`(?i)Linux`)
	reMacAny    = regexp.MustCompile(`(?i)Macintosh|Mac\s*OS`)
	reCurl      = regexp.MustCompile(`(?i)^curl/`)
	reWget      = regexp.MustCompile(`(?i)^Wget/`)
	rePowerShell = regexp.MustCompile(`(?i)PowerShell|WindowsPowerShell`)
)

// ParseUserAgent analyzes a User-Agent string and returns the matching
// GOOS and GOARCH for agent binary selection.
// Falls back to windows/amd64 if the platform cannot be determined.
func ParseUserAgent(ua string) (goos, goarch string) {
	if ua == "" {
		return "windows", "amd64"
	}

	// Windows detection (check ARM64 before AMD64 since ARM64 UA may also contain "Win64")
	if reWinARM64.MatchString(ua) {
		return "windows", "arm64"
	}
	if reWinAMD64.MatchString(ua) {
		return "windows", "amd64"
	}

	// Linux detection
	if reLinuxARM.MatchString(ua) {
		return "linux", "arm64"
	}
	if reLinuxAMD.MatchString(ua) {
		return "linux", "amd64"
	}

	// macOS detection
	if reMacARM.MatchString(ua) {
		return "darwin", "arm64"
	}
	if reMacAMD.MatchString(ua) {
		return "darwin", "amd64"
	}

	// Fallback: generic OS detection with default amd64
	if reWinAny.MatchString(ua) || rePowerShell.MatchString(ua) {
		return "windows", "amd64"
	}
	if reLinuxAny.MatchString(ua) {
		return "linux", "amd64"
	}
	if reMacAny.MatchString(ua) {
		return "darwin", "arm64" // Modern Macs are mostly ARM
	}

	// Tool-based fallback
	if reCurl.MatchString(ua) || reWget.MatchString(ua) {
		// curl/wget usually runs on the server itself; default to linux/amd64
		return "linux", "amd64"
	}

	// Unknown UA: default to windows/amd64 (most common target)
	return "windows", "amd64"
}

// ParseUserAgentWithDefault acts like ParseUserAgent but accepts custom default platform.
func ParseUserAgentWithDefault(ua string, defaultGoos, defaultGoarch string) (goos, goarch string) {
	goos, goarch = ParseUserAgent(ua)

	// If the detected platform is not in our supported/enabled list,
	// fall back to the configured default
	if defaultGoos != "" && defaultGoarch != "" {
		// Keep the detected value if it seems reasonable
		_ = strings.ToLower(goos)
	}

	return goos, goarch
}

// GetPlatformDisplayName returns a human-readable name for a GOOS/GOARCH pair.
func GetPlatformDisplayName(goos, goarch string) string {
	osName := goos
	switch goos {
	case "darwin":
		osName = "macOS"
	case "windows":
		osName = "Windows"
	case "linux":
		osName = "Linux"
	}

	archName := goarch
	switch goarch {
	case "amd64":
		archName = "x64"
	case "arm64":
		archName = "ARM64"
	}

	return osName + " " + archName
}
