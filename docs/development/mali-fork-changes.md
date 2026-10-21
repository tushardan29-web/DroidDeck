# This fork vs upstream DroidDeck

What `tushardan29-web/DroidDeck` (branch `mali-support`, default `main`) changes compared with
upstream `Droid-Deck/DroidDeck`. The fork was cut at upstream `ebc7f89f` (itch.io store merge) and
synced with upstream `facaa18a..64b966af` on 2026-10-20 (`ecdff9b4`). Everything below is the
complete delta: 31 files, about 952 insertions. Inherited work from
[emir34-34/mediatek-mali-support](https://github.com/emir34-34/DroidDeck/tree/mediatek-mali-support)
is marked *(inherited)*; its history is the [changelog](../releases/mali-fork.md) and its lessons the
[timeline](mali-timeline.md).

## App (`app/src`, 17 files)

- **Mali detection** *(inherited)*: `DeviceSupport.kt`, `DeviceReport.kt`, `GpuInfo.kt` — a Mali
  (MediaTek Dimensity/Helio, identified from the GL driver, e.g. "Mali-G77 MC9") is classified
  experimental instead of unsupported, and reported in device reports.
- **Driver import** *(inherited, extended here)*: `LinuxVulkanDriverManager.java`,
  `LinuxVulkanDriver.java`, `TurnipDriver.java`, `DriverMenus.kt`, `DriverPairs.kt`,
  `GpuDriversPanel.kt`, `DriverPairsTest.kt`, `values/strings.xml`, `values-es/strings.xml` — the
  GPU drivers page accepts any glibc `libvulkan_*.so` (not only Turnip), selects the first imported
  driver on a Mali, and shows Mali-specific guidance instead of the Adreno pair cards. Import
  contract: basename-matched `libvulkan_[A-Za-z0-9_]+\.so`, aarch64 ELF linked against glibc, an
  optional `meta.json` (name/driverVersion/minGlibc/kind), and a generated `icd.json` whose
  `api_version` is hardcoded to `1.1.274` (a zip's own icd.json is ignored). Every *other* zip
  entry is kept beside the driver, flattened to its basename - the Mali kit's second library
  (`libpanvk_backend.so`) and lavapipe's whole `$ORIGIN` closure travel with the driver or it
  cannot load.
- **Session** *(inherited, plus ours)*: `LinuxRuntime.java` — `bindGpuNode` presents `/dev/mali0`
  inside the session as a synthetic `renderD<minor>` under a private `/dev/dri` (with a fake
  sysfs `DRIVER=panfrost`); the host `/dev/dri/card0` (MediaTek display controller) is not
  visible to the session. `SessionService.kt` — Mali session environment; this fork also added
  `VK_LOADER_DEBUG=error` after the Vulkan ICD is resolved so loader failures land in
  `session.log`, and on a Mali host `PANVK_KBASE_DRI3=0` (panvk-kbase then presents through
  CPU/SHM WSI instead of DRI3, which would want a dma-buf export kbase refuses) plus
  `LIBGL_KOPPER_DISABLE=false` — both taken from `jdmg28/DroidDeck-Mali`, the fork where the
  Mali env block was proven. `SessionActivity.kt`, `MainActivity.kt` — Mali flow wiring; a session
  started
  without a Mali runtime driver opens a dialog instead of dying silently.
- **Compositor** *(inherited)*: `app/src/main/cpp/waylandcomp/src/vk_present.c` — the compositor
  no longer requires dma-buf import extensions the phone's own Mali driver may lack.
- **Version** (ours): `app/build.gradle` — `versionCode 15`, `versionName "0.5.0-mali"`, so the
  fork's APK upgrades over the previous Mali build but is a separate install from official
  DroidDeck (signed with the repo test key, not the upstream release key).

## Gamescope (ours)

- `tools/gamescope/patches/0120-tolerate-vulkan-devices-without-drm-nodes.patch` — upstream
  gamescope refuses any Vulkan device that exposes no DRM primary/render node; every kbase-backed
  Mali driver hits that gate (it reports no DRM nodes at all). The patch keeps such devices and
  leaves the DRM bookkeeping unset. Documented in `tools/gamescope/PATCHES.md`.
- `tools/gamescope/patches/0130-continue-when-memory-cannot-be-exported.patch` — `BInit` forces
  every flippable texture exportable and failed the whole texture when `vkGetMemoryFdKHR`
  answered `-2` (panvk on kbase hands out no dma-bufs), so gamescope asserted on its very first
  flat texture. The image is kept - it only samples and composites - and just the dmabuf and its
  backend import are skipped; pipewire capture (`n_planes != 1` errors out) and a null backend fb
  already tolerate that.
- `tools/gamescope/release.env` — now pins this fork's own patched build,
  `tushardan29-web/DroidDeck` release `gamescope-3.16.29-mali-02` (sha256 `e3c08083…`), instead of
  upstream's `Droid-Deck/DroidDeck-Components` `gamescope-3.16.29-cursor-…` release. The APK's
  packaging fetches gamescope from this file, so the patch is live in every fork build.

## PanVK (inherited)

- `tools/panvk/build-panvk-kbase.sh` — builds an aarch64-glibc `libvulkan_panfrost.so` from
  [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) for
  import into the Linux runtime.
- `tools/panvk/patches/0001-kbase-report-node-rdev-as-drm-render-device.patch` — reports
  `/dev/mali0` as a DRM render/primary device.
- `tools/panvk/patches/0002-fix-provoking-vertex-fn-address-truncation.patch` — the G720 fix: a
  helper command stream was cut to 32 bits, hanging every queue on the first touch.

## Linuxfs preload (inherited)

- `tools/linuxfs/preload/drm.c` — GEM-handle emulation for PRIME ioctls, previously KGSL-only,
  now also treats kbase's `/dev/mali0` as the session's render node.

## Driver-zip tooling (ours)

- `tools/mali/build-driver-zips.sh` — builds the two importable ICD zips used for testing on this
  device: `mali-lavapipe-linux-icd.zip` (ALARM `vulkan-swrast`, Mesa 26.2 software Vulkan) and
  `mali-panvk-kbase-linux-icd.zip` (zenithblue's glibc PanVK RC3 for kbase), each with a
  `meta.json` recording the max GLIBC symbol. With `MALI_ADAPTER` set (the CI
  `mali-driver-kit.yml` workflow always sets it) the PanVK zip becomes the **adapter kit**: the
  DRM adapter is the ICD and the PanVK build ships beside it as `libpanvk_backend.so`.
  The zips and the rationale for each driver choice are in
  [mali-graphics-options.md](mali-graphics-options.md).
- `tools/mali/droiddeck_mali_icd.c` — the DRM adapter (CDLL ICD shim), derived from
  `jdmg28/DroidDeck-Mali`'s MIT G615 adapter: it forwards everything to
  `libpanvk_backend.so` and answers gamescope's `VK_EXT_physical_device_drm` query with the
  real device numbers of the session's Mali node. Changes from theirs: any ARM Mali gate
  (`Mali-` prefix), and a `/dev/dri/renderD*` scan fallback, because this fork's session does
  not bind `/dev/mali0` — it binds the kbase node as `/dev/dri/renderD<minor>`.
  `tools/mali/adapter-LICENSE` (MIT) and `tools/mali/adapter-NOTICE.txt` travel inside the kit.
- `.github/workflows/mali-driver-kit.yml` — builds the adapter (native gcc on an arm runner),
  runs `build-driver-zips.sh`, checks the zip layout the importer expects (first
  `libvulkan_*.so` = adapter, backend not named `libvulkan_*`), uploads `mali-driver-zips`.

## First-frame diagnosis and chosen fixes (2026-10-21)

Session 01 (56 min, 0.5.1-mali) proved why Steam mode never reaches READY — the whole chain in
one run: zenithblue's RC3 reports no DRM nodes, so gamescope's renderer DRM-fd lookup fails
(`wlserver: Failed to get DRM FD from renderer`, and our 0120 fallback finds no candidate node),
XWayland's glamor falls back to software (`GBM Wayland interfaces not available`), the guest has
**no DRI3**, and Steam's webhelper compositor window therefore fails `CreateOutputWindow:
failed to acquire a gl context` 338 times across 298 webhelper restarts — no window ever maps,
`frame.first` never fires, and the app waits forever at "Waiting for Steam's first frame".
KDE renders because Plasma is Wayland-native; the aggravator is Valve's `steamwebhelper.sh`
forcing `LIBGL_KOPPER_DISABLE=true`, which per
[panvk-kbase-upstream-report.md](panvk-kbase-upstream-report.md) bug 2 breaks exactly this GLX
path. `jdmg28`'s G615 kit already solved this with their adapter, field-proven with Steam UI
screenshots — gated to "Mali-G615", hence never firing here.

Chosen options (in order):

1. **A — env probe, no rebuild**: `SDL_VIDEODRIVER=wayland` in
   `Android/data/com.droiddeck.launcher/files/droiddeck-env` moves webhelper's compositor off
   X11/GLX onto Wayland/EGL → zink → gamescope WSI, which already works in this session (ANGLE
   and the desktop both present that way). Single variable, tested first.
2. **B — DRM adapter kit** (this change): import the new
   `mali-panvk-kbase-linux-icd.zip`, select *PanVK kbase (Mali, DRM adapter)*. Fixes the root
   gate — gamescope gets its DRM fd, XWayland can start accelerated, GLX/DRI3 return.
3. **C — deferred**: rebuild Zen's RC3 with `tools/panvk/patches/0001` (native rdev reporting,
   supersedes the adapter) and `0002` (the `fn_addr` uint32 truncation that freezes Steam's
   UI). RC3 still has that bug (`panvk_vX_cmd_draw.c:2073`, none of its 173 patches touch it),
   so C is required for long sessions even once the first frame lands.
4. **D — gamescope SHM present fallback** (`0140-present-via-shm-when-dma-buf-export-unavailable.patch`,
   chosen 2026-10-21 as "both, sequenced", first): after A+B the session reaches gamescope with a
   healthy renderer, but kbase still refuses every `vkGetMemoryFdKHR` (`VkResult: -2`), so patch
   0130 leaves the flippable textures without an exported dma-buf, `CWaylandPlane::Present` finds
   no backend framebuffer, and the Wayland backend attaches a **null** buffer — the toplevel never
   maps, the compositor reports `0 windows open`, and `frame.first` never fires (0 GPU frames and
   0 window opens across all of session 01; the two observed `window redraws` are cursor/decorations).
   GameScope's Wayland backend had no non-dma-buf path, so 0140 adds one: flippable images are
   flagged `bTransferSrc`, `CVulkanTexture::ReadbackToHost` copies the composed frame into a
   persistently mapped host buffer, and `CWaylandPlane::Present` attaches it as a ping-pong pair
   of wl_shm buffers (our compositor copies shm frames synchronously and releases immediately, so
   two buffers never stall). Gated by the new `shm_present_fallback` convar (default on; Adreno
   still takes the dma-buf path first), with heartbeat logging (`present heartbeat #…: visible=…,
   layers=… shm-readbacks=… null-presents=…`) to prove the paint path ran. The root fix stays
    sequenced after it: **driver-side dma-buf export** for kbase (ION/`/dev/dma_heap` placeable
    port, shipped with C), after which 0140's fallback simply stops firing.
    Session 01 of mali-03 then located an even earlier break: client commits carrying no
    exportable sync fd died in gamescope's intake — `CVulkanTexture::memoryFence()` returns -1
    on kbase, `g_ImageWaiter.AddWaitable` got `EBADF`, and the commit never signaled (4 commits,
    4 dead waitables, no `present heartbeat`, so paint never ran and 0140 never fired).
    `0150-accept-client-commits-when-no-sync-fd.patch` accepts fence-less commits as ready
    (existing dmabuf/timeline fences still wait), so D ships as 0150 + 0140: intake first,
    output second.

## Documentation (ours)

- `docs/development/mediatek.md` *(inherited)* — the original MediaTek/Mali support write-up
  (POCO X8 Pro, Mali-G720 MC8, what works and what does not).
- `docs/development/mali-graphics-options.md` — driver options evaluated for this device
  (lavapipe, PanVK/kbase, FristOneRR/bionic, Turnip, …) with evidence, including the
  `jdmg28/DroidDeck-Mali` G615 fork that already runs and what of it this fork took.
- `docs/development/mali-timeline.md` — every attempt so far with pass/fail results and open items.
- `docs/development/panvk-kbase-upstream-report.md` *(inherited)* — findings for the upstream
  PanVK kbase project.
- `docs/releases/mali-fork.md` *(inherited, extended here)* — release-by-release changelog.

## CI and infrastructure (on the fork, not in git)

- **Workflows never trigger on push** to this fork. After pushing, run
  `gh workflow run "Build APK" --repo tushardan29-web/DroidDeck --ref main` (same for
  `Build gamescope (patched)`).
- The `sign`, `announce` and `catalog` jobs always fail/skip (the fork has no `signing-main`
  secrets); that is cosmetic. The deliverable is the `droiddeck-apk` artifact of a green
  **Build APK** job.
- `build.yml` fetches its hosted Debian image release **from the same repository** the workflow
  runs in; Docker Hub's registry rate-limits `debian:bullseye` pulls. The upstream release
  `ci-debian-bullseye-r1` (`debian-bullseye.tar.zst`, sha256 `5ac13c6b…`) was therefore mirrored
  onto this fork's releases once; keep it when pruning releases.
- `Build gamescope (patched)` publishes the patched gamescope tarball to this fork's releases;
  `tools/gamescope/release.env` must be updated (tag + sha256 from the published `.sha256`) and
  the APK rebuilt for it to take effect.

## Deliberately unchanged

- `keystore/` — the fork signs with the repo test key; an official DroidDeck install must be
  uninstalled first, and vice versa. See the open question in the timeline.
- The ICD `api_version` hardcode (`1.1.274`) in `LinuxVulkanDriverManager` — the Vulkan loader
  does not filter ICDs by it, and DXVK gates on the *device's* version instead (PanVK here is
  Vulkan 1.1). Untouched on purpose.
