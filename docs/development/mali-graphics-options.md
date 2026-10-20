# Mali graphics: options, decisions and evidence

The device this fork targets is a realme RMX3031 (Dimensity 1200, **Mali-G77 MC9**,
Android 13, kernel 4.19.191, `/dev/mali0` = kbase **Valhall JM**, `/dev/dri/card0` =
mediatek-drm with no render node). Everything below is what was checked for it, in the
order the checks were made (2026-10-19/20).

## Where the failure actually is

The app's own compositor already uses the phone's `vulkan.mali.so` - that is what the
MediaTek changes in this branch enable (dma-buf extensions become optional, Turnip is
never picked). A session dies one layer in: `SessionService` forces
`MESA_LOADER_DRIVER_OVERRIDE=zink` and exports the runtime's `freedreno_icd.json`, so
every guest Vulkan call goes to Turnip, which finds no Adreno and returns
`vkEnumeratePhysicalDevices = VK_ERROR_INCOMPATIBLE_DRIVER`.

So the missing piece is **one glibc aarch64 Vulkan ICD inside the runtime**. The
existing import path (Contents > Installed > Linux runtime driver) is the delivery
mechanism: it accepts any `libvulkan_*.so` that is aarch64 ELF and links `libc.so.6`,
stages it under `files/linux_vulkan_drivers/<id>/`, writes `icd.json` with an absolute
`library_path`, and the session passes it on as `VK_DRIVER_FILES`.

## Dependency evidence (Arch Linux ARM, offline)

`tools/linuxfs/build-linuxfs.sh` resolves its seeds from ALARM's `desc` + `depends`
files (ALARM keeps `%DEPENDS%` in a separate file, not in `desc`). Re-running that
closure over `core`/`extra`/`alarm` gives **298 packages / 54,818 files**, and against
that set every one of the **24 `NEEDED` sonames of `libvulkan_lvp.so` is present**,
including `libLLVM.so.23.1` (llvm-libs, pulled in by the `mesa` seed) and
`libdisplay-info.so.3`. The same holds for PanVK's glibc build, whose sonames are a
subset of that list.

Consequence: **no runtime rebuild and no app change are needed for a software or
PanVK ICD** - only a zip the importer already understands.

## Options considered

