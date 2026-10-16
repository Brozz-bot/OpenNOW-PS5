# Game home-screen forwarding

An OpenNOW forwarder is a separate PS5 tile for one GeForce NOW catalog
variant. It starts OpenNOW `PPSA99082` with that variant's ID and store.
OpenNOW restores or refreshes the saved NVIDIA login, resolves the variant,
and requests its stream with saved stream settings. It skips catalog browsing.

## Requirements

- OpenNOW must be installed and registered as `PPSA99082`.
- ShadowMountPlus must scan `/data/homebrew` and support directory titles on
  the console's firmware. It owns registration through its TitleDir bridge or
  `sceAppInstUtilAppInstallTitleDir`. OpenNOW does not edit the shell database.
- A standard launcher must listen on `127.0.0.1:10199`, or a compatible
  elfldr must listen on `127.0.0.1:9021`. Each forwarder carries the standard
  launcher and sends it to elfldr when the launcher is absent.
- OpenNOW needs write access to `/data/homebrew`. Before creating workers,
  startup checks access and, if necessary, requests the existing filesystem
  elevation capability through the bundled, title-specific
  `opennow-forwarder-elevator.elf`. This grants broad process privileges, not
  a path-restricted permission. See [sandbox elevation](SANDBOX_ELEVATION.md).
  Normal browsing remains available when elevation is unavailable. An
  uncertain elevation result blocks startup until the title is closed.
- A tile does not grant a game license, change an NVIDIA subscription, or
  make an unsupported game playable. Creating artwork requires Internet access.

After creation, request a homebrew rescan in ShadowMountPlus. After removing
a folder, refresh or remove its registration there. Art is copied during
registration, so changing art requires registration again.

## Launch arguments

```text
--gfn-app-id <positive numeric catalog variant ID> [--store <store>]
```

The ID is `Game::id`, not the parent application's UUID, name, or store
product ID. Lookup uses upstream OpenNOW's `variantIds` filter. The current
GFN API accepts positive signed 32-bit variant IDs. A specified store must
match the resolved variant. Without a store, OpenNOW uses its catalog store.
Arguments are separate strings, not shell syntax.

The standard launcher passes the JSON argument array directly to
`sceSystemServiceLaunchApp`. OpenNOW accepts this array without an executable
name and also accepts a conventional leading `argv[0]`. Duplicate flags,
unknown flags, missing values, nonnumeric IDs, control characters in stores,
and oversized values are rejected. The parser bounds IDs to 95 bytes and
stores to 47 bytes before lookup validates the numeric API range.

For a synthetic catalog variant, the configuration is:

```json
{
  "format": "ps5-forwarder/1",
  "target": "PPSA99082",
  "creator": "OpenNOW PS5",
  "args": ["--gfn-app-id", "12345", "--store", "XBOX"]
}
```

`12345` is a test value, not an asserted ID for any released game.

With no saved account, sign-in remains on screen until authorization succeeds.
Unknown, unavailable, unentitled, and mismatched-store variants report an error
without allocating a session. A conflicting remote session is not stopped or
adopted. Circle returns from forwarded launch to the normal launcher without
signing out. During a stream, normal session controls apply.

## Writer API and UI integration

`src/forwarder/forwarder.hpp` exposes synchronous worker operations without
exceptions or RTTI:

- `create(const Game&, const Options&, const Job&)` creates a tile.
- `list(Visit, void*, const Paths&)` visits recognized OpenNOW tiles.
- `remove(const char* titleId, const Paths&)` removes one recognized tile.
- `Result` carries a status, title ID, count, and native error code.
- `message(result.code)` returns displayable status text.

The game-detail UI can wire **Add to Home Screen** as follows:

1. Copy the selected `Game` while holding the catalog-view mutex. Do not pass
   a pointer into a mutable catalog page.
2. Disable the action when `forwarder::available()` is false or a stream is
   active. Keep at most one operation queued.
3. Run `forwarder::create(game, {}, job)` on a worker. A 1 MiB stack exceeds
   the bounded encoder and file-operation frames. Do not run downloads,
   encoding, or filesystem work on the render or active streaming worker.
4. Publish progress through the UI's mutex or event queue. Callbacks execute
   on the calling worker and must not reenter writer operations.
   `Job::cancel` points to an atomic flag that must outlive the operation.
5. Show the result message and title ID. Treat `alreadyExists` as success.
   Offer the ShadowMountPlus rescan instruction.
