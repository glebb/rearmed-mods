#!/usr/bin/env python3
# htdemucs_6s split for rs-stems: the parts that stay outside Core ML (STFT, complex-as-channels packing, padding,
# normalisation, inverse STFT; rs-stems does them in stems_dsp.c) and the network core that is converted (both
# encoders, the cross-domain transformer, both decoders), for one segment of the model's own length (7.8 s = 343980
# samples), batch 1. Demucs is by Alexandre Defossez and Meta (MIT); this uses its code and weights unchanged.
#
#   pre(model, mix)            -> x (1, 4, 2048, 336), xt (1, 2, 343980), and the state post() needs
#   Core(model)(x, xt)         -> x_out (1, 24, 2048, 336), xt_out (1, 12, 343980)   (the stock code, traced)
#   post(model, state, x_out, xt_out) -> (1, 6, 2, 343980), the same as model(mix)
#
# CoreANE is the same network written for the Neural Engine (all of it lands there; see results.md): the
# frequency branch in (C, T, F) layout so every frequency kernel runs along the last axis and the DConv is
# (k x 1) convolutions instead of a (B*F, C, T) batch; the long time branch folded into 64 rows with halos;
# transposed convolutions as convolution + interleave; GELU and GLU from exp and division (the ANE's own gelu,
# sigmoid and tanh are too coarse); GroupNorms that pre-scale by the group's peak so fp16 can't overflow.
# The transformer keeps the stock (B, S, C) form (its attention already runs on the ANE).
import math

import torch
import torch.nn as nn
import torch.nn.functional as F

from demucs.htdemucs import HTDemucs
from demucs.pretrained import get_model
from demucs.transformer import create_2d_sin_embedding, create_sin_embedding

SR = 44100
SEG = 343980  # int(7.8 * 44100), the training segment the model always runs on


def load_model():
    bag = get_model("htdemucs_6s")
    model = bag.models[0] if hasattr(bag, "models") else bag
    assert isinstance(model, HTDemucs)
    model.eval()
    assert int(model.segment * model.samplerate) == SEG
    return model


# ---------------------------------------------------------------------------------------------- outside parts

def pre(model, mix):
    """mix: (1, 2, SEG) float32 torch tensor (already normalised by the caller as apply does)."""
    assert mix.shape[-1] == SEG
    z = model._spec(mix)                      # complex (1, 2, 2048, 336)
    mag = model._magnitude(z)                 # (1, 4, 2048, 336): real/imag as channels
    mean = mag.mean(dim=(1, 2, 3), keepdim=True)
    std = mag.std(dim=(1, 2, 3), keepdim=True)
    x = (mag - mean) / (1e-5 + std)
    meant = mix.mean(dim=(1, 2), keepdim=True)
    stdt = mix.std(dim=(1, 2), keepdim=True)
    xt = (mix - meant) / (1e-5 + stdt)
    return x, xt, (z, mean, std, meant, stdt)


def post(model, state, x_out, xt_out, idx=None):
    """idx: optional list of source indices to finish (the inverse STFT is most of the CPU work here)."""
    z, mean, std, meant, stdt = state
    B, _, Fq, T = x_out.shape
    S = len(model.sources)
    x = x_out.reshape(B, S, -1, Fq, T)
    xt = xt_out.reshape(B, S, -1, SEG)
    if idx is not None:
        x, xt = x[:, idx], xt[:, idx]
    x = x * std[:, None] + mean[:, None]
    zout = model._mask(z, x)
    x = model._ispec(zout, SEG)
    xt = xt * stdt[:, None] + meant[:, None]
    return xt + x


# ------------------------------------------------------------------------------------- the stock core, as-is

