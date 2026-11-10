#!/usr/bin/env python3
"""Decode every sound in an N64 libultra ALBankFile (.ctl/.tbl) to 16-bit mono WAV."""
import struct, sys, wave, os

def u8(b, o): return b[o]
def s16(b, o): return struct.unpack_from('>h', b, o)[0]
def s32(b, o): return struct.unpack_from('>i', b, o)[0]

def decode_vadpcm(data, order, npred, book):
    # book[p][o][i]: npred x order x 8 coefficient table
    out = []; hist = [0] * 8
    for f in range(len(data) // 9):
        fr = data[f*9:(f+1)*9]
        scale = 1 << (fr[0] >> 4); pred = fr[0] & 0xF
        nib = []
        for byte in fr[1:]:
            for v in (byte >> 4, byte & 0xF):
                nib.append(v - 16 if v >= 8 else v)
        for half in range(2):
            ins = [n * scale for n in nib[half*8:(half+1)*8]]
            outs = []
            for i in range(8):
                acc = 0
                for o in range(order):
                    acc += book[pred][o][i] * hist[8 - order + o] if order <= 8 else 0
                # standard libultra inner-product with in-frame samples
                acc2 = 0
                for j in range(i):
                    acc2 += book[pred][order-1][i-1-j] * ins[j] if i-1-j >= 0 else 0
                total = (ins[i] << 11) + acc + acc2
                v = total >> 11
                v = max(-32768, min(32767, v))
                outs.append(v)
            out.extend(outs); hist = outs
    return out

def main(ctl_path, tbl_path, names_path, outdir):
    ctl = open(ctl_path, 'rb').read(); tbl = open(tbl_path, 'rb').read()
    names = {}
    if names_path and os.path.exists(names_path):
        for line in open(names_path):
            i, n = line.strip().split(': ', 1); names[int(i)] = n
    os.makedirs(outdir, exist_ok=True)
    assert s16(ctl, 0) == 0x4231, 'not an ALBankFile'
    bank = s32(ctl, 4)
    inst_count = s16(ctl, bank); rate = s32(ctl, bank + 4)
    idx = 0
    for ii in range(inst_count):
        inst = s32(ctl, bank + 12 + 4*ii)
        if inst == 0: continue
        snd_count = s16(ctl, inst + 14)
        for si in range(snd_count):
            snd = s32(ctl, inst + 16 + 4*si)
            keymap = s32(ctl, snd + 4); wt = s32(ctl, snd + 8)
            base = s32(ctl, wt); length = s32(ctl, wt + 4); typ = u8(ctl, wt + 8)
            key_base = u8(ctl, keymap + 4) if keymap else 60
            detune = struct.unpack_from('>b', ctl, keymap + 5)[0] if keymap else 0
            data = tbl[base:base+length]
            if typ == 0:
                book_off = s32(ctl, wt + 16)
                order = s32(ctl, book_off); npred = s32(ctl, book_off + 4)
                raw = [s16(ctl, book_off + 8 + 2*k) for k in range(order*npred*8)]
                book = [[raw[(p*order + o)*8:(p*order + o)*8 + 8] for o in range(order)] for p in range(npred)]
                pcm = decode_vadpcm(data, order, npred, book)
            else:
                pcm = [s16(data, 2*k) for k in range(len(data)//2)]
            # playback rate: bank rate shifted by keyBase relative to middle C (60) and detune cents
            sr = int(rate * 2 ** (((60 - key_base) * 100 + detune) / 1200.0))
            name = names.get(idx, f'SFX_{idx}')
            with wave.open(os.path.join(outdir, f'{idx:03d}_{name}.wav'), 'wb') as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(max(4000, sr))
                w.writeframes(struct.pack(f'<{len(pcm)}h', *pcm))
            idx += 1
    print(f'bank rate {rate}, decoded {idx} sounds')

if __name__ == '__main__':
    main(*sys.argv[1:5])
