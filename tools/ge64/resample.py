#!/usr/bin/env python3
"""Deterministic 16-bit mono resampling and AIFF writing, in pure Python.

Used instead of sox so every machine produces byte-identical samples (and so an identical ROM):
sox builds differ between distros, and Python's libm-backed math functions may differ in the last
bit between platforms, so this module only uses +, -, *, / on IEEE doubles.

The resampler is a polyphase windowed-sinc (Blackman window), with the cutoff at the lower of the
two Nyquist frequencies.
"""
import struct, wave
from fractions import Fraction

PI = 3.14159265358979311600  # the double closest to pi
TAPS_PER_SIDE = 16

def _sin(x):
    """sin(x) from range reduction and a Taylor series (only basic arithmetic)."""
    two_pi = 2.0 * PI
    k = int(x / two_pi)
    x -= k * two_pi
    if x > PI:
        x -= two_pi
    elif x < -PI:
        x += two_pi
    term, total, x2 = x, x, x * x
    for n in range(1, 16):
        term = -term * x2 / ((2 * n) * (2 * n + 1))
        total += term
    return total

def _cos(x):
    return _sin(x + PI / 2.0)

def _kernel(t, cutoff, half_width):
    """Windowed sinc at offset t (input samples); cutoff as a fraction of the input rate."""
    if t <= -half_width or t >= half_width:
        return 0.0
    if t == 0.0:
        s = 2.0 * cutoff
    else:
        s = _sin(2.0 * PI * cutoff * t) / (PI * t)
    w = 0.42 + 0.5 * _cos(PI * t / half_width) + 0.08 * _cos(2.0 * PI * t / half_width)
    return s * w

def resample(samples, rate_in, rate_out):
    """samples: list of ints (16-bit mono). Returns a list of ints at rate_out."""
    if rate_in == rate_out:
        return list(samples)
    ratio = Fraction(rate_out, rate_in)  # output samples per input sample
    up, down = ratio.numerator, ratio.denominator
    cutoff = 0.5 * min(1.0, rate_out / rate_in)
    half_width = TAPS_PER_SIDE / (2.0 * cutoff)  # widen the kernel when downsampling
    reach = int(half_width) + 1
    # one table of weights per output phase (output sample j sits at input position j*down/up)
    phases = []
    for p in range(up):
        frac = Fraction(p * down % up, up)
        base = -reach + 1
        weights = [_kernel(float(base + i - frac), cutoff, half_width) for i in range(2 * reach)]
        phases.append(weights)
    n_in = len(samples)
    n_out = n_in * up // down
    taps = 2 * reach
    padded = [0] * taps + list(samples) + [0] * taps  # zero padding instead of bounds checks
    out = []
    append = out.append
    for j in range(n_out):
        start = j * down // up - reach + 1 + taps
        weights = phases[j % up]
        acc = 0.0
        for s, w in zip(padded[start:start + taps], weights):
            acc += s * w
        v = int(acc + 0.5) if acc >= 0 else -int(-acc + 0.5)
        append(32767 if v > 32767 else (-32768 if v < -32768 else v))
    return out

def _extended(rate):
    """Integer sample rate as an 80-bit IEEE extended float (AIFF COMM chunk)."""
    e = rate.bit_length() - 1
    return struct.pack('>HQ', 16383 + e, rate << (63 - e))

def write_aiff(path, samples, rate):
    data = struct.pack('>%dh' % len(samples), *samples)
    comm = struct.pack('>hIh', 1, len(samples), 16) + _extended(rate)
    ssnd = struct.pack('>II', 0, 0) + data
    chunks = b'COMM' + struct.pack('>I', len(comm)) + comm + b'SSND' + struct.pack('>I', len(ssnd)) + ssnd
    with open(path, 'wb') as f:
        f.write(b'FORM' + struct.pack('>I', 4 + len(chunks)) + b'AIFF' + chunks)

def convert_wav(src, dst, rate_out):
    """Mono 16-bit WAV -> mono 16-bit AIFF at rate_out. Returns the source duration in seconds."""
    with wave.open(src) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, src
        rate_in, n = w.getframerate(), w.getnframes()
        samples = list(struct.unpack('<%dh' % n, w.readframes(n)))
    write_aiff(dst, resample(samples, rate_in, rate_out), rate_out)
    return n / rate_in
