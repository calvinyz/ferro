#!/usr/bin/env bash
# Vendors the official ONNX Runtime release into third_party/onnxruntime.
# Pinned to the same version across platforms so cross-platform latency
# numbers stay comparable. Idempotent; pass --force to re-fetch.
set -euo pipefail

ORT_VERSION="1.29.0"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$REPO_ROOT/third_party/onnxruntime"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

if [[ -d "$VENDOR_DIR" && "$FORCE" -eq 0 ]]; then
    echo "ONNX Runtime already vendored at $VENDOR_DIR (use --force to re-fetch)"
    exit 0
fi

OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS/$ARCH" in
    Linux/x86_64)   SLUG="linux-x64";     SHA256="c3fddc4f139a045b0c4902c57410f0694f1c2fdf9b6939fbe38b1aeae7cd14ba" ;;
    Linux/aarch64)  SLUG="linux-aarch64"; SHA256="e1799098ebc054b370f6176a450f158720f297818c613e5dc99b92e2ec82346f" ;;
    Darwin/arm64)   SLUG="osx-arm64";     SHA256="d0706fc34f315d8c88639d0a8c81f2e09e815f282cabed3493c06a054352cf92" ;;
    *) echo "unsupported platform: $OS/$ARCH" >&2; exit 1 ;;
esac

ASSET="onnxruntime-${SLUG}-${ORT_VERSION}.tgz"
URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ASSET}"

curl -fsSL -o "$WORK_DIR/$ASSET" "$URL"

if command -v sha256sum >/dev/null 2>&1; then
    echo "${SHA256}  ${WORK_DIR}/${ASSET}" | sha256sum -c -
else
    echo "${SHA256}  ${WORK_DIR}/${ASSET}" | shasum -a 256 -c -
fi

tar xzf "$WORK_DIR/$ASSET" -C "$WORK_DIR"
EXTRACTED="$WORK_DIR/onnxruntime-${SLUG}-${ORT_VERSION}"

rm -rf "$VENDOR_DIR"
mkdir -p "$VENDOR_DIR"
# The shipped CMake config expects include/onnxruntime/ but the tarball puts
# headers at include/, so nest them to match (and to match Homebrew's layout).
mkdir -p "$VENDOR_DIR/include/onnxruntime"
cp -r "$EXTRACTED/include/." "$VENDOR_DIR/include/onnxruntime/"
cp -rP "$EXTRACTED/lib" "$VENDOR_DIR/lib"

echo "vendored ONNX Runtime ${ORT_VERSION} (${SLUG}) into $VENDOR_DIR"
