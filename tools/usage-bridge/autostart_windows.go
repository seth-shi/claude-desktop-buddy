//go:build windows

package main

import (
	"fmt"
	"os/exec"
	"strings"
	"syscall"
)

// Per-user autostart via the HKCU "Run" key — runs at logon in the user session
// with NO admin rights. (schtasks /create typically needs elevation and returns
// "access denied".) The console window Explorer opens at logon is hidden by
// hideConsole() once --background starts.
const runKey = `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
const runValue = "ClaudeBuddyUsage"

func serviceKind() string { return "Windows startup (HKCU Run key)" }

// hideConsole hides the console window attached to this process.
func hideConsole() {
	getConsoleWindow := syscall.NewLazyDLL("kernel32.dll").NewProc("GetConsoleWindow")
	showWindow := syscall.NewLazyDLL("user32.dll").NewProc("ShowWindow")
	if hwnd, _, _ := getConsoleWindow.Call(); hwnd != 0 {
		const swHide = 0
		_, _, _ = showWindow.Call(hwnd, swHide)
	}
}

func startupCmd(exe, port, proxy string, interval int) string {
	s := fmt.Sprintf(`"%s" --background --port %s --interval %d`, exe, port, interval)
	if proxy != "" {
		s += " --proxy " + proxy
	}
	return s
}

func installService(exe, port, proxy string, interval int) error {
	if out, err := exec.Command("reg", "add", runKey, "/v", runValue,
		"/t", "REG_SZ", "/d", startupCmd(exe, port, proxy, interval), "/f").CombinedOutput(); err != nil {
		return fmt.Errorf("reg add failed: %v (%s)", err, strings.TrimSpace(string(out)))
	}
	return startBackgroundNow(exe, port, proxy, interval) // run now, windowless
}

// startBackgroundNow launches the bridge detached with no console window, so the
// user doesn't have to log out and back in for it to start.
func startBackgroundNow(exe, port, proxy string, interval int) error {
	args := []string{"--background", "--port", port, "--interval", fmt.Sprintf("%d", interval)}
	if proxy != "" {
		args = append(args, "--proxy", proxy)
	}
	cmd := exec.Command(exe, args...)
	const createNoWindow = 0x08000000
	cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: createNoWindow, HideWindow: true}
	return cmd.Start() // don't Wait — it keeps running on its own
}

func removeService() error {
	// Stop the running instance (drops DTR → device resets once, expected), then
	// remove the autostart entry.
	_ = exec.Command("taskkill", "/IM", "claude-buddy-usage.exe", "/F").Run()
	out, err := exec.Command("reg", "delete", runKey, "/v", runValue, "/f").CombinedOutput()
	if err != nil {
		low := strings.ToLower(string(out))
		if strings.Contains(low, "unable to find") || strings.Contains(string(out), "找不到") {
			return nil // already gone
		}
		return fmt.Errorf("reg delete failed: %v (%s)", err, strings.TrimSpace(string(out)))
	}
	return nil
}

func serviceStatus() (string, error) {
	out, err := exec.Command("reg", "query", runKey, "/v", runValue).CombinedOutput()
	if err != nil {
		return "auto-start: NOT installed", nil
	}
	running := "process NOT running"
	if o, _ := exec.Command("tasklist", "/FI", "IMAGENAME eq claude-buddy-usage.exe").CombinedOutput(); strings.Contains(string(o), "claude-buddy-usage.exe") {
		running = "process running"
	}
	return "auto-start: installed (" + running + ")\n" + strings.TrimSpace(string(out)), nil
}
