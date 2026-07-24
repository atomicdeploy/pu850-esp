package main

import (
	"context"
	"crypto/md5"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
	"unicode"
)

const (
	metadataTimeout = 5 * time.Second
	maxDownloadSize = 64 << 20
)

var (
	exactMD5Pattern     = regexp.MustCompile(`(?i)^[0-9a-f]{32}$`)
	contentRangePattern = regexp.MustCompile(
		`(?i)^bytes ([0-9]+)-([0-9]+)/([0-9]+)$`,
	)
)

type remoteFirmwareInfo struct {
	hash   string
	size   int64
	source string
	values map[string]string
}

func firstEnvironment(names ...string) string {
	for _, name := range names {
		if value := strings.TrimSpace(os.Getenv(name)); value != "" {
			return value
		}
	}
	return ""
}

func validateBearer(value string) error {
	for _, character := range value {
		if unicode.IsControl(character) {
			return errors.New("bearer token contains a control character")
		}
	}
	return nil
}

func deriveAPIFromDownloadURL(raw string) (string, error) {
	if strings.TrimSpace(raw) == "" {
		return "", errors.New(
			"-download requires -api or an explicit -download-url",
		)
	}
	parsed, err := url.Parse(strings.TrimSpace(raw))
	if err != nil {
		return "", fmt.Errorf("parse -download-url: %w", err)
	}
	if parsed.Scheme != "http" ||
		parsed.User != nil ||
		parsed.Hostname() == "" ||
		parsed.Fragment != "" {
		return "", errors.New(
			"-download-url must be a safe absolute http:// URL",
		)
	}
	return (&url.URL{
		Scheme: parsed.Scheme,
		Host:   parsed.Host,
		Path:   "/update",
	}).String(), nil
}

func absoluteOutputPath(path string) (string, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return "", errors.New("path is empty")
	}
	absolute, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	parent, err := os.Stat(filepath.Dir(absolute))
	if err != nil {
		return "", fmt.Errorf("inspect parent directory: %w", err)
	}
	if !parent.IsDir() {
		return "", errors.New("parent path is not a directory")
	}
	for _, candidate := range []string{absolute, absolute + ".part"} {
		info, statErr := os.Stat(candidate)
		if statErr == nil && !info.Mode().IsRegular() {
			return "", fmt.Errorf("%s is not a regular file", candidate)
		}
		if statErr != nil && !errors.Is(statErr, os.ErrNotExist) {
			return "", fmt.Errorf("inspect %s: %w", candidate, statErr)
		}
	}
	return absolute, nil
}

func setRequestHeaders(
	request *http.Request,
	opts options,
	authority string,
) {
	request.Header.Set("User-Agent", userAgent)
	if opts.bearer != "" {
		request.Header.Set("Authorization", "Bearer "+opts.bearer)
	}
	if authority != "" {
		request.Host = authority
	}
}

