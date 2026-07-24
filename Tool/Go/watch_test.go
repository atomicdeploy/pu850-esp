package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestWatchTrackerRequiresStableWindowAndDoesNotReplay(t *testing.T) {
	t.Parallel()
	start := time.Unix(1_700_000_000, 0)
	observation := watchObservation{signature: "build-a", ready: true}
	tracker := watchTracker{}

	if got := tracker.observe(observation, start, time.Second); got != watchStabilizing {
		t.Fatalf("initial action = %v, want stabilizing", got)
	}
	if got := tracker.observe(
		observation,
		start.Add(time.Second-time.Nanosecond),
		time.Second,
	); got != watchStabilizing {
		t.Fatalf("pre-deadline action = %v, want stabilizing", got)
	}
	if got := tracker.observe(
		observation,
		start.Add(time.Second),
		time.Second,
	); got != watchCandidateReady {
		t.Fatalf("stable action = %v, want candidate ready", got)
	}

	tracker.markHandled(observation.signature)
	if got := tracker.observe(
		observation,
		start.Add(10*time.Second),
		time.Second,
	); got != watchIdle {
		t.Fatalf("handled action = %v, want idle", got)
	}
}

func TestWatchTrackerCoalescesNewestChangeAfterUpload(t *testing.T) {
	t.Parallel()
	start := time.Unix(1_700_000_000, 0)
	tracker := watchTracker{}
	first := watchObservation{signature: "build-a", ready: true}
	second := watchObservation{signature: "build-b", ready: true}
	newest := watchObservation{signature: "build-c", ready: true}

	_ = tracker.observe(first, start, time.Second)
	if got := tracker.observe(
		first,
		start.Add(time.Second),
		time.Second,
	); got != watchCandidateReady {
		t.Fatalf("first stable action = %v", got)
	}
	// processWatchScan marks the exact candidate before its synchronous upload.
	tracker.markHandled(first.signature)

	if got := tracker.observe(
		second,
		start.Add(2*time.Second),
		time.Second,
	); got != watchStabilizing {
		t.Fatalf("second action = %v, want stabilizing", got)
	}
	if got := tracker.observe(
		newest,
		start.Add(2500*time.Millisecond),
		time.Second,
	); got != watchStabilizing {
		t.Fatalf("newest action = %v, want stabilizing", got)
	}
	if got := tracker.observe(
		newest,
		start.Add(3500*time.Millisecond),
		time.Second,
	); got != watchCandidateReady {
		t.Fatalf("newest stable action = %v, want candidate ready", got)
	}
}

func TestProcessWatchScanQueuesOnlyNewestBuildChangedDuringUpload(
	t *testing.T,
) {
	t.Parallel()
	firmwarePath, manifestPath := writeWatchBuild(
		t,
		[]byte("firmware-a"),
	)
	opts := options{firmware: firmwarePath}
	tracker := watchTracker{}
	notices := watchNoticeDeduper{}
	ui := &consoleUI{}
	lastUploaded := ""
	uploadCount := 0
	upload := func() (string, error) {
		uploadCount++
		candidate, err := readManifest(manifestPath)
		if err != nil {
			return "", err
		}
		if uploadCount == 1 {
			rewriteWatchBuild(
				t,
				firmwarePath,
				manifestPath,
				[]byte("firmware-newest"),
			)
		}
		return candidate.compressedMD5, nil
	}
	start := time.Unix(1_700_000_000, 0)

	for _, now := range []time.Time{
		start,
		start.Add(time.Second),
		start.Add(2 * time.Second),
		start.Add(3 * time.Second),
		start.Add(4 * time.Second),
	} {
		if err := processWatchScan(
			context.Background(),
			opts,
			now,
			&lastUploaded,
			upload,
			ui,
			&tracker,
			&notices,
		); err != nil {
			t.Fatal(err)
		}
	}
	if uploadCount != 2 {
		t.Fatalf("upload count = %d, want 2", uploadCount)
	}
	newest, err := readManifest(manifestPath)
	if err != nil {
		t.Fatal(err)
	}
	if lastUploaded != newest.compressedMD5 {
		t.Fatalf(
			"last uploaded = %q, newest = %q",
			lastUploaded,
			newest.compressedMD5,
		)
	}
}

func TestWatchTrackerHandlesDeletionAndRecreation(t *testing.T) {
	t.Parallel()
	start := time.Unix(1_700_000_000, 0)
	tracker := watchTracker{}
	missing := watchObservation{
		signature: "firmware-missing",
		reason:    "firmware missing",
	}
	rebuilt := watchObservation{signature: "build-new", ready: true}

	if got := tracker.observe(missing, start, time.Second); got != watchBlocked {
		t.Fatalf("missing action = %v, want blocked", got)
	}
	if got := tracker.observe(
		rebuilt,
		start.Add(time.Second),
		time.Second,
	); got != watchStabilizing {
		t.Fatalf("recreated action = %v, want stabilizing", got)
	}
	if got := tracker.observe(
		rebuilt,
		start.Add(2*time.Second),
		time.Second,
	); got != watchCandidateReady {
		t.Fatalf("stable recreated action = %v, want candidate ready", got)
	}
}

