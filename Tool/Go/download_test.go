package main

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/md5"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestDownloadFirmwareVerifiesHeadersAndAtomicallyReplaces(t *testing.T) {
	t.Parallel()
	firmware := []byte("authoritative running firmware")
	rawMD5 := md5Hex(firmware)
	token := "test-secret"
	var metadataRequests atomic.Int32
	var downloadRequests atomic.Int32

	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			if request.Header.Get("Authorization") != "Bearer "+token {
				http.Error(response, "missing auth", http.StatusUnauthorized)
				return
			}
			switch request.URL.Path {
			case "/update/info":
				metadataRequests.Add(1)
				_, _ = fmt.Fprintf(
					response,
					`{"hash":%q,"size":%d}`,
					rawMD5,
					len(firmware),
				)
			case "/firmware/download":
				downloadRequests.Add(1)
				setFirmwareHeaders(response, firmware, rawMD5)
				_, _ = response.Write(firmware)
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	directory := t.TempDir()
	output := filepath.Join(directory, "backup.bin")
	if err := os.WriteFile(output, []byte("old"), 0o600); err != nil {
		t.Fatal(err)
	}
	endpoint, err := resolveEndpoint(
		context.Background(),
		server.URL+"/update",
	)
	if err != nil {
		t.Fatal(err)
	}
	opts := testNetworkOptions(server.URL + "/update")
	opts.bearer = token
	err = downloadFirmware(
		context.Background(),
		newHTTPClient(),
		&endpoint,
		opts,
		output,
		false,
		&consoleUI{},
	)
	if err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(output)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(firmware) {
		t.Fatalf("downloaded bytes = %q", got)
	}
	if _, err := os.Stat(output + ".part"); !os.IsNotExist(err) {
		t.Fatalf(".part still exists: %v", err)
	}
	if metadataRequests.Load() != 1 || downloadRequests.Load() != 1 {
		t.Fatalf(
			"requests: metadata=%d download=%d",
			metadataRequests.Load(),
			downloadRequests.Load(),
		)
	}
}

