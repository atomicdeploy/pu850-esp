package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	watchScanInterval = 250 * time.Millisecond
	watchStableWindow = time.Second
)

var errWatchCandidateChanged = errors.New(
	"watched build changed while it was being validated",
)

type watchAction uint8

const (
	watchBlocked watchAction = iota
	watchStabilizing
	watchIdle
	watchCandidateReady
)

type watchObservation struct {
	signature   string
	reason      string
	firmwareMD5 string
	ready       bool
}

type watchTracker struct {
	seenSignature    string
	handledSignature string
	stableSince      time.Time
}

type watchNoticeDeduper struct {
	lastKey string
}

type watchPathState struct {
	key     string
	exists  bool
	regular bool
	info    fs.FileInfo
	err     error
}

func (tracker *watchTracker) observe(
	observation watchObservation,
	now time.Time,
	stableWindow time.Duration,
) watchAction {
	if observation.signature != tracker.seenSignature {
		tracker.seenSignature = observation.signature
		tracker.stableSince = now
	}
	if !observation.ready {
		return watchBlocked
	}
	if tracker.handledSignature == observation.signature {
		return watchIdle
	}
	if now.Before(tracker.stableSince) ||
		now.Sub(tracker.stableSince) < stableWindow {
		return watchStabilizing
	}
	return watchCandidateReady
}

func (tracker *watchTracker) markHandled(signature string) {
	tracker.handledSignature = signature
}

func (deduper *watchNoticeDeduper) shouldReport(key string) bool {
	if key == deduper.lastKey {
		return false
	}
	deduper.lastKey = key
	return true
}

func (deduper *watchNoticeDeduper) setSilent(key string) {
	deduper.lastKey = key
}

func watchFirmware(
	ctx context.Context,
	opts options,
	lastUploaded string,
	upload func() (string, error),
	ui *consoleUI,
) error {
	ticker := time.NewTicker(watchScanInterval)
	defer ticker.Stop()

	tracker := watchTracker{}
	notices := watchNoticeDeduper{}

	// Scan immediately. This also ensures a rebuild completed while a prior
	// upload was being verified is not mistaken for the already-uploaded file.
	scan := make(chan time.Time, 1)
	scan <- time.Now()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case now := <-scan:
			if err := processWatchScan(
				ctx,
				opts,
				now,
				&lastUploaded,
				upload,
				ui,
				&tracker,
				&notices,
			); err != nil {
				return err
			}
		case <-ticker.C:
			if err := processWatchScan(
				ctx,
				opts,
				time.Now(),
				&lastUploaded,
				upload,
				ui,
				&tracker,
				&notices,
			); err != nil {
				return err
			}
		}
	}
}

func processWatchScan(
	ctx context.Context,
	opts options,
	now time.Time,
	lastUploaded *string,
	upload func() (string, error),
	ui *consoleUI,
	tracker *watchTracker,
	notices *watchNoticeDeduper,
) error {
	observation := inspectWatchFiles(opts.firmware)
	action := tracker.observe(observation, now, watchStableWindow)
	switch action {
	case watchBlocked:
		key := "blocked:" + observation.signature
		if notices.shouldReport(key) {
			ui.warn("Watch", observation.reason)
		}
		return nil
	case watchStabilizing:
		key := "stabilizing:" + observation.signature
		if notices.shouldReport(key) {
			ui.info(
				"Watch",
				fmt.Sprintf(
					"Build change detected; waiting %s for firmware and manifest stability.",
					watchStableWindow,
				),
			)
		}
		return nil
	case watchIdle:
		notices.setSilent("idle:" + observation.signature)
		return nil
	case watchCandidateReady:
		// Continue below.
	default:
		return errors.New("unknown watcher state")
	}

	candidate, err := validateWatchCandidate(opts.firmware, observation)
	if errors.Is(err, errWatchCandidateChanged) {
		// A build moved while it was being read. The next scan observes the
		// newest complete state and restarts the stability window.
		return nil
	}
	if err != nil {
		tracker.markHandled(observation.signature)
		if notices.shouldReport("invalid:" + observation.signature) {
			ui.fail("Watched build invalid", err)
		}
		return nil
	}
	if strings.EqualFold(candidate.compressedMD5, *lastUploaded) {
		tracker.markHandled(observation.signature)
		if *lastUploaded != "" &&
			notices.shouldReport("unchanged:"+observation.signature) {
			ui.info(
				"Watch",
				"Stable rebuild has the same compressed MD5; skipped.",
			)
		}
		return nil
	}

	// Mark this exact snapshot before starting the synchronous upload. If it
	// fails, it is not replayed indefinitely. If any watched file changes
	// during the upload, the next scan has a new signature and queues only that
	// newest stable build.
	tracker.markHandled(observation.signature)
	notices.setSilent("uploading:" + observation.signature)
	hash, uploadErr := upload()
	if uploadErr != nil {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		if errors.Is(uploadErr, context.Canceled) {
			return uploadErr
		}
		ui.fail("Watched upload failed", uploadErr)
		return nil
	}
	*lastUploaded = hash
	return nil
}

