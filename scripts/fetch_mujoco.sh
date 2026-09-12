#!/usr/bin/env bash
# Vendors the official MuJoCo SDK (headers + shared library) into
# cpp_core/third_party/mujoco/{include,lib}, normalized to the same layout
# on every platform. Idempotent; pass --force to re-fetch.
set -euo pipefail

MUJOCO_VERSION="3.12.0"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$REPO_ROOT/third_party/mujoco"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

if [[ -d "$VENDOR_DIR" && "$FORCE" -eq 0 ]]; then
    echo "MuJoCo already vendored at $VENDOR_DIR (use --force to re-fetch)"
    exit 0
fi

BASE_URL="https://github.com/google-deepmind/mujoco/releases/download/${MUJOCO_VERSION}"
OS="$(uname -s)"
ARCH="$(uname -m)"

download_and_verify() {
    local asset="$1"
    curl -fsSL -o "$WORK_DIR/$asset" "$BASE_URL/$asset"
    curl -fsSL -o "$WORK_DIR/$asset.sha256" "$BASE_URL/$asset.sha256"
    if command -v sha256sum >/dev/null 2>&1; then
        (cd "$WORK_DIR" && sha256sum -c "$asset.sha256")
    else
        (cd "$WORK_DIR" && shasum -a 256 -c "$asset.sha256")
    fi
}

rm -rf "$VENDOR_DIR"
mkdir -p "$VENDOR_DIR/include/mujoco" "$VENDOR_DIR/lib"

if [[ "$OS" == "Darwin" ]]; then
    ASSET="mujoco-${MUJOCO_VERSION}-macos-universal2.dmg"
    download_and_verify "$ASSET"

    MOUNT_DIR="$WORK_DIR/mnt"
    mkdir -p "$MOUNT_DIR"
    hdiutil attach -nobrowse -quiet -mountpoint "$MOUNT_DIR" "$WORK_DIR/$ASSET"
    trap 'hdiutil detach "$MOUNT_DIR" -quiet 2>/dev/null || true; rm -rf "$WORK_DIR"' EXIT

    FRAMEWORK="$MOUNT_DIR/MuJoCo.app/Contents/Frameworks/mujoco.framework/Versions/A"
    cp "$FRAMEWORK"/Headers/*.h "$VENDOR_DIR/include/mujoco/"
    cp "$FRAMEWORK"/libmujoco."${MUJOCO_VERSION}".dylib "$VENDOR_DIR/lib/"

    hdiutil detach "$MOUNT_DIR" -quiet

    # The framework-relative install name doesn't resolve once copied out
    # flat; rewrite it to a plain @rpath entry so CMake's rpath handling works.
    install_name_tool -id "@rpath/libmujoco.${MUJOCO_VERSION}.dylib" \
        "$VENDOR_DIR/lib/libmujoco.${MUJOCO_VERSION}.dylib"
    # install_name_tool invalidates the existing signature; arm64 macOS
    # refuses to load unsigned dylibs, so re-sign ad-hoc.
    codesign --force --sign - "$VENDOR_DIR/lib/libmujoco.${MUJOCO_VERSION}.dylib"

elif [[ "$OS" == "Linux" ]]; then
    case "$ARCH" in
        x86_64)  ASSET="mujoco-${MUJOCO_VERSION}-linux-x86_64.tar.gz" ;;
        aarch64) ASSET="mujoco-${MUJOCO_VERSION}-linux-aarch64.tar.gz" ;;
        *) echo "unsupported Linux arch: $ARCH" >&2; exit 1 ;;
    esac
    download_and_verify "$ASSET"

    tar xzf "$WORK_DIR/$ASSET" -C "$WORK_DIR"
    EXTRACTED="$WORK_DIR/mujoco-${MUJOCO_VERSION}"
    cp -r "$EXTRACTED"/include/mujoco/* "$VENDOR_DIR/include/mujoco/"
    cp -P "$EXTRACTED"/lib/libmujoco.so* "$VENDOR_DIR/lib/"

else
    echo "unsupported OS: $OS" >&2
    exit 1
fi

echo "vendored MuJoCo ${MUJOCO_VERSION} into $VENDOR_DIR"
