package main

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"mime/multipart"
	"net"
	"net/http"
	"net/textproto"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
	"unicode"
)

const (
	userAgent       = "ASA-Firmware-Transfer-Go/1.1.0"
	defaultTimeout  = 45 * time.Second
	defaultReboot   = 75 * time.Second
	defaultInterval = 2 * time.Second
	lookupTimeout   = 20 * time.Second
	maxResponseSize = 1 << 20
	maxFirmwareSize = 16 << 20
	exitSuccess     = 0
	exitOperation   = 1
	exitUsage       = 2
)

var md5LinePattern = regexp.MustCompile(
	`(?i)^([0-9a-f]{32})\s+\*?(.+?)(?:\s+\(compressed\))?\s*$`,
)

var errHelpRequested = errors.New("help requested")

type options struct {
	api           string
	firmware      string
	download      string
	downloadURL   string
	backup        string
	bearer        string
	timeout       time.Duration
	rebootTimeout time.Duration
	pollInterval  time.Duration
	watch         bool
	checkOnly     bool
	resume        bool
	force         bool
	noColor       bool
}

type manifest struct {
	rawMD5        string
	compressedMD5 string
	fileName      string
}

type resolvedEndpoint struct {
	update               *url.URL
	updateInfo           *url.URL
	info                 *url.URL
	download             *url.URL
	downloadUpdateInfo   *url.URL
	downloadInfo         *url.URL
	originalHost         string
	resolvedIP           string
	apiAuthority         string
	downloadOriginalHost string
	downloadResolvedIP   string
	downloadAuthority    string
}

type consoleUI struct {
	color bool
	mu    sync.Mutex
}

type progressReader struct {
	reader  io.Reader
	total   int64
	read    int64
	lastPct int
	ui      *consoleUI
}

func main() {
	os.Exit(run(os.Args[1:]))
}

func run(args []string) int {
	opts, err := parseOptions(args)
	if err != nil {
		if errors.Is(err, errHelpRequested) {
			fmt.Fprintln(os.Stdout, usage())
			return exitSuccess
		}
		fmt.Fprintln(os.Stderr, sanitizeConsole(err.Error()))
		return exitUsage
	}

	ui := &consoleUI{
		color: !opts.noColor &&
			os.Getenv("NO_COLOR") == "" &&
			enableVirtualTerminal(os.Stdout),
	}
	ctx, stop := signal.NotifyContext(
		context.Background(),
		os.Interrupt,
	)
	defer stop()

	var endpoint resolvedEndpoint
	if !opts.checkOnly {
		endpoint, err = resolveConfiguredEndpoint(
			ctx,
			opts.api,
			opts.downloadURL,
		)
		if err != nil {
			ui.fail("Endpoint", err)
			return exitUsage
		}
		if endpoint.originalHost != endpoint.resolvedIP {
			ui.info(
				"DNS",
				fmt.Sprintf(
					"%s → %s (cached for this process)",
					endpoint.originalHost,
					endpoint.resolvedIP,
				),
			)
		}
		if endpoint.downloadOriginalHost != endpoint.originalHost &&
			endpoint.downloadOriginalHost != endpoint.downloadResolvedIP {
			ui.info(
				"Download DNS",
				fmt.Sprintf(
					"%s → %s (cached for this process)",
					endpoint.downloadOriginalHost,
					endpoint.downloadResolvedIP,
				),
			)
		}
	}

	client := newHTTPClient()
	if opts.download != "" {
		err = downloadFirmware(
			ctx,
			client,
			&endpoint,
			opts,
			opts.download,
			opts.resume,
			ui,
		)
		if err != nil {
			if errors.Is(err, context.Canceled) {
				ui.warn("Cancelled", "Download cancelled.")
			} else {
				ui.fail("Download failed", err)
			}
			return exitOperation
		}
		return exitSuccess
	}

	upload := func() (string, error) {
		return uploadFirmware(ctx, client, &endpoint, opts, ui)
	}

	if opts.watch {
		ui.info(
			"Watch",
			"Waiting for a stable firmware and MD5 manifest, then watching for rebuilds.",
		)
		err = watchFirmware(ctx, opts, "", upload, ui)
		if err != nil && !errors.Is(err, context.Canceled) {
			ui.fail("Watcher failed", err)
			return exitOperation
		}
		ui.clearProgress()
		return exitSuccess
	}

	_, err = upload()
	if err != nil {
		if errors.Is(err, context.Canceled) {
			ui.warn("Cancelled", "Upload cancelled.")
		} else {
			ui.fail("Upload failed", err)
		}
		return exitOperation
	}
	return exitSuccess
}

