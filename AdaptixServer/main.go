package main

import (
	"AdaptixServer/core/profile"
	"AdaptixServer/core/server"
	"AdaptixServer/core/utils/logs"
	"AdaptixServer/core/utils/token"
	_ "embed"
	"flag"
	"fmt"
	"os"
	"path/filepath"
)

//go:embed profile.yaml
var defaultProfileYAML []byte

func main() {
	fmt.Printf("\n[===== Adaptix Framework %v =====]\n\n", server.SMALL_VERSION)

	var (
		err         error
		debug       = flag.Bool("debug", false, "Enable debug mode")
		profilePath = flag.String("profile", "profile.yaml", "Path to YAML profile (default: profile.yaml in current working directory)")
	)

	flag.Usage = func() {
		fmt.Printf("Usage: AdaptixServer [options]\n")
		fmt.Printf("Options:\n")
		flag.PrintDefaults()
		fmt.Printf("\nBy default uses ./profile.yaml in the working directory; if missing, it is created from the built-in template (same as AdaptixServer/profile.yaml in the source tree).\n")
		fmt.Printf("Use -profile to use another file (missing paths are also created from that template).\n")
		fmt.Printf("Secrets may be overridden by env: ADAPTIX_TEAMSERVER_PASSWORD, ADAPTIX_TEAMSERVER_PASSWORD_FILE,\n")
		fmt.Printf("ADAPTIX_TEAMSERVER_CERT, ADAPTIX_TEAMSERVER_KEY, ADAPTIX_TEAMSERVER_INTERFACE, ADAPTIX_TEAMSERVER_MANAGE_PASSWORD.\n")
		fmt.Printf("ADAPTIX_TLS_AUTOGEN, ADAPTIX_TLS_AUTOGEN_FORCE, ADAPTIX_TLS_AUTOGEN_CN (see Teamserver.tls_autogen in profile).\n\n")
		fmt.Printf("Example:\n")
		fmt.Printf("   AdaptixServer [-debug]\n")
		fmt.Printf("   AdaptixServer -profile /path/to/custom.yaml [-debug]\n")
	}
	flag.Parse()

	logs.NewPrintLogger(*debug)
	logs.RepoLogsInstance, err = logs.NewRepoLogs()
	if err != nil {
		logs.Error("", err.Error())
		os.Exit(0)
	}

	ts := server.NewTeamserver()
	if ts == nil {
		os.Exit(1)
	}

	if err = ensureProfileFromTemplate(*profilePath); err != nil {
		logs.Error("", "profile bootstrap: "+err.Error())
		os.Exit(1)
	}

	err = ts.SetProfile(*profilePath)
	if err != nil {
		logs.Error("", err.Error())
		os.Exit(1)
	}

	if err = profile.RunTLSAutogenIfConfigured(*profilePath, ts.Profile); err != nil {
		logs.Error("", "TLS autogen: "+err.Error())
		os.Exit(1)
	}

	err = ts.Profile.IsValid()
	if err != nil {
		logs.Error("", err.Error())
		os.Exit(0)
	}

	token.InitJWT(ts.Profile.Server.ATokenLive, ts.Profile.Server.RTokenLive)

	ts.Start()
}

// ensureProfileFromTemplate writes the embedded default profile when the target file does not exist.
func ensureProfileFromTemplate(profilePath string) error {
	if len(defaultProfileYAML) == 0 {
		return fmt.Errorf("built-in profile template is empty (rebuild AdaptixServer with profile.yaml next to main.go)")
	}
	abs, err := filepath.Abs(profilePath)
	if err != nil {
		return err
	}
	if _, err := os.Stat(abs); err == nil {
		return nil
	}
	if !os.IsNotExist(err) {
		return err
	}
	dir := filepath.Dir(abs)
	if dir != "." && dir != "" {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return fmt.Errorf("create profile directory: %w", err)
		}
	}
	if err := os.WriteFile(abs, defaultProfileYAML, 0644); err != nil {
		return fmt.Errorf("write default profile: %w", err)
	}
	logs.Success("", "Created profile from built-in template: %s", abs)
	return nil
}
