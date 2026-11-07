# RS Re-ARMed runtime headers

From [RS Re-ARMed](https://github.com/bbelgg/rs-rearmed) 1.5.1 (`tools/ws7/recomp/runtime/rc.h`, `rc_rt.h`, `rc_call.h`,
`rc_hook.h`, `rc_abi.h`, `rc_cc.h`, `rc_mod.h`; `tools/ws7/recomp/overrides/rc_ov.h`; `shims/include/rs_overlay.h`, and `rs_render.h` from the release after it), MIT
licensed (LICENSE here). They declare the runtime that the game executable exports and the mods library calls: the guest
context, calls into translated code, hook points, game names, 2D drawing over the frame. Update them together with the RS Re-ARMed version the mods need.