func parseOptions(args []string) (options, error) {
	var opts options
	set := flag.NewFlagSet("ASAFirmwareTransfer", flag.ContinueOnError)
	set.SetOutput(io.Discard)
	set.StringVar(
		&opts.api,
		"api",
		firstEnvironment("ASA_FIRMWARE_API", "UPDATE_API"),
		"OTA update URL (or ASA_FIRMWARE_API/UPDATE_API)",
	)
	set.StringVar(
		&opts.download,
		"download",
		"",
		"download and verify the running firmware to this path",
	)
	set.StringVar(
		&opts.downloadURL,
		"download-url",
		"",
		"explicit firmware download URL",
	)
	set.StringVar(
		&opts.backup,
		"backup",
		"",
		"verified firmware backup path before an upload",
	)
	set.StringVar(
		&opts.bearer,
		"bearer",
		firstEnvironment(
			"UPDATE_BEARER_TOKEN",
			"UPDATE_TOKEN",
			"ASA_FIRMWARE_BEARER_TOKEN",
			"FIRMWARE_BEARER_TOKEN",
		),
		"optional bearer token (prefer the environment variable)",
	)
	set.DurationVar(
		&opts.timeout,
		"timeout",
		defaultTimeout,
		"upload request timeout",
	)
	set.DurationVar(
		&opts.rebootTimeout,
		"reboot-timeout",
		defaultReboot,
		"maximum time to verify the rebooted firmware",
	)
	set.DurationVar(
		&opts.pollInterval,
		"poll-interval",
		defaultInterval,
		"firmware verification poll interval",
	)
	set.BoolVar(
		&opts.watch,
		"watch",
		false,
		"upload now, then upload stable file changes",
	)
	set.BoolVar(
		&opts.checkOnly,
		"check",
		false,
		"validate the firmware and manifest without uploading",
	)
	set.BoolVar(
		&opts.resume,
		"resume",
		false,
		"resume one download from OUTPUT.part with one Range request",
	)
	set.BoolVar(
		&opts.force,
		"force",
		false,
		"upload even when the device already reports the target raw MD5",
	)
	set.BoolVar(&opts.noColor, "no-color", false, "disable ANSI colors")
	if err := set.Parse(args); errors.Is(err, flag.ErrHelp) {
		return options{}, errHelpRequested
	} else if err != nil {
		return options{}, fmt.Errorf("%w\n\n%s", err, usage())
	}
	if opts.timeout < time.Second || opts.timeout > 10*time.Minute {
		return options{}, errors.New("-timeout must be between 1s and 10m")
	}
	if opts.rebootTimeout < time.Second ||
		opts.rebootTimeout > 10*time.Minute {
		return options{}, errors.New(
			"-reboot-timeout must be between 1s and 10m",
		)
	}
	if opts.pollInterval < 100*time.Millisecond ||
		opts.pollInterval > time.Minute {
		return options{}, errors.New(
			"-poll-interval must be between 100ms and 1m",
		)
	}
	opts.api = strings.TrimSpace(opts.api)
	opts.downloadURL = strings.TrimSpace(opts.downloadURL)
	opts.bearer = strings.TrimSpace(opts.bearer)
	if err := validateBearer(opts.bearer); err != nil {
		return options{}, err
	}

	if opts.download != "" {
		if set.NArg() != 0 {
			return options{}, fmt.Errorf(
				"-download takes an output path and no firmware argument\n\n%s",
				usage(),
			)
		}
		if opts.watch || opts.checkOnly || opts.backup != "" || opts.force {
			return options{}, errors.New(
				"-download cannot be combined with -watch, -check, -backup, or -force",
			)
		}
		if opts.api == "" {
			var err error
			opts.api, err = deriveAPIFromDownloadURL(opts.downloadURL)
			if err != nil {
				return options{}, err
			}
		}
		var err error
		opts.download, err = absoluteOutputPath(opts.download)
		if err != nil {
			return options{}, fmt.Errorf("download output: %w", err)
		}
		return opts, nil
	}

	if opts.resume {
		return options{}, errors.New("-resume requires -download OUTPUT")
	}
	if set.NArg() != 1 {
		return options{}, fmt.Errorf(
			"provide exactly one firmware .bin path\n\n%s",
			usage(),
		)
	}
	opts.firmware = set.Arg(0)
	if opts.watch && opts.checkOnly {
		return options{}, errors.New("-watch and -check cannot be combined")
	}
	if opts.force && opts.checkOnly {
		return options{}, errors.New("-force has no meaning with -check")
	}
	if opts.backup != "" && opts.checkOnly {
		return options{}, errors.New("-backup cannot be combined with -check")
	}
	if opts.downloadURL != "" && opts.backup == "" {
		return options{}, errors.New(
			"-download-url requires -download OUTPUT or -backup FILE",
		)
	}
	if opts.api == "" && !opts.checkOnly {
		return options{}, errors.New(
			"set ASA_FIRMWARE_API/UPDATE_API or pass -api http://device.local/update",
		)
	}
	if opts.backup != "" {
		var err error
		opts.backup, err = absoluteOutputPath(opts.backup)
		if err != nil {
			return options{}, fmt.Errorf("backup output: %w", err)
		}
	}

	absolute, err := filepath.Abs(opts.firmware)
	if err != nil {
		return options{}, fmt.Errorf("resolve firmware path: %w", err)
	}
	info, err := os.Stat(absolute)
	if err != nil {
		if opts.watch && errors.Is(err, os.ErrNotExist) {
			opts.firmware = absolute
			return opts, nil
		}
		return options{}, fmt.Errorf("inspect firmware: %w", err)
	}
	if !info.Mode().IsRegular() {
		return options{}, errors.New("firmware path is not a regular file")
	}
	opts.firmware = absolute
	if opts.backup != "" && strings.EqualFold(opts.backup, opts.firmware) {
		return options{}, errors.New(
			"-backup must not overwrite the selected upload firmware",
		)
	}
	return opts, nil
}