func fetchRemoteFirmwareInfo(
	parent context.Context,
	client *http.Client,
	endpoint *resolvedEndpoint,
	opts options,
	downloadOrigin bool,
) (remoteFirmwareInfo, error) {
	if endpoint == nil {
		return remoteFirmwareInfo{}, errors.New(
			"missing resolved information endpoint",
		)
	}
	urls := []*url.URL{endpoint.updateInfo, endpoint.info}
	authority := endpoint.apiAuthority
	if downloadOrigin {
		urls = []*url.URL{
			endpoint.downloadUpdateInfo,
			endpoint.downloadInfo,
		}
		authority = endpoint.downloadAuthority
	}

	var failures []error
	for _, target := range urls {
		if target == nil {
			continue
		}
		timeout := opts.timeout
		if timeout <= 0 || timeout > metadataTimeout {
			timeout = metadataTimeout
		}
		requestCtx, cancel := context.WithTimeout(parent, timeout)
		request, err := http.NewRequestWithContext(
			requestCtx,
			http.MethodGet,
			target.String(),
			nil,
		)
		if err == nil {
			setRequestHeaders(request, opts, authority)
			var response *http.Response
			response, err = client.Do(request)
			if err == nil {
				var body string
				body, err = readLimited(response.Body)
				closeErr := response.Body.Close()
				err = errors.Join(err, closeErr)
				if err == nil &&
					(response.StatusCode < 200 ||
						response.StatusCode >= 300) {
					err = fmt.Errorf(
						"HTTP %d",
						response.StatusCode,
					)
				}
				if err == nil {
					values := parseInfo(body)
					hash := strings.ToLower(strings.TrimSpace(
						findInfoValue(
							values,
							"hash",
							"firmwarehash",
							"firmwaremd5",
							"sketchmd5",
						),
					))
					if !exactMD5Pattern.MatchString(hash) {
						err = fmt.Errorf(
							"response did not contain an exact raw firmware MD5",
						)
					} else {
						size := int64(-1)
						sizeText := strings.TrimSpace(
							findInfoValue(
								values,
								"size",
								"firmwaresize",
								"sketchsize",
							),
						)
						if sizeText != "" {
							size, err = strconv.ParseInt(
								sizeText,
								10,
								64,
							)
							if err != nil || size < 0 ||
								size > maxDownloadSize {
								err = errors.New(
									"response contained an invalid firmware size",
								)
							}
						}
						if err == nil {
							cancel()
							return remoteFirmwareInfo{
								hash:   hash,
								size:   size,
								source: target.Path,
								values: values,
							}, nil
						}
					}
				}
			}
		}
		cancel()
		failures = append(
			failures,
			fmt.Errorf("%s: %w", target.Path, err),
		)
	}
	if len(failures) == 0 {
		return remoteFirmwareInfo{}, errors.New(
			"no firmware information endpoint is configured",
		)
	}
	return remoteFirmwareInfo{}, errors.Join(failures...)
}

