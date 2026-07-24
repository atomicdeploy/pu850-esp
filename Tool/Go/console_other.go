//go:build !windows

package main

import "os"

func enableVirtualTerminal(file *os.File) bool {
	return isCharacterDevice(file)
}