6. Use `list` for management, and pass its selected `titleId` to `remove`.
   Refresh the list after completion.

`Options` accepts an injected artwork fetcher and alternate paths for host
tests. `Paths::collisionRoots` replaces the default console inventory with
caller-supplied roots. Supply every custom ShadowMountPlus scan root for a
nonstandard layout. An explicit empty list uses a nonnull array and count zero.
Do not replace `/data/homebrew` with sandbox storage because the mounter
cannot discover those folders.

The UI action is intentionally left for the parallel UI work. This change
does not edit `src/ui/tv_ui.cpp` or `src/ui/tv_ui.hpp`.

## Files, IDs, and atomicity

The writer follows [PS5 Forwarder Format version 1](../vendor/ps5-forwarder-format/SPEC.md).
Each tile contains the shared `eboot.bin` and `sce_module/libc.prx`, standard
`forwarder.json`, and `sce_sys/param.json`. The title name is the game name,
the category is game, and the content version is `01.000.000`.

Allocation starts at a stable hash of the variant ID and store within
`PPSA99200` through `PPSA99899`. It skips registry reservations, occupied
folders, embedded title IDs, installed app-metadata roots, and the default
internal and external homebrew scan roots. OpenNOW's own ID is never used.
An exclusive empty-directory reservation prevents cooperating writers from
allocating the same path. Existing apps and tiles are not overwritten.
Repeating creation for the same ID and store returns the existing tile,
including one made by Forwarder Manager with the same argument contract.

No offline allocator can reserve IDs against disconnected storage, future
installations, or an unlisted scan root. Attach relevant storage, supply custom
roots, and do not run another tile manager concurrently. PS5 has no private
title-ID allocation range that guarantees immunity from future installations.

The icon is a 512 x 512 PNG. Both backgrounds are 3840 x 2160 DX10 BC7 DDS
files, using hero artwork or the cover when no hero URL exists. Fetching,
JPEG validation, decoding, and cropping reuse existing artwork code. BC7
encoding resamples a 960 x 540 decoded image and streams blocks to disk,
without allocating a full 4K image or DDS buffer. PNG encoding streams
stored-deflate scanlines. Downloads remain capped at 1.5 MiB and source JPEGs
at 2048 x 2048. Publisher artwork is downloaded, not distributed with the app.

Creation synchronizes a hidden sibling folder, then renames it over its empty
reservation. The mounter never sees incomplete metadata. Cancellation and
ordinary write failures clean the stage. A power loss may leave a hidden stage
or an empty reservation, which can be removed while OpenNOW is closed.
Existing valid tiles are not rewritten on retry. Removal renames a recognized
tile out of the visible namespace before deleting it. An ownership record
allows interrupted deletion to finish on retry. Symlinks are not traversed.

## Verification and console testing

`bash tools/test-forwarder.sh` tests metadata against pinned `psfwd`, title-ID
collisions and exhaustion, standard-reader interoperability, cancellation,
failed publication, interrupted removal, symlink safety, argument parsing,
launch resolution, and encoded images. `bash tools/test-port.sh` includes
these checks under ASan and UBSan. `tools/verify-forwarder-template.py` checks
both vendored program hashes. The writer checks them before creation too.

Console tests are still required for elevation and retained sandbox paths,
native directory enumeration and writes, ShadowMountPlus registration and
removal, artwork display, launcher bootstrap, argv delivery, saved-login
refresh, and direct game streaming. Host and build checks do not prove those.

## Research sources

The implementation was checked against actual source in:

- `ps5-forwarder-format`: `SPEC.md`, `src/psfwd.c`,
  `forwarder/src/main.c`, and `launcher/launcher.c`.
- `ps5-forwarder-manager`: `src/fwd/store.cpp` and `src/fwd/image.cpp`.
- ShadowMountPlus: `include/sm_paths.h`, `src/sm_gameinfo.c`,
  `src/sm_scan.c`, and `src/sm_install.c`.
- `ps5-payload-dev/websrv`: `src/ps5/sys.c` and `src/ps5/hbldr.c`.
- etaHEN: `Source Code/util/source/common_utils.c`, `launchApp`.
- OpenNOW desktop: `native/opennow-core/src/gfn/catalog.rs`,
  `gfn/catalog_actions.rs`, `cloudmatch.rs`, and `artwork_cache.rs`.

Redistributed source and binary pins are in `upstream-lock.json` and
[third-party notices](../THIRD_PARTY_NOTICES.md).
