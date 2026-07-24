package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestParseOptionsHelp(t *testing.T) {
	t.Parallel()
	_, err := parseOptions([]string{"-h"})
	if !errors.Is(err, errHelpRequested) {
		t.Fatalf("parseOptions(-h) error = %v, want help sentinel", err)
	}
}

func TestLookupTimeoutAllowsColdMDNS(t *testing.T) {
	t.Parallel()
	if lookupTimeout != 20*time.Second {
		t.Fatalf("lookup timeout = %s, want 20s", lookupTimeout)
	}
}

func TestReadManifest(t *testing.T) {
	t.Parallel()
	directory := t.TempDir()
	path := filepath.Join(directory, "firmware.bin.md5")
	content := "" +
		"0123456789abcdef0123456789abcdef *firmware.bin\n" +
		"fedcba9876543210fedcba9876543210 *firmware.bin (compressed)\n"
	if err := os.WriteFile(path, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	got, err := readManifest(path)
	if err != nil {
		t.Fatal(err)
	}
	if got.rawMD5 != "0123456789abcdef0123456789abcdef" ||
		got.compressedMD5 != "fedcba9876543210fedcba9876543210" ||
		got.fileName != "firmware.bin" {
		t.Fatalf("manifest = %+v", got)
	}
}

func TestUploadFirmwareValidatesMultipartAndRebootHash(t *testing.T) {
	t.Parallel()
	directory := t.TempDir()
	firmwarePath := filepath.Join(directory, "firmware.bin")
	rawFirmware := []byte("raw firmware bytes")
	firmware := gzipBytes(t, rawFirmware)
	if err := os.WriteFile(firmwarePath, firmware, 0o600); err != nil {
		t.Fatal(err)
	}
	compressedSum := md5.Sum(firmware)
	compressedMD5 := hex.EncodeToString(compressedSum[:])
	rawMD5 := md5Hex(rawFirmware)
	manifestText := fmt.Sprintf(
		"%s *firmware.bin\n%s *firmware.bin (compressed)\n",
		rawMD5,
		compressedMD5,
	)
	if err := os.WriteFile(
		firmwarePath+".md5",
		[]byte(manifestText),
		0o600,
	); err != nil {
		t.Fatal(err)
	}

	postCount := 0
	server := httptest.NewServer(http.HandlerFunc(
		func(response http.ResponseWriter, request *http.Request) {
			switch request.URL.Path {
			case "/update":
				postCount++
				reader, err := request.MultipartReader()
				if err != nil {
					t.Errorf("MultipartReader: %v", err)
					http.Error(response, "bad multipart", http.StatusBadRequest)
					return
				}
				fields := make(map[string][]byte)
				for {
					part, nextErr := reader.NextPart()
					if nextErr == io.EOF {
						break
					}
					if nextErr != nil {
						t.Errorf("NextPart: %v", nextErr)
						return
					}
					value, readErr := io.ReadAll(part)
					if readErr != nil {
						t.Errorf("ReadAll: %v", readErr)
						return
					}
					fields[part.FormName()] = value
				}
				if string(fields["MD5"]) != compressedMD5 {
					t.Errorf("MD5 field = %q", fields["MD5"])
				}
				if string(fields["firmware"]) != string(firmware) {
					t.Errorf("firmware field = %q", fields["firmware"])
				}
				_, _ = io.WriteString(response, "ok!")
			case "/info":
				_, _ = fmt.Fprintf(
					response,
					"firmware hash: %s\nbuild: test\nuptime: 00:00:01\n",
					rawMD5,
				)
			default:
				http.NotFound(response, request)
			}
		},
	))
	defer server.Close()

	endpointURL := server.URL + "/update"
	endpoint, err := resolveEndpoint(context.Background(), endpointURL)
	if err != nil {
		t.Fatal(err)
	}
	opts := options{
		api:           endpointURL,
		firmware:      firmwarePath,
		timeout:       5 * time.Second,
		rebootTimeout: 5 * time.Second,
		pollInterval:  10 * time.Millisecond,
		force:         true,
		noColor:       true,
	}
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
	if postCount != 1 {
		t.Fatalf("POST count = %d, want 1", postCount)
	}
}

func TestValidateFirmwareRejectsInvalidGzipAndRawMismatch(t *testing.T) {
	t.Parallel()
	directory := t.TempDir()
	invalidPath := filepath.Join(directory, "invalid.bin")
	invalid := []byte("not a gzip stream")
	if err := os.WriteFile(invalidPath, invalid, 0o600); err != nil {
		t.Fatal(err)
	}
	_, err := validateFirmware(invalidPath, manifest{
		compressedMD5: md5Hex(invalid),
		rawMD5:        md5Hex([]byte("raw")),
	})
	if err == nil || !strings.Contains(err.Error(), "valid gzip") {
		t.Fatalf("invalid gzip error = %v", err)
	}

	raw := []byte("bounded raw firmware")
	compressed := gzipBytes(t, raw)
	mismatchPath := filepath.Join(directory, "mismatch.bin")
	if err := os.WriteFile(mismatchPath, compressed, 0o600); err != nil {
		t.Fatal(err)
	}
	_, err = validateFirmware(mismatchPath, manifest{
		compressedMD5: md5Hex(compressed),
		rawMD5:        "0123456789abcdef0123456789abcdef",
	})
	if err == nil || !strings.Contains(err.Error(), "raw firmware MD5 mismatch") {
		t.Fatalf("raw mismatch error = %v", err)
	}
}

func TestParseOptionsReadsUpdateBearerToken(t *testing.T) {
	directory := t.TempDir()
	firmwarePath := filepath.Join(directory, "firmware.bin")
	if err := os.WriteFile(firmwarePath, []byte("fixture"), 0o600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("UPDATE_BEARER_TOKEN", "preferred-update-token")
	t.Setenv("UPDATE_TOKEN", "fallback-update-token")
	t.Setenv("ASA_FIRMWARE_BEARER_TOKEN", "")
	t.Setenv("FIRMWARE_BEARER_TOKEN", "")

	opts, err := parseOptions([]string{"-check", firmwarePath})
	if err != nil {
		t.Fatal(err)
	}
	if opts.bearer != "preferred-update-token" {
		t.Fatalf("bearer = %q, want UPDATE_BEARER_TOKEN", opts.bearer)
	}
}

func TestParseInfo(t *testing.T) {
	t.Parallel()
	values := parseInfo(
		"hostname: asa-device\r\n" +
			"firmware hash: abcdef\r\n" +
			"ignored\r\n",
	)
	if values["hostname"] != "asa-device" ||
		values["firmware hash"] != "abcdef" {
		t.Fatalf("parseInfo = %#v", values)
	}
}

func TestParseInfoJSONAndSanitizeConsole(t *testing.T) {
	t.Parallel()
	values := parseInfo(
		`{"device":{"firmware_hash":"ABCDEF","build":"test\u001b[31m"}}`,
	)
	if findInfoValue(values, "firmwarehash") != "ABCDEF" {
		t.Fatalf("JSON firmware hash = %#v", values)
	}
	if got := sanitizeConsole(findInfoValue(values, "build")); got != "test [31m" {
		t.Fatalf("sanitized build = %q", got)
	}
}

func TestBuildMultipartOmitsFilenameButPreservesFields(t *testing.T) {
	t.Parallel()
	directory := t.TempDir()
	path := filepath.Join(directory, "firmware.bin")
	if err := os.WriteFile(path, []byte("firmware"), 0o600); err != nil {
		t.Fatal(err)
	}
	body, contentType, err := buildMultipart(
		path,
		"0123456789abcdef0123456789abcdef",
	)
	if err != nil {
		t.Fatal(err)
	}
	boundary := strings.TrimPrefix(
		contentType,
		"multipart/form-data; boundary=",
	)
	reader := multipart.NewReader(strings.NewReader(string(body)), boundary)
	for {
		part, nextErr := reader.NextPart()
		if nextErr == io.EOF {
			break
		}
		if nextErr != nil {
			t.Fatal(nextErr)
		}
		if part.FormName() == "firmware" && part.FileName() != "" {
			t.Fatalf("firmware filename = %q, want empty", part.FileName())
		}
	}
}