class Core(nn.Module):
    """Encoders + cross transformer + decoders of HTDemucs.forward, unchanged."""

    def __init__(self, model):
        super().__init__()
        self.m = model

    def forward(self, x, xt):
        m = self.m
        saved, saved_t, lengths, lengths_t = [], [], [], []
        for idx, encode in enumerate(m.encoder):
            lengths.append(x.shape[-1])
            inject = None
            if idx < len(m.tencoder):
                lengths_t.append(xt.shape[-1])
                tenc = m.tencoder[idx]
                xt = tenc(xt)
                if not tenc.empty:
                    saved_t.append(xt)
                else:
                    inject = xt
            x = encode(x, inject)
            if idx == 0 and m.freq_emb is not None:
                frs = torch.arange(x.shape[-2], device=x.device)
                emb = m.freq_emb(frs).t()[None, :, :, None].expand_as(x)
                x = x + m.freq_emb_scale * emb
            saved.append(x)
        x, xt = m.crosstransformer(x, xt)
        for idx, decode in enumerate(m.decoder):
            skip = saved.pop(-1)
            x, pre_ = decode(x, skip, lengths.pop(-1))
            offset = m.depth - len(m.tdecoder)
            if idx >= offset:
                tdec = m.tdecoder[idx - offset]
                length_t = lengths_t.pop(-1)
                if tdec.empty:
                    pre_ = pre_[:, :, 0]
                    xt, _ = tdec(pre_, None, length_t)
                else:
                    skip = saved_t.pop(-1)
                    xt, _ = tdec(xt, skip, length_t)
        return x, xt



# ------------------------------------------------------------------------------ the Neural Engine rewrite

