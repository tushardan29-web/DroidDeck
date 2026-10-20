#!/usr/bin/env bash
# Builds the glibc aarch64 Vulkan ICD zips a Mali phone imports under
# GPU drivers > Linux runtime driver. Both are fetched, never vendored:
#   - Mesa lavapipe from Arch Linux ARM (software rendering, Vulkan 1.4)
#   - PanVK kbase from zenithblue-oss/panvk-kbase-android (talks to /dev/mali0)
# Usage: tools/mali/build-driver-zips.sh [output dir]   (default build/mali-driver-zips)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../build/mali-driver-zips}
mkdir -p "$out"; out=$(cd "$out" && pwd)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

mirror=http://mirror.archlinuxarm.org/aarch64/extra
swrast_pkg=${VULKAN_SWRAST_PKG:-vulkan-swrast-1%3A26.2.5-1-aarch64.pkg.tar.xz}
panvk_tag=${PANVK_TAG:-panvk-kbase-v0.1.0-rc3}
panvk_url=https://github.com/zenithblue-oss/panvk-kbase-android/releases/download/$panvk_tag/libvulkan_panfrost-glibc-aarch64.so

curl -fsSL --retry 5 -o "$work/swrast.pkg.tar.xz" "$mirror/$swrast_pkg"
tar -xf "$work/swrast.pkg.tar.xz" -C "$work" usr/lib/libvulkan_lvp.so
mkdir -p "$work/panvk"
curl -fsSL --retry 5 -o "$work/panvk/libvulkan_panfrost.so" "$panvk_url"

# minGlibc is the highest GLIBC_ symbol the library asks the loader for; the
# importer copies it into meta.json and the driver row shows it.
meta() {
  local so=$1 name=$2 version=$3 glibc
  glibc=$(strings "$so" | grep -E '^GLIBC_2\.[0-9]+$' | sort -V | tail -1 | cut -d_ -f2)
  printf '{\n  "kind": "linux-vulkan-icd",\n  "name": "%s",\n  "driverVersion": "%s",\n  "minGlibc": "%s"\n}\n' \
    "$name" "$version" "$glibc" > "$(dirname "$so")/meta.json"
}

check() {
  local so=$1
  readelf -h "$so" > "$so.hdr"
  grep -q AArch64 "$so.hdr" || { echo "not an aarch64 ELF: $so" >&2; exit 1; }
  strings "$so" > "$so.str"
  grep -q 'libc\.so\.6' "$so.str" || { echo "does not link glibc: $so" >&2; exit 1; }
  rm -f "$so.hdr" "$so.str"
}

check "$work/usr/lib/libvulkan_lvp.so"
meta "$work/usr/lib/libvulkan_lvp.so" "Mesa lavapipe (software)" "mesa 26.2.5"
check "$work/panvk/libvulkan_panfrost.so"
meta "$work/panvk/libvulkan_panfrost.so" "PanVK kbase (Mali)" "$panvk_tag"

(cd "$work/usr/lib" && zip -q -9 "$out/mali-lavapipe-linux-icd.zip" libvulkan_lvp.so meta.json)
(cd "$work/panvk" && zip -q -9 "$out/mali-panvk-kbase-linux-icd.zip" libvulkan_panfrost.so meta.json)

for z in "$out"/*.zip; do
  echo "== $(basename "$z") $(stat -c%s "$z") bytes"
  unzip -l "$z" | sed -n '4,6p'
  sha256sum "$z"
done
