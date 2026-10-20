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
- Root cause found in the 3.16.29 source: `CVulkanTexture::BInit` forces
  `flags.bExportable = true` whenever `flags.bFlippable` is set, so the flat texture *does* reach
  `vkGetMemoryFdKHR` regardless of what the constructor passed, and panvk on kbase answers `-2`.
  Fix written as patch **0130** (keep the texture, skip only the dmabuf and its backend import);
  the syncobj-fd scan stays as 0120 left it - opening mali0 would answer no DRM ioctls either way.

## 2026-10-20 · FristOneRR / Panvk-Driver v3.0 analysis

- Their Mali-G77 MC9 (Dimensity 8050) is marked **working** — encouraging for this device, but
  their driver is a **bionic** `.so` (`NEEDED liblog.so libhardware.so libnativewindow.so
  libsync.so libc.so`, no GLIBC symbols); DroidDeck's importer requires a glibc ELF, so it is not
  importable. Winlator runs it through a glibc↔bionic proxy (the Vortek architecture). **not
  usable here** — kept as reference: their v9 JM backend is the same generation that zen's glibc
  PanVK ports.

## 2026-10-20 · Sessions 11/12/13 (jdmg28's three zips on our build)

- Imported the three release zips of `jdmg28/DroidDeck-Mali` by hand into `0.5.0-mali`:
  `PanVK-kbase-v0.1.0-rc3_exact_BC_universal`, `PanVK-kbase-v0.1.0-rc3-lowmem_universal`,
  `Software-Vulkan-lavapipe`. All three reached the loader, each died one step in:
  - the kits: `loader_scanned_icd_add: … libvulkan_droiddeck_mali.so doesn't support interface
    version compatible with loader, skip this ICD` - the shim negotiates only after
    `dlopen(libpanvk_backend.so)` from beside itself, and fails when that file is absent;
  - lavapipe: `libLLVM.so.19.1: cannot open shared object file`.
  **failed** with one shared cause: `LinuxVulkanDriverManager.installDriver` kept only
  `libvulkan_*.so` + `meta.json` from a zip and threw every companion away - the kit's
  `libpanvk_backend.so`, lavapipe's whole `$ORIGIN` closure. Fixed: import every entry, flattened
  to its basename (our own `icd.json`/`meta.json` still win).

## 2026-10-20 · jdmg28/DroidDeck-Mali analysed and its fixes incorporated

- Their fork (Mali-G615 on a POCO X6 Pro, Steam Big Picture + Clicker Heroes through DXVK at
  ~29 fps on screen) runs the same zenithblue `panvk-kbase` and the same upstream app. Its Mali
  fix is made of: a 67 KB **ICD shim** (`libvulkan_droiddeck_mali.so`, C source inside their kit
  zip) that proxies the backend ICD and injects `VK_EXT_physical_device_drm` - primary/render set
  to `/dev/mali0`'s `st_rdev`, **only when the device name says Mali-G615** - against a session
  binding that exposes mali0 as a render node; the env knobs `PANVK_KBASE_DRI3=0` (present via
  CPU/SHM WSI; DRI3 present wants dma-buf export from kbase) and `LIBGL_KOPPER_DISABLE=false`;
  a **Debian trixie lavapipe zip** (mesa 25.0.7 + libLLVM19 + the whole NEEDED closure, every
  `.so` `patchelf`'d to `$ORIGIN`, CI-verified with the runtime's own loader); and session
  tunings (dxvk memory caps, fossilize layer off, steamwebhelper with `VK_ICD_FILENAMES=/dev/null`).
- What it means here: **on Mali-G77 the shim never injects** (its target filter is G615), so it
  loads as a plain proxy and gamescope keeps depending on our own 0120 - their
  mali0-as-render-node bind does not need porting. zen's RC3 added the **`jm-v9` series**,
  validated 38/38 on a Mali-G57 (v9 JM, uAPI 11.38) - this Mali-G77's exact generation - so RC3
  is expected to draw here, not merely enumerate.
- Ported into this fork: the importer companion-files fix (above), their mali env block
  (`PANVK_KBASE_DRI3=0` / `LIBGL_KOPPER_DISABLE=false`) in `SessionService`, gamescope patch
  **0130** for the export refusal, and the evidence recorded in the options doc. Their five
  gamescope patches (0002/0009/0019/0020/0112) were already ours - same Armada lineage.

## Open items

1. **Re-test the three zips** on the next build (importer fix + mali env + gamescope 0130, new
   `gamescope-3.16.29-mali-02` release): expected - kits negotiate and gamescope passes the
   flat-texture step; the next real gate is drawing (jm-v9 on G77) and then Steam itself.
2. **Own the artifacts**: port their `mali-panvk-driver.yml` and `software-vulkan-driver.yml`
   workflows so this fork publishes its own kit and lavapipe zips instead of hand-carrying theirs;
   retire the ALARM-built lavapipe half of `tools/mali/build-driver-zips.sh` (no LLVM closure, no
   `$ORIGIN` runpath) in their favour.
3. **Games on panvk**: once a session starts, Proton/DXVK 2.x needs device Vulkan 1.3; PanVK here
   is Vulkan 1.1, so games need DXVK 1.10 (or WineD3D). Separate step.
4. **Signing**: fork APKs use the repo test key (`versionCode` chain upgrades within the fork;
   official installs must be uninstalled first). Decide whether to adopt a fork release key.
5. **ICD `api_version` hardcode** (`1.1.274`): proven cosmetic for loading (the loader does not
   filter on it); DXVK gates on the device version. No change made, on purpose.
