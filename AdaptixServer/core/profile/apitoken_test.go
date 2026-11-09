package profile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeTmpProfile(t *testing.T, content string) string {
	p := filepath.Join(t.TempDir(), "profile.yaml")
	if err := os.WriteFile(p, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestEnsureApiTokenGeneratesAndWritesBack(t *testing.T) {
	SetStaticApiToken("")
	p := writeTmpProfile(t, "# top comment\nTeamserver:\n  password: \"x\" # keep me\n  only_password: true\nWeb:\n  port: 8080\n")
	tok, generated, err := EnsureApiToken(p, &TsProfile{})
	if err != nil || !generated {
		t.Fatalf("gen failed: %v generated=%v", err, generated)
	}
	if len(tok) != 64 {
		t.Fatalf("token should be 64 hex, got %d", len(tok))
	}
	data, _ := os.ReadFile(p)
	s := string(data)
	if !strings.Contains(s, "api_token: \""+tok+"\"") {
		t.Fatalf("token not written back")
	}
	if !strings.Contains(s, "# top comment") || !strings.Contains(s, "# keep me") {
		t.Fatalf("comments lost during write-back")
	}
	if !strings.Contains(s, "password: \"x\"") || !strings.Contains(s, "port: 8080") {
		t.Fatalf("other fields lost")
	}
	if !CheckStaticApiToken(tok) {
		t.Fatalf("CheckStaticApiToken should accept generated token")
	}
	if CheckStaticApiToken("wrong") || CheckStaticApiToken("") {
		t.Fatalf("CheckStaticApiToken should reject wrong/empty")
	}
}

func TestEnsureApiTokenReplacesExistingEmptyLine(t *testing.T) {
	SetStaticApiToken("")
	p := writeTmpProfile(t, "Teamserver:\n  password: \"x\"\n  api_token: \"\"\n  port_bind_note: keep\n")
	tok, _, err := EnsureApiToken(p, &TsProfile{})
	if err != nil {
		t.Fatal(err)
	}
	data, _ := os.ReadFile(p)
	if strings.Count(string(data), "api_token:") != 1 {
		t.Fatalf("should replace in place, not duplicate")
	}
	if !strings.Contains(string(data), tok) || !strings.Contains(string(data), "port_bind_note") {
		t.Fatalf("replacement lost content")
	}
}

func TestEnsureApiTokenKeepsPresetValue(t *testing.T) {
	SetStaticApiToken("")
	p := writeTmpProfile(t, "Teamserver:\n  api_token: \"my-fixed-token\"\n")
	tok, generated, err := EnsureApiToken(p, &TsProfile{ApiToken: "my-fixed-token"})
	if err != nil || generated {
		t.Fatalf("preset should be reused, not regenerated")
	}
	if tok != "my-fixed-token" || !CheckStaticApiToken("my-fixed-token") {
		t.Fatalf("preset token not honored")
	}
	data, _ := os.ReadFile(p)
	if !strings.Contains(string(data), "my-fixed-token") {
		t.Fatalf("file should be untouched when preset")
	}
}