func usage() string {
	return `Usage:
  ASAFirmwareTransfer [options] firmware.bin
  ASAFirmwareTransfer -download OUTPUT [options]

Options:
  -api URL              OTA endpoint; ASA_FIRMWARE_API or UPDATE_API
  -check                Validate file and MD5 manifest without uploading
  -watch                Wait for a stable build, upload it, then watch rebuilds
  -force                Upload even if the raw firmware MD5 already matches
  -backup FILE          Verified, atomic backup before upload
  -download FILE        Verified, atomic firmware download
  -download-url URL     Override the derived /firmware/download endpoint
  -resume               Resume FILE.part with one bounded Range request
  -bearer TOKEN         Optional bearer token; UPDATE_BEARER_TOKEN is supported
  -timeout 45s          Upload timeout
  -reboot-timeout 75s   Verification deadline after upload
  -poll-interval 2s     Verification polling interval
  -no-color             Disable ANSI colors

Watch mode tolerates build-time deletion/recreation, waits one second for both
files to become content-stable, and pauses while firmware.bin.gz or the sibling
~local.h build marker exists. Uploads are skipped when /update/info or /info
already reports the manifest's raw MD5 unless -force is used. Successful uploads
always require a post-reboot raw-hash match.

Download defaults to /firmware/download on the -api origin. It requires exact
length and MD5 verification, writes OUTPUT.part, then atomically replaces OUTPUT.
Ctrl+C cancels active transfers without replaying work.`
}

func resolveEndpoint(
	ctx context.Context,
	raw string,
) (resolvedEndpoint, error) {
	return resolveConfiguredEndpoint(ctx, raw, "")
}

func resolveConfiguredEndpoint(
	ctx context.Context,
	rawAPI string,
	rawDownload string,
) (resolvedEndpoint, error) {
	update, originalHost, resolvedIP, authority, err := resolveHTTPURL(
		ctx,
		rawAPI,
	)
	if err != nil {
		return resolvedEndpoint{}, fmt.Errorf("update endpoint: %w", err)
	}
	updateInfo := update.ResolveReference(&url.URL{Path: "/update/info"})
	infoURL := update.ResolveReference(&url.URL{Path: "/info"})
	downloadURL := update.ResolveReference(
		&url.URL{Path: "/firmware/download"},
	)
	downloadOriginalHost := originalHost
	downloadResolvedIP := resolvedIP
	downloadAuthority := authority
	downloadUpdateInfo := updateInfo
	downloadInfo := infoURL

	if strings.TrimSpace(rawDownload) != "" {
		downloadURL, downloadOriginalHost, downloadResolvedIP,
			downloadAuthority, err = resolveHTTPURL(ctx, rawDownload)
		if err != nil {
			return resolvedEndpoint{}, fmt.Errorf(
				"download endpoint: %w",
				err,
			)
		}
		downloadUpdateInfo = downloadURL.ResolveReference(
			&url.URL{Path: "/update/info"},
		)
		downloadInfo = downloadURL.ResolveReference(
			&url.URL{Path: "/info"},
		)
	}

	return resolvedEndpoint{
		update:               update,
		updateInfo:           updateInfo,
		info:                 infoURL,
		download:             downloadURL,
		downloadUpdateInfo:   downloadUpdateInfo,
		downloadInfo:         downloadInfo,
		originalHost:         originalHost,
		resolvedIP:           resolvedIP,
		apiAuthority:         authority,
		downloadOriginalHost: downloadOriginalHost,
		downloadResolvedIP:   downloadResolvedIP,
		downloadAuthority:    downloadAuthority,
	}, nil
}

