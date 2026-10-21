# Rules

- Comments should be rare, factual, and explain only non-obvious external constraints or business invariants.

# This fork

`tushardan29-web/DroidDeck`, a MediaTek/Arm Mali fork of `Droid-Deck/DroidDeck`. Before testing
any Mali/Vulkan driver path, read `docs/development/mali-timeline.md` — it records what has already
been tried and why it passed or failed. The full delta against upstream is
`docs/development/mali-fork-changes.md`.

- **CI:** workflows never trigger on push here. After pushing, run `gh workflow run "Build APK" --ref main`
  and poll `gh run watch`. The `sign`, `announce` and `catalog` jobs always fail without the
  upstream signing secrets — cosmetic; the deliverable is the `droiddeck-apk` artifact of the
  green **Build APK** job. `versionCode`/`versionName` live in `app/build.gradle` (currently
  17 / `0.5.2-mali`).
- **Patched gamescope:** patches in `tools/gamescope/patches/`, documented in `PATCHES.md`;
  `tools/gamescope/release.env` pins the release the APK build downloads. To ship a patch:
  `gh workflow run "Build gamescope (patched)" --ref main -f tag=gamescope-3.16.29-mali-02`,
  then update `release.env` (tag + sha256 from the published `.sha256`) and rebuild the APK.
- **Build infrastructure:** `build.yml` fetches the hosted Debian image release from the same
  repository it runs in (`ci-debian-bullseye-r1`, mirrored from `Droid-Deck/DroidDeck-Components`).
  Keep that release; Docker Hub pulls of `debian:bullseye` are rate-limited.
- **Mali driver zips:** `tools/mali/build-driver-zips.sh` builds the importable ICD zips
  (lavapipe + PanVK kbase). The import contract — basename regex, aarch64-glibc ELF check,
  `meta.json`, generated `icd.json` with a hardcoded `1.1.274` api_version — is
  `app/src/main/java/com/droiddeck/launcher/gpu/LinuxVulkanDriverManager.java`.
- **Testing on the device:** realme RMX3031 (Dimensity 1200, Mali-G77 MC9, Android 13, kernel
  4.19). Inside a session `/dev/dri` holds only a synthetic `renderD<minor>` for `/dev/mali0`
  (`LinuxRuntime.bindGpuNode`); the host `card0` is the display controller and is not visible to
  the session. Session logs are app-private; the user exports them to
  `/storage/emulated/0/droiddecklog` for analysis. Extra session environment goes in
  `getExternalFilesDir(null)/droiddeck-env`.