func inspectWatchFiles(firmwarePath string) watchObservation {
	manifestPath := firmwarePath + ".md5"
	gzipPath := firmwarePath + ".gz"
	markerPath := watchBuildMarkerPath(firmwarePath)

	firmware := inspectWatchPath(firmwarePath)
	manifestFile := inspectWatchPath(manifestPath)
	gzipFile := inspectWatchPath(gzipPath)
	buildMarker := inspectWatchPath(markerPath)

	signatureParts := []string{
		"firmware=" + firmware.key,
		"manifest=" + manifestFile.key,
		"gzip=" + gzipFile.key,
		"marker=" + buildMarker.key,
	}
	blocked := func(reason string) watchObservation {
		return watchObservation{
			signature: strings.Join(signatureParts, "|"),
			reason:    reason,
		}
	}

	for _, item := range []struct {
		label string
		path  string
		state watchPathState
	}{
		{"firmware", firmwarePath, firmware},
		{"MD5 manifest", manifestPath, manifestFile},
		{"gzip build artifact", gzipPath, gzipFile},
		{"build marker", markerPath, buildMarker},
	} {
		if item.state.err != nil {
			return blocked(fmt.Sprintf(
				"Cannot inspect %s %q: %v",
				item.label,
				item.path,
				item.state.err,
			))
		}
	}

	if buildMarker.exists {
		return blocked(fmt.Sprintf(
			"Build marker %q is present; waiting for the build to finish.",
			markerPath,
		))
	}
	if gzipFile.exists {
		return blocked(fmt.Sprintf(
			"Compression artifact %q is present; waiting for the build to finish.",
			gzipPath,
		))
	}
	if !firmware.exists {
		return blocked(fmt.Sprintf(
			"Firmware %q is temporarily unavailable; waiting for it to be rebuilt.",
			firmwarePath,
		))
	}
	if !firmware.regular {
		return blocked(fmt.Sprintf(
			"Firmware %q is not a regular file.",
			firmwarePath,
		))
	}
	if !manifestFile.exists {
		return blocked(fmt.Sprintf(
			"MD5 manifest %q is temporarily unavailable; waiting for it.",
			manifestPath,
		))
	}
	if !manifestFile.regular {
		return blocked(fmt.Sprintf(
			"MD5 manifest %q is not a regular file.",
			manifestPath,
		))
	}

	firmwareDigest, err := digestUnchangedFile(firmwarePath, firmware.info)
	if err != nil {
		signatureParts = append(signatureParts, "firmware-read="+err.Error())
		return blocked(fmt.Sprintf(
			"Firmware is still changing or temporarily unreadable: %v",
			err,
		))
	}
	manifestDigest, err := digestUnchangedFile(manifestPath, manifestFile.info)
	if err != nil {
		signatureParts = append(signatureParts, "manifest-read="+err.Error())
		return blocked(fmt.Sprintf(
			"MD5 manifest is still changing or temporarily unreadable: %v",
			err,
		))
	}
	signatureParts = append(
		signatureParts,
		"firmware-md5="+firmwareDigest,
		"manifest-md5="+manifestDigest,
	)
	return watchObservation{
		signature:   strings.Join(signatureParts, "|"),
		firmwareMD5: firmwareDigest,
		ready:       true,
	}
}

func inspectWatchPath(path string) watchPathState {
	info, err := os.Stat(path)
	if errors.Is(err, fs.ErrNotExist) {
		return watchPathState{key: "missing"}
	}
	if err != nil {
		return watchPathState{
			key: "error:" + err.Error(),
			err: err,
		}
	}
	return watchPathState{
		key: fmt.Sprintf(
			"present:%d:%d:%s",
			info.Size(),
			info.ModTime().UnixNano(),
			info.Mode().String(),
		),
		exists:  true,
		regular: info.Mode().IsRegular(),
		info:    info,
	}
}

func digestUnchangedFile(path string, before fs.FileInfo) (string, error) {
	file, err := os.Open(path)
	if err != nil {
		return "", err
	}
	hash := md5.New()
	_, copyErr := io.Copy(hash, file)
	closeErr := file.Close()
	if err := errors.Join(copyErr, closeErr); err != nil {
		return "", err
	}
	after, err := os.Stat(path)
	if err != nil {
		return "", err
	}
	if before.Size() != after.Size() ||
		!before.ModTime().Equal(after.ModTime()) ||
		before.Mode() != after.Mode() {
		return "", errWatchCandidateChanged
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func validateWatchCandidate(
	firmwarePath string,
	observation watchObservation,
) (manifest, error) {
	candidate, err := readManifest(firmwarePath + ".md5")
	if err != nil {
		return manifest{}, err
	}
	if !strings.EqualFold(
		candidate.fileName,
		filepath.Base(firmwarePath),
	) {
		return manifest{}, fmt.Errorf(
			"manifest names %q, but selected firmware is %q",
			candidate.fileName,
			filepath.Base(firmwarePath),
		)
	}
	if !strings.EqualFold(
		candidate.compressedMD5,
		observation.firmwareMD5,
	) {
		return manifest{}, fmt.Errorf(
			"compressed firmware MD5 mismatch: expected %s, calculated %s",
			candidate.compressedMD5,
			observation.firmwareMD5,
		)
	}
	after := inspectWatchFiles(firmwarePath)
	if !after.ready || after.signature != observation.signature {
		return manifest{}, errWatchCandidateChanged
	}
	return candidate, nil
}

func watchBuildMarkerPath(firmwarePath string) string {
	// The Node uploader's `${filePath}/../~local.h` resolves lexically to a
	// sibling of the firmware. Construct that path directly instead of treating
	// the firmware file itself as a directory.
	return filepath.Join(filepath.Dir(filepath.Clean(firmwarePath)), "~local.h")
}
