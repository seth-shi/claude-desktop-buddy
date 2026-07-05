//go:build darwin

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

func serviceKind() string { return "launchd (LaunchAgent)" }
func hideConsole()        {} // launchd already runs us with no window

func plistPath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, "Library", "LaunchAgents", serviceLabel+".plist")
}

func installService(exe, port, proxy string, interval int) error {
	args := []string{exe, "--background", "--port", port, "--interval", fmt.Sprintf("%d", interval)}
	if proxy != "" {
		args = append(args, "--proxy", proxy)
	}
	var b strings.Builder
	b.WriteString(`<?xml version="1.0" encoding="UTF-8"?>` + "\n")
	b.WriteString(`<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">` + "\n")
	b.WriteString(`<plist version="1.0"><dict>` + "\n")
	b.WriteString("  <key>Label</key><string>" + serviceLabel + "</string>\n")
	b.WriteString("  <key>ProgramArguments</key><array>\n")
	for _, a := range args {
		b.WriteString("    <string>" + xmlEscape(a) + "</string>\n")
	}
	b.WriteString("  </array>\n")
	b.WriteString("  <key>RunAtLoad</key><true/>\n") // start now + at every login
	b.WriteString("  <key>KeepAlive</key><true/>\n") // restart if it ever exits
	b.WriteString("</dict></plist>\n")

	p := plistPath()
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(p, []byte(b.String()), 0o644); err != nil {
		return err
	}
	_ = exec.Command("launchctl", "unload", p).Run() // ignore if not loaded
	if out, err := exec.Command("launchctl", "load", "-w", p).CombinedOutput(); err != nil {
		return fmt.Errorf("launchctl load failed: %v (%s)", err, strings.TrimSpace(string(out)))
	}
	return nil
}

func removeService() error {
	p := plistPath()
	_ = exec.Command("launchctl", "unload", p).Run()
	if err := os.Remove(p); err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}

func serviceStatus() (string, error) {
	if _, err := os.Stat(plistPath()); err != nil {
		return "auto-start: NOT installed", nil
	}
	out, _ := exec.Command("launchctl", "list").CombinedOutput()
	if strings.Contains(string(out), serviceLabel) {
		return "auto-start: installed and loaded (launchd)", nil
	}
	return "auto-start: installed but not loaded — re-run `enable`", nil
}

func xmlEscape(s string) string {
	return strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;").Replace(s)
}