func TestDownloadFirmwareResumesExactlyOnce(t *testing.T) {
	t.Parallel()
	firmware := []byte("firmware with a resumable second half")
	rawMD5 := md5Hex(firmware)
	prefixLength := 13
	var rangeRequests atomic.Int32

	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			switch request.URL.Path {
			case "/update/info":
				_, _ = fmt.Fprintf(
					response,
					`{"hash":%q,"size":%d}`,
					rawMD5,
					len(firmware),
				)
			case "/firmware/download":
				rangeRequests.Add(1)
				wantRange := fmt.Sprintf("bytes=%d-", prefixLength)
				if request.Header.Get("Range") != wantRange {
					t.Errorf(
						"Range = %q, want %q",
						request.Header.Get("Range"),
						wantRange,
					)
				}
				suffix := firmware[prefixLength:]
				response.Header().Set(
					"Content-Length",
					strconvItoa(len(suffix)),
				)
				response.Header().Set(
					"Content-Range",
					fmt.Sprintf(
						"bytes %d-%d/%d",
						prefixLength,
						len(firmware)-1,
						len(firmware),
					),
				)
				response.Header().Set(
					"Content-MD5",
					contentMD5(suffix),
				)
				response.Header().Set("X-Firmware-MD5", rawMD5)
				response.Header().Set("ETag", `"`+rawMD5+`"`)
				response.WriteHeader(http.StatusPartialContent)
				_, _ = response.Write(suffix)
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	output := filepath.Join(t.TempDir(), "resumed.bin")
	if err := os.WriteFile(
		output+".part",
		firmware[:prefixLength],
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	endpoint, err := resolveEndpoint(
		context.Background(),
		server.URL+"/update",
	)
	if err != nil {
		t.Fatal(err)
	}
	err = downloadFirmware(
		context.Background(),
		newHTTPClient(),
		&endpoint,
		testNetworkOptions(server.URL+"/update"),
		output,
		true,
		&consoleUI{},
	)
	if err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(output)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(firmware) {
		t.Fatalf("resumed bytes = %q", got)
	}
	if rangeRequests.Load() != 1 {
		t.Fatalf("Range request count = %d, want 1", rangeRequests.Load())
	}
}

func TestDownloadFirmwareFallsBackToPlainInfo(t *testing.T) {
	t.Parallel()
	firmware := []byte("firmware verified through plain info")
	rawMD5 := md5Hex(firmware)
	var updateInfoRequests atomic.Int32
	var plainInfoRequests atomic.Int32

	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			switch request.URL.Path {
			case "/update/info":
				updateInfoRequests.Add(1)
				http.NotFound(response, request)
			case "/info":
				plainInfoRequests.Add(1)
				_, _ = fmt.Fprintf(
					response,
					"firmware hash: %s\n",
					rawMD5,
				)
			case "/firmware/download":
				response.Header().Set(
					"Content-Length",
					strconvItoa(len(firmware)),
				)
				_, _ = response.Write(firmware)
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	output := filepath.Join(t.TempDir(), "fallback.bin")
	endpoint, err := resolveEndpoint(
		context.Background(),
		server.URL+"/update",
	)
	if err != nil {
		t.Fatal(err)
	}
	err = downloadFirmware(
		context.Background(),
		newHTTPClient(),
		&endpoint,
		testNetworkOptions(server.URL+"/update"),
		output,
		false,
		&consoleUI{},
	)
	if err != nil {
		t.Fatal(err)
	}
	if updateInfoRequests.Load() != 1 || plainInfoRequests.Load() != 1 {
		t.Fatalf(
			"fallback requests: update=%d plain=%d",
			updateInfoRequests.Load(),
			plainInfoRequests.Load(),
		)
	}
}

func TestDownloadMismatchKeepsPartAndExistingDestination(t *testing.T) {
	t.Parallel()
	firmware := []byte("firmware with a deliberately wrong header")
	rawMD5 := md5Hex(firmware)
	wrongMD5 := "ffffffffffffffffffffffffffffffff"

	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			switch request.URL.Path {
			case "/update/info":
				_, _ = fmt.Fprintf(
					response,
					`{"hash":%q,"size":%d}`,
					rawMD5,
					len(firmware),
				)
			case "/firmware/download":
				response.Header().Set(
					"Content-Length",
					strconvItoa(len(firmware)),
				)
				response.Header().Set("Content-MD5", wrongMD5)
				_, _ = response.Write(firmware)
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	output := filepath.Join(t.TempDir(), "protected.bin")
	if err := os.WriteFile(output, []byte("keep me"), 0o600); err != nil {
		t.Fatal(err)
	}
	endpoint, err := resolveEndpoint(
		context.Background(),
		server.URL+"/update",
	)
	if err != nil {
		t.Fatal(err)
	}
	err = downloadFirmware(
		context.Background(),
		newHTTPClient(),
		&endpoint,
		testNetworkOptions(server.URL+"/update"),
		output,
		false,
		&consoleUI{},
	)
	if err == nil || !strings.Contains(err.Error(), "Content-MD5 mismatch") {
		t.Fatalf("download error = %v", err)
	}
	got, readErr := os.ReadFile(output)
	if readErr != nil {
		t.Fatal(readErr)
	}
	if string(got) != "keep me" {
		t.Fatalf("destination was replaced with %q", got)
	}
	part, readErr := os.ReadFile(output + ".part")
	if readErr != nil {
		t.Fatal(readErr)
	}
	if string(part) != string(firmware) {
		t.Fatalf("retained .part = %q", part)
	}
}

func TestUploadPreflightSkipsIdenticalRawFirmware(t *testing.T) {
	t.Parallel()
	directory := t.TempDir()
	firmwarePath, rawMD5, compressedMD5 := writeTransferBuild(
		t,
		directory,
		[]byte("compressed upload"),
	)
	var posts atomic.Int32
	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			switch request.URL.Path {
			case "/update/info":
				_, _ = fmt.Fprintf(
					response,
					`{"hash":%q,"size":123}`,
					rawMD5,
				)
			case "/update":
				posts.Add(1)
				_, _ = io.WriteString(response, "ok!")
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	endpoint, err := resolveEndpoint(
		context.Background(),
		server.URL+"/update",
	)
	if err != nil {
		t.Fatal(err)
	}
	opts := testNetworkOptions(server.URL + "/update")
	opts.firmware = firmwarePath
	got, err := uploadFirmware(
		context.Background(),
		newHTTPClient(),
		&endpoint,
		opts,
		&consoleUI{},
	)
	if err != nil {
		t.Fatal(err)
	}
	if got != compressedMD5 {
		t.Fatalf("returned MD5 = %q, want %q", got, compressedMD5)
	}
	if posts.Load() != 0 {
		t.Fatalf("POST count = %d, want 0", posts.Load())
	}
}

func TestUploadCreatesVerifiedBackupBeforePosting(t *testing.T) {
	t.Parallel()
	directory := t.TempDir()
	uploadPath, targetRawMD5, _ := writeTransferBuild(
		t,
		directory,
		[]byte("new compressed upload"),
	)
	runningFirmware := []byte("old raw running firmware")
	runningMD5 := md5Hex(runningFirmware)
	backupPath := filepath.Join(directory, "before.bin")
	var posted atomic.Bool

	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			switch request.URL.Path {
			case "/update/info":
				hash := runningMD5
				size := len(runningFirmware)
				if posted.Load() {
					hash = targetRawMD5
					size = 456
				}
				_, _ = fmt.Fprintf(
					response,
					`{"hash":%q,"size":%d}`,
					hash,
					size,
				)
			case "/firmware/download":
				setFirmwareHeaders(
					response,
					runningFirmware,
					runningMD5,
				)
				_, _ = response.Write(runningFirmware)
			case "/update":
				backup, err := os.ReadFile(backupPath)
				if err != nil ||
					string(backup) != string(runningFirmware) {
					t.Errorf("backup before POST = %q, %v", backup, err)
				}
				posted.Store(true)
				_, _ = io.WriteString(response, "ok!")
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	endpoint, err := resolveEndpoint(
		context.Background(),
		server.URL+"/update",
	)
	if err != nil {
		t.Fatal(err)
	}
	opts := testNetworkOptions(server.URL + "/update")
	opts.firmware = uploadPath
	opts.backup = backupPath
	opts.pollInterval = time.Millisecond
	_, err = uploadFirmware(
		context.Background(),
		newHTTPClient(),
		&endpoint,
		opts,
		&consoleUI{},
	)
	if err != nil {
		t.Fatal(err)
	}
	if !posted.Load() {
		t.Fatal("firmware was not posted")
	}
}

func TestParseDownloadOptionsDerivesAPI(t *testing.T) {
	t.Parallel()
	output := filepath.Join(t.TempDir(), "download.bin")
	opts, err := parseOptions([]string{
		"-download",
		output,
		"-download-url",
		"http://127.0.0.1:8080/custom-download",
		"-resume",
	})
	if err != nil {
		t.Fatal(err)
	}
	if opts.api != "http://127.0.0.1:8080/update" ||
		opts.download != output ||
		!opts.resume {
		t.Fatalf("download options = %+v", opts)
	}
}

func testNetworkOptions(api string) options {
	return options{
		api:           api,
		timeout:       5 * time.Second,
		rebootTimeout: 5 * time.Second,
		pollInterval:  time.Millisecond,
		noColor:       true,
	}
}

func writeTransferBuild(
	t *testing.T,
	directory string,
	raw []byte,
) (string, string, string) {
	t.Helper()
	path := filepath.Join(directory, "firmware.bin")
	compressed := gzipBytes(t, raw)
	if err := os.WriteFile(path, compressed, 0o600); err != nil {
		t.Fatal(err)
	}
	rawMD5 := md5Hex(raw)
	compressedMD5 := md5Hex(compressed)
	manifest := fmt.Sprintf(
		"%s *firmware.bin\n%s *firmware.bin (compressed)\n",
		rawMD5,
		compressedMD5,
	)
	if err := os.WriteFile(path+".md5", []byte(manifest), 0o600); err != nil {
		t.Fatal(err)
	}
	return path, rawMD5, compressedMD5
}

func gzipBytes(t *testing.T, raw []byte) []byte {
	t.Helper()
	var output bytes.Buffer
	writer, err := gzip.NewWriterLevel(&output, gzip.BestCompression)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := writer.Write(raw); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	return output.Bytes()
}

func setFirmwareHeaders(
	response http.ResponseWriter,
	firmware []byte,
	rawMD5 string,
) {
	response.Header().Set("Content-Length", strconvItoa(len(firmware)))
	response.Header().Set("Content-MD5", contentMD5(firmware))
	response.Header().Set("X-Firmware-MD5", rawMD5)
	response.Header().Set("ETag", `"`+rawMD5+`"`)
}

func contentMD5(value []byte) string {
	sum := md5.Sum(value)
	return base64.StdEncoding.EncodeToString(sum[:])
}

func md5Hex(value []byte) string {
	sum := md5.Sum(value)
	return hex.EncodeToString(sum[:])
}

func strconvItoa(value int) string {
	return fmt.Sprintf("%d", value)
}