func resolveHTTPURL(
	ctx context.Context,
	raw string,
) (*url.URL, string, string, string, error) {
	parsed, err := url.Parse(strings.TrimSpace(raw))
	if err != nil {
		return nil, "", "", "", fmt.Errorf("parse URL: %w", err)
	}
	if parsed.Scheme != "http" {
		return nil, "", "", "", errors.New(
			"URL must use http:// for the local firmware service",
		)
	}
	if parsed.User != nil ||
		parsed.Hostname() == "" ||
		parsed.Fragment != "" {
		return nil, "", "", "", errors.New(
			"URL is not a safe absolute HTTP URL",
		)
	}

	originalHost := parsed.Hostname()
	resolvedIP := originalHost
	if net.ParseIP(originalHost) == nil {
		// A cold Windows mDNS lookup can legitimately take more than five
		// seconds, especially through a VPN adapter. Resolve once with a
		// realistic bound, then keep using the pinned address.
		lookupCtx, cancel := context.WithTimeout(ctx, lookupTimeout)
		defer cancel()
		addresses, lookupErr := net.DefaultResolver.LookupIPAddr(
			lookupCtx,
			originalHost,
		)
		if lookupErr != nil {
			return nil, "", "", "", fmt.Errorf(
				"resolve %s: %w",
				originalHost,
				lookupErr,
			)
		}
		if len(addresses) == 0 {
			return nil, "", "", "", fmt.Errorf(
				"resolve %s: no addresses returned",
				originalHost,
			)
		}
		resolvedIP = addresses[0].IP.String()
		for _, address := range addresses {
			if address.IP.To4() != nil {
				resolvedIP = address.IP.String()
				break
			}
		}
	}

	resolved := *parsed
	if port := parsed.Port(); port != "" {
		resolved.Host = net.JoinHostPort(resolvedIP, port)
	} else if strings.Contains(resolvedIP, ":") {
		resolved.Host = "[" + resolvedIP + "]"
	} else {
		resolved.Host = resolvedIP
	}
	return &resolved, originalHost, resolvedIP, parsed.Host, nil
}

func newHTTPClient() *http.Client {
	transport := http.DefaultTransport.(*http.Transport).Clone()
	// Firmware updates are strictly local. Never send them through a proxy.
	transport.Proxy = nil
	return &http.Client{
		Transport: transport,
		CheckRedirect: func(
			request *http.Request,
			via []*http.Request,
		) error {
			return http.ErrUseLastResponse
		},
	}
}

