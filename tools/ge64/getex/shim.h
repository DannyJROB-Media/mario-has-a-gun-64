// Minimal stand-ins so GoldenEye's texture decompressor compiles as a 32-bit host program
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32; typedef float f32; typedef int bool;
#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL 0
#endif
#define G_IM_FMT_RGBA 0
#define G_IM_FMT_YUV 1
#define G_IM_FMT_CI 2
#define G_IM_FMT_IA 3
#define G_IM_FMT_I 4
#define G_IM_SIZ_4b 0
#define G_IM_SIZ_8b 1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3
#define G_MDSFT_TEXTLUT 14
#define G_TT_NONE (0 << G_MDSFT_TEXTLUT)
#define G_TT_RGBA16 (2 << G_MDSFT_TEXTLUT)
#define G_TT_IA16 (3 << G_MDSFT_TEXTLUT)
#define bzero(p, n) memset((p), 0, (n))

struct texpool { u8 *start; struct tex *end; u8 *leftpos; struct tex *rightpos; };
struct tex {
    u16 texturenum : 12; u16 unk00_0c : 4; u8 *data; u8 width; u8 height; u8 unk0a;
    u8 maxlod : 3; u8 gbiformat : 3; u8 depth : 2;
    u32 lutmodeindex : 2; u32 hasExplicitLods : 1; u32 unk0c_03 : 1; u32 next : 24;
};
struct texcacheitem { s16 texturenum; u8 widths[7]; u8 heights[7]; };
struct image_entry { u32 word; u32 word2; };
struct huft { u8 e; u8 b; union { u16 n; struct huft *t; } v; };

extern struct image_entry g_Textures[];
extern u8 *img_curpos;
extern s32 img_bitcount;
u32 texReadBits(s32 bitCount);
void texSetBitstring(s32 pos);
u32 decompressdata(u8 *src, u8 *dst, struct huft *huffman_table);
s32 rzipGetSomething(void);
extern u8 *rz_inbuf; extern u8 *rz_outbuf; extern u32 rz_inptr; extern u32 rz_wp; extern struct huft *rz_hlist;

#define TEXFORMAT_RGBA32     0x00 // 32-bit RGBA (8/8/8/8)
#define TEXFORMAT_RGBA16     0x01 // 16-bit RGBA (5/5/5/1)
#define TEXFORMAT_RGB24      0x02 // 24-bit RGB (8/8/8)
#define TEXFORMAT_RGB15      0x03 // 15-bit RGB (5/5/5)
#define TEXFORMAT_IA16       0x04 // 16-bit grayscale+alpha
#define TEXFORMAT_IA8        0x05 // 8-bit grayscale+alpha (4/4)
#define TEXFORMAT_IA4        0x06 // 4-bit grayscale+alpha (3/1)
#define TEXFORMAT_I8         0x07 // 8-bit grayscale
#define TEXFORMAT_I4         0x08 // 4-bit grayscale
#define TEXFORMAT_RGBA16_CI8 0x09 // 16-bit 5551 paletted colour with 8-bit palette indexes
#define TEXFORMAT_RGBA16_CI4 0x0a // 16-bit 5551 paletted colour with 4-bit palette indexes
#define TEXFORMAT_IA16_CI8   0x0b // 16-bit 88 paletted greyscale+alpha with 8-bit palette indexes
#define TEXFORMAT_IA16_CI4   0x0c // 16-bit 88 paletted greyscale+alpha with 4-bit palette indexes
#define TEXCOMPMETHOD_UNCOMPRESSED0      0
#define TEXCOMPMETHOD_UNCOMPRESSED1      1
#define TEXCOMPMETHOD_HUFFMAN            2
#define TEXCOMPMETHOD_HUFFMANPERHCHANNEL 3
#define TEXCOMPMETHOD_RLE                4
#define TEXCOMPMETHOD_LOOKUP             5
#define TEXCOMPMETHOD_HUFFMANLOOKUP      6
#define TEXCOMPMETHOD_RLELOOKUP          7
#define TEXCOMPMETHOD_HUFFMANBLUR        8
#define TEXCOMPMETHOD_RLEBLUR            9
#define ARRAYCOUNT(a) (sizeof(a) / sizeof((a)[0]))