func TestInspectWatchFilesHonorsBuildSentinels(t *testing.T) {
	t.Parallel()
	firmwarePath, _ := writeWatchBuild(t, []byte("firmware-a"))

	observation := inspectWatchFiles(firmwarePath)
	if !observation.ready {
		t.Fatalf("complete build not ready: %s", observation.reason)
	}

	if err := os.WriteFile(firmwarePath+".gz", []byte("partial"), 0o600); err != nil {
		t.Fatal(err)
	}
	observation = inspectWatchFiles(firmwarePath)
	if observation.ready ||
		!strings.Contains(observation.reason, "Compression artifact") {
		t.Fatalf("gzip observation = %+v", observation)
	}
	if err := os.Remove(firmwarePath + ".gz"); err != nil {
		t.Fatal(err)
	}

	markerPath := watchBuildMarkerPath(firmwarePath)
	if markerPath != filepath.Join(filepath.Dir(firmwarePath), "~local.h") {
		t.Fatalf("marker path = %q", markerPath)
	}
	if err := os.WriteFile(markerPath, []byte("#define BUILDING"), 0o600); err != nil {
		t.Fatal(err)
	}
	observation = inspectWatchFiles(firmwarePath)
	if observation.ready ||
		!strings.Contains(observation.reason, "Build marker") {
		t.Fatalf("marker observation = %+v", observation)
	}
}

func TestInspectWatchFilesHashesContentNotOnlyMetadata(t *testing.T) {
	t.Parallel()
	firmwarePath, _ := writeWatchBuild(t, []byte("firmware-a"))
	firstInfo, err := os.Stat(firmwarePath)
	if err != nil {
		t.Fatal(err)
	}
	first := inspectWatchFiles(firmwarePath)
	if !first.ready {
		t.Fatalf("first observation not ready: %s", first.reason)
	}

	// Reuse the same length and restore the timestamp. Content hashing must
	// still notice this replacement even if metadata-only polling would not.
	if err := os.WriteFile(firmwarePath, []byte("firmware-b"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.Chtimes(
		firmwarePath,
		firstInfo.ModTime(),
		firstInfo.ModTime(),
	); err != nil {
		t.Fatal(err)
	}
	second := inspectWatchFiles(firmwarePath)
	if !second.ready {
		t.Fatalf("second observation not ready: %s", second.reason)
	}
	if first.firmwareMD5 == second.firmwareMD5 ||
		first.signature == second.signature {
		t.Fatalf(
			"content replacement was missed: first=%+v second=%+v",
			first,
			second,
		)
	}
}

func TestValidateWatchCandidateRejectsMismatch(t *testing.T) {
	t.Parallel()
	firmwarePath, manifestPath := writeWatchBuild(
		t,
		[]byte("firmware-a"),
	)
	observation := inspectWatchFiles(firmwarePath)
	if !observation.ready {
		t.Fatalf("observation not ready: %s", observation.reason)
	}
	content := "" +
		"0123456789abcdef0123456789abcdef *firmware.bin\n" +
		"ffffffffffffffffffffffffffffffff *firmware.bin (compressed)\n"
	if err := os.WriteFile(manifestPath, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
	observation = inspectWatchFiles(firmwarePath)
	_, err := validateWatchCandidate(firmwarePath, observation)
	if err == nil || !strings.Contains(err.Error(), "MD5 mismatch") {
		t.Fatalf("validateWatchCandidate error = %v", err)
	}
}

func TestWatchNoticeDeduperSuppressesOnlyConsecutiveState(t *testing.T) {
	t.Parallel()
	deduper := watchNoticeDeduper{}
	if !deduper.shouldReport("missing") {
		t.Fatal("first notice was suppressed")
	}
	if deduper.shouldReport("missing") {
		t.Fatal("duplicate notice was reported")
	}
	if !deduper.shouldReport("ready") || !deduper.shouldReport("missing") {
		t.Fatal("state transition did not re-enable notice")
	}
}

func TestParseOptionsAllowsMissingFirmwareOnlyInWatchMode(t *testing.T) {
	t.Parallel()
	path := filepath.Join(t.TempDir(), "future.bin")
	opts, err := parseOptions([]string{
		"-watch",
		"-api",
		"http://127.0.0.1/update",
		path,
	})
	if err != nil {
		t.Fatalf("watch parse failed: %v", err)
	}
	if opts.firmware != path {
		t.Fatalf("firmware = %q, want %q", opts.firmware, path)
	}
	if _, err := parseOptions([]string{
		"-api",
		"http://127.0.0.1/update",
		path,
	}); err == nil {
		t.Fatal("one-shot mode accepted missing firmware")
	}
}

func writeWatchBuild(
	t *testing.T,
	firmware []byte,
) (firmwarePath string, manifestPath string) {
	t.Helper()
	directory := t.TempDir()
	firmwarePath = filepath.Join(directory, "firmware.bin")
	manifestPath = firmwarePath + ".md5"
	rewriteWatchBuild(t, firmwarePath, manifestPath, firmware)
	return firmwarePath, manifestPath
}

func rewriteWatchBuild(
	t *testing.T,
	firmwarePath string,
	manifestPath string,
	firmware []byte,
) {
	t.Helper()
	if err := os.WriteFile(firmwarePath, firmware, 0o600); err != nil {
		t.Fatal(err)
	}
	sum := md5.Sum(firmware)
	compressedMD5 := hex.EncodeToString(sum[:])
	content := fmt.Sprintf(
		"0123456789abcdef0123456789abcdef *firmware.bin\n"+
			"%s *firmware.bin (compressed)\n",
		compressedMD5,
	)
	if err := os.WriteFile(manifestPath, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
}
