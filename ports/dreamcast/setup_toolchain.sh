#!/usr/bin/env bash
# Pinned third-party build of GCC 15.1.0 + KallistiOS 2.2.1 for Linux x86_64.
set -euo pipefail
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  printf '%s\n' 'This download is for Linux x86_64 (including x86_64 WSL).' >&2
  exit 2
fi
if [[ $# != 1 ]]; then
  printf 'Usage: bash %s SDK_DIRECTORY\n' "$0" >&2
  exit 2
fi
mkdir -p "$1"
sdk_dir=$(cd "$1" && pwd)
archive=$(mktemp "$sdk_dir/toolchain.XXXXXXXX.tar.gz")
trap 'rm -f "$archive"' EXIT
url='https://github.com/drpaneas/dreamcast-toolchain-builds/releases/download/gcc15.1.0-kos2.2.1/dreamcast-toolchain-gcc15.1.0-kos2.2.1-linux-x86_64-ubuntu22.tar.gz'
expected='36ee8b174331f53996990327de8df77ff662041989bbafab9e68572383d03b41'
curl --fail --location --retry 2 "$url" --output "$archive"
actual=$(sha256sum "$archive")
if [[ ${actual%% *} != "$expected" ]]; then
  printf '%s\n' 'Toolchain SHA-256 mismatch; nothing extracted.' >&2
  exit 1
fi
tar --no-same-owner -xzf "$archive" -C "$sdk_dir"
printf 'Next: source %q\n' "$sdk_dir/kos/environ.sh"