func uploadFirmware(
	parent context.Context,
	client *http.Client,
	endpoint *resolvedEndpoint,
	opts options,
	ui *consoleUI,
) (string, error) {
	if endpoint == nil {
		return "", errors.New("missing resolved update endpoint")
	}

	m, err := readManifest(opts.firmware + ".md5")
	if err != nil {
		return "", err
	}
	if !strings.EqualFold(m.fileName, filepath.Base(opts.firmware)) {
		return "", fmt.Errorf(
			"manifest names %q, but selected firmware is %q",
			m.fileName,
			filepath.Base(opts.firmware),
		)
	}
	validated, err := validateFirmware(opts.firmware, m)
	if err != nil {
		return "", err
	}
	ui.success(
		"Validated",
		fmt.Sprintf(
			"%s · %d compressed / %d raw bytes · MD5 %s",
			filepath.Base(opts.firmware),
			validated.compressedSize,
			validated.rawSize,
			validated.compressedMD5,
		),
	)
	if opts.checkOnly {
		ui.success(
			"Check only",
			"Firmware and raw/compressed MD5 entries are valid; nothing uploaded.",
		)
		return m.compressedMD5, nil
	}

	if !opts.force {
		remote, preflightErr := fetchRemoteFirmwareInfo(
			parent,
			client,
			endpoint,
			opts,
			false,
		)
		if preflightErr != nil {
			ui.warn(
				"Preflight",
				"Could not read the current firmware hash; upload will continue.",
			)
		} else if strings.EqualFold(remote.hash, m.rawMD5) {
			ui.success(
				"Already current",
				"Device raw firmware MD5 already matches; upload skipped.",
			)
			return m.compressedMD5, nil
		}
	}

	if opts.backup != "" {
		ui.info(
			"Backup",
			"Saving and verifying the running firmware before upload.",
		)
		if err := downloadFirmware(
			parent,
			client,
			endpoint,
			opts,
			opts.backup,
			false,
			ui,
		); err != nil {
			return "", fmt.Errorf("pre-upload backup: %w", err)
		}
	}

	payload, contentType, err := buildMultipart(
		opts.firmware,
		m.compressedMD5,
	)
	if err != nil {
		return "", err
	}

	status, responseBody, err := postFirmware(
		parent,
		client,
		endpoint,
		opts,
		ui,
		payload,
		contentType,
	)
	if err != nil && canRefreshEndpoint(*endpoint) && isDialFailure(err) {
		ui.warn(
			"DNS retry",
			"Cached address failed; resolving the device once more.",
		)
		refreshed, refreshErr := resolveConfiguredEndpoint(
			parent,
			opts.api,
			opts.downloadURL,
		)
		if refreshErr != nil {
			return "", errors.Join(err, refreshErr)
		}
		*endpoint = refreshed
		status, responseBody, err = postFirmware(
			parent,
			client,
			endpoint,
			opts,
			ui,
			payload,
			contentType,
		)
	}
	if err != nil {
		return "", err
	}
	if status < 200 || status >= 300 {
		return "", fmt.Errorf(
			"updater returned HTTP %d: %s",
			status,
			sanitizeConsole(responseBody),
		)
	}
	if responseBody != "ok!" {
		return "", fmt.Errorf(
			"updater returned unexpected body %q instead of exactly %q",
			sanitizeConsole(responseBody),
			"ok!",
		)
	}
	ui.success("Accepted", "Device accepted the firmware and is rebooting.")
	if err := verifyReboot(
		parent,
		client,
		endpoint,
		m.rawMD5,
		opts,
		ui,
	); err != nil {
		return "", err
	}
	return m.compressedMD5, nil
}

func postFirmware(
	parent context.Context,
	client *http.Client,
	endpoint *resolvedEndpoint,
	opts options,
	ui *consoleUI,
	payload []byte,
	contentType string,
) (int, string, error) {
	requestCtx, cancel := context.WithTimeout(parent, opts.timeout)
	defer cancel()
	progress := &progressReader{
		reader:  bytes.NewReader(payload),
		total:   int64(len(payload)),
		lastPct: -1,
		ui:      ui,
	}
	request, err := http.NewRequestWithContext(
		requestCtx,
		http.MethodPost,
		endpoint.update.String(),
		progress,
	)
	if err != nil {
		return 0, "", fmt.Errorf("create upload request: %w", err)
	}
	request.Header.Set("Content-Type", contentType)
	setRequestHeaders(request, opts, endpoint.apiAuthority)
	request.ContentLength = int64(len(payload))
	ui.info("Upload", "Sending firmware to "+endpoint.update.String())
	response, err := client.Do(request)
	if err != nil {
		return 0, "", fmt.Errorf("POST update: %w", err)
	}
	body, readErr := readLimited(response.Body)
	closeErr := response.Body.Close()
	if readErr != nil || closeErr != nil {
		return 0, "", errors.Join(readErr, closeErr)
	}
	return response.StatusCode, body, nil
}

