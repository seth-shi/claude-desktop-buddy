//go:build !windows && !darwin

package main

import "fmt"

func serviceKind() string { return "unsupported OS" }
func hideConsole()        {}

func installService(exe, port, proxy string, interval int) error {
	return fmt.Errorf("`enable` auto-start is implemented for macOS and Windows only; " +
		"on Linux, create a systemd user service that runs: " + exe + " --background --port " + port)
}

func removeService() error {
	return fmt.Errorf("auto-start is not managed by this tool on this OS")
}

func serviceStatus() (string, error) {
	return "auto-start: unsupported on this OS", nil
}
