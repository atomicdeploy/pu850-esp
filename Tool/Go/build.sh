#!/usr/bin/env sh
set -eu

if [ "${1:-}" != "--skip-tests" ]; then
	go test -count=10 ./...
	go vet ./...
fi

mkdir -p bin/win-x64 bin/win-x86 bin/linux-x64

CGO_ENABLED=0 GOOS=windows GOARCH=amd64 \
	go build -buildvcs=false -trimpath -ldflags '-s -w' \
	-o bin/win-x64/ASAFirmwareTransfer.exe .

CGO_ENABLED=0 GOOS=windows GOARCH=386 \
	go build -buildvcs=false -trimpath -ldflags '-s -w' \
	-o bin/win-x86/ASAFirmwareTransfer.exe .

CGO_ENABLED=0 GOOS=linux GOARCH=amd64 \
	go build -buildvcs=false -trimpath -ldflags '-s -w' \
	-o bin/linux-x64/ASAFirmwareTransfer .

sha256sum \
	bin/win-x64/ASAFirmwareTransfer.exe \
	bin/win-x86/ASAFirmwareTransfer.exe \
	bin/linux-x64/ASAFirmwareTransfer