func readManifest(path string) (manifest, error) {
	file, err := os.Open(path)
	if err != nil {
		return manifest{}, fmt.Errorf("open MD5 manifest: %w", err)
	}
	defer file.Close()

	var result manifest
	scanner := bufio.NewScanner(file)
	lineNumber := 0
	recordCount := 0
	for scanner.Scan() {
		lineNumber++
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			return manifest{}, errors.New(
				"MD5 manifest must contain exactly two non-empty records",
			)
		}
		recordCount++
		if recordCount > 2 {
			return manifest{}, errors.New(
				"MD5 manifest must contain exactly two non-empty records",
			)
		}
		match := md5LinePattern.FindStringSubmatch(line)
		if match == nil {
			return manifest{}, fmt.Errorf(
				"MD5 manifest line %d is invalid",
				lineNumber,
			)
		}
		hash := strings.ToLower(match[1])
		compressed := strings.HasSuffix(
			strings.ToLower(line),
			"(compressed)",
		)
		name := strings.TrimSpace(match[2])
		if strings.HasSuffix(strings.ToLower(name), "(compressed)") {
			name = strings.TrimSpace(
				name[:len(name)-len("(compressed)")],
			)
		}
		name = filepath.Base(strings.TrimPrefix(name, "*"))
		if result.fileName != "" &&
			!strings.EqualFold(result.fileName, name) {
			return manifest{}, errors.New(
				"MD5 manifest entries name different firmware files",
			)
		}
		result.fileName = name
		target := &result.rawMD5
		if compressed {
			target = &result.compressedMD5
		}
		if *target != "" {
			return manifest{}, errors.New(
				"MD5 manifest contains a duplicate raw or compressed entry",
			)
		}
		*target = hash
	}
	if err := scanner.Err(); err != nil {
		return manifest{}, fmt.Errorf("read MD5 manifest: %w", err)
	}
	if recordCount != 2 ||
		result.fileName == "" ||
		result.rawMD5 == "" ||
		result.compressedMD5 == "" {
		return manifest{}, errors.New(
			"MD5 manifest must contain exactly raw and '(compressed)' entries",
		)
	}
	return result, nil
}

func hashFile(path string) (string, int64, error) {
	file, err := os.Open(path)
	if err != nil {
		return "", 0, fmt.Errorf("open firmware: %w", err)
	}
	defer file.Close()
	hash := md5.New()
	size, err := io.Copy(hash, file)
	if err != nil {
		return "", 0, fmt.Errorf("hash firmware: %w", err)
	}
	return hex.EncodeToString(hash.Sum(nil)), size, nil
}

type validatedFirmware struct {
	compressedMD5  string
	rawMD5         string
	compressedSize int64
	rawSize        int64
}

func validateFirmware(
	path string,
	expected manifest,
) (validatedFirmware, error) {
	compressedMD5, compressedSize, err := hashFile(path)
	if err != nil {
		return validatedFirmware{}, err
	}
	if compressedSize <= 0 || compressedSize > maxFirmwareSize {
		return validatedFirmware{}, fmt.Errorf(
			"compressed firmware must be between 1 and %d bytes",
			maxFirmwareSize,
		)
	}
	if !strings.EqualFold(compressedMD5, expected.compressedMD5) {
		return validatedFirmware{}, fmt.Errorf(
			"compressed firmware MD5 mismatch: expected %s, calculated %s",
			expected.compressedMD5,
			compressedMD5,
		)
	}

	file, err := os.Open(path)
	if err != nil {
		return validatedFirmware{}, fmt.Errorf("open gzip firmware: %w", err)
	}
	defer file.Close()

	raw, err := gzip.NewReader(file)
	if err != nil {
		return validatedFirmware{}, fmt.Errorf(
			"firmware is not a valid gzip stream: %w",
			err,
		)
	}
	rawHash := md5.New()
	rawSize, copyErr := io.Copy(
		rawHash,
		io.LimitReader(raw, maxFirmwareSize+1),
	)
	closeErr := raw.Close()
	if copyErr != nil || closeErr != nil {
		return validatedFirmware{}, fmt.Errorf(
			"decompress firmware: %w",
			errors.Join(copyErr, closeErr),
		)
	}
	if rawSize <= 0 || rawSize > maxFirmwareSize {
		return validatedFirmware{}, fmt.Errorf(
			"raw firmware must be between 1 and %d bytes",
			maxFirmwareSize,
		)
	}

	rawMD5 := hex.EncodeToString(rawHash.Sum(nil))
	if !strings.EqualFold(rawMD5, expected.rawMD5) {
		return validatedFirmware{}, fmt.Errorf(
			"raw firmware MD5 mismatch: expected %s, calculated %s",
			expected.rawMD5,
			rawMD5,
		)
	}
	return validatedFirmware{
		compressedMD5:  compressedMD5,
		rawMD5:         rawMD5,
		compressedSize: compressedSize,
		rawSize:        rawSize,
	}, nil
}

