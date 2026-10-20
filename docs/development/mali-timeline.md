# Mali bring-up timeline

Every attempt on this fork's Arm Mali support, newest last, with what passed, what failed, and the
evidence. Device under test unless noted: realme RMX3031 (MT6893, Dimensity 1200, **Mali-G77 MC9**),
Android 13, kernel 4.19.191+. The inherited G720 work was tested on a POCO X8 Pro (Dimensity,
Mali-G720 MC8). Status chips: **passed** (worked as intended), **partial**, **failed** (did not
reach its goal), **open** (unresolved). Changelog per release: [mali-fork.md](../releases/mali-fork.md);
what each change is: [mali-fork-changes.md](mali-fork-changes.md).

## 2026-10-03/04 · Inherited: emir34-34's first bring-up (G720)

- PanVK kbase glibc r5 + APK `a7a0942`: Steam client reached Big Picture, scrolled. **passed**
  (client UI on G720).
- r6: removed two wrong-diagnosis patches, added the 32-bit address-truncation fix
  (`tools/panvk/patches/0002`); 120 touch gestures over ~5 min with no GPU fault, interface at
  0.45–0.7 GB. **passed** (the queue-hang fix).
- Games: never ran. The G720 has no BC (DXT) texture compression, so DXVK refuses the GPU.
  WineD3D got My Summer Car to its splash, then a crash in menu load. **failed** (games on G720 —
  hardware limitation, not fixable in software).

## 2026-10-19/20 · This fork: rebased on 0.5.0, version 15

- Merged emir34-34/mediatek-mali-support into upstream `ebc7f89f`, bumped to
  `versionCode 15 / 0.5.0-mali`, synced upstream `facaa18a..64b966af`. **passed** (clean merge,
  build green after CI fixes below).

## 2026-10-20 · CI on the fork

- First `Build APK` run failed at "Fetch the Debian image": `build.yml` looks for the hosted
  Debian image release **in the same repository**, and Docker Hub ECR pulls of `debian:bullseye`
  were rate-limited. Mirrored upstream release `ci-debian-bullseye-r1` (`debian-bullseye.tar.zst`,
  sha256 `5ac13c6b…`) onto this fork. **passed** after the mirror; note it when pruning releases.

## 2026-10-20 · Driver zips (tooling)

- `tools/mali/build-driver-zips.sh` built `mali-lavapipe-linux-icd.zip` (ALARM `vulkan-swrast`,
  Mesa 26.2, aarch64 glibc) and `mali-panvk-kbase-linux-icd.zip` (zenithblue zen glibc RC3,
  renamed `libvulkan_panfrost.so`); both imported in the app under
  `Mesa_lavapipe_software_` / `PanVK_kbase_Mali_`. **passed** (import contract works end to end:
  basename regex, glibc check, meta.json).

## 2026-10-20 · Sessions 06/07 (pre-patch gamescope, 0.5.0-mali v15)

- **lavapipe:** `vkCreateInstance: Found no drivers!` → gamescope aborted with `-9`. **failed.**
  (Cause found later: see session 09.)
- **panvk:** everything up to the device works — kbase uAPI **11.38** ok, MediaTek atom layout
  detected, `gpu_id 0x90800011` recognized as Mali-G77 v9 (the device-table fear from the options
  doc is gone), instance created, physical device `Mali-G77 MC9` selected. Then gamescope refused
  the device: *no primary node*. **partial** — the driver works; gamescope's DRM-node gate kills it.
- Lesson: upstream gamescope hard-requires a DRM primary node unless the Vulkan swapchain path is
  used, and a render node unconditionally. A kbase Mali driver reports neither, and the session's
  synthetic `renderD33` is not a DRM device at all (it is `/dev/mali0` under that name).

## 2026-10-20 · Gamescope patch 0120 + rebuilt gamescope + APK

