//go:build !windows

package main

import "os"

func atomicReplace(source string, destination string) error {
	return os.Rename(source, destination)
}