func buildMultipart(
	firmwarePath string,
	compressedMD5 string,
) ([]byte, string, error) {
	file, err := os.Open(firmwarePath)
	if err != nil {
		return nil, "", fmt.Errorf("open firmware: %w", err)
	}
	defer file.Close()

	var payload bytes.Buffer
	writer := multipart.NewWriter(&payload)
	if err := writer.WriteField("MD5", compressedMD5); err != nil {
		return nil, "", fmt.Errorf("write MD5 multipart field: %w", err)
	}
	header := make(textproto.MIMEHeader)
	header.Set("Content-Disposition", `form-data; name="firmware"`)
	header.Set("Content-Type", "application/octet-stream")
	part, err := writer.CreatePart(header)
	if err != nil {
		return nil, "", fmt.Errorf("create firmware multipart field: %w", err)
	}
	if _, err := io.Copy(part, file); err != nil {
		return nil, "", fmt.Errorf("write firmware multipart field: %w", err)
	}
	if err := writer.Close(); err != nil {
		return nil, "", fmt.Errorf("finish multipart body: %w", err)
	}
	return payload.Bytes(), writer.FormDataContentType(), nil
}

func verifyReboot(
	parent context.Context,
	client *http.Client,
	endpoint *resolvedEndpoint,
	expectedRawMD5 string,
	opts options,
	ui *consoleUI,
) error {
	if endpoint == nil {
		return errors.New("missing resolved information endpoint")
	}

	verifyCtx, stopVerification := context.WithTimeout(
		parent,
		opts.rebootTimeout,
	)
	defer stopVerification()
	attempt := 0
	var last error
	refreshed := false
	for verifyCtx.Err() == nil {
		attempt++
		requestCtx, cancel := context.WithTimeout(
			verifyCtx,
			3*time.Second,
		)
		remote, err := fetchRemoteFirmwareInfo(
			requestCtx,
			client,
			endpoint,
			opts,
			false,
		)
		if err == nil {
			actual := strings.ToLower(remote.hash)
			if strings.EqualFold(actual, expectedRawMD5) {
				cancel()
				ui.success(
					"Verified",
					fmt.Sprintf(
						"Firmware hash %s; build %s; uptime %s",
						actual,
						sanitizeConsole(valueOr(
							findInfoValue(
								remote.values,
								"build",
								"date",
							),
							"not reported",
						)),
						sanitizeConsole(valueOr(
							findInfoValue(remote.values, "uptime"),
							"not reported",
						)),
					),
				)
				return nil
			}
			err = fmt.Errorf(
				"device reports firmware hash %q",
				sanitizeConsole(actual),
			)
		}
		cancel()
		if err != nil &&
			!refreshed &&
			canRefreshEndpoint(*endpoint) &&
			isDialFailure(err) {
			refreshed = true
			ui.warn(
				"DNS retry",
				"Cached address failed during reboot; resolving once more.",
			)
			var refreshedEndpoint resolvedEndpoint
			refreshedEndpoint, refreshErr := resolveConfiguredEndpoint(
				verifyCtx,
				opts.api,
				opts.downloadURL,
			)
			if refreshErr == nil {
				*endpoint = refreshedEndpoint
				continue
			}
			err = errors.Join(err, refreshErr)
		}
		last = err
		ui.progress(
			fmt.Sprintf(
				"Waiting for reboot and hash match · attempt %d",
				attempt,
			),
		)
		timer := time.NewTimer(opts.pollInterval)
		select {
		case <-verifyCtx.Done():
			timer.Stop()
			break
		case <-timer.C:
			continue
		}
		break
	}
	ui.clearProgress()
	if parent.Err() != nil {
		return parent.Err()
	}
	if last == nil {
		last = errors.New("device did not report the expected firmware hash")
	}
	return fmt.Errorf(
		"firmware verification timed out after %s: %w",
		opts.rebootTimeout,
		last,
	)
}

func readLimited(body io.Reader) (string, error) {
	data, err := io.ReadAll(io.LimitReader(body, maxResponseSize+1))
	if err != nil {
		return "", err
	}
	if len(data) > maxResponseSize {
		return "", errors.New("HTTP response exceeded 1 MiB")
	}
	return string(data), nil
}

func parseInfo(body string) map[string]string {
	values := make(map[string]string)
	trimmed := strings.TrimSpace(body)
	if strings.HasPrefix(trimmed, "{") {
		var root any
		if json.Unmarshal([]byte(trimmed), &root) == nil {
			flattenInfoJSON(root, "", values)
			return values
		}
	}

	scanner := bufio.NewScanner(strings.NewReader(body))
	for scanner.Scan() {
		line := scanner.Text()
		index := strings.IndexByte(line, ':')
		if index <= 0 {
			continue
		}
		key := strings.ToLower(strings.TrimSpace(line[:index]))
		value := strings.TrimSpace(line[index+1:])
		if key != "" && value != "" {
			values[key] = value
		}
	}
	return values
}

