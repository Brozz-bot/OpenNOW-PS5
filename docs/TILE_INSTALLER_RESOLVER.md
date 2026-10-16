# Tile installer PID lookup

The installer uses read-only process metadata to associate the requesting PID
with one sandbox. It never scans `/mnt/sandbox` or looks for marker files.

`sceKernelGetAppInfo(pid, ...)` must return exactly `PPSA99082` before and
after the lookup. The two calls must also report the same application ID.
Between those calls, `sysctl(CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, pid)`
must return exactly one valid root-directory vnode. Its path must match
`/mnt/sandbox/PPSA99082_NNN`, with exactly three decimal suffix digits.
The installer rejects missing, ambiguous, truncated, oversized, or malformed
records. CWD, jail, executable, and ordinary file entries cannot supply the root.

## Source evidence and limits

These sources establish the public ABI and upstream PID-to-root semantics.
They do not establish that Sony's shipped kernel exposes this query on every
firmware. None of the inspected PS5 projects directly exercises this root-entry
lookup. A console test must confirm visibility, record layout, and path output
from the independent elfldr payload. An unsupported query fails closed without
elevation or a guessed-directory fallback.

- The [SDK v0.42 sysctl header](https://github.com/ps5-payload-dev/sdk/blob/4eb701204fc3f8d31e84cf8ca272974e2be9c867/include/freebsd/sys/sysctl.h#L904)
  declares `KERN_PROC_FILEDESC`. Its [process header](https://github.com/ps5-payload-dev/sdk/blob/4eb701204fc3f8d31e84cf8ca272974e2be9c867/include/freebsd/sys/user.h#L337-L408)
  declares `struct kinfo_file`, including the root descriptor, vnode type,
  attribute-valid flag, packed record length, and path. Native parsing uses
  those declarations and `offsetof`, not numeric kernel-memory offsets.
- The SDK identifies these headers as FreeBSD releng/11.4. The matching
  [read-only handler](https://github.com/freebsd/freebsd-src/blob/d106002967eff712894ab8b5337bbbff106ce5e0/sys/kern/kern_descrip.c#L3599-L3620)
  selects the fourth MIB component as the PID. Its [root export](https://github.com/freebsd/freebsd-src/blob/d106002967eff712894ab8b5337bbbff106ce5e0/sys/kern/kern_descrip.c#L3558-L3562)
  associates that process's `fd_rdir` with `KF_FD_TYPE_ROOT`.
  [Vnode export](https://github.com/freebsd/freebsd-src/blob/d106002967eff712894ab8b5337bbbff106ce5e0/sys/kern/vfs_vnops.c#L2345-L2359)
  fills the type and path. [Path resolution](https://github.com/freebsd/freebsd-src/blob/d106002967eff712894ab8b5337bbbff106ce5e0/sys/kern/vfs_cache.c#L2194-L2212)
  is relative to the caller's root, so the installer must already run with
  elfldr's normal global root. The app remains sandboxed.
- [Websrv's payload loader](https://github.com/ps5-payload-dev/websrv/blob/7748df9d2a43822a5ebfda149f43d4f2696d640b/src/ps5/elfldr.c#L328-L346)
  demonstrates that independent loader environment. Its root/jail mutation is
  upstream loader context, not code copied into OpenNOW or the installer.
- [etaHEN's AppInfo declaration](https://github.com/etaHEN/etaHEN/blob/dafa13b562ddb137a4b4a97b9aaa287c0c57cc9c/Source%20Code/util/source/common_utils.c#L26-L41)
  and PID query at lines 416-418 provide the selected title layout, with
  `app_type` before `title_id[10]`. The same layout occurs in its daemon and
  ShellUI sources. Compile-time assertions check the title offset `0x14` and
  structure size `0x60`.
- [ShadowMountPlus declares a different AppInfo layout](https://github.com/drakmor/ShadowMountPlus/blob/98c524890176c7b474fcb03387bec88c4a559861/include/sm_platform.h#L31-L36),
  with the title at `0x10`. The installer chooses etaHEN's repeated layout
  rather than probing multiple offsets. This disagreement requires console
  confirmation; a mismatched layout is rejected.
- etaHEN's [marker search](https://github.com/etaHEN/etaHEN/blob/dafa13b562ddb137a4b4a97b9aaa287c0c57cc9c/Source%20Code/daemon/source/commands.cpp#L667-L679)
  supports the three-digit path grammar, but does not establish PID ownership.
  ShadowMountPlus's [highest-suffix search](https://github.com/drakmor/ShadowMountPlus/blob/98c524890176c7b474fcb03387bec88c4a559861/src/sm_fakelib.c#L397-L438)
  also cannot establish ownership. Neither mechanism is used.

The SDK source headers match the installed, hash-verified SDK v0.42 archive.
Research revisions are recorded in `upstream-lock.json`. No implementation
from these research repositories is redistributed by this lookup.

The helper uses the SDK's ordinary payload CRT. Its `payload_init` calls
`__patch_init`, which adjusts capabilities and attributes for its own PID
from `SYS_getpid`. These are active SDK startup writes in the independent
helper, not dormant symbols. The requesting app PID is never passed to that
startup code. Project resolver and dispatcher code does not call credential
or root setters. OpenNOW uses the separate application CRT, not the payload CRT.

## Verification

`tests/sandbox_resolver_test.cpp` injects identity results and packed snapshots
into the same parser used by the native payload. It covers decoys, wrong and
unterminated titles, changed application IDs, duplicate roots, bad attributes,
path grammar, record boundaries, query failures, and output capacity.
`bash tools/test-forwarder.sh` runs it with ASan and UBSan. The payload build
also compiles the real SDK record type and read-only imports.