ROWS = 64                 # the long time branch is folded into 64 rows
W0 = 5376                 # 64 * 5376 = 344064 >= 343980 (84 samples of padding)
LEVEL_LEN = [SEG, 85995, 21499, 5375, 1344]          # valid lengths of the time branch per level
LEVEL_SHAPE = [(ROWS, W0), (ROWS, W0 // 4), (ROWS, W0 // 16), (1, 5376), (1, 1344)]
T_FRAMES = 336


def fold_time(xt):
    """(1, C, SEG) -> (1, C, ROWS, W0), zero padded (outside the model)."""
    pad = ROWS * W0 - xt.shape[-1]
    return F.pad(xt, (0, pad)).reshape(xt.shape[0], xt.shape[1], ROWS, W0)


def unfold_time(xt):
    """(1, C, ROWS, W0) -> (1, C, SEG)."""
    return xt.reshape(xt.shape[0], xt.shape[1], ROWS * W0)[..., :SEG]


def _mask(level):
    r, w = LEVEL_SHAPE[level]
    m = (torch.arange(r * w) < LEVEL_LEN[level]).float().reshape(1, 1, r, w)
    return m


_GC = math.sqrt(2 / math.pi)


def gelu(x):
    """GELU (tanh form) as x * sigmoid(2u), the sigmoid written with exp and a division: the Neural Engine's own
    gelu, sigmoid and tanh are coarse approximations (gelu: ~2e-3 absolute error near 0, ~350x fp16 rounding),
    which the DConv GroupNorms then amplify in quiet frequency rows; its exp and division are accurate."""
    return x / (1 + torch.exp(-2 * _GC * (x + 0.044715 * x * x * x)))


def glu(x):
    """GLU over channels with the accurate sigmoid (see gelu)."""
    a, b = x.chunk(2, dim=1)
    return a / (1 + torch.exp(-b))


def halo(x, a, b):
    """Add a left halo of `a` and a right halo of `b` samples to each row of a folded (1, C, R, W) sequence:
    row r gets the end of row r-1 in front and the start of row r+1 behind (zeros at the ends)."""
    B, C, R, W = x.shape
    parts = []
    if a:
        left = F.pad(x[:, :, :R - 1, W - a:], (0, 0, 1, 0)) if R > 1 else x.new_zeros(B, C, 1, a)
        parts.append(left)
    parts.append(x)
    if b:
        right = F.pad(x[:, :, 1:, :b], (0, 0, 0, 1)) if R > 1 else x.new_zeros(B, C, 1, b)
        parts.append(right)
    return torch.cat(parts, dim=3)


class Norm(nn.Module):
    """GroupNorm(1, C)-style normalisation written out so fp16 can't overflow or lose small groups: the
    deviations from the mean are divided by their own peak before squaring (so the squares are <= 1), and
    eps is scaled to match. `axes` are the reduced axes (one group per remaining index); with a `mask` (zeros in
    the padding of a folded sequence) the statistics cover the valid part only."""

    def __init__(self, weight, bias, eps, axes, mask=None, wshape=(1, -1, 1, 1)):
        super().__init__()
        self.register_buffer("w", weight.detach().clone().reshape(wshape))
        self.register_buffer("b", bias.detach().clone().reshape(wshape))
        self.axes = axes
        self.eps = eps
        if mask is not None:
            self.register_buffer("mask", mask)
            self.frac = float(mask.numel() / mask.sum())
        else:
            self.mask = None
            self.frac = 1.0

    def _reduce(self, x, fn):
        # reduce the last (longest) axis first, then the rest: smaller reductions
        if 3 in self.axes and len(self.axes) > 1:
            x = fn(x, (3,))
            return fn(x, tuple(a for a in self.axes if a != 3))
        return fn(x, self.axes)

    def forward(self, x):
        if self.mask is not None:
            x = x * self.mask
        m = self._reduce(x, lambda t, a: t.mean(dim=a, keepdim=True))
        if self.frac != 1.0:
            m = m * self.frac
        d = x - m
        if self.mask is not None:
            d = d * self.mask
        peak = self._reduce(d.abs(), lambda t, a: t.amax(dim=a, keepdim=True))
        s = 1.0 / torch.clamp(peak, min=2.0 ** -10)
        d = d * s
        v = self._reduce(d * d, lambda t, a: t.mean(dim=a, keepdim=True))
        if self.frac != 1.0:
            v = v * self.frac
        return d * torch.rsqrt(v + self.eps * s * s) * self.w + self.b


def _conv(weight, bias, stride=(1, 1), padding=(0, 0), dilation=(1, 1)):
    c = nn.Conv2d(weight.shape[1], weight.shape[0], tuple(weight.shape[2:]), stride, padding, dilation,
                  bias=bias is not None)
    c.weight.data.copy_(weight)
    if bias is not None:
        c.bias.data.copy_(bias)
    return c


class ConvTr(nn.Module):
    """ConvTranspose (kernel 8, stride 4) along W, without the Neural Engine's deconvolution (which comes out
    wrong for overlapping kernels: ~19 dB SNR): output block n (samples 4n..4n+3) is W[:, :, 0:4] x[n] +
    W[:, :, 4:8] x[n-1], so a kernel-2 convolution makes the 4 phases as channels and a transpose interleaves
    them. (1, Cin, H, N) -> (1, Cout, H, 4N + 4), the full transposed-convolution output."""

    def __init__(self, weight, bias):
        super().__init__()
        cin, cout, kh, k = weight.shape                  # ConvTranspose2d weight (Cin, Cout, 1, 8)
        assert kh == 1 and k == 8
        w = weight[:, :, 0, :]                           # (Cin, Cout, 8)
        taps = torch.stack([w[:, :, 4:8], w[:, :, 0:4]], dim=-1)    # (Cin, Cout, 4 phases, 2 taps)
        taps = taps.permute(2, 1, 0, 3).reshape(4 * cout, cin, 1, 2)  # out channel = phase * Cout + co
        self.conv = _conv(taps, bias.repeat(4), padding=(0, 1))
        self.cout = cout

    def forward(self, x):
        B, _, H, N = x.shape
        y = self.conv(x)                                 # (1, 4*Cout, H, N+1)
        y = y.reshape(B, 4, self.cout * H, N + 1).permute(0, 2, 3, 1)   # (1, Cout*H, N+1, 4)
        return y.reshape(B, self.cout, H, 4 * (N + 1))


def _tf(w):
    """Conv2d weight over (F, T) -> the same over (T, F): swap the two kernel axes."""
    return w.transpose(2, 3).contiguous()


def _1d(w):
    """Conv1d weight (O, I, K) -> Conv2d (O, I, 1, K)."""
    return w[:, :, None, :].contiguous()


class DConvTF(nn.Module):
    """DConv of the frequency branch: the stock code runs Conv1d over T on a (B*F, C, T) batch; here the
    tensor stays (1, C, T, F) and the convs are (k x 1) along T, GroupNorm per F over (C, T)."""

    def __init__(self, dconv):
        super().__init__()
        self.layers = nn.ModuleList()
        for i, layer in enumerate(dconv.layers):
            c1, n1, _, c2, n2, _, ls = layer
            d = c1.dilation[0]
            self.layers.append(nn.ModuleList([
                _conv(_1d(c1.weight).transpose(2, 3), c1.bias, padding=(d, 0), dilation=(d, 1)),
                Norm(n1.weight, n1.bias, n1.eps, (1, 2)),
                _conv(c2.weight[:, :, :, None], c2.bias),
                Norm(n2.weight, n2.bias, n2.eps, (1, 2)),
            ]))
            self.register_buffer(f"ls{i}", ls.scale.detach().clone().reshape(1, -1, 1, 1))

    def forward(self, x):
        for i, (c1, n1, c2, n2) in enumerate(self.layers):
            h = gelu(n1(c1(x)))
            h = glu(n2(c2(h)))
            x = x + getattr(self, f"ls{i}") * h
        return x


class DConvT(nn.Module):
    """DConv of the time branch on a folded (1, C, R, W) sequence: halo + (1 x k) conv, masked GroupNorm."""

    def __init__(self, dconv, level):
        super().__init__()
        mask = _mask(level)
        self.register_buffer("mask", mask)
        self.layers = nn.ModuleList()
        self.dil = []
        for i, layer in enumerate(dconv.layers):
            c1, n1, _, c2, n2, _, ls = layer
            d = c1.dilation[0]
            self.dil.append(d)
            self.layers.append(nn.ModuleList([
                _conv(_1d(c1.weight), c1.bias, dilation=(1, d)),
                Norm(n1.weight, n1.bias, n1.eps, (1, 2, 3), mask),
                _conv(c2.weight[:, :, :, None], c2.bias),
                Norm(n2.weight, n2.bias, n2.eps, (1, 2, 3), mask),
            ]))
            self.register_buffer(f"ls{i}", ls.scale.detach().clone().reshape(1, -1, 1, 1))

    def forward(self, x):
        for i, (c1, n1, c2, n2) in enumerate(self.layers):
            d = self.dil[i]
            h = c1(halo(x * self.mask, d, d))
            h = gelu(n1(h))
            h = glu(n2(c2(h)))
            x = x + getattr(self, f"ls{i}") * h
        return x


class EncTF(nn.Module):
    def __init__(self, enc):
        super().__init__()
        assert enc.freq and not enc.empty and enc.rewrite is not None
        assert isinstance(enc.norm1, nn.Identity) and isinstance(enc.norm2, nn.Identity)
        self.conv = _conv(_tf(enc.conv.weight), enc.conv.bias, stride=(1, enc.stride), padding=(0, enc.pad))
        self.dconv = DConvTF(enc.dconv)
        self.rewrite = _conv(_tf(enc.rewrite.weight), enc.rewrite.bias)

    def forward(self, x):
        y = gelu(self.conv(x))
        y = self.dconv(y)
        return glu(self.rewrite(y))


class EncT(nn.Module):
    """Time encoder layer, folded: input at level `level`, output at level+1."""

    def __init__(self, enc, level):
        super().__init__()
        assert not enc.freq and not enc.empty and enc.stride == 4 and enc.conv.kernel_size[0] == 8
        self.level = level
        self.register_buffer("mask", _mask(level))
        self.conv = _conv(_1d(enc.conv.weight), enc.conv.bias, stride=(1, 4))
        self.dconv = DConvT(enc.dconv, level + 1)
        self.rewrite = _conv(_1d(enc.rewrite.weight), enc.rewrite.bias)

    def forward(self, x):
        y = self.conv(halo(x * self.mask, 2, 2))            # k8 s4, 2 samples of context either side
        if LEVEL_SHAPE[self.level + 1][0] != y.shape[2]:    # level 2 -> 3: unfold the rows
            y = y.reshape(y.shape[0], y.shape[1], *LEVEL_SHAPE[self.level + 1])
        y = gelu(y)
        y = self.dconv(y)
        return glu(self.rewrite(y))


class DecTF(nn.Module):
    def __init__(self, dec):
        super().__init__()
        assert dec.freq and not dec.empty and dec.rewrite is not None and dec.pad == 2
        assert isinstance(dec.norm1, nn.Identity) and isinstance(dec.norm2, nn.Identity)
        k = dec.rewrite.kernel_size
        self.rewrite = _conv(_tf(dec.rewrite.weight), dec.rewrite.bias, padding=(k[1] // 2, k[0] // 2))
        self.dconv = DConvTF(dec.dconv) if dec.dconv is not None else None
        self.conv_tr = ConvTr(_tf(dec.conv_tr.weight), dec.conv_tr.bias)
        self.last = dec.last

    def forward(self, x, skip):
        x = x + skip
        y = glu(self.rewrite(x))
        if self.dconv is not None:
            y = self.dconv(y)
        z = self.conv_tr(y)[..., 2:-2]
        return z if self.last else gelu(z)


class DecT(nn.Module):
    """Time decoder layer, folded: input at level `level`, output at level-1."""

    def __init__(self, dec, level):
        super().__init__()
        assert not dec.freq and not dec.empty and dec.stride == 4 and dec.pad == 2
        self.level = level
        self.register_buffer("mask", _mask(level))
        self.rewrite = _conv(_1d(dec.rewrite.weight), dec.rewrite.bias)
        self.dconv = DConvT(dec.dconv, level) if dec.dconv is not None else None
        self.conv_tr = ConvTr(_1d(dec.conv_tr.weight), dec.conv_tr.bias)
        self.last = dec.last

    def forward(self, x, skip):
        x = x + skip
        y = glu(self.rewrite(halo(x * self.mask, 1, 1)))
        if self.dconv is not None:
            y = self.dconv(y)
        if LEVEL_SHAPE[self.level - 1][0] != y.shape[2]:    # level 3 -> 2: fold into rows again
            y = y.reshape(y.shape[0], y.shape[1], LEVEL_SHAPE[self.level - 1][0], -1)
        W = y.shape[3]
        z = self.conv_tr(halo(y * self.mask_fold(y), 1, 1))[..., 6:6 + 4 * W]
        return z if self.last else gelu(z)

    def mask_fold(self, y):
        return self.mask.reshape(1, 1, y.shape[2], y.shape[3])


class MHA(nn.Module):
    """nn.MultiheadAttention (batch first, no mask) written out with F.scaled_dot_product_attention."""

    def __init__(self, mha):
        super().__init__()
        self.h = mha.num_heads
        C = mha.embed_dim
        self.q = nn.Linear(C, C)
        self.k = nn.Linear(C, C)
        self.v = nn.Linear(C, C)
        for i, lin in enumerate((self.q, self.k, self.v)):
            lin.weight.data.copy_(mha.in_proj_weight[i * C:(i + 1) * C])
            lin.bias.data.copy_(mha.in_proj_bias[i * C:(i + 1) * C])
        self.o = nn.Linear(C, C)
        self.o.weight.data.copy_(mha.out_proj.weight)
        self.o.bias.data.copy_(mha.out_proj.bias)

    def forward(self, q, kv):
        B, S, C = q.shape
        h = self.h
        Q = self.q(q).reshape(B, S, h, C // h).transpose(1, 2)
        K = self.k(kv).reshape(B, kv.shape[1], h, C // h).transpose(1, 2)
        V = self.v(kv).reshape(B, kv.shape[1], h, C // h).transpose(1, 2)
        a = F.scaled_dot_product_attention(Q, K, V)
        return self.o(a.transpose(1, 2).reshape(B, S, C))


class TLayer(nn.Module):
    """MyTransformerEncoderLayer / CrossTransformerEncoderLayer (norm_first, layer scale, norm_out)."""

    def __init__(self, layer, cross):
        super().__init__()
        assert layer.norm_first and layer.norm_out is not None
        self.cross = cross
        self.attn = MHA(layer.cross_attn if cross else layer.self_attn)
        self.norm1, self.norm2 = layer.norm1, layer.norm2
        self.norm3 = layer.norm3 if cross else None
        self.linear1, self.linear2 = layer.linear1, layer.linear2
        self.register_buffer("g1", layer.gamma_1.scale.detach().clone())
        self.register_buffer("g2", layer.gamma_2.scale.detach().clone())
        no = layer.norm_out
        assert no.num_groups == 1
        self.norm_out = Norm(no.weight, no.bias, no.eps, (1, 2), wshape=(1, 1, -1))

    def forward(self, x, k=None):
        if self.cross:
            x = x + self.g1 * self.attn(self.norm1(x), self.norm2(k))
            x = x + self.g2 * self.linear2(gelu(self.linear1(self.norm3(x))))
        else:
            y = self.norm1(x)
            x = x + self.g1 * self.attn(y, y)
            x = x + self.g2 * self.linear2(gelu(self.linear1(self.norm2(x))))
        return self.norm_out(x)


class Transformer(nn.Module):
    def __init__(self, ct_):
        super().__init__()
        assert ct_.emb == "sin" and ct_.classic_parity == 0 and ct_.weight_pos_embed == 1.0
        C, Fr, T1, T2 = 384, 8, T_FRAMES, 1344
        pe2 = create_2d_sin_embedding(C, Fr, T1, "cpu", ct_.max_period)          # (1, C, Fr, T1)
        self.register_buffer("pe2", pe2.permute(0, 3, 2, 1).reshape(1, T1 * Fr, C).contiguous())
        pe1 = create_sin_embedding(T2, C, shift=0, device="cpu", max_period=ct_.max_period)  # (T2, 1, C)
        self.register_buffer("pe1", pe1.permute(1, 0, 2).contiguous())
        self.norm_in, self.norm_in_t = ct_.norm_in, ct_.norm_in_t
        self.layers = nn.ModuleList(
            [TLayer(l_, i % 2 == 1) for i, l_ in enumerate(ct_.layers)])
        self.layers_t = nn.ModuleList(
            [TLayer(l_, i % 2 == 1) for i, l_ in enumerate(ct_.layers_t)])

    def forward(self, x, xt):
        B, C, T1, Fr = x.shape                      # frequency branch, (1, C, T, F) layout
        x = x.reshape(B, C, T1 * Fr).transpose(1, 2)  # tokens in (t, f) order, as the stock "(t1 fr)"
        x = self.norm_in(x) + self.pe2
        T2 = xt.shape[-1]
        xt = xt.reshape(B, C, T2).transpose(1, 2)
        xt = self.norm_in_t(xt) + self.pe1
        for i in range(len(self.layers)):
            if i % 2 == 0:
                x = self.layers[i](x)
                xt = self.layers_t[i](xt)
            else:
                old_x = x
                x = self.layers[i](x, xt)
                xt = self.layers_t[i](xt, old_x)
        x = x.transpose(1, 2).reshape(B, C, T1, Fr)
        xt = xt.transpose(1, 2).reshape(B, C, 1, T2)
        return x, xt


class CoreANE(nn.Module):
    """Inputs: x (1, 4, 336, 2048) = pre()'s x transposed to (T, F); xt (1, 2, 64, 5376) = fold_time(xt).
    Outputs: x_out (1, 24, 336, 2048) (transpose back), xt_out (1, 12, 64, 5376) (unfold_time)."""

    def __init__(self, model):
        super().__init__()
        m = model
        assert len(m.tencoder) == 4 and len(m.tdecoder) == 4 and m.depth == 4
        self.enc = nn.ModuleList([EncTF(e) for e in m.encoder])
        self.tenc = nn.ModuleList([EncT(e, i) for i, e in enumerate(m.tencoder)])
        self.dec = nn.ModuleList([DecTF(d) for d in m.decoder])
        self.tdec = nn.ModuleList([DecT(d, 4 - i) for i, d in enumerate(m.tdecoder)])
        frs = torch.arange(512)
        emb = (m.freq_emb(frs).t() * m.freq_emb_scale).detach()       # (48, 512)
        self.register_buffer("emb", emb[None, :, None, :].contiguous())  # (1, 48, 1, 512) in (T, F) layout
        self.transformer = Transformer(m.crosstransformer)

    def forward(self, x, xt):
        saved, saved_t = [], []
        for i in range(4):
            xt = self.tenc[i](xt)
            saved_t.append(xt)
            x = self.enc[i](x)
            if i == 0:
                x = x + self.emb
            saved.append(x)
        x, xt = self.transformer(x, xt)
        for i in range(4):
            x = self.dec[i](x, saved.pop(-1))
            xt = self.tdec[i](xt, saved_t.pop(-1))
        return x, xt