func downloadFirmware(
	parent context.Context,
	client *http.Client,
	endpoint *resolvedEndpoint,
	opts options,
	outputPath string,
	resume bool,
	ui *consoleUI,
) error {
	if endpoint == nil || endpoint.download == nil {
		return errors.New("missing resolved firmware download endpoint")
	}
	partPath := outputPath + ".part"
	offset := int64(0)
	if resume {
		info, err := os.Stat(partPath)
		if err == nil {
			if !info.Mode().IsRegular() {
				return errors.New("download .part path is not a regular file")
			}
			offset = info.Size()
			if offset < 0 || offset > maxDownloadSize {
				return errors.New(
					"download .part exceeds the 64 MiB resume bound",
				)
			}
		} else if !errors.Is(err, os.ErrNotExist) {
			return fmt.Errorf("inspect download .part: %w", err)
		}
	}

	remote, metadataErr := fetchRemoteFirmwareInfo(
		parent,
		client,
		endpoint,
		opts,
		true,
	)
	if metadataErr == nil && offset > 0 && remote.size >= 0 {
		if offset > remote.size {
			return fmt.Errorf(
				"download .part is %d bytes, larger than remote firmware size %d",
				offset,
				remote.size,
			)
		}
		if offset == remote.size {
			actual, _, err := hashFile(partPath)
			if err != nil {
				return err
			}
			if !strings.EqualFold(actual, remote.hash) {
				return fmt.Errorf(
					"complete .part MD5 mismatch: expected %s, calculated %s",
					remote.hash,
					actual,
				)
			}
			if err := atomicReplace(partPath, outputPath); err != nil {
				return fmt.Errorf("commit verified download: %w", err)
			}
			ui.success(
				"Downloaded",
				fmt.Sprintf(
					"%s · %d bytes · MD5 %s",
					filepath.Base(outputPath),
					offset,
					actual,
				),
			)
			return nil
		}
	}

	response, cancel, err := startDownloadRequest(
		parent,
		client,
		endpoint,
		opts,
		offset,
	)
	if err != nil &&
		canRefreshDownloadEndpoint(*endpoint) &&
		isDialFailure(err) {
		ui.warn(
			"DNS retry",
			"Cached download address failed; resolving the device once more.",
		)
		refreshed, refreshErr := resolveConfiguredEndpoint(
			parent,
			opts.api,
			opts.downloadURL,
		)
		if refreshErr != nil {
			return errors.Join(err, refreshErr)
		}
		*endpoint = refreshed
		response, cancel, err = startDownloadRequest(
			parent,
			client,
			endpoint,
			opts,
			offset,
		)
	}
	if err != nil {
		return err
	}
	defer cancel()
	defer response.Body.Close()

	totalSize, err := validateDownloadResponse(response, offset)
	if err != nil {
		return err
	}
	if response.ContentLength <= 0 {
		return errors.New(
			"download response must provide a positive Content-Length",
		)
	}
	if response.ContentLength > maxDownloadSize-offset ||
		totalSize > maxDownloadSize {
		return errors.New("download exceeds the 64 MiB safety bound")
	}

	openFlags := os.O_CREATE | os.O_WRONLY | os.O_TRUNC
	if offset > 0 {
		openFlags = os.O_WRONLY | os.O_APPEND
	}
	file, err := os.OpenFile(partPath, openFlags, 0o600)
	if err != nil {
		return fmt.Errorf("open download .part: %w", err)
	}
	sectionHash := md5.New()
	written, copyErr := io.Copy(
		io.MultiWriter(file, sectionHash),
		io.LimitReader(response.Body, response.ContentLength+1),
	)
	syncErr := file.Sync()
	closeErr := file.Close()
	if copyErr != nil || syncErr != nil || closeErr != nil {
		return errors.Join(copyErr, syncErr, closeErr)
	}
	if written != response.ContentLength {
		return fmt.Errorf(
			"download length mismatch: Content-Length %d, received %d",
			response.ContentLength,
			written,
		)
	}
	sectionMD5 := hex.EncodeToString(sectionHash.Sum(nil))
	if advertised := strings.TrimSpace(
		response.Header.Get("Content-MD5"),
	); advertised != "" {
		expected, parseErr := parseContentMD5(advertised)
		if parseErr != nil {
			return parseErr
		}
		if !strings.EqualFold(expected, sectionMD5) {
			return fmt.Errorf(
				"Content-MD5 mismatch: expected %s, calculated %s",
				expected,
				sectionMD5,
			)
		}
	}

	actualMD5, finalSize, err := hashFile(partPath)
	if err != nil {
		return err
	}
	if finalSize != totalSize {
		return fmt.Errorf(
			"firmware size mismatch: expected %d, downloaded %d",
			totalSize,
			finalSize,
		)
	}
	if metadataErr == nil && remote.size >= 0 && remote.size != finalSize {
		return fmt.Errorf(
			"%s size mismatch: expected %d, downloaded %d",
			remote.source,
			remote.size,
			finalSize,
		)
	}

	expectedHashes := make(map[string]string)
	if metadataErr == nil {
		expectedHashes[remote.source] = remote.hash
	}
	if value := strings.TrimSpace(
		response.Header.Get("X-Firmware-MD5"),
	); value != "" {
		normalized, normalizeErr := normalizeMD5(value)
		if normalizeErr != nil {
			return fmt.Errorf("X-Firmware-MD5: %w", normalizeErr)
		}
		expectedHashes["X-Firmware-MD5"] = normalized
	}
	if value, ok := md5FromETag(response.Header.Get("ETag")); ok {
		expectedHashes["ETag"] = value
	}
	if offset == 0 {
		if value := strings.TrimSpace(
			response.Header.Get("Content-MD5"),
		); value != "" {
			expected, parseErr := parseContentMD5(value)
			if parseErr != nil {
				return parseErr
			}
			expectedHashes["Content-MD5"] = expected
		}
	}
	if len(expectedHashes) == 0 {
		if metadataErr != nil {
			return fmt.Errorf(
				"download has no trustworthy raw firmware MD5: %w",
				metadataErr,
			)
		}
		return errors.New("download has no trustworthy raw firmware MD5")
	}
	for source, expected := range expectedHashes {
		if !strings.EqualFold(expected, actualMD5) {
			return fmt.Errorf(
				"%s raw firmware MD5 mismatch: expected %s, calculated %s",
				source,
				expected,
				actualMD5,
			)
		}
	}

	if err := atomicReplace(partPath, outputPath); err != nil {
		return fmt.Errorf("commit verified download: %w", err)
	}
	ui.success(
		"Downloaded",
		fmt.Sprintf(
			"%s · %d bytes · MD5 %s",
			filepath.Base(outputPath),
			finalSize,
			actualMD5,
		),
	)
	return nil
}

