# The model

rs-stems separates songs with [Demucs](https://github.com/facebookresearch/demucs) (Alexandre Défossez, Meta AI), its
six-source model `htdemucs_6s` (drums, bass, other, vocals, guitar, piano), converted to Core ML so it runs on the
Neural Engine. This folder makes that model. It is only for development: the helper needs none of it at run time, and
the model itself isn't in the repository.

Demucs, its code and its trained weights, is under the MIT licence, so the converted model can be shipped with the
helper, with Demucs' copyright notice and licence beside it ([`../LICENSE-demucs`](../LICENSE-demucs)).

## What the conversion does

`htcore.py` splits the model in two. The parts with complex numbers, padding or statistics over a whole segment (the
STFT and its inverse, the complex-as-channels packing, the normalisations) stay outside the model; rs-stems does them
with Accelerate (`stems_dsp.c`). The rest, the network itself, is rewritten so that all of it runs on the Neural Engine
and stays accurate in its fp16 (`CoreANE`):

- the frequency branch in (time, frequency) layout, so every frequency-axis kernel runs along the last axis;
- the long time branch folded into 64 rows of 5376 samples, each convolution with a halo of the neighbouring rows (the
  Neural Engine has a size limit per axis);
- transposed convolutions as convolutions whose output phases are interleaved (the Neural Engine's own transposed
  convolution with overlapping kernels comes out wrong);
- GELU and GLU written with `exp` and a division (the Neural Engine's `gelu`, `sigmoid` and `tanh` are coarse);
- GroupNorms that scale by each group's peak before squaring, so fp16 doesn't overflow.

`convert.py` checks the rewrite against the stock code in PyTorch, traces it and converts it: fp16, macOS 15 (its
attention is macOS 15's op), one 7.8 s segment. Against PyTorch fp32 the guitar and bass come out at about 50 dB SNR.

## Making it

It needs Python with PyTorch, demucs and coremltools; it was made with torch 2.15, demucs 4.1.0, coremltools 9.0 and
numpy 2.5 (`convert_patch.py` works around coremltools 9.0 with that numpy). The first run downloads the weights into
`TORCH_HOME`.

```bash
python3 -m venv venv && venv/bin/pip install torch demucs coremltools
venv/bin/python stems/convert/convert.py --check                       # the rewrite against the stock code
venv/bin/python stems/convert/convert.py build/stems/htdemucs_6s.mlpackage
stems/install.sh                                                       # compiles and installs it with the helper
```
