// claude-buddy-usage — push Claude subscription usage to the buddy device over
// USB serial. Single self-contained binary (no Python, no Bluetooth): it reads
// the OAuth token the Claude CLI already stored, polls /api/oauth/usage, and
// writes one compact JSON line to the device's serial port every interval.
//
// The device parses that line the same way it parses the BLE heartbeat, so the
// two green/gold side bars (5h + 7d quota) fill in. Serial is a separate channel
// from BLE, so this runs happily alongside the Claude desktop app's connection.
//
// Usage:
//   claude-buddy-usage                 # resolve token + port, then poll & push
//   claude-buddy-usage --port COM5     # pick the serial port explicitly
//   claude-buddy-usage --once          # push a single update and exit
//   claude-buddy-usage test            # fetch usage and print it, no serial
//   claude-buddy-usage ports           # list available serial ports
//   claude-buddy-usage --port COM3 enable   # install auto-start at login (launchd / Task Scheduler)
//   claude-buddy-usage disable         # remove auto-start
//   claude-buddy-usage status          # show auto-start install + running state
// Flags: --token <t>  --port <p>  --interval <sec>  --proxy <url>  --once
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"math"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"go.bug.st/serial"
)

const usageURL = "https://api.anthropic.com/api/oauth/usage"
const keychainService = "Claude Code-credentials" // macOS Keychain entry

// errUnauthorized marks a usage query rejected for auth reasons (HTTP 401/403),
// so callers can trigger a CLI-driven token refresh and retry.
var errUnauthorized = errors.New("auth failed: token invalid or expired")

// ── logging ──────────────────────────────────────────────────────
// logWriter is stdout for foreground use; setupBackground() swaps it for a log
// file when running as an auto-start service (console is hidden then).
var logWriter io.Writer = os.Stdout

func logf(format string, a ...any) {
	fmt.Fprintf(logWriter, time.Now().Format("15:04:05")+" "+format+"\n", a...)
}

// ── config (~/.claude-desktop-buddy.json) ────────────────────────
type config struct {
	OAuthToken string `json:"oauth_token,omitempty"`
	Port       string `json:"port,omitempty"`
	Proxy      string `json:"proxy,omitempty"`
}

func configPath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".claude-desktop-buddy.json")
}

func loadConfig() config {
	var c config
	if b, err := os.ReadFile(configPath()); err == nil {
		_ = json.Unmarshal(b, &c)
	}
	return c
}

func saveConfig(c config) {
	if b, err := json.MarshalIndent(c, "", "  "); err == nil {
		_ = os.WriteFile(configPath(), b, 0o600)
	}
}

// ── token resolution ─────────────────────────────────────────────
// Priority: --token / env -> config file -> macOS Keychain -> ~/.claude/.credentials.json
func readToken(explicit string) (string, error) {
	if explicit != "" {
		return strings.TrimSpace(explicit), nil
	}
	if e := os.Getenv("CLAUDE_OAUTH_TOKEN"); e != "" {
		return strings.TrimSpace(e), nil
	}
	if c := loadConfig(); c.OAuthToken != "" {
		return strings.TrimSpace(c.OAuthToken), nil
	}
	if runtime.GOOS == "darwin" {
		if t := keychainToken(); t != "" {
			return t, nil
		}
	}
	if t := credFileToken(); t != "" {
		return t, nil
	}
	return "", fmt.Errorf("no usable subscription token; run `claude auth login` (or pass --token)")
}

func tokenFromJSON(b []byte) string {
	var data struct {
		A struct {
			AccessToken string `json:"accessToken"`
		} `json:"claudeAiOauth"`
	}
	if json.Unmarshal(b, &data.A) == nil && data.A.AccessToken != "" {
		return data.A.AccessToken
	}
	// tolerate the whole object being unmarshaled into A directly failing above
	var wrap struct {
		Oauth struct {
			AccessToken string `json:"accessToken"`
		} `json:"claudeAiOauth"`
	}
	if json.Unmarshal(b, &wrap) == nil {
		return wrap.Oauth.AccessToken
	}
	return ""
}

// credsPath returns the Claude CLI's OAuth credential store (.credentials.json).
func credsPath() string {
	dir := os.Getenv("CLAUDE_CONFIG_DIR")
	if dir == "" {
		home, _ := os.UserHomeDir()
		dir = filepath.Join(home, ".claude")
	}
	return filepath.Join(dir, ".credentials.json")
}

func credFileToken() string {
	b, err := os.ReadFile(credsPath())
	if err != nil {
		return ""
	}
	return tokenFromJSON(b)
}

