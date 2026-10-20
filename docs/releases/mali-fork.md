# DroidDeck MediaTek / Mali fork: changelog

Experimental Arm Mali (MediaTek) support, inherited from
[emir34-34's fork](https://github.com/emir34-34/DroidDeck) (changelog below) and continued on
`tushardan29-web/DroidDeck`. Newest first. Emir34-34's APKs and PanVK driver are published on the
[original fork's releases](https://github.com/emir34-34/DroidDeck/releases); this fork builds its
APK through its Actions **Build APK** workflow (`droiddeck-apk` artifact) and its patched gamescope
through **Build gamescope (patched)**. What each change is:
[mali-fork-changes.md](../development/mali-fork-changes.md); what has been tried:
[mali-timeline.md](../development/mali-timeline.md); the original write-up:
[mediatek.md](../development/mediatek.md).

## 2026-10-20 · This fork: G77 MC9 bring-up, patched gamescope, driver zips (v15, `7a8b4784`)

Tested on a realme RMX3031 (Dimensity 1200, Mali-G77 MC9, Android 13) — a different GPU generation
than the G720 this work was inherited from.

### App
- Rebased onto DroidDeck 0.5.0 (`versionCode 15`, `0.5.0-mali`); upstream `facaa18a..64b966af`
  merged in. Mali detection, driver import, and the Mali session environment from emir34-34's work
  carry over unchanged.
- `VK_LOADER_DEBUG=error` is set after the ICD is resolved, so Vulkan loader failures (missing
  ICD libraries, JSON problems) land in `session.log` instead of being lost.

### Gamescope
- New patch `0120-tolerate-vulkan-devices-without-drm-nodes`: upstream gamescope refuses any
  Vulkan device that exposes no DRM primary/render node — every kbase-backed Mali driver hits that
  gate. The patch keeps the device with DRM bookkeeping unset. Verified live in sessions 09/10.
- The patched build is published on this fork's releases as
  `gamescope-3.16.29-mali-01` and pinned in `tools/gamescope/release.env`, so every APK build
  ships it.

### Tooling and docs
- `tools/mali/build-driver-zips.sh` builds the two test ICD zips: `mali-lavapipe-linux-icd.zip`
  (Mesa 26.2 software Vulkan) and `mali-panvk-kbase-linux-icd.zip` (zenithblue zen glibc RC3 for
  kbase); both import cleanly in the app.
- CI: the hosted Debian image release (`ci-debian-bullseye-r1`) is mirrored onto this fork because
  the build fetches it from the running repository and Docker Hub pulls were rate-limited.
- New records: [mali-graphics-options.md](../development/mali-graphics-options.md) (driver options
  with evidence), [mali-timeline.md](../development/mali-timeline.md) (every attempt, pass/fail),
  [mali-fork-changes.md](../development/mali-fork-changes.md) (full delta vs upstream).

### Current state on the G77 MC9
- PanVK passes everything it can: kbase uAPI 11.38, MediaTek atom layout, `gpu_id 0x90800011`
  recognized, instance and physical device created. With patch 0120 gamescope now keeps the
  device; the next failure is `vkGetMemoryFdKHR` returning -2 while creating a flat texture, which
  gamescope treats as fatal. Steam UI does not come up yet; see the timeline's open items.
- lavapipe is blocked one layer earlier: the rootfs has no `libLLVM.so.23.1`, so the loader
  ignores the ICD entirely.

## 2026-10-04 · PanVK kbase glibc r6 (APK unchanged, `91516fa`)

**Current state:** the Steam client works. Games do not run yet. DXVK (D3D10/11) refuses this
Mali-G720 because it has no BC texture compression in hardware. With WineD3D, My Summer Car reaches
its splash screen and crashes while loading the menu.

## 2026-10-04 · PanVK kbase glibc r6 (APK unchanged, `91516fa`)

- **Fixes Steam's interface freezing on the first touches, and its memory blowing up to 3 GB.** The
  driver called a helper command stream through an address cut to 32 bits. On Android's Mali kernel
  driver GPU addresses are high, so the GPU jumped to an invalid address (a translation fault at
  `0xfffef000`) and every queue stopped. One-line fix in `tools/panvk/patches/0002`.
- **Removes r5's patches 0002 and 0003.** They were built on a wrong diagnosis (a queue deadlock)
  and only changed timing.
- Tested on a POCO X8 Pro (Mali-G720): 120 scroll/tap/back gestures in Big Picture over ~5 minutes,
  with no GPU fault and the interface process at 0.45-0.7 GB.

## 2026-10-04 · APK `91516fa`

- **GPU drivers page on a Mali:** the Auto/Manual pair cards and the "No drivers listed yet" list
  only ever applied to Adreno. On a Mali they are replaced by what to set instead: the imported
  PanVK as the runtime driver, and the display driver on Auto (the phone's own Mali driver).

Driver: unchanged (PanVK kbase glibc r5).

## 2026-10-03 · APK `a7a0942` + PanVK kbase glibc r5

First published build.

### App
- **Mali detection:** a Mali (MediaTek Dimensity/Helio) is recognised and named from the GL driver
  ("Mali-G720 MC8"). It is marked experimental instead of unsupported.
- **Display:** the screen is drawn with the phone's own Mali Vulkan driver. The compositor no longer
  requires dma-buf import extensions the driver may lack.
- **Linux runtime driver import:** accepts any glibc `libvulkan_*.so` (such as PanVK's
  `libvulkan_panfrost.so`), not only Turnip. On a Mali the first imported driver is selected.
- **Runtime:** `/dev/mali0` is presented as the session's GPU render node. The GPU clock is read from
  the Mali devfreq node for the performance overlay.
- **Clear errors:** starting Steam on a Mali with no Linux runtime driver opens a dialog pointing to
  GPU drivers, instead of a session that ends at once. The session-ended screen also says when
  gamescope found no GPU, or when the runtime still ran Turnip.

### Driver: PanVK kbase glibc r5
- PanVK for Android's Mali kernel driver (kbase/CSF), from
  [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) `ca16389`,
  built for the Linux runtime (aarch64 glibc) by `tools/panvk/build-panvk-kbase.sh`.
- `0001`: reports `/dev/mali0` as the DRM render and primary device, which gamescope needs.
- `0002`: cross-queue syncs reach queues in other groups (SYSTEM scope on kbase).
- `0003`: fixes the deadlock that froze Steam's interface and grew it to 3 GB on the first touch: the
  geometry queue may run at most 8 render passes ahead of the fragment queue.
- Tested on a POCO X8 Pro (MT6899, Mali-G720 MC8): the Steam client reaches the Big Picture library
  and scrolls.

### Known issues
- A GPU queue timeout can still freeze Steam's interface in longer use.
- Games (Proton/DXVK) are untested.