func startDownloadRequest(
	parent context.Context,
	client *http.Client,
	endpoint *resolvedEndpoint,
	opts options,
	offset int64,
) (*http.Response, context.CancelFunc, error) {
	requestCtx, cancel := context.WithTimeout(parent, opts.timeout)
	request, err := http.NewRequestWithContext(
		requestCtx,
		http.MethodGet,
		endpoint.download.String(),
		nil,
	)
	if err != nil {
		cancel()
		return nil, func() {}, fmt.Errorf(
			"create download request: %w",
			err,
		)
	}
	setRequestHeaders(request, opts, endpoint.downloadAuthority)
	request.Header.Set("Accept", "application/octet-stream")
	request.Header.Set("Accept-Encoding", "identity")
	if offset > 0 {
		request.Header.Set("Range", fmt.Sprintf("bytes=%d-", offset))
	}
	response, err := client.Do(request)
	if err != nil {
		cancel()
		return nil, func() {}, fmt.Errorf("GET firmware: %w", err)
	}
	return response, cancel, nil
}

func validateDownloadResponse(
	response *http.Response,
	offset int64,
) (int64, error) {
	if offset == 0 {
		if response.StatusCode != http.StatusOK {
			return 0, fmt.Errorf(
				"firmware download returned HTTP %d",
				response.StatusCode,
			)
		}
		return response.ContentLength, nil
	}
	if response.StatusCode != http.StatusPartialContent {
		return 0, fmt.Errorf(
			"resume requires HTTP 206, received %d",
			response.StatusCode,
		)
	}
	match := contentRangePattern.FindStringSubmatch(
		strings.TrimSpace(response.Header.Get("Content-Range")),
	)
	if match == nil {
		return 0, errors.New(
			"resume response has an invalid Content-Range",
		)
	}
	start, startErr := strconv.ParseInt(match[1], 10, 64)
	end, endErr := strconv.ParseInt(match[2], 10, 64)
	total, totalErr := strconv.ParseInt(match[3], 10, 64)
	if startErr != nil || endErr != nil || totalErr != nil ||
		start != offset ||
		end < start ||
		total <= end ||
		response.ContentLength != end-start+1 {
		return 0, errors.New(
			"resume response Content-Range does not match the requested offset",
		)
	}
	return total, nil
}

func parseContentMD5(value string) (string, error) {
	value = strings.TrimSpace(value)
	if exactMD5Pattern.MatchString(value) {
		return strings.ToLower(value), nil
	}
	decoded, err := base64.StdEncoding.DecodeString(value)
	if err != nil || len(decoded) != md5.Size {
		return "", errors.New(
			"Content-MD5 is neither 16-byte base64 nor 32-digit hexadecimal MD5",
		)
	}
	return hex.EncodeToString(decoded), nil
}

func normalizeMD5(value string) (string, error) {
	value = strings.TrimSpace(value)
	if !exactMD5Pattern.MatchString(value) {
		return "", errors.New("value is not an exact 32-digit hexadecimal MD5")
	}
	return strings.ToLower(value), nil
}

func md5FromETag(value string) (string, bool) {
	value = strings.TrimSpace(value)
	if strings.HasPrefix(strings.ToLower(value), "w/") {
		value = strings.TrimSpace(value[2:])
	}
	value = strings.Trim(value, `"`)
	normalized, err := normalizeMD5(value)
	return normalized, err == nil
}

func canRefreshDownloadEndpoint(endpoint resolvedEndpoint) bool {
	return endpoint.downloadOriginalHost != "" &&
		endpoint.downloadOriginalHost != endpoint.downloadResolvedIP
}