func keychainToken() string {
	out, err := exec.Command("security", "find-generic-password", "-s", keychainService, "-w").Output()
	if err != nil {
		return ""
	}
	return tokenFromJSON([]byte(strings.TrimSpace(string(out))))
}

// findClaude locates the Claude CLI on PATH or common install locations.
func findClaude() string {
	if p, err := exec.LookPath("claude"); err == nil {
		return p
	}
	home, _ := os.UserHomeDir()
	var candidates []string
	if runtime.GOOS == "windows" {
		local := os.Getenv("LOCALAPPDATA")
		if local == "" {
			local = filepath.Join(home, "AppData", "Local")
		}
		candidates = []string{
			filepath.Join(local, "Microsoft", "WinGet", "Links", "claude.exe"),
			filepath.Join(home, ".local", "bin", "claude.exe"),
		}
	} else {
		candidates = []string{filepath.Join(home, ".local", "bin", "claude")}
	}
	for _, c := range candidates {
		if _, err := os.Stat(c); err == nil {
			return c
		}
	}
	return ""
}

// ensureToken resolves a token, running `claude auth login` once if none exists.
func ensureToken(explicit string) (string, error) {
	if t, err := readToken(explicit); err == nil {
		return t, nil
	}
	claude := findClaude()
	if claude == "" {
		return "", fmt.Errorf("not logged in and the Claude CLI wasn't found; install it, then run `claude auth login`")
	}
	logf("not logged in — launching `claude auth login` (authorize in the browser)...")
	cmd := exec.Command(claude, "auth", "login")
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	_ = cmd.Run()
	return readToken(explicit)
}

// ── usage query ──────────────────────────────────────────────────
type window struct {
	Utilization *float64 `json:"utilization"`
	ResetsAt    string   `json:"resets_at"`
}

type usageResp struct {
	FiveHour *window `json:"five_hour"`
	SevenDay *window `json:"seven_day"`
}

func queryUsage(token string) (*usageResp, error) {
	req, _ := http.NewRequest("GET", usageURL, nil)
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("anthropic-beta", "oauth-2025-04-20")
	req.Header.Set("Accept", "application/json")
	resp, err := (&http.Client{Timeout: 15 * time.Second}).Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode == 401 || resp.StatusCode == 403 {
		return nil, fmt.Errorf("%w (HTTP %d) — expired or insufficient scope; use a `claude auth login` token, not setup-token", errUnauthorized, resp.StatusCode)
	}
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("usage query HTTP %d", resp.StatusCode)
	}
	var u usageResp
	if err := json.NewDecoder(resp.Body).Decode(&u); err != nil {
		return nil, err
	}
	return &u, nil
}

// ── token refresh ────────────────────────────────────────────────
// usingCLICreds reports whether the active token comes from the CLI's
// .credentials.json — the only source we can refresh. An explicit --token, the
// CLAUDE_OAUTH_TOKEN env var, or a token pinned in our own config are opaque to
// us (no refresh token), so we leave those untouched.
func usingCLICreds(explicit string) bool {
	if explicit != "" || os.Getenv("CLAUDE_OAUTH_TOKEN") != "" {
		return false
	}
	if c := loadConfig(); c.OAuthToken != "" {
		return false
	}
	return true
}

// refreshTokenViaCLI forces the Claude CLI to refresh its own OAuth token.
//
// We can't refresh the token ourselves: Anthropic's edge blocks third parties
// from hitting the /v1/oauth/token endpoint directly (it returns 403 "Request
// not allowed"), and `claude auth status` alone won't refresh either. The CLI
// only refreshes as a side effect of an actual API call — so we make it perform
// a tiny throwaway request on the cheapest model. The CLI writes the fresh token
// back to .credentials.json, which the caller then re-reads.
//
// Cost is negligible (a couple of Haiku tokens) and this only runs after a 401,
// i.e. at most once per token lifetime (~8h).
func refreshTokenViaCLI() error {
	claude := findClaude()
	if claude == "" {
		return fmt.Errorf("Claude CLI not found on PATH — run any `claude` command (or `claude auth login`) to refresh the token")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, claude, "-p", "ok", "--model", "claude-haiku-4-5-20251001")
	out, _ := cmd.CombinedOutput() // exit code is unreliable here; inspect the output instead
	if s := strings.TrimSpace(string(out)); strings.Contains(s, "Failed to authenticate") ||
		strings.Contains(s, "Request not allowed") || strings.Contains(s, "API Error") {
		return fmt.Errorf("the CLI itself couldn't reach the API to refresh (proxy off / region-blocked?): %s", s)
	}
	logf("nudged the CLI to refresh its OAuth token")
	return nil
}

