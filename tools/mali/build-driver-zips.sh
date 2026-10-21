#!/usr/bin/env bash
# Builds the glibc aarch64 Vulkan ICD zips a Mali phone imports under
# GPU drivers > Linux runtime driver. Both are fetched, never vendored:
#   - Mesa lavapipe from Arch Linux ARM (software rendering, Vulkan 1.4)
#   - PanVK kbase from zenithblue-oss/panvk-kbase-android (talks to /dev/mali0)
# With MALI_ADAPTER pointing at a prebuilt DRM adapter .so (see
# droiddeck_mali_icd.c and .github/workflows/mali-driver-kit.yml), the PanVK
# zip becomes the adapter kit: the adapter is the ICD, the PanVK build ships
# beside it as libpanvk_backend.so, which is what the adapter dlopens and
# what the importer keeps as a companion library.
# Usage: tools/mali/build-driver-zips.sh [output dir]   (default build/mali-driver-zips)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../../build/mali-driver-zips}
mkdir -p "$out"; out=$(cd "$out" && pwd)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

mirror=http://mirror.archlinuxarm.org/aarch64/extra
swrast_pkg=${VULKAN_SWRAST_PKG:-vulkan-swrast-1%3A26.2.5-1-aarch64.pkg.tar.xz}
panvk_tag=${PANVK_TAG:-panvk-kbase-v0.1.0-rc3}
panvk_sha=${PANVK_SHA256:-43d4783f664f21843b2fb2d5a3fdf4646fadb942e42d320ccb0e7449bd8e352f}
panvk_url=https://github.com/zenithblue-oss/panvk-kbase-android/releases/download/$panvk_tag/libvulkan_panfrost-glibc-aarch64.so
adapter=${MALI_ADAPTER:-}
adapter_version=${MALI_ADAPTER_VERSION:-0.2.0}

curl -fsSL --retry 5 -o "$work/swrast.pkg.tar.xz" "$mirror/$swrast_pkg"
tar -xf "$work/swrast.pkg.tar.xz" -C "$work" usr/lib/libvulkan_lvp.so
mkdir -p "$work/backend"
curl -fsSL --retry 5 -o "$work/backend/panvk-backend.so" "$panvk_url"
if [ -n "$panvk_sha" ]; then
  echo "$panvk_sha  $work/backend/panvk-backend.so" | sha256sum -c -
fi

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
check "$work/backend/panvk-backend.so"

if [ -n "$adapter" ]; then
  # The adapter kit: first libvulkan_*.so in the zip is the ICD the importer
  # adopts; the backend must keep a name that is not libvulkan_* so it travels
  # as a companion the importer copies beside it.
  kit=$work/panvk
  mkdir -p "$kit"
  check "$adapter"
  strings "$adapter" > "$adapter.str"
  grep -q 'libpanvk_backend\.so' "$adapter.str" || {
    echo "the adapter does not look for libpanvk_backend.so" >&2; exit 1; }
  grep -q 'VK_EXT_physical_device_drm' "$adapter.str" || {
    echo "the adapter does not implement the DRM facade" >&2; exit 1; }
  rm -f "$adapter.str"
  cp "$adapter" "$kit/libvulkan_droiddeck_mali.so"
  mv "$work/backend/panvk-backend.so" "$kit/libpanvk_backend.so"
  cp "$here/adapter-NOTICE.txt" "$kit/NOTICE.txt"
  cp "$here/adapter-LICENSE" "$kit/adapter-LICENSE"
  meta "$kit/libpanvk_backend.so" "PanVK kbase (Mali, DRM adapter)" "$panvk_tag + adapter $adapter_version"
  (cd "$kit" && zip -q -9 "$out/mali-panvk-kbase-linux-icd.zip" \
    libvulkan_droiddeck_mali.so libpanvk_backend.so meta.json NOTICE.txt adapter-LICENSE)
else
  kit=$work/panvk
  mkdir -p "$kit"
  mv "$work/backend/panvk-backend.so" "$kit/libvulkan_panfrost.so"
  meta "$kit/libvulkan_panfrost.so" "PanVK kbase (Mali)" "$panvk_tag"
  (cd "$kit" && zip -q -9 "$out/mali-panvk-kbase-linux-icd.zip" libvulkan_panfrost.so meta.json)
fi

(cd "$work/usr/lib" && zip -q -9 "$out/mali-lavapipe-linux-icd.zip" libvulkan_lvp.so meta.json)

for z in "$out"/*.zip; do
  echo "== $(basename "$z") $(stat -c%s "$z") bytes"
  unzip -l "$z" | sed -n '4,12p'
  sha256sum "$z"
done