- `tools/gamescope/patches/0120-tolerate-vulkan-devices-without-drm-nodes.patch`: when a device
  exposes neither DRM node, keep the device and leave DRM bookkeeping unset. Built via
  `Build gamescope (patched)` → release `gamescope-3.16.29-mali-01` (sha256 `51f2106e…`), pinned
  in `tools/gamescope/release.env`, APK rebuilt (run `39372068237`) and the embedded gamescope
  verified to contain the new code path. **passed** (patch builds, ships, and later sessions prove
  it executes).

## 2026-10-20 · Sessions 09/10 (post-patch APK)

- **lavapipe:** patch no longer blocks it; the loader now reports
  `libLLVM.so.23.1: cannot open shared object file` for `libvulkan_lvp.so`, ignores the JSON, and
  gamescope again finds no drivers. **failed** — rootfs r9 ships no LLVM (Turnip/Zink do not need
  one; the earlier 298-package closure check was against ALARM's package DB, not the pruned image).
  Fix planned: ship `libLLVM.so.23.1` (+ its deps) in the lavapipe zip and prepend the imported
  driver directory to `LD_LIBRARY_PATH` in `SessionService`.
- **panvk:** patch 0120 works exactly as designed (`exposes no DRM nodes; continuing without them`).
  Next failure, in order: *no DRM device found* (the candidate list `card0`/`renderD128` never opens
  — the session only has `renderD33`), then
  `vulkan: vkGetMemoryFdKHR failed (VkResult: -2)` in `vulkan_create_flat_texture`, asserted at
  `rendervulkan.cpp:3199`. **failed at the dma-buf export step** — this is the current blocker.
- In-progress diagnosis (3.16.29 source, scratch): `vkGetMemoryFdKHR` is only called when
  `flags.bExportable == true`, yet the flat/cursor texture path constructs with `bExportable =
  false`. The Arch packaging tag `3.16.29-1` (what the patched-gamescope build clones) is pristine
  upstream + our patches only, so the export path's trigger is still unexplained. Planned fix
  either way: gamescope patch 0121 tolerating `VK_ERROR_INITIALIZATION_FAILED` from
  `vkGetMemoryFdKHR` for these textures, plus extending the syncobj-fd scan to any file in the
  session's `/dev/dri` (so `renderD33` is found).

## 2026-10-20 · FristOneRR / Panvk-Driver v3.0 analysis

- Their Mali-G77 MC9 (Dimensity 8050) is marked **working** — encouraging for this device, but
  their driver is a **bionic** `.so` (`NEEDED liblog.so libhardware.so libnativewindow.so
  libsync.so libc.so`, no GLIBC symbols); DroidDeck's importer requires a glibc ELF, so it is not
  importable. Winlator runs it through a glibc↔bionic proxy (the Vortek architecture). **not
  usable here** — kept as reference: their v9 JM backend is the same generation that zen's glibc
  PanVK ports.

## Open items

1. **panvk / gamescope export** (current blocker): finish the `CVulkanTexture::BInit` read to find
   what forces the export path, then gamescope patch 0121 (tolerate `vkGetMemoryFdKHR` -2; scan the
   session `/dev/dri` for a syncobj fd). Rebuild gamescope → `release.env` → APK.
2. **lavapipe runtime**: add `libLLVM.so.23.1` (+ NEEDED deps) to the lavapipe zip; prepend the
   imported driver dir to `LD_LIBRARY_PATH` in `SessionService.kt`. Expect further missing sonames
   one per run — iterate.
3. **Games on panvk**: once a session starts, Proton/DXVK 2.x needs device Vulkan 1.3; PanVK here
   is Vulkan 1.1, so games need DXVK 1.10 (or WineD3D). Separate step.
4. **Signing**: fork APKs use the repo test key (`versionCode` chain upgrades within the fork;
  official installs must be uninstalled first). Decide whether to adopt a fork release key.
5. **ICD `api_version` hardcode** (`1.1.274`): proven cosmetic for loading (the loader does not
   filter on it); DXVK gates on the device version. No change made, on purpose.
