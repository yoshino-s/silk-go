//go:build !((linux && (386 || amd64 || arm || arm64)) || (darwin && (amd64 || arm64)) || (windows && (386 || amd64 || arm64)))

package main

import (
	"fmt"
	"os"
	"runtime"
)

func main() {
	fmt.Fprintf(os.Stderr, "silk-encoder: unsupported %s/%s\n", runtime.GOOS, runtime.GOARCH)
	os.Exit(1)
}
