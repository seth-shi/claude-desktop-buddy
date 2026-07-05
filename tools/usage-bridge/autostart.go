// Auto-start (enable/disable/status) — installs the bridge as a per-user
// login service so it runs in the background and restarts if it dies.
//
// The OS-specific bits (installService / removeService / serviceStatus /
// hideConsole / serviceKind) live in autostart_<os>.go:
//   - macOS   → a launchd LaunchAgent (RunAtLoad + KeepAlive)
//   - Windows → a Scheduled Task (onlogon), console hidden via ShowWindow
//   - other   → returns "unsupported" (use a systemd user service on Linux)
package main

import (
	"fmt"
	"os"
	"path/filepath"
)

const serviceLabel = "com.claudebuddy.usage" // launchd label
const serviceTaskName = "ClaudeBuddyUsage"   // Windows scheduled-task name

// selfExe is the absolute, symlink-resolved path to this binary — what the
// login service will execute.
func selfExe() (string, error) {
	p, err := os.Executable()
	if err != nil {
		return "", err
	}
	if rp, err := filepath.EvalSymlinks(p); err == nil {
		p = rp
	}
	return filepath.Abs(p)
}

// logFilePath is where the background instance appends its output.
func logFilePath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".claude-buddy-usage.log")
}

// setupBackground redirects logging to the log file and hides the console
// (Windows). Called when the process is launched with --background.
func setupBackground() {
	if f, err := os.OpenFile(logFilePath(), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644); err == nil {
		logWriter = f
	}
	hideConsole() // no-op except on Windows
}

// cmdEnable installs the login service pointing at this binary, running the
// push loop in the background. It resolves (and remembers) the serial port and
// proxy so the service has everything it needs with no shell env.
func cmdEnable(explicitPort, proxy string, interval int) error {
	exe, err := selfExe()
	if err != nil {
		return err
	}
	port, err := choosePort(explicitPort)
	if err != nil {
		return fmt.Errorf("can't determine the serial port: %w — plug the device in or pass --port", err)
	}
	// Persist port/proxy so a plain `run` (and future `disable`) match.
	c := loadConfig()
	c.Port = port
	if proxy != "" {
		c.Proxy = proxy
	}
	saveConfig(c)

	if err := installService(exe, port, proxy, interval); err != nil {
		return err
	}
	fmt.Printf("✓ auto-start enabled via %s\n", serviceKind())
	fmt.Printf("  binary   : %s\n", exe)
	fmt.Printf("  port     : %s\n", port)
	fmt.Printf("  interval : %ds\n", interval)
	if proxy != "" {
		fmt.Printf("  proxy    : %s\n", proxy)
	}
	fmt.Printf("  log      : %s\n", logFilePath())
	fmt.Println("  manage   : `claude-buddy-usage status` / `claude-buddy-usage disable`")
	return nil
}

func cmdDisable() error {
	if err := removeService(); err != nil {
		return err
	}
	fmt.Printf("✓ auto-start disabled (%s)\n", serviceKind())
	return nil
}

func cmdStatus() error {
	s, err := serviceStatus()
	if err != nil {
		return err
	}
	fmt.Println(s)
	fmt.Printf("log: %s\n", logFilePath())
	return nil
}