func flattenInfoJSON(
	value any,
	prefix string,
	values map[string]string,
) {
	switch typed := value.(type) {
	case map[string]any:
		for key, child := range typed {
			fullKey := key
			if prefix != "" {
				fullKey = prefix + "." + key
			}
			flattenInfoJSON(child, fullKey, values)
		}
	case string:
		if prefix != "" {
			values[strings.ToLower(prefix)] = strings.TrimSpace(typed)
		}
	case float64, bool:
		if prefix != "" {
			values[strings.ToLower(prefix)] = fmt.Sprint(typed)
		}
	}
}

func findInfoValue(values map[string]string, candidates ...string) string {
	for _, candidate := range candidates {
		for key, value := range values {
			if normalizeInfoKey(key) == candidate {
				return strings.TrimSpace(value)
			}
		}
	}
	for _, candidate := range candidates {
		for key, value := range values {
			if strings.HasSuffix(normalizeInfoKey(key), candidate) {
				return strings.TrimSpace(value)
			}
		}
	}
	return ""
}

func normalizeInfoKey(value string) string {
	var normalized strings.Builder
	for _, character := range strings.ToLower(value) {
		if unicode.IsLetter(character) || unicode.IsDigit(character) {
			normalized.WriteRune(character)
		}
	}
	return normalized.String()
}

func sanitizeConsole(value string) string {
	value = strings.Map(
		func(character rune) rune {
			if unicode.IsControl(character) {
				return ' '
			}
			return character
		},
		value,
	)
	value = strings.Join(strings.Fields(value), " ")
	characters := []rune(value)
	if len(characters) > 512 {
		value = string(characters[:512]) + "…"
	}
	return value
}

func canRefreshEndpoint(endpoint resolvedEndpoint) bool {
	return endpoint.originalHost != "" &&
		net.ParseIP(endpoint.originalHost) == nil
}

func isDialFailure(err error) bool {
	var operationError *net.OpError
	return errors.As(err, &operationError) &&
		strings.EqualFold(operationError.Op, "dial")
}

func isCharacterDevice(file *os.File) bool {
	if file == nil {
		return false
	}
	info, err := file.Stat()
	return err == nil && info.Mode()&os.ModeCharDevice != 0
}

func (reader *progressReader) Read(buffer []byte) (int, error) {
	n, err := reader.reader.Read(buffer)
	reader.read += int64(n)
	if reader.total > 0 {
		percent := int(reader.read * 100 / reader.total)
		if percent != reader.lastPct {
			reader.lastPct = percent
			reader.ui.progress(fmt.Sprintf("Uploading · %d%%", percent))
		}
	}
	return n, err
}

func (ui *consoleUI) style(code, value string) string {
	if !ui.color {
		return value
	}
	return "\x1b[" + code + "m" + value + "\x1b[0m"
}

func (ui *consoleUI) line(icon, label, message, color string) {
	ui.mu.Lock()
	defer ui.mu.Unlock()
	label = sanitizeConsole(label)
	message = sanitizeConsole(message)
	prefix := ""
	if ui.color {
		prefix = "\r\x1b[K"
	}
	fmt.Printf(
		"%s%s %s %s\n",
		prefix,
		icon,
		ui.style(color+";1", label),
		message,
	)
}

func (ui *consoleUI) info(label, message string) {
	ui.line("ℹ️", label, message, "36")
}

func (ui *consoleUI) success(label, message string) {
	ui.line("✅", label, message, "32")
}

func (ui *consoleUI) warn(label, message string) {
	ui.line("⚠️", label, message, "33")
}

func (ui *consoleUI) fail(label string, err error) {
	ui.line("❌", label, err.Error(), "31")
}

func (ui *consoleUI) progress(message string) {
	ui.mu.Lock()
	defer ui.mu.Unlock()
	message = sanitizeConsole(message)
	if !ui.color {
		fmt.Printf("⏳ %s\n", message)
		return
	}
	fmt.Printf(
		"\r\x1b[K⏳ %s",
		ui.style("33", message),
	)
}

func (ui *consoleUI) clearProgress() {
	ui.mu.Lock()
	defer ui.mu.Unlock()
	if ui.color {
		fmt.Print("\r\x1b[K")
	}
}

func valueOr(value, fallback string) string {
	if strings.TrimSpace(value) == "" {
		return fallback
	}
	return value
}
