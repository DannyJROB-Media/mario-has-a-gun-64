// Host tool: inflate one GoldenEye texture (assets/images/split/N.bin) using the decomp's own
// decompressor, and dump format info plus the raw N64 texel data (and palette, if any).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

s32 texInflateZlib(u8 *src, u8 *dst, s32 arg2, s32 forcenumimages, struct texpool *arg4);
s32 texInflateNonZlib(u8 *src, u8 *dst, s32 arg2, s32 forcenumimages, struct texpool *arg4);

static u8 sPool[0x40000] __attribute__((aligned(16)));

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    static u8 src[0x10000] __attribute__((aligned(16)));
    size_t n;
    struct texpool pool;
    struct tex *tex;
    u8 *dst;
    s32 out, explicitLods, iszlib, lod;

    if (f == NULL) { perror(argv[1]); return 1; }
    n = fread(src, 1, sizeof(src), f);
    fclose(f);
    (void) n;

    pool.start = sPool;
    pool.end = (struct tex *) (sPool + sizeof(sPool));
    pool.leftpos = sPool;
    pool.rightpos = pool.end;
    pool.rightpos--;
    tex = pool.rightpos;
    memset(tex, 0, sizeof(*tex));
    dst = pool.leftpos;

    explicitLods = (src[0] & 0x80) >> 7;
    iszlib = (src[0] & 0x40) >> 6;
    lod = src[0] & 0x3f;
    if (iszlib) {
        out = texInflateZlib(src + 1, dst, explicitLods, lod, &pool);
    } else {
        out = texInflateNonZlib(src + 1, dst, explicitLods, lod, &pool);
    }

    printf("bytes=%d width=%d height=%d gbiformat=%d depth=%d lutmode=%d numcolours=%d zlib=%d lods=%d/%d\n",
           out, tex->width, tex->height, tex->gbiformat, tex->depth, tex->lutmodeindex, tex->unk0a + 1,
           iszlib, explicitLods, lod);
    if (argc > 2) {
        f = fopen(argv[2], "wb");
        fwrite(dst, 1, out, f);
        fclose(f);
    }
    return 0;
}
