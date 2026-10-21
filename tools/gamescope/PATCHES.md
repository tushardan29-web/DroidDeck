# gamescope patches carried by the app

Built by `.github/workflows/build-gamescope.yml` on top of the exact gamescope the Linux runtime
ships (3.16.29, Arch Linux ARM's package, same build options), and staged from the apk over
`/usr/local/bin/gamescope` at each session start - the hosted runtime image is never touched.
The binary's shared-library needs are checked against `runtime-sonames.txt`, the runtime's own
library list, before anything is published.

- `0002-steamcompmgr-fallback-appid-focus.patch` - Armada (armada-os/armada), verbatim.
- `0009-fix-arm64-steam-night-mode.patch` - Armada, verbatim: the ARM64 client packs the
  night-mode property differently; the slider did nothing.
- `0019-steamcompmgr-arm64-virtual-white.patch` - Armada, verbatim: the colour-temperature
  slider's (x, y) arrives as one 64-bit element from the ARM64 client; y is recovered from x.
- `0020-color-p3-red-is-wide-gamut.patch` - Armada, verbatim.
- `0100-realtime-queue-and-gamepad-cursor.patch` - this app, two of Armada's ported by hand onto
  3.16.29: realtime-priority Vulkan queues on request (`GAMESCOPE_FORCE_VULKAN_REALTIME=1`)
  without CAP_SYS_NICE, which proot can never have (a no-op on KGSL Turnip, which has a single
  submit-queue priority); and the gamepad-driven cursor sprite following the X pointer that XTest
  moves (it sat frozen). The X pointer is asked for only while a cursor image is drawn - every
  vblank while shown, every 50 ms while hidden for inactivity - since each ask is a blocking round
  trip to Xwayland on the paint thread; with no image, wlserver's position is used as upstream does.
- `0101-nested-wayland-cursor-routing.patch` - this app: `gamescope_cursor_composite=2` lets the
  nested Wayland session paint its cursor in the output using the real X pointer position; client
  cursor surfaces are cleared to prevent a duplicate at the host pointer, while Gamescope's original
  default host-cursor fallback is preserved when its no-keyboard-focus policy selects it. Empty
  client cursor images and Gamescope's hidden flag still suppress the painted cursor.
- `0110-wayland-backend-touch.patch` - this app: the nested Wayland backend bound only the host's
  pointer and keyboard, so a finger on the phone's screen never reached Steam. It now binds
  `wl_touch` too and hands each finger to wlserver's touch path (`wlserver_touchdown` / `motion` /
  `up`) - the one a Steam Deck's touchscreen drives - so what a touch does follows the client's
  touch mode (Steam's Big Picture sets Passthrough: a real touch, rows scroll under a finger).
  Finger ids are offset by one, since the nested pointer already moves wlserver's touch 0.
- `0111-wayland-pointer-warps-in-passthrough.patch` - this app: the nested pointer's motion goes to
  wlserver as touch 0, and in Passthrough (Big Picture's touch mode) a motion for a touch that is not
  down moves nothing - so in the app's touchpad mode the Steam client saw no hover and a click landed
  wherever the pointer had last been. The motion now always warps the real pointer as well
  (`bAlwaysWarpCursor`), which the other touch modes did already.
- `0112-restore-iconified-game-on-resume.patch` - this app: the Steam menu is an overlay that
  takes input without changing the focus window, and a fullscreen wine game minimizes itself when
  it loses input. gamescope only takes a window out of iconic when the focus window changes, and
  wine will not activate a window it believes iconic, so after Resume the game stayed minimized: a
  black screen with its small caption in the top-left corner (Titanfall 2, GE-Proton 11). The
  iconify request is remembered and the window goes back to NormalState before input returns to it,
  then focus is handed over again. `GAMESCOPE_RESTORE_FOCUS_WINDOW` on the root window asks for the
  same restore from outside (the session script's resume watcher).
- `0114-take-override-redirect-from-mapnotify.patch` - upstream (ValveSoftware/gamescope 3829340),
  verbatim; drop it once the runtime's gamescope includes it. Wine creates a game's window
  override-redirect and makes it managed before mapping it, which sends no X event. When gamescope
  read the window's attributes first, it kept the window as override-redirect and left it out of
  `GAMESCOPE_FOCUSABLE_WINDOWS`, so the client kept its loading screen over the running game
  (Skyrim SE, A Plague Tale: Innocence, on the games' own Xwayland). MapNotify now refreshes the flag.
- `0115-limiter-paces-per-window.patch` - this app: on a fixed-refresh output the frame limiter
  (Steam's Frame Limit) let a capped window's frames out only on every Nth refresh of a global grid,
  so a frame finished just after its slot waited a whole extra slot. At 30 on 60 Hz one picture was
  held for three or four refreshes - "small stops and ladders" with frame generation - where a game's
  own limiter slips such a frame by one refresh. The fixed-refresh branch now paces each window by
  time, as the VRR branch already did: its next frame goes out on the first refresh a capped interval
  after its last one, with half a refresh of tolerance so an on-time window keeps every Nth refresh.
- `0120-tolerate-vulkan-devices-without-drm-nodes.patch` - this app: gamescope refused to init
  any Vulkan device that exposes no DRM primary/render node (`rendervulkan.cpp` returns false
  before anything else runs), which is every driver that talks to a Mali GPU through kbase rather
  than through a KMS driver - the phone's `/dev/dri/card0` is the display controller, a different
  device, so the check could never pass on this hardware. When a device exposes neither node the
  device is now kept and only the primary/render bookkeeping stays unset; syncobj imports are the
  one thing that needs an fd, so any DRM card is opened for those (a render node here may be the
  Mali device bound under that name, which answers no DRM ioctls). Adreno/Turnip keeps the original
  path - it reports both nodes - so nothing changes for the runtime's default driver.
- `0130-continue-when-memory-cannot-be-exported.patch` - this app: `BInit` forces every flippable
  texture exportable (`flags.bFlippable` sets `flags.bExportable`, `rendervulkan.cpp`) and failed
  the whole texture when `vkGetMemoryFdKHR` refused a dma-buf - which panvk on kbase does with
  `VkResult: -2`, so gamescope died on its first flat texture (`vulkan_create_flat_texture`
  asserts on `bRes`). The image itself never needed the fd: it samples and composites inside
  gamescope all the same. The failure is now a warning, `m_dmabuf` stays empty (`n_planes == 0`),
  and the flippable backend import is skipped when there is no plane to import; the dmabuf's only
  consumers - pipewire capture (`n_planes != 1` errors out) and a null backend fb - tolerate that
  already. Adreno/Turnip still exports, so the success path is unchanged.

- `0140-present-via-shm-when-dma-buf-export-unavailable.patch` - this app: on Mali kbase the
  Vulkan device never hands out a dma-buf (`vkGetMemoryFdKHR` → `VkResult: -2`), so after 0130
  the composed frame exists but has no exported fd, `CWaylandPlane::Present` finds no backend
  framebuffer and attaches a **null** buffer - the toplevel never maps, the compositor keeps
  reporting `0 windows open`, and the app waits forever at "Waiting for Steam's first frame"
  (session 01: zero window opens, zero GPU frames in 56 minutes; the two `window redraws` are
  cursor/decorations). Flippable images are now flagged `bTransferSrc`, a new
  `CVulkanTexture::ReadbackToHost` copies the composed frame into a persistently mapped host
  buffer (4-byte-per-pixel formats, GENERAL layout restored after the copy), and the plane
  attaches it as a ping-pong pair of wl_shm buffers - safe here because our compositor copies
  shm frames synchronously and releases the wl_buffer immediately. Gated by the new
  `shm_present_fallback` convar (default on; the dma-buf path is still tried first, so Adreno
  never enters the fallback), with a `present heartbeat` log line to prove the paint path ran.
  The root fix (kbase dma-buf export via the ION/dma-heap placeable port, shipped with the
  driver rebuild) makes this fallback go quiet by itself.

- `0150-accept-client-commits-when-no-sync-fd.patch` - this app: a client buffer with no dmabuf
  (wl_shm, or an export this device refuses) fell through to `CVulkanTexture::memoryFence()`,
  which exports the texture's memory as a sync fd - impossible on kbase, so it printed
  `vkGetMemoryFdKHR failed` and returned -1. The commit was then registered with
  `g_ImageWaiter.AddWaitable`, whose `epoll_ctl(-1)` logged `Failed to add waitable: Bad file
  descriptor`; the commit never signaled, `hasRepaint` never set, and gamescope never painted a
  frame at all (session 01 of mali-03: exactly 4 client commits, 4 dead waitables, no `present
  heartbeat`, 0 windows while Steam's UI ran). With no fence to wait on such commits are now
  accepted as ready (`bKnownReady`, the path pre-signaled timeline commits already take); a
  fence that exists - dmabuf fd or acquire timeline eventfd - still gets its wait. Complements
  0140: this unblocks the client -> gamescope intake, 0140 the gamescope -> compositor output.

Sixteen more of Armada's patches are DRM/lease/HDR-on-KMS work for a native display, which this
app's Wayland-hosted gamescope never reaches, or need a newer gamescope than the runtime has.
