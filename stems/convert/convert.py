#!/usr/bin/env python3
# Converts htdemucs_6s's network core, rewritten for the Neural Engine (htcore.CoreANE), to the Core ML package
# rs-stems runs: fp16, macOS 15, one 7.8 s segment, inputs x (1, 4, 336, 2048) and xt (1, 2, 64, 5376), outputs x_out
# (1, 24, 336, 2048) and xt_out (1, 12, 64, 5376). The rewrite is checked against the model's own code in PyTorch first.
#
# usage: convert.py OUT.mlpackage    (stems/install.sh takes build/stems/htdemucs_6s.mlpackage by default)
#        convert.py --check          the check alone
import argparse
import time
import warnings

import torch

import htcore

warnings.filterwarnings("ignore")


def check(model):
    """The rewrite against the stock core on random inputs; returns the inputs in the rewrite's layout."""
    x = torch.randn(1, 4, 2048, 336)
    xt = torch.randn(1, 2, htcore.SEG)
    core, ref = htcore.CoreANE(model).eval(), htcore.Core(model).eval()
    with torch.no_grad():
        a, at = ref(x, xt)
        x, xt = x.transpose(2, 3).contiguous(), htcore.fold_time(xt)   # the rewrite's (T, F) and folded layouts
        b, bt = core(x, xt)
        b, bt = b.transpose(2, 3), htcore.unfold_time(bt)
    print(f"rewrite vs stock core: frequency output max diff {(a - b).abs().max():.2e} (max {a.abs().max():.2f}), "
          f"time output max diff {(at - bt).abs().max():.2e} (max {at.abs().max():.2f})", flush=True)
    return core, x, xt


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", nargs="?", help="the .mlpackage to write")
    ap.add_argument("--check", action="store_true", help="only compare the rewrite with the stock core")
    args = ap.parse_args()
    if not args.check and not args.out:
        ap.error("give the .mlpackage to write, or --check")
    torch.backends.mha.set_fastpath_enabled(False)  # trace plain ops, not _native_multi_head_attention
    model = htcore.load_model()
    core, x, xt = check(model)
    if args.check:
        return

    import numpy as np
    import coremltools as ct
    import convert_patch  # noqa: F401  (coremltools 9.0 with numpy 2.5)

    t = time.perf_counter()
    with torch.no_grad():
        traced = torch.jit.trace(core, (x, xt), check_trace=False)
    print(f"trace {time.perf_counter() - t:.1f} s", flush=True)
    t = time.perf_counter()
    mlmodel = ct.convert(
        traced,
        inputs=[ct.TensorType(name="x", shape=x.shape, dtype=np.float32),
                ct.TensorType(name="xt", shape=xt.shape, dtype=np.float32)],
        outputs=[ct.TensorType(name="x_out", dtype=np.float32), ct.TensorType(name="xt_out", dtype=np.float32)],
        convert_to="mlprogram",
        compute_precision=ct.precision.FLOAT16,
        minimum_deployment_target=ct.target.macOS15,
        compute_units=ct.ComputeUnit.CPU_AND_NE,
    )
    print(f"convert {time.perf_counter() - t:.1f} s", flush=True)
    mlmodel.short_description = f"htdemucs_6s network core for the Neural Engine, one {htcore.SEG}-sample segment"
    mlmodel.save(args.out)
    print("saved", args.out)


if __name__ == "__main__":
    main()