| # | Option | Evidence | Verdict |
|---|--------|----------|---------|
| 1 | **Mesa lavapipe (llvmpipe)** - `vulkan-swrast` from ALARM | 14,435,320 B aarch64 glibc `.so`; own ICD manifest says `api_version 1.4.354`; max `GLIBC_2.38`; all 24 sonames in the runtime closure | **Chosen first.** Zero code, proves the pipeline, Steam Big Picture on CPU; games playable but slow |
| 2 | **PanVK over kbase** - `zenithblue-oss/panvk-kbase-android` RC3 | Ships a ready `libvulkan_panfrost-glibc-aarch64.so` (21,908,648 B, max `GLIBC_2.38`) built for proot/chroot runtimes; speaks Android's `mali_kbase` directly | **Chosen second, as a device test.** Their own docs: v9 (Valhall JM) is *experimental*, G77's `gpu_id 0x90800011` was missing from Mesa's model table (`vkEnumeratePhysicalDevices` fails), and `KBASE_IOCTL_MEM_ALLOC_EX` returned ENOTTY on 4.14-era kernels - ours is 4.19, so both are open questions |
| 2b | **FristOneRR PanVK 3.0** (`FristOneRR/FristOneRR-Panvk-Driver`, MIT, Mesa 26.3.0-devel) | Device table has **Mali-G77 MC9 with Dimensity 8050 - the same GPU as ours - marked working**; Valhall v9 JM is the main target; BC4-BC7 is emulated inside the driver; released 2026-10-20 | **Not importable: it is bionic.** `libvulkan_panfrost.so` needs `liblog.so`, `libhardware.so`, `libnativewindow.so`, `libsync.so`, `libc.so` and has no `GLIBC_` symbols, so it fails the importer's glibc check; Winlator loads it through `FristOneRR-Wrapper`, a glibc-to-bionic IPC proxy (the Vortek architecture this fork deferred). Kept as the **reference** for a glibc rebuild - their v9 JM work is what zenithblue's glibc build already ports |
| 3 | **Vortek** (`brunodev85/vortek`, LGPL-2.1) | glibc ICD client that proxies over IPC to a bionic server on the *system* Mali driver; emulates BCn textures Mali lacks, patches missing extensions | Deferred. Highest ceiling (DXVK becomes viable) but the server half must be embedded in this app - a separate project |
| 4 | **VirGL** (Winlator's Mali path) | Guest GL to an Android-side GLES renderer; community result is DX9-era titles only | Deferred: large new component, small payoff |
| 5 | Turnip / the Mali fork's PanVK | Turnip is Adreno-only; the `panvk-g720-kbase-csf` build is CSF, and G77 is JM | Ruled out |
| 6 | **`jdmg28/DroidDeck-Mali`** - the G615 fork's whole Mali path | Steam Big Picture, a KDE desktop and DXVK games (Clicker Heroes ~29 fps) photographed running on a Mali-G615 MC6 with the same zenithblue `panvk-kbase` RC3 and the same upstream app; their gamescope is **unpatched** - a 67 KB ICD shim in their kit fakes `VK_EXT_physical_device_drm`, but only for a device name matching "Mali-G615", and their session binds mali0 as a render node | **Pieces adopted**: the importer keeping companion files, `PANVK_KBASE_DRI3=0` + `LIBGL_KOPPER_DISABLE=false`, and the lavapipe recipe (Debian mesa + `libLLVM` + `$ORIGIN` runpaths). Their mali0-as-render-node bind is not ported - on a G77 the shim never fires and our gamescope 0120 covers the gate instead. zen RC3's `jm-v9` series (38/38 on a Mali-G57, v9 JM, uAPI 11.38 - this device's exact generation) is what makes their setup worth copying here |

Decision (2026-10-20): build **1**, then test **2**; **3** and **4** only if both fail
the device test.

## Import contract (what a driver zip must contain)

* any entry whose **basename** starts with `libvulkan_` and ends in `.so` - one only.
* aarch64 ELF, with the ASCII string `libc.so.6` (the glibc check).
* optional `meta.json` (any path): `name`, `driverVersion`, `minGlibc`, `kind`.
* **every other entry is kept** beside the driver, flattened to its basename - a second
  library the ICD `dlopen`s (`libpanvk_backend.so` in the Mali kit) or a `$ORIGIN`
  closure (`libLLVM.so.19.1` and friends in a lavapipe zip). The zip's own
  `icd.json`/`meta.json` are the exceptions: the importer writes both itself. Until
  2026-10-20 the importer silently dropped everything else, which is why sessions
  11/12/13 could not load either kind of zip.
* The importer keeps the library's own name when it matches
  `libvulkan_[A-Za-z0-9_]+\.so`. Hyphens fail that pattern - `libvulkan_panfrost-
  glibc-aarch64.so` would be renamed to `libvulkan_freedreno.so` - so the file is
  renamed to `libvulkan_panfrost.so` **inside the zip**.
* `icd.json` is generated by the importer with a hard-coded `api_version` of
  `1.1.274`. Open question: if the loader or DXVK (which wants Vulkan 1.3) filters on
  it, a driver that really reports 1.4 will look like a 1.1 driver. Neither zip can
  fix that from the outside; changing the importer to take the version from the
  source zip's own `icd.json` is the fix if it shows up.

## Where the built zips live

`tools/mali/build-driver-zips.sh` recreates both from their upstream sources (nothing is
vendored in the tree; output defaults to the gitignored `build/mali-driver-zips/`):

* `mali-lavapipe-linux-icd.zip` - ALARM `vulkan-swrast` (`mesa 1:26.2.5-1`)
* `mali-panvk-kbase-linux-icd.zip` - panvk-kbase-android `panvk-kbase-v0.1.0-rc3`

Both were also copied to `/storage/emulated/0/Download/` for the in-app picker. Import
either under **GPU drivers > Linux runtime driver**, leave the display driver on Auto,
and pick it in the same tab's runtime row. The FristOneRR zip is deliberately absent -
it is bionic, and the importer rejects anything that does not link glibc.

The ALARM-built lavapipe zip is **superseded for real use** by
`DroidDeck-Software-Vulkan-lavapipe.zip` from `jdmg28/DroidDeck-Mali`'s releases: theirs
bundles Debian's mesa 25.0.7 with `libLLVM.so.19.1` and its whole NEEDED closure, every
`.so` `patchelf`'d to `$ORIGIN`, and was CI-verified with the runtime's own loader -
ours has no LLVM and no runpath. The kits (`DroidDeck-Mali-CSF-PanVK-kbase-*.zip`,
`…-rc3-exactBC-universal`) come from the same releases.
