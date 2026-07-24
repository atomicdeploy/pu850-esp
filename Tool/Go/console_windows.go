//go:build windows

package main

import (
	"os"
	"syscall"
	"unsafe"
)

const enableVirtualTerminalProcessing = 0x0004

var (
	kernel32DLL    = syscall.NewLazyDLL("kernel32.dll")
	getConsoleMode = kernel32DLL.NewProc("GetConsoleMode")
	setConsoleMode = kernel32DLL.NewProc("SetConsoleMode")
)

func enableVirtualTerminal(file *os.File) bool {
	if !isCharacterDevice(file) {
		return false
	}
	var mode uint32
	handle := file.Fd()
	succeeded, _, _ := getConsoleMode.Call(
		handle,
		uintptr(unsafe.Pointer(&mode)),
	)
	if succeeded == 0 {
		return false
	}
	succeeded, _, _ = setConsoleMode.Call(
		handle,
		uintptr(mode|enableVirtualTerminalProcessing),
	)
	return succeeded != 0
}