// fetchUsage resolves a token, queries usage, and on a 401 asks the CLI to
// refresh the token (see refreshTokenViaCLI) before one retry. This is the
// single entry point both `test` and the push loop use.
func fetchUsage(explicitToken string) (*usageResp, error) {
	tok, err := readToken(explicitToken)
	if err != nil {
		return nil, err
	}
	u, err := queryUsage(tok)
	if errors.Is(err, errUnauthorized) && usingCLICreds(explicitToken) {
		logf("usage rejected (token expired) — asking the CLI to refresh, then retrying")
		if rerr := refreshTokenViaCLI(); rerr != nil {
			return nil, fmt.Errorf("%v; %w", err, rerr)
		}
		if tok, err = readToken(explicitToken); err != nil {
			return nil, err
		}
		u, err = queryUsage(tok)
	}
	return u, err
}

func parseISO(s string) (time.Time, error) {
	// Handles both plain and fractional seconds, Z or numeric offset.
	return time.Parse("2006-01-02T15:04:05.999999999Z07:00", strings.TrimSpace(s))
}

func secsToReset(iso string) (int, bool) {
	if iso == "" {
		return 0, false
	}
	t, err := parseISO(iso)
	if err != nil {
		return 0, false
	}
	d := int(time.Until(t).Seconds())
	if d < 0 {
		d = 0
	}
	return d, true
}

// buildLine -> {"rem5":..,"sec5":..,"rem7":..,"sec7":..} (only windows present in the response).
func buildLine(u *usageResp) string {
	m := map[string]int{}
	add := func(w *window, remKey, secKey string) {
		if w == nil || w.Utilization == nil {
			return
		}
		m[remKey] = int(math.Round(100 - *w.Utilization))
		if s, ok := secsToReset(w.ResetsAt); ok {
			m[secKey] = s
		}
	}
	add(u.FiveHour, "rem5", "sec5")
	add(u.SevenDay, "rem7", "sec7")
	b, _ := json.Marshal(m)
	return string(b)
}

func fmtCountdown(secs int) string {
	d, r := secs/86400, secs%86400
	h, r := r/3600, r%3600
	mnt := r / 60
	switch {
	case d > 0:
		return fmt.Sprintf("%dd%dh", d, h)
	case h > 0:
		return fmt.Sprintf("%dh%dm", h, mnt)
	default:
		return fmt.Sprintf("%dm", mnt)
	}
}

// ── serial port ──────────────────────────────────────────────────
func choosePort(explicit string) (string, error) {
	if explicit != "" {
		return explicit, nil
	}
	if c := loadConfig(); c.Port != "" {
		return c.Port, nil
	}
	ports, err := serial.GetPortsList()
	if err != nil {
		return "", err
	}
	switch len(ports) {
	case 0:
		return "", fmt.Errorf("no serial ports found; plug the device in via USB")
	case 1:
		return ports[0], nil
	default:
		return "", fmt.Errorf("multiple serial ports %v; pick one with --port", ports)
	}
}

// ── commands ─────────────────────────────────────────────────────
func cmdPorts() {
	ports, err := serial.GetPortsList()
	if err != nil {
		logf("error listing ports: %v", err)
		return
	}
	if len(ports) == 0 {
		fmt.Println("no serial ports found")
		return
	}
	for _, p := range ports {
		fmt.Println("  " + p)
	}
}

// openSerial opens the port and asserts DTR/RTS. The ESP32-S3 USB-Serial-JTAG
// treats DTR as "host present" and DROPS all host->device data until it is set,
// so without this the device never receives our quota lines (its TX still works,
// which is why it looks like a one-way dead link).
func openSerial(portName string) (serial.Port, error) {
	p, err := serial.Open(portName, &serial.Mode{BaudRate: 115200})
	if err != nil {
		return nil, err
	}
	_ = p.SetDTR(true)
	_ = p.SetRTS(true)
	return p, nil
}

// cmdSerialTest writes one fixed quota line to the device — verifies the
// host->serial->device wiring without needing a token or the usage API.
func cmdSerialTest(explicitPort string) error {
	portName, err := choosePort(explicitPort)
	if err != nil {
		return err
	}
	p, err := openSerial(portName)
	if err != nil {
		return err
	}
	defer p.Close()
	line := `{"rem5":66,"sec5":9000,"rem7":42,"sec7":250000}`
	// Hold ONE connection open (DTR stays asserted) and refresh every 5s. Rapid
	// open/close cycles toggle DTR/RTS and trip the ESP32-S3 USB-Serial-JTAG
	// auto-reset, which wipes the quota; a held connection avoids that and keeps
	// the bars fresh. Ctrl-C to stop.
	logf("holding %s open, pushing test quota every 5s (Ctrl-C to stop): %s  (left ~66%%, right ~42%%)", portName, line)
	for {
		if _, err := p.Write([]byte(line + "\n")); err != nil {
			return err
		}
		time.Sleep(5 * time.Second)
	}
}

