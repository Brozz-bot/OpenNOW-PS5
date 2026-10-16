# Template inputs

OpenNOW keeps the unmodified, pinned `eboot.bin` and `sce_module/libc.prx`
under `assets/forwarder-template` instead of duplicating them here.
See that folder's `UPSTREAM.md` and `upstream-lock.json`.

The standard launcher is embedded in `eboot.bin`. To regenerate its standalone
ELF from the retained source, run this directory's parent Makefile with
`make launcher PS5_PAYLOAD_SDK=<public-sdk-path>`, then `make embed`.
Generated ELF files remain ignored build outputs.