func cmdTest(explicitToken string) error {
	if _, err := ensureToken(explicitToken); err != nil {
		return err
	}
	logf("querying %s ...", usageURL)
	u, err := fetchUsage(explicitToken)
	if err != nil {
		return err
	}
	fmt.Println("\n=== line that would be sent to the device ===")
	fmt.Println(buildLine(u))
	fmt.Println("\n=== remaining balance ===")
	report := func(name string, w *window) {
		if w == nil || w.Utilization == nil {
			return
		}
		rem := 100 - *w.Utilization
		cd := "?"
		if s, ok := secsToReset(w.ResetsAt); ok {
			cd = fmtCountdown(s)
		}
		fmt.Printf("  %-10s remaining %5.1f%%   resets in %s\n", name, rem, cd)
	}
	report("five_hour", u.FiveHour)
	report("seven_day", u.SevenDay)
	return nil
}

func run(explicitToken, explicitPort string, interval int, once bool) error {
	// Resolve (and, if needed, log in for) a token up front; push() re-reads it
	// each cycle so a refreshed token is picked up without a restart.
	if _, err := ensureToken(explicitToken); err != nil {
		return err
	}
	portName, err := choosePort(explicitPort)
	if err != nil {
		return err
	}
	logf("device port %s, pushing every %ds", portName, interval)

	push := func(p serial.Port) error {
		u, err := fetchUsage(explicitToken) // refreshes the token if it's near expiry / on 401
		if err != nil {
			return err
		}
		line := buildLine(u)
		if line == "{}" {
			logf("no known usage windows in response, skipping")
			return nil
		}
		if _, err := p.Write([]byte(line + "\n")); err != nil {
			return fmt.Errorf("serial write: %w", err)
		}
		logf("sent %s", line)
		return nil
	}

	for {
		p, err := openSerial(portName)
		if err != nil {
			logf("open %s failed: %v (retry in 10s)", portName, err)
			if once {
				return err
			}
			time.Sleep(10 * time.Second)
			continue
		}
		// Remember the working port for next time.
		if c := loadConfig(); c.Port != portName {
			c.Port = portName
			saveConfig(c)
		}
		for {
			if err := push(p); err != nil {
				logf("%v", err)
				if strings.Contains(err.Error(), "serial write") {
					break // reopen the port
				}
			}
			if once {
				p.Close()
				return nil
			}
			time.Sleep(time.Duration(interval) * time.Second)
		}
		p.Close()
		time.Sleep(2 * time.Second)
	}
}

func main() {
	token := flag.String("token", "", "override OAuth token (else config/keychain/CLI credentials)")
	port := flag.String("port", "", "serial port, e.g. COM3 or /dev/ttyACM0 (else config/auto)")
	interval := flag.Int("interval", 300, "poll interval in seconds")
	once := flag.Bool("once", false, "push a single update and exit")
	proxy := flag.String("proxy", "", "HTTP(S) proxy for API calls, e.g. http://127.0.0.1:7897 (else config/env)")
	background := flag.Bool("background", false, "run as a background service: hide console + log to file (used by `enable`)")
	flag.Parse()

	// Resolve the proxy (flag > saved config) and apply it to this process — and
	// thus to the `claude` child too — so calls work without a preset shell env.
	prox := *proxy
	if prox == "" {
		prox = loadConfig().Proxy
	}
	if prox != "" {
		_ = os.Setenv("HTTPS_PROXY", prox)
		_ = os.Setenv("HTTP_PROXY", prox)
	}
	if *background {
		setupBackground()
	}

	var err error
	switch flag.Arg(0) {
	case "ports":
		cmdPorts()
	case "serialtest":
		err = cmdSerialTest(*port)
	case "test":
		err = cmdTest(*token)
	case "refresh":
		// Nudge the CLI to refresh its token now (manual test of the refresh path).
		err = refreshTokenViaCLI()
	case "enable":
		err = cmdEnable(*port, prox, *interval)
	case "disable":
		err = cmdDisable()
	case "status":
		err = cmdStatus()
	default:
		err = run(*token, *port, *interval, *once)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "\n[error]", err)
		os.Exit(1)
	}
}
