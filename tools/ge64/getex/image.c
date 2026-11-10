// Adapted from n64decomp/007 src/game/image.c (texture decompression) to run as a host tool.
#include "shim.h"


#define TEX_ALPHA_WEIGHT 961

// bss
//8008C720
struct texpool *ptr_texture_alloc_start;
//8008C724
s32 ptr_texture_alloc_end;
//8008C728
s32 ptr_next_available_space;
//8008C72C
s32 ptr_last_entry_facemapping;
//8008C730
struct texcacheitem g_TexCacheItems[150];
//8008D090
s32 g_TexCacheCount;
//8008D094
s32 g_TexNumToLoad;

// data
//D:80049170
u32 bytes = 0x6DDD0;
//D:80049174
u32 D_80049174 = 0;

//D:80049178 #1	#bytes in pixel data for image
s32 g_TexFormatNumChannels[] = 
{
    4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1
};
//D:800491AC #2	1=alphagrab.  Grabs 1 bit of alpha data for each pixel
s32 g_TexFormatHas1BitAlpha[] = 
{
    0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0
};
//D:800491E0 #3	#bits in 'samples', *2	-1=bitmask
s32 g_TexFormatChannelSizes[] = 
{
    0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10, 0x100, 0x10, 0x100, 0x10
};
//D:80049214 #4	bitcount for pixel data
s32 g_TexFormatBitsPerPixel[] = 
{
     0x20, 0x10, 0x18, 0xF, 0x10, 8, 4, 8, 4, 0x10, 0x10, 0x10, 0x10, 
};
//D:80049248 #5	N64 image types (0=color, 1=YUV, 2=indexed, 3=IA, 4=I)
s32 g_TexFormatGbiMappings[] = 
{
    G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_RGBA,	
    G_IM_FMT_IA, G_IM_FMT_IA, G_IM_FMT_IA, 
    G_IM_FMT_I, G_IM_FMT_I, 
    G_IM_FMT_CI, G_IM_FMT_CI, G_IM_FMT_CI, G_IM_FMT_CI,
};
//D:8004927C #6	N64 pixel sizes (0=4bit, 1=8bit, 2=16bit, 3=32bit)
s32 g_TexFormatDepths[] = 
{
	G_IM_SIZ_32b,
    G_IM_SIZ_16b,
	G_IM_SIZ_32b,
	G_IM_SIZ_16b,
	G_IM_SIZ_16b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
	G_IM_SIZ_8b,
	G_IM_SIZ_4b,
};
//D:800492B0 #7	imageflip values for indexed types
s32 g_TexFormatLutModes[] = {
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_NONE,
	G_TT_RGBA16,
	G_TT_RGBA16,
	G_TT_IA16,
	G_TT_IA16,
};
//D:800492e4
s32 D_800492E4[] = 
{
    0, 0, 0, 0, 0, 0, 0
};
struct image_entry g_Textures[1];


void nullsub_41(s32 arg0) {
    if (arg0);
}


/**
 * Inflate images (levels of detail) from a zlib-compressed texture.
 *
 * Zlib-compressed textures are always paletted and always use 16-bit colours.
 * The texture header contains palette information, then each image follows with
 * its own header and zlib compresed data.
 *
 * The texture header is:
 *
 * ffffffff nnnnnnnn [palette]
 *
 * f = pixel format (see TEXFORMAT constants)
 * n = number of colours in the palette minus 1
 * [palette] = 16 bits * number of colours
 *
 * Each images's header is:
 *
 * wwwwwwww hhhhhhhh [data]
 *
 * w = width in pixels
 * h = height in pixels
 * [data] = zlib compressed list of indices into the palette
 *
 * The zlib data is prefixed with the standard 5-byte rarezip header.
 */
s32 texInflateZlib(u8 *src, u8 *dst, s32 arg2, s32 forcenumimages, struct texpool *arg4)
{
    s32 i;
    s32 imagebytesout;
    s32 numimages;
    s32 totalbytesout;
    s32 format;
    bool foundthething;
    bool writetocache;
    s32 width;
    s32 height;
    u8 *end;
    u8 *start;
    s32 numcolours;
    s32 j;
    s32 unused;
    u8 scratch2[0x800];
    u8 scratch[0x2100];
    u16 palette[0x100];

    totalbytesout = 0;
    writetocache = FALSE;

    texSetBitstring(src);

    if (arg2 && forcenumimages)
    {
        numimages = forcenumimages;
    }
    else
    {
        numimages = 1;
    }

    arg4->rightpos->maxlod = forcenumimages;
    arg4->rightpos->hasExplicitLods = arg2;

    if (arg2)
    {
        writetocache = TRUE;

        for (i = 0; i < g_TexCacheCount; i++)
        {
            if (g_TexCacheItems[i].texturenum == arg4->rightpos->texturenum)
            {
                writetocache = FALSE;
            }
        }
    }

    format = texReadBits(8);
    numcolours = texReadBits(8) + 1;

    for (i = 0; i < numcolours; i++)
    {
        palette[i] = texReadBits(16);
    }

    foundthething = FALSE;

    for (j = 0; j < numimages; j++)
    {
        width = texReadBits(8);
        height = texReadBits(8);

        if (j == 0)
        {
            arg4->rightpos->width = width;
            arg4->rightpos->height = height;
            arg4->rightpos->unk0a = numcolours - 1;
            arg4->rightpos->gbiformat = g_TexFormatGbiMappings[format];
            arg4->rightpos->depth = g_TexFormatDepths[format];
            arg4->rightpos->lutmodeindex = g_TexFormatLutModes[format] >> G_MDSFT_TEXTLUT;
        }
        else if (writetocache)
        {
            g_TexCacheItems[g_TexCacheCount].widths[j - 1] = width;
            g_TexCacheItems[g_TexCacheCount].heights[j - 1] = height;
        }

        if ((width * height) >= 4097)
        {
            return j * 0;
        }

        decompressdata(img_curpos, &scratch2, (struct huft *)&scratch);
        imagebytesout = texAlignIndices(scratch2, width, height, format, &dst[totalbytesout]);
        texSetBitstring(rzipGetSomething());

        if ((arg2 == 1) && (forcenumimages > 0))
        {
            texSwapAltRowBytes(&dst[totalbytesout], width, height, format);
        }
        totalbytesout += imagebytesout;
    }

    if (writetocache)
    {
        g_TexCacheItems[g_TexCacheCount].texturenum = arg4->rightpos->texturenum;

        g_TexCacheCount++;

        if (g_TexCacheCount >= ARRAYCOUNT(g_TexCacheItems))
        {
            g_TexCacheCount = 0;
        }
    }

    if (!arg2)
    {
        if (forcenumimages >= 2)
        {
            s32 tmpwidth;
            s32 tmpheight;

            tmpwidth = width;
            tmpheight = height;

            start = dst;
            end = &dst[totalbytesout];

            for (j = 1; j < forcenumimages; j++)
            {
                imagebytesout = texShrinkPaletted(start, end, tmpwidth, tmpheight, format, palette, numcolours);

                if (totalbytesout + imagebytesout > 0x800)
                {
                    arg4->rightpos->maxlod = j;
                    break;
                }

                texSwapAltRowBytes(start, tmpwidth, tmpheight, format);

                totalbytesout += imagebytesout;

                tmpwidth = (tmpwidth + 1) >> 1;
                tmpheight = (tmpheight + 1) >> 1;

                start = end;
                end += imagebytesout;
                if (1);
            }

            texSwapAltRowBytes(start, tmpwidth, tmpheight, format);
        }
        else if (forcenumimages == 1)
        {
            texSwapAltRowBytes(dst, width, height, format);
        }
    }

    for (i = 0; i < numcolours; i++)
    {
        if ((!totalbytesout) && (!totalbytesout));
        dst[totalbytesout + 0] = palette[i] >> 8;
        dst[totalbytesout + 1] = palette[i] & 0xff;
        totalbytesout += 2;
    }

    totalbytesout = (totalbytesout + 7) & ~7;

    return totalbytesout;
}


/**
 * Copy a list of palette indices to the dst buffer, but ensure each row is
 * aligned to an 8 byte boundary.
 *
 * Return the number of output bytes.
 */
s32 texAlignIndices(u8 *src, s32 width, s32 height, s32 format, u8 *dst)
{
    s32 x;
    s32 y;
    u8 *outptr;
    s32 indicesperbyte;

    outptr = dst;

    if (format == TEXFORMAT_RGBA16_CI8 || format == TEXFORMAT_IA16_CI8)
    {
        indicesperbyte = 1;
    }
    else if (format == TEXFORMAT_RGBA16_CI4 || format == TEXFORMAT_IA16_CI4)
    {
        indicesperbyte = 2;
    }
    else if (indicesperbyte)
    {
    }

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x += indicesperbyte)
        {
            *outptr = *src;
            outptr++;
            src++;
        }

        outptr = (u8 *)(((u32)outptr + 7) & ~7);
    }

    return outptr - dst;
}


/**
 * Shrink a paletted texture to half its size by averaging each each 2x2 group
 * of pixels.
 *
 * Return the number of bytes written.
 */
s32 texShrinkPaletted(u8 *src, u8 *dst, s32 srcwidth, s32 srcheight, s32 format, u16 *palette, s32 numcolours)
{
    s32 j;
    s32 i;
    s32 alignedsrcwidth;
    s32 aligneddstwidth;
    s32 dstheight;
    s16 colour1;
    s16 colour2;
    s16 colour3;
    s16 colour4;
    s32 r;
    s32 g;
    s32 b;
    s32 a;
    s32 nextrow;
    u8 *dst8;
    s32 nextcol;
    s32 c;
    u8 *src8;

    dst8 = dst;
    src8 = src;
    dstheight = (srcheight + 1) >> 1;

    switch (format)
    {
        case TEXFORMAT_RGBA16_CI8:
        case TEXFORMAT_IA16_CI8:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 7) & 0xff8;
            alignedsrcwidth = (srcwidth + 7) & 0xff8;
            break;

        case TEXFORMAT_RGBA16_CI4:
        case TEXFORMAT_IA16_CI4:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 15) & 0xff0;
            alignedsrcwidth = (srcwidth + 15) & 0xff0;
            break;
    }


    switch (format)
    {
        case TEXFORMAT_RGBA16_CI8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    colour1 = palette[src8[j]];
                    colour2 = palette[src8[nextcol]];
                    colour3 = palette[src8[nextrow + j]];
                    colour4 = palette[src8[nextrow + nextcol]];

                    r = ((((colour1 >> 0xB) & 0x1F) + ((colour2 >> 0xB) & 0x1F) + ((colour3 >> 0xB) & 0x1F) + ((colour4 >> 0xB) & 0x1F)) >> 2) & 0x1F;
                    g = ((((colour1 >> 6) & 0x1F) + ((colour2 >> 6) & 0x1F) + ((colour3 >> 6) & 0x1F) + ((colour4 >> 6) & 0x1F)) >> 2) & 0x1F;
                    b = ((((colour1 >> 1) & 0x1F) + ((colour2 >> 1) & 0x1F) + ((colour3 >> 1) & 0x1F) + ((colour4 >> 1) & 0x1F)) >> 2) & 0x1F;
                    a = (((colour1 & 1) + (colour2 & 1) + (colour3 & 1) + (colour4 & 1) + 2) >> 2) & 1;

                    dst8[j >> 1] = texFindClosestColourIndexRGBA(palette, numcolours, r, g, b, a);
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_IA16_CI8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    colour1 = palette[src8[j]];
                    colour2 = palette[src8[nextcol]];
                    colour3 = palette[src8[nextrow + j]];
                    colour4 = palette[src8[nextrow + nextcol]];

                    c = ((((colour1 >> 8) & 0xff) + ((colour2 >> 8) & 0xff) + ((colour3 >> 8) & 0xff) + ((colour4 >> 8) & 0xff)) >> 2) & 0xff;
                    a = ((((colour1 >> 0) & 0xff) + ((colour2 >> 0) & 0xff) + ((colour3 >> 0) & 0xff) + ((colour4 >> 0) & 0xff) + 1) >> 2) & 0xff;

                    dst8[j >> 1] = texFindClosestColourIndexIA(palette, numcolours, c, a);
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_RGBA16_CI4:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth >> 1 : 0;

                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    colour1 = palette[(src8[j >> 1] >> 4) & 0xf];
                    colour2 = palette[src8[j >> 1] >> ((j + 1 < srcwidth ? 0 : 4)) & 0xf];
                    colour3 = palette[(src8[nextrow + (j >> 1)] >> 4) & 0xf];
                    colour4 = palette[src8[nextrow + (j >> 1)] >> ((j + 1 < srcwidth ? 0 : 4)) & 0xf];

                    r = ((((colour1 >> 0xB) & 0x1F) + ((colour2 >> 0xB) & 0x1F) + ((colour3 >> 0xB) & 0x1F) + ((colour4 >> 0xB) & 0x1F)) >> 2) & 0x1F;
                    g = ((((colour1 >> 6) & 0x1F) + ((colour2 >> 6) & 0x1F) + ((colour3 >> 6) & 0x1F) + ((colour4 >> 6) & 0x1F)) >> 2) & 0x1F;
                    b = ((((colour1 >> 1) & 0x1F) + ((colour2 >> 1) & 0x1F) + ((colour3 >> 1) & 0x1F) + ((colour4 >> 1) & 0x1F)) >> 2) & 0x1F;
                    a = ((((colour1 & 1) + (colour2 & 1) + (colour3 & 1) + (colour4 & 1) + 2) >> 2) & 1);

                    dst8[j >> 2] = (texFindClosestColourIndexRGBA(palette, numcolours, r, g, b, a) * 0x10) & 0xFFFF;

                    colour1 = palette[(src8[(j + 2) >> 1] >> 4) & 0xf];
                    colour2 = palette[(src8[(j + 2) >> 1] >> (j + 3 < srcwidth ? 0 : 4)) & 0xf];
                    colour3 = palette[(src8[nextrow + ((j + 2) >> 1)] >> 4) & 0xf];
                    colour4 = palette[(src8[nextrow + ((j + 2) >> 1)] >> (j + 3 < srcwidth ? 0 : 4)) & 0xf];

                    r = ((((colour1 >> 0xB) & 0x1F) + ((colour2 >> 0xB) & 0x1F) + ((colour3 >> 0xB) & 0x1F) + ((colour4 >> 0xB) & 0x1F)) >> 2) & 0x1F;
                    g = ((((colour1 >> 6) & 0x1F) + ((colour2 >> 6) & 0x1F) + ((colour3 >> 6) & 0x1F) + ((colour4 >> 6) & 0x1F)) >> 2) & 0x1F;
                    b = ((((colour1 >> 1) & 0x1F) + ((colour2 >> 1) & 0x1F) + ((colour3 >> 1) & 0x1F) + ((colour4 >> 1) & 0x1F)) >> 2) & 0x1F;
                    a = ((((colour1 & 1) + (colour2 & 1) + (colour3 & 1) + (colour4 & 1) + 2) >> 2) & 1);

                    dst8[j >> 2] |= texFindClosestColourIndexRGBA(palette, numcolours, r, g, b, a) & 0xff;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;

        case TEXFORMAT_IA16_CI4:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth >> 1 : 0;

                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    // @bug: The brackets are wrong in colour2 and colour4 which
                    // causes the index shift to be part of the ternary condition.
                    // It's done correctly in TEXFORMAT_RGBA16_CI4 (above).
                    // This buggy calculation is repeated further below.
                    colour1 = palette[(src8[j >> 1] >> 4) & 0xf];
                    colour2 = palette[(src8[j >> 1] >> (j + 1 < srcwidth) ? 0 : 4) & 0xf];
                    colour3 = palette[(src8[nextrow + (j >> 1)] >> 4) & 0xf];
                    colour4 = palette[(src8[nextrow + (j >> 1)] >> (j + 1 < srcwidth) ? 0 : 4) & 0xf];

                    c = ((((colour1 >> 8) & 0xff) + ((colour2 >> 8) & 0xff) + ((colour3 >> 8) & 0xff) + ((colour4 >> 8) & 0xff)) >> 2) & 0xff;
                    a = ((((colour1 >> 0) & 0xff) + ((colour2 >> 0) & 0xff) + ((colour3 >> 0) & 0xff) + ((colour4 >> 0) & 0xff) + 1) >> 2) & 0xff;

                    dst8[j >> 2] = (texFindClosestColourIndexIA(palette, numcolours, c, a) * 0x10) & 0xFFFF;

                    colour1 = palette[(src8[(j + 2) >> 1] >> 4) & 0xf];
                    colour2 = palette[(src8[(j + 2) >> 1] >> (j + 3 < srcwidth) ? 0 : 4) & 0xf];
                    colour3 = palette[(src8[nextrow + ((j + 2) >> 1)] >> 4) & 0xf];
                    colour4 = palette[(src8[nextrow + ((j + 2) >> 1)] >> (j + 3 < srcwidth) ? 0 : 4) & 0xf];

                    c = ((((colour1 >> 8) & 0xff) + ((colour2 >> 8) & 0xff) + ((colour3 >> 8) & 0xff) + ((colour4 >> 8) & 0xff)) >> 2) & 0xff;
                    a = ((((colour1 >> 0) & 0xff) + ((colour2 >> 0) & 0xff) + ((colour3 >> 0) & 0xff) + ((colour4 >> 0) & 0xff) + 1) >> 2) & 0xff;

                    dst8[j >> 2] |= texFindClosestColourIndexIA(palette, numcolours, c, a) & 0xff;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;
    }

    return 0;
}


/**
 * Find the palette entry closest to the target colour and return its index.
 * Used for texture LOD generation/shrinking a colour-indexed texture (see texShrinkPaletted): the 2x2
 * box filter averages four palette entries, and the average may not be a colour
 * the palette already holds, so it has to be requantized to the nearest entry. This allows the LOD
 * textures to use existing palettes instead of creating new ones.
 * 
 * The design of the algorithm is to:
 * 1) Return an exact match in the palette for the target colour if there is one.
 * 2) Failing that, a binary search uses cheap scalar ordering to find a promising region in the palette.
 * 3) Perform a more meaningful RGBA comparison on only a small section of the palette.
 * 
 * Stage 2 is an approximation so this function isn't necessarily guaranteed to return the closest matching palette index for a target colour.
 * 
 * Used by TEXFORMAT_RGBA16_CI8, TEXFORMAT_RGBA16_CI4
 *
 * @param palette    Palette to search, RGBA5551, assumed ordered by the stage 2 key
 * @param numcolours Number of entries in palette
 * @param r          Target red, 0..31
 * @param g          Target green, 0..31
 * @param b          Target blue, 0..31
 * @param a          Target alpha, 0..1
 * @return Index of the closest entry, or 0 if numcolours is not positive
 */
s32 texFindClosestColourIndexRGBA(u16 *palette, s32 numcolours, s32 r, s32 g, s32 b, s32 a)
{
    s32 low;
    s32 high;
    s32 i;
    u16 targetcolour;
    s32 targetmagnitude;
 
    // Cursor into the palette: the midpoint in stage 2, the scan position in stage 3.
    s32 paletteidx;
 
    u16 colour;
    s32 red;
    s32 green;
    s32 blue;
    s32 alpha;
    s32 magnitude;
 
    s32 diffr;
    s32 diffg;
    s32 diffb;
    s32 alphapenalty;
    s32 distance;
 
    s32 bestindex;
    s32 bestvalue;
 
    // Stage 1: scan the whole palette for a matching colour and return its index if one is found.
    targetcolour = ((r << 11) | (g << 6) | (b << 1) | a);
 
    for (i = 0; i < numcolours; i++)
    {
        if (targetcolour == palette[i])
        {
            return i;
        }
    }
 
    /** 
     * Stage 2: Find a promising palette neighborhood.
     */
    low = 0;
    high = numcolours - 1;
    // TEX_ALPHA_WEIGHT = 31^2, the maximum possible squared difference in one five-bit color channel.
    targetmagnitude = (r * r) + (g * g) + (b * b) + (a * TEX_ALPHA_WEIGHT);
 
    while (high - low >= 2)
    {
        paletteidx = (high + low) >> 1;
 
        colour = palette[paletteidx];
        red = (colour >> 11) & 0x1F;
        green = (colour >> 6) & 0x1F;
        blue = (colour >> 1) & 0x1F;
        alpha = colour & 1;
 
        magnitude = (red * red) + (green * green) + (blue * blue) + (alpha * TEX_ALPHA_WEIGHT);
 
        if (magnitude < targetmagnitude)
        {
            low = paletteidx;
            continue;
        }
 
        if (targetmagnitude < magnitude)
        {
            high = paletteidx;
        }
        else
        {
            high = paletteidx;
            low = paletteidx;
        }
    }
 
    // Stage 3: Search the nearby palette entries accurately. 
    low = high - 4;
 
    if (low < 0)
    {
        low = 0;
    }
 
    high += 4;
 
    if (high >= numcolours)
    {
        high = numcolours - 1;
    }
 
    bestindex = 0;
    bestvalue = 999999;
 
    for (paletteidx = low; paletteidx <= high; paletteidx++)
    {
        colour = palette[paletteidx];
        diffr = ((colour >> 11) & 0x1F) - r;
        diffg = ((colour >> 6) & 0x1F) - g;
        diffb = ((colour >> 1) & 0x1F) - b;
 
        // An alpha mismatch costs 961, strongly discouraging a palette entry with the wrong transparency.
        alphapenalty = (a == (colour & 1)) ? 0 : TEX_ALPHA_WEIGHT;
 
        distance = alphapenalty;
        distance += diffr * diffr;
        distance += diffg * diffg;
        distance += diffb * diffb;
 
        if (distance < bestvalue)
        {
            bestindex = paletteidx;
            bestvalue = distance;
        }
    }
 
    return bestindex;
}


/**
 * Find the palette entry closest to the given intensity/alpha pair and return its index.
 *
 * Used by TEXFORMAT_IA16_CI8 and TEXFORMAT_IA16_CI4 to
 * requantize box-filtered averages back onto the palette.
 *
 * Colours are IA16, packed iiiiiiiiaaaaaaaa, intensity and alpha both 0..255.
 *
 * @param palette    Palette to search, IA16, assumed ordered by the stage 2 key
 * @param numcolours Number of entries in palette
 * @param intensity  Target intensity, 0..255
 * @param alpha      Target alpha, 0..255
 * @return Index of the closest entry, or 0 if numcolours is not positive
 */
s32 texFindClosestColourIndexIA(u16 *palette, s32 numcolours, s32 intensity, s32 alpha)
{
    s32 scanstart;
    s32 high;
    s32 i;
    s32 scanidx;
    s32 bestindex;
    s32 bestvalue;
    s32 low;
    s32 targetcolour;
    s32 targetmagnitude;
    s32 colour;
    s32 entryintensity;
    s32 entryalpha;
    s32 magnitude;
    s32 diffi;
    s32 diffa;
    s32 distance;
 
    // Stage 1: scan the whole palette for a matching intensity and return its index if one is found.
    targetcolour = (intensity << 8) | alpha;
 
    for (i = 0; i < numcolours; i++)
    {
        if ((u16)targetcolour == palette[i])
        {
            return i;
        }
    }
 
    // Stage 2: binary search by squared magnitude.
    low = 0;
    high = numcolours - 1;
    targetmagnitude = (intensity * intensity) + (alpha * alpha);
 
    while (high - low >= 2)
    {
        s32 mid;
 
        mid = (high + low) >> 1;
        colour = palette[mid];
 
        entryintensity = (colour >> 8) & 0xFF;
        entryalpha = colour & 0xFF;
        magnitude = (entryintensity * entryintensity) + (entryalpha * entryalpha);
 
        if (magnitude < targetmagnitude)
        {
            low = mid;
            continue;
        }
 
        if (targetmagnitude < magnitude)
        {
            high = mid;
        }
        else
        {
            // Equal magnitude. Collapse the window to end the search here.
            low = mid;
            high = mid;
        }
    }
 
    // Stage 3: widen the window by four entries either side and clamp it.
    scanstart = high - 4;
    scanidx = scanstart;
    high += 4;

    if (scanidx < 0)
    {
        scanstart = 0;
    }
 
    if (high >= numcolours)
    {
        high = numcolours - 1;
    }

    /* 999999 is an unreachable sentinel: the worst possible distance is
     * 2 * 255 * 255 = 130050. */
    bestindex = 0;
    bestvalue = 999999;

    scanidx = scanstart;
 
    if (scanidx <= high)
    {
        s32 scanend;

        for (;;)
        {
            colour = palette[scanidx];
 
            diffi = ((colour >> 8) & 0xff) - intensity;
            diffa = (colour & 0xff) - alpha;
            distance = (diffi * diffi) + (diffa * diffa);
            scanend = high + 1;
 
            if (distance < bestvalue)
            {
                bestindex = scanidx;
                bestvalue = distance;
            }

            scanidx++;

            if (scanend == scanidx)
            {
                break;
            }
        }
    }
 
    return bestindex;
}


/**
 * Inflate images (levels of detail) from a non-zlib texture.
 *
 * Each image can have a different compression method and pixel format,
 * which is described in a three byte header per image:
 *
 * ffffwwww wwwwhhhh hhhhcccc
 *
 * f = pixel format (see TEXFORMAT constants)
 * w = width in pixels
 * h = height in pixels
 * c = compression method (see TEXCOMPMETHOD constants)
 */
s32 texInflateNonZlib(u8 *src, u8 *dst, s32 arg2, s32 forcenumimages, struct texpool *arg4)
{
    u8 scratch[0x2000];
    u8 lookup[0x1000];
    u32 stack;
    s32 i;
    s32 numimages;
    s32 width;
    s32 height;
    s32 compmethod;
    s32 j;
    s32 totalbytesout = 0;
    s32 imagebytesout;
    s32 format;
    s32 value;
    u8 *start;
    u8 *end;
    s32 writetocache = FALSE;

    texSetBitstring(src);

    numimages = arg2 && forcenumimages ? forcenumimages : 1;

    arg4->rightpos->maxlod = forcenumimages;
    arg4->rightpos->hasExplicitLods = arg2;

    if (arg2)
    {
        writetocache = TRUE;

        for (i = 0; i < g_TexCacheCount; i++)
        {
            if (g_TexCacheItems[i].texturenum == arg4->rightpos->texturenum)
            {
                writetocache = FALSE;
            }
        }
    }

    for (i = 0; i < numimages; i++)
    {
        format = texReadBits(4);
        width = texReadBits(8);
        height = texReadBits(8);
        compmethod = texReadBits(4);

        if (i == 0)
        {
            arg4->rightpos->width = width;
            arg4->rightpos->height = height;
            arg4->rightpos->gbiformat = g_TexFormatGbiMappings[format];
            arg4->rightpos->depth = g_TexFormatDepths[format];
            arg4->rightpos->lutmodeindex = g_TexFormatLutModes[format] >> G_MDSFT_TEXTLUT;
        }
        else if (writetocache)
        {
            g_TexCacheItems[g_TexCacheCount].widths[i - 1] = width;
            g_TexCacheItems[g_TexCacheCount].heights[i - 1] = height;
        }

        if (width * height > 0x2000)
        {
            return 0;
        }

        switch (compmethod)
        {
            case TEXCOMPMETHOD_UNCOMPRESSED0:
            case TEXCOMPMETHOD_UNCOMPRESSED1:
                imagebytesout = texReadUncompressed(&dst[totalbytesout], width, height, format);
                break;

            case TEXCOMPMETHOD_HUFFMAN:
                texInflateHuffman(scratch, g_TexFormatNumChannels[format] * width * height, g_TexFormatChannelSizes[format]);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_HUFFMANPERHCHANNEL:
                for (j = 0; j < g_TexFormatNumChannels[format]; j++)
                {
                    if (1);
                    texInflateHuffman(&scratch[width * height * j], width * height, g_TexFormatChannelSizes[format]);
                }

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_RLE:
                texInflateRle(scratch, g_TexFormatNumChannels[format] * width * height);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_LOOKUP:
                value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
                imagebytesout = texInflateLookup(width, height, &dst[totalbytesout], lookup, value, format);
                break;

            case TEXCOMPMETHOD_HUFFMANLOOKUP:
                value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
                texInflateHuffman(scratch, width * height, value);
                imagebytesout = texInflateLookupFromBuffer(scratch, width, height, &dst[totalbytesout], lookup, value, format);
                break;

            case TEXCOMPMETHOD_RLELOOKUP:
                value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
                texInflateRle(scratch, width * height);
                imagebytesout = texInflateLookupFromBuffer(scratch, width, height, &dst[totalbytesout], lookup, value, format);
                break;

            case TEXCOMPMETHOD_HUFFMANBLUR:
                stack = texReadBits(3);
                texInflateHuffman(scratch, g_TexFormatNumChannels[format] * width * height, g_TexFormatChannelSizes[format]);
                texBlur(scratch, width, g_TexFormatNumChannels[format] * height, stack, g_TexFormatChannelSizes[format]);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            case TEXCOMPMETHOD_RLEBLUR:
                stack = texReadBits(3);
                texInflateRle(scratch, g_TexFormatNumChannels[format] * width * height);
                texBlur(scratch, width, g_TexFormatNumChannels[format] * height, stack, g_TexFormatChannelSizes[format]);

                if (g_TexFormatHas1BitAlpha[format])
                {
                    texReadAlphaBits(&scratch[width * height * 3], width * height);
                }

                imagebytesout = texChannelsToPixels(scratch, width, height, &dst[totalbytesout], format);
                break;

            default:
                while (TRUE)
                {
                    // Hang forever!
                };
        }

        if (arg2 == 1 && forcenumimages > 0)
        {
            texSwapAltRowBytes(&dst[totalbytesout], width, height, format);
        }

        imagebytesout = (imagebytesout + 7) & ~7;
        totalbytesout += imagebytesout;

        if (img_bitcount == 0)
        {
            img_curpos++;
        }
        else
        {
            img_bitcount = 0;
        }
    }

    if (writetocache)
    {
        g_TexCacheItems[g_TexCacheCount].texturenum = arg4->rightpos->texturenum;

        g_TexCacheCount++;

        // Resetting this variable to 0 here suggests that the g_TexCacheItems
        // array is used in a circular manner, and that g_TexCacheCount is just
        // the index of the oldest/next element. But earlier in this function
        // there's a loop that iterates up to g_TexCacheCount, which doesn't
        // make any sense if this value is used as a pointer in a circular list.
        // Could be a @bug, or maybe they intended to reset the cache every time
        // it fills up.
        if (g_TexCacheCount >= ARRAYCOUNT(g_TexCacheItems))
        {
            g_TexCacheCount = 0;
        }
    }

    if (!arg2)
    {
        if (forcenumimages >= 2)
        {
            s32 tmpwidth = width;
            s32 tmpheight = height;

            start = dst;
            end = &dst[totalbytesout];

            for (i = 1; i < forcenumimages; i++)
            {
                imagebytesout = texShrinkNonPaletted(start, end, tmpwidth, tmpheight, format);

                texSwapAltRowBytes(start, tmpwidth, tmpheight, format);

                totalbytesout += imagebytesout;

                tmpwidth = (tmpwidth + 1) >> 1;
                tmpheight = (tmpheight + 1) >> 1;

                start = end;
                end += imagebytesout;
            }

            texSwapAltRowBytes(start, tmpwidth, tmpheight, format);
        }
        else if (forcenumimages == 1)
        {
            texSwapAltRowBytes(dst, width, height, format);
        }
    }

    return totalbytesout;
}


/**
 * Shrink a non-paletted texture to half its size by averaging each each 2x2
 * group of pixels.
 *
 * Return the number of bytes written.
 *
 * If the source width is an odd number, the destination's final column is
 * calculated by sampling the final source column twice. Likewise for the height.
 */

s32 texShrinkNonPaletted(u8 *src, u8 *dst, s32 srcwidth, s32 srcheight, s32 format)
{
    s32 i;
    s32 j;
    s32 alignedsrcwidth;
    s32 aligneddstwidth;
    u32 *dst32 = (u32 *) dst;
    u16 *dst16 = (u16 *) dst;
    u8 *dst8 = dst;
    u32 *src32 = (u32 *) src;
    u16 *src16 = (u16 *) src;
    u8 *src8 = src;
    s32 dstheight = (srcheight + 1) >> 1;
    s32 r;
    s32 g;
    s32 b;
    s32 a;
    s32 c;
    u32 tl32;
    u32 tr32;
    u32 bl32;
    u32 br32;
    u16 tl16;
    u16 tr16;
    u16 bl16;
    u16 br16;
    u8 tl8;
    u8 tr8;
    u8 bl8;
    u8 br8;
    s32 nextrow;
    s32 nextcol;

    switch (format)
    {
        case TEXFORMAT_RGBA32:
        case TEXFORMAT_RGB24:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 3) & 0xffc;
            alignedsrcwidth = (srcwidth + 3) & 0xffc;
            break;
        case TEXFORMAT_RGBA16:
        case TEXFORMAT_RGB15:
        case TEXFORMAT_IA16:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 3) & 0xffc;
            alignedsrcwidth = (srcwidth + 3) & 0xffc;
            break;
        case TEXFORMAT_IA8:
        case TEXFORMAT_I8:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 7) & 0xff8;
            alignedsrcwidth = (srcwidth + 7) & 0xff8;
            break;
        case TEXFORMAT_IA4:
        case TEXFORMAT_I4:
            aligneddstwidth = (((srcwidth + 1) >> 1) + 15) & 0xff0;
            alignedsrcwidth = (srcwidth + 15) & 0xff0;
            break;
    }

    switch (format)
    {
        case TEXFORMAT_RGBA32:
        case TEXFORMAT_RGB24:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl32 = src32[j];
                    tr32 = src32[nextcol];
                    bl32 = src32[nextrow + j];
                    br32 = src32[nextrow + nextcol];

                    r = ((((tl32 >> 24) & 0xff) + ((tr32 >> 24) & 0xff) + ((bl32 >> 24) & 0xff) + ((br32 >> 24) & 0xff)) >> 2) & 0xff;
                    g = ((((tl32 >> 16) & 0xff) + ((tr32 >> 16) & 0xff) + ((bl32 >> 16) & 0xff) + ((br32 >> 16) & 0xff)) >> 2) & 0xff;
                    b = ((((tl32 >>  8) & 0xff) + ((tr32 >>  8) & 0xff) + ((bl32 >>  8) & 0xff) + ((br32 >>  8) & 0xff)) >> 2) & 0xff;
                    a = ((((tl32 >>  0) & 0xff) + ((tr32 >>  0) & 0xff) + ((bl32 >>  0) & 0xff) + ((br32 >>  0) & 0xff) + 1) >> 2) & 0xff;

                    dst32[j >> 1] = r << 24 | g << 16 | b << 8 | a;
                }

                dst32 += aligneddstwidth;
                src32 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth * 4;

        case TEXFORMAT_RGBA16:
        case TEXFORMAT_RGB15:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl16 = src16[j];
                    tr16 = src16[nextcol];
                    bl16 = src16[nextrow + j];
                    br16 = src16[nextrow + nextcol];

                    r = ((((tl16 >> 11) & 0x1f) + ((tr16 >> 11) & 0x1f) + ((bl16 >> 11) & 0x1f) + ((br16 >> 11) & 0x1f)) >> 2) & 0x1f;
                    g = ((((tl16 >>  6) & 0x1f) + ((tr16 >>  6) & 0x1f) + ((bl16 >>  6) & 0x1f) + ((br16 >>  6) & 0x1f)) >> 2) & 0x1f;
                    b = ((((tl16 >>  1) & 0x1f) + ((tr16 >>  1) & 0x1f) + ((bl16 >>  1) & 0x1f) + ((br16 >>  1) & 0x1f)) >> 2) & 0x1f;
                    a = ((((tl16 >>  0) & 0x01) + ((tr16 >>  0) & 0x01) + ((bl16 >>  0) & 0x01) + ((br16 >>  0) & 0x01) + 2) >> 2) & 0x01;

                    dst16[j >> 1] = r << 11 | g << 6 | b << 1 | a;
                }

                dst16 += aligneddstwidth;
                src16 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth * 2;

        case TEXFORMAT_IA16:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl16 = src16[j];
                    tr16 = src16[nextcol];
                    bl16 = src16[nextrow + j];
                    br16 = src16[nextrow + nextcol];

                    c = (((tl16 >> 8) & 0xff) + ((tr16 >> 8) & 0xff) + ((bl16 >> 8) & 0xff) + ((br16 >> 8) & 0xff)) >> 2;
                    a = ((tl16 & 0xff) + (tr16 & 0xff) + (bl16 & 0xff) + (br16 & 0xff) + 1) >> 2;

                    dst16[j >> 1] = ((u8)c << 8) | (a & 0xFF);
                }

                dst16 += aligneddstwidth;
                src16 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth * 2;

        case TEXFORMAT_IA8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl8 = src8[j];
                    tr8 = src8[nextcol];
                    bl8 = src8[nextrow + j];
                    br8 = src8[nextrow + nextcol];

                    c = ((((tl8 >> 4) & 0xf) + ((tr8 >> 4) & 0xf) + ((bl8 >> 4) & 0xf) + ((br8 >> 4) & 0xf)) << 2) & 0xF0;
                    a = (((tl8 & 0xf) + (tr8 & 0xf) + (bl8 & 0xf) + (br8 & 0xf) + 1) >> 2) & 0xF;

                    dst8[j >> 1] = c | a;
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_I8:
            for (i = 0; i < srcheight; i += 2)
            {
                nextrow = i + 1 < srcheight ? alignedsrcwidth : 0;

                for (j = 0; j < alignedsrcwidth; j += 2)
                {
                    nextcol = j + 1 < srcwidth ? j + 1 : j;

                    tl8 = src8[j];
                    tr8 = src8[nextcol];
                    bl8 = src8[nextrow + j];
                    br8 = src8[nextrow + nextcol];

                    c = (u16)((tl8 + tr8 + bl8 + br8 + 1) >> 2);

                    dst8[j >> 1] = c;
                }

                dst8 += aligneddstwidth;
                src8 += alignedsrcwidth * 2;
            }

            return dstheight * aligneddstwidth;

        case TEXFORMAT_IA4:
            for (i = 0; i < srcheight; i += 2)
            {
                nextcol = i + 1;

                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    tl8 = src8[j >> 1];
                    tr8 = src8[(nextcol < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1)];
                    bl8 = src8[(j >> 1) + 1];
                    br8 = src8[(nextcol < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1) + 1];

                    c = (((((tl8 >> 5) & 7) + ((tl8 >> 1) & 7) + ((tr8 >> 5) & 7) + ((tr8 >> 1) & 7)) << 3) & 0xe0)
                        | (((((bl8 >> 5) & 7) + ((bl8 >> 1) & 7) + ((br8 >> 5) & 7) + ((br8 >> 1) & 7)) >> 1) & 0xe);

                    a = (((((tl8 >> 4) & 1) + (tl8 & 1) + ((tr8 >> 4) & 1) + (tr8 & 1) + 1) << 2) & 0x10)
                        | (((((bl8 >> 4) & 1) + (bl8 & 1) + ((br8 >> 4) & 1) + (br8 & 1) + 1) >> 2) & 1);

                    dst8[j >> 2] = c | a;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;

        case TEXFORMAT_I4:
            for (i = 0; i < srcheight; i += 2)
            {
                for (j = 0; j < alignedsrcwidth; j += 4)
                {
                    tl8 = src8[j >> 1];
                    tr8 = src8[(i + 1 < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1)];
                    bl8 = src8[(j >> 1) + 1];
                    br8 = src8[(i + 1 < srcheight ? (alignedsrcwidth >> 1) : 0) + (j >> 1) + 1];

                    c = ((((tl8 >> 4) & 0xf) + (tl8 & 0xf) + ((tr8 >> 4) & 0xf) + (tr8 & 0xf)) << 2) & 0xf0;
                    a = ((((bl8 >> 4) & 0xf) + (bl8 & 0xf) + ((br8 >> 4) & 0xf) + (br8 & 0xf)) >> 2) & 0xf;

                    dst8[j >> 2] = c | a;
                }

                dst8 += aligneddstwidth >> 1;
                src8 += alignedsrcwidth;
            }

            return (aligneddstwidth >> 1) * dstheight;
    }

    return 0;
}


/**
 * Inflate Huffman data.
 *
 * This function operates on single channels rather than whole colours.
 * For example, for an RGBA32 image this function may be called once for each
 * channel with chansize = 256. This means the resulting data is in the format
 * RRR...GGG...BBB...AAA..., and the caller must convert it into a proper pixel
 * format.
 *
 * A typical Huffman implementation stores a tree, where each node contains
 * the lookup value and its frequency (number of uses). However, Rare's
 * implementation only stores a list of frequencies. It uses the chansize
 * to know how many values there are.
 */
void texInflateHuffman(u8 *dst, s32 numiterations, s32 chansize)
{
	u16 frequencies[2048];
	s16 nodes[2048][2];
	s32 i;
	s32 rootindex;
	s32 sum;
	u16 minfreq1;
	u16 minfreq2;
	s32 minindex1; // 5c
	s32 minindex2; // 58
	s32  done = 0;

	// Read the frequencies list
	for (i = 0; i < chansize; i++) {
		frequencies[i] = texReadBits(8);
	}

	// Initialise the tree
	for (i = 0; i < 2048; i++) {
		nodes[i][0] = -1;
		nodes[i][1] = -1;
	}

	// Find the two smallest frequencies
	minfreq1 = 9999;
	minfreq2 = 9999;

	for (i = 0; i < chansize; i++) {
		if (frequencies[i] < minfreq1) {
			if (minfreq2 < minfreq1) {
				minfreq1 = frequencies[i];
				minindex1 = i;
			} else {
				minfreq2 = frequencies[i];
				minindex2 = i;
			}
		} else if (frequencies[i] < minfreq2) {
			minfreq2 = frequencies[i];
			minindex2 = i;
		}
	}

	// Build the tree.
	// For each node in tree, a branch value < 10000 means this branch
	// leads to another node, and the value is the target node's index.
	// A branch value >= 10000 means the branch is a leaf node,
	// and the value is the channel value + 10000.
	while (!done) {
		sum = frequencies[minindex1] + frequencies[minindex2];

		if (sum == 0) {
			sum = 1;
		}

		frequencies[minindex1] = 9999;
		frequencies[minindex2] = 9999;

		if (nodes[minindex1][0] < 0 && nodes[minindex1][1] < 0) {
			nodes[minindex1][0] = minindex1 + 10000;
			rootindex = minindex1;
			frequencies[minindex1] = sum;

			if (nodes[minindex2][0] < 0 && nodes[minindex2][1] < 0) {
				nodes[minindex1][1] = minindex2 + 10000;
			} else {
				nodes[minindex1][1] = minindex2;
			}
		} else if (nodes[minindex2][0] < 0 && nodes[minindex2][1] < 0) {
			nodes[minindex2][0] = minindex2 + 10000;
			rootindex = minindex2;
			frequencies[minindex2] = sum;

			if (nodes[minindex1][0] < 0 && nodes[minindex1][1] < 0) {
				nodes[minindex2][1] = minindex1 + 10000;
			} else {
				nodes[minindex2][1] = minindex1;
			}
		} else {
			for (rootindex = 0; nodes[rootindex][0] >= 0 || nodes[rootindex][1] >= 0 || frequencies[rootindex] < 9999; rootindex++);

			frequencies[rootindex] = sum;
			nodes[rootindex][0] = minindex1;
			nodes[rootindex][1] = minindex2;
		}

		// Find the two smallest frequencies again for the next iteration
		minfreq1 = 9999;
		minfreq2 = 9999;

		for (i = 0; i < chansize; i++) {
			if (frequencies[i] < minfreq1) {
				if (minfreq1 > minfreq2) {
					minfreq1 = frequencies[i];
					minindex1 = i;
				} else {
					minfreq2 = frequencies[i];
					minindex2 = i;
				}
			} else if (frequencies[i] < minfreq2) {
				minfreq2 = frequencies[i];
				minindex2 = i;
			}
		}

		if (minfreq1 == 9999 || minfreq2 == 9999) {
			done = 1;
		}
	}

	// Read bits off the bitstring, traverse the tree
	// and write the channel values to dst
	for (i = 0; i < numiterations; i++) {
		s32 indexorvalue = rootindex;

		while (indexorvalue < 10000) {
			indexorvalue = nodes[indexorvalue][texReadBits(1)];
		}

		if (chansize <= 256) {
			dst[i] = indexorvalue - 10000;
		} else {
			u16 *tmp = (u16 *)dst;
			tmp[i] = indexorvalue - 10000;
		}
	}
}




/**
 * Inflate runlength-encoded data.
 *
 * This data consists of a 10 bit header followed by a list of directives,
 * where each directive can either be a literal block or a repeat (run) of
 * blocks within a sliding window.
 *
 * The header format is:
 *
 * 3 bits btfieldsize: The size in bits of the backtrack distance fields
 * 3 bits rlfieldsize: The size in bits of the runlen fields
 * 4 bits blocksize: The size in bits of each block of data
 *
 * In the data, the first bit is 0 if it's a literal block or 1 if it's a run.
 *
 * For literal blocks, the next <blocksize> bits should be read and appended to
 * the output stream.
 *
 * For runs, the next <btfieldsize> bits are the backtrack length (in blocks)
 * plus one, and the next <rlfieldsize> bits are the run length (in blocks)
 * minus a calculated fudge value.
 *
 * The fudge value is calculated based on the field sizes. For small runs it is
 * more space efficient to use multiple literal directives rather than a run
 * directive. Because of this, smaller runs are not used and the run lengths
 * in the data can be offset accordingly - this offset is the fudge value.
 *
 * Every run must be followed by a literal block without the 1-bit marker.
 * The algorithm does not support back to back runs.
 */
void texInflateRle(u8 *dst, s32 blockstotal)
{
	s32 btfieldsize = texReadBits(3);
	s32 rlfieldsize = texReadBits(3);
	s32 blocksize = texReadBits(4);
	s32 cost;
	s32 fudge;
	s32 blocksdone;
	s32 i;

	// Calculate the fudge value
	cost = btfieldsize + rlfieldsize + blocksize + 1;
	fudge = 0;

	while (cost > 0) {
		cost = cost - blocksize - 1;
		fudge++;
	}

	blocksdone = 0;

	while (blocksdone < blockstotal) {
		if (texReadBits(1) == 0) {
			// Found a literal directive
			if (blocksize <= 8) {
				dst[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			} else {
				u16 *tmp = (u16 *)dst;
				tmp[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			}
		} else {
			// Found a run directive
			s32 startblockindex = blocksdone - texReadBits(btfieldsize) - 1;
			s32 runnumblocks = texReadBits(rlfieldsize) + fudge;

			if (blocksize <= 8) {
				for (i = startblockindex; i < startblockindex + runnumblocks; i++) {
					dst[blocksdone] = dst[i];
					blocksdone++;
				}

				// The next instruction must be a literal
				dst[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			} else {
				u16 *tmp = (u16 *)dst;

				for (i = startblockindex; i < startblockindex + runnumblocks; i++) {
					tmp[blocksdone] = tmp[i];
					blocksdone++;
				}

				// The next instruction must be a literal
				tmp[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			}
		}
	}
}




/**
 * Populate a lookup table by reading it out of the bit string.
 *
 * The first 11 bits denote the number of colours in the lookup table.
 * The data following this is a list of colours, where each colour is sized
 * according to the texture's format.
 *
 * This function does NOT work with pixel formats of 8 bits or less.
 */
s32 texBuildLookup(s8 *lookup, s32 bitsperpixel)
{
    s32 numcolors = texReadBits(11);
    s32 i;
    
    if (bitsperpixel <= 16)
    {
        u16 *image = (u16*)lookup;
        for (i = 0; i < numcolors; i++)
        {
            image[i] = texReadBits(bitsperpixel);
        }
    }
    else if (bitsperpixel <= 24)
    {
        u32 *image = (u32*)lookup;
        for (i = 0; i < numcolors; i++)
        {
            image[i] = texReadBits(bitsperpixel);
        }
    }
    else
    {
        u32 *image = (u32*)lookup;
        for (i = 0; i < numcolors; i++)
        {
            image[i] = texReadBits(24) << 8 | texReadBits(bitsperpixel - 24);
        }
    }
    return numcolors;
}






s32 texGetBitSize(s32 decimal)
{
	s32 count = 0;

	decimal--;

	while (decimal > 0) {
		decimal >>= 1;
		count++;
	}

	return count;
}



void texReadAlphaBits(u8 *image,s32 count)
{
  int i;
  
    for(i = 0; i < count; i++)
    {
          image[i] = texReadBits(1);
    }
}



/**
 * Read pixel data from the bitstream and write to dst,
 * ensuring each row is aligned according to the pixel format.
 *
 * Return the number of output bytes.
 */
s32 texReadUncompressed(u8 *dst, s32 width, s32 height, s32 format)
{
	u32 *dst32 = (u32 *)(((u32)dst + 0xf) & ~0xf);
	u16 *dst16 = (u16 *)(((u32)dst + 7) & ~7);
	u8 *dst8 = (u8 *)(((u32)dst + 7) & ~7);
	s32 x;
	s32 y;

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = texReadBits(16) << 16;
				dst32[x] |= texReadBits(16);
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = texReadBits(24) << 8 | 0xff;
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = texReadBits(16);
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = texReadBits(15) << 1 | 1;
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = texReadBits(8);
			}

			dst8 += (width + 7) & 0xff8;
		}

		return ((width + 7) & 0xff8) * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = texReadBits(8);
			}

			dst8 += ((width + 15) & 0xff0) >> 1;
		}

		return (((width + 15) & 0xff0) >> 1) * height;
	}

	return 0;
}


s32 texChannelsToPixels(u8 *src, s32 width, s32 height, u8 *dst, s32 format)
{
    u32 *dst32;
    u16 *dst16;
    u8 *dst8;
    s32 x;
    s32 y;
    s32 pos;
    s32 mult;
    s32 rgb_width;

    dst32 = (u32 *)dst;
    dst16 = (u16 *)dst;
    dst8  = (u8 *)dst;

    pos = 0;
    mult = width * height;
    rgb_width = (width + 3) & 0xffc;

    switch (format)
    {
        case TEXFORMAT_RGBA32:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst32[x] = src[pos] << 24 | src[pos + mult] << 16 | src[pos + mult * 2] << 8 | src[pos + mult * 3];
                    pos++;
                }

                dst32 += rgb_width;
            }

            return (rgb_width) * height * 4;

        case TEXFORMAT_RGB24:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst32[x] = src[pos] << 24 | src[pos + mult] << 16 | src[pos + mult * 2] << 8 | 0xff;
                    pos++;
                }

                dst32 += rgb_width;
            }

            return (rgb_width) * height * 4;

        case TEXFORMAT_RGBA16:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst16[x] = src[pos] << 11 | src[pos + mult] << 6 | src[pos + mult * 2] << 1 | src[pos + mult * 3];
                    pos++;
                }

                dst16 += rgb_width;
            }

            return (rgb_width) * height * 2;

        case TEXFORMAT_IA16:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst16[x] = src[pos] << 8 | src[pos + mult];
                    pos++;
                }

                dst16 += rgb_width;
            }

            return (rgb_width) * height * 2;

        case TEXFORMAT_RGB15:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst16[x] = src[pos] << 11 | src[pos + mult] << 6 | src[pos + mult * 2] << 1 | 1;
                    pos++;
                }

                dst16 += rgb_width;
            }

            return (rgb_width) * height * 2;

        case TEXFORMAT_IA8:
            for (y = 0; y < height; y++)
            {
                if ((width + 7) & 0xff8);

                for (x = 0; x < width; x++)
                {
                    dst8[x] = src[pos] << 4 | src[pos + mult];
                    pos++;
                }

                dst8 += (width + 7) & 0xff8;
            }

            return ((width + 7) & 0xff8) * height;

        case TEXFORMAT_I8:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    dst8[x] = src[pos];
                    pos++;
                }

                dst8 += (width + 7) & 0xff8;
            }

            return ((width + 7) & 0xff8) * height;

        case TEXFORMAT_IA4:
            for (y = 0; y < height; y++)
            {
                if ((width + 15) & 0xff0);

                for (x = 0; x < width; x += 2)
                {
                    dst8[x >> 1] = src[pos] << 5 | src[pos + mult * 3] << 4 | src[pos + 1] << 1 | src[pos + mult * 3 + 1];
                    pos += 2;
                }

                if (width & 1) { pos--; }

                dst8 += (width + 15) & 0xff0;
            }

            return (((width + 15) & 0xff0) >> 1) * height;

        case TEXFORMAT_I4:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x += 2)
                {
                    dst8[x >> 1] = src[pos] << 4 | src[pos + 1];
                    pos += 2;
                }

                if (width & 1) { pos--; }

                dst8 += ((width + 15) & 0xff0) >> 1;
            }

            return (((width + 15) & 0xff0) >> 1) * height;
    }

    return 0;
}


/**
 * Inflate a texture using the provided lookup table.
 *
 * The lookup table is a bitstring of colours in the pixel format described by
 * the format argument. The number of colours in the lookup table is given by
 * the numcolours argument.
 *
 * The data in the global source bitstring is expected to be a tightly packed
 * list of indices into the lookup table. The number of bits for each index
 * is calculated based on the number of colours in the lookup table. For
 * example, if the lookup table contains 8 colours then the indices will be 0-7,
 * which requires 3 bits per index.
 *
 * Return the number of bytes written to dst.
 */
s32 texInflateLookup(s32 width, s32 height, u8 *dst, u8 *lookup, s32 numcolours, s32 format)
{
	u32 *lookup32 = (u32 *)lookup;
	u16 *lookup16 = (u16 *)lookup;
	u32 *dst32 = (u32 *)dst;
	u16 *dst16 = (u16 *)dst;
	u8 *dst8 = (u8 *)dst;
	s32 x;
	s32 y;
	s32 bitspercolour = texGetBitSize(numcolours);

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = lookup32[texReadBits(bitspercolour)];
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = lookup32[texReadBits(bitspercolour)] << 8;
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = lookup16[texReadBits(bitspercolour)];
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = lookup16[texReadBits(bitspercolour)] << 1 | 1;
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = lookup16[texReadBits(bitspercolour)];
			}

			dst8 += (width + 7) & 0xff8;
		}

		return ((width + 7) & 0xff8) * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = lookup16[texReadBits(bitspercolour)] << 4;

				if (x + 1 < width) {
					dst8[x >> 1] |= lookup[(texReadBits(bitspercolour) * 2) + 1];
				}
			}

			dst8 += ((width + 15) & 0xff0) >> 1;
		}

		return (((width + 15) & 0xff0) >> 1) * height;
	}

	return 0;
}


/**
 * Like texInflateLookup, but the indices are provided in the src argument
 * as u8s or u16s rather than read from the global bitstring as tightly packed
 * bits.
 *
 * Whether u8s or u16s are expected depends on whether the number of colours
 * in the lookup table. If there are more than 256 colours then it must use
 * u16s, otherwise it expects u8s.
 */

s32 texInflateLookupFromBuffer(u8 *src, s32 width, s32 height, u8 *dst, u8 *lookup, s32 numcolours, s32 format)
{
    s32 x;
    s32 y;
    u32 *lookup32;
    u16 *lookup16;
    u8 *src8;
    u16 *src16;
    u32 *dst32;
    u16 *dst16;
    u8 *dst8;
    u32 basic_and_val;

    lookup32 = (u32 *)lookup;
    lookup16 = (u16 *)lookup;

    basic_and_val = 0xffc;

    dst32 = (u32 *)dst;
    dst16 = (u16 *)dst;
    dst8 = (u8 *)dst;

    if (numcolours <= 256)
    {
        src8 = (u8 *)src;
    }
    else
    {
        src16 = (u16 *)src;
    }

    switch (format)
    {
        case TEXFORMAT_RGBA32:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst32[x] = lookup32[src8[x]];
                    }
                    else
                    {
                        dst32[x] = lookup32[src16[x]];
                    }
                }

                dst32 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 4;

        case TEXFORMAT_RGB24:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst32[x] = lookup32[src8[x]] << 8 | 0xff;
                    }
                    else
                    {
                        dst32[x] = lookup32[src16[x]] << 8 | 0xff;
                    }
                }

                dst32 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 4;

        case TEXFORMAT_RGBA16:
        case TEXFORMAT_IA16:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst16[x] = lookup16[src8[x]];
                    }
                    else
                    {
                        dst16[x] = lookup16[src16[x]];
                    }
                }

                dst16 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 2;

        case TEXFORMAT_RGB15:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst16[x] = lookup16[src8[x]] << 1 | 1;
                    }
                    else
                    {
                        dst16[x] = lookup16[src16[x]] << 1 | 1;
                    }
                }

                dst16 += (width + 3) & basic_and_val;
                src8 += width;
                src16 += width;
            }

            return ((width + 3) & basic_and_val) * height * 2;

        case TEXFORMAT_IA8:
        case TEXFORMAT_I8:
            for (y = 0; y < height; y++)
            {
                if ((width + 7) & 0xff8);

                for (x = 0; x < width; x++)
                {
                    if (numcolours <= 256)
                    {
                        dst8[x] = lookup16[src8[x]];
                    }
                    else
                    {
                        dst8[x] = lookup16[src16[x]];
                    }
                }

                dst8 += (width + 7) & 0xff8;
                src8 += width;
                src16 += width;
            }

            return ((width + 7) & 0xff8) * height;

        case TEXFORMAT_IA4:
        case TEXFORMAT_I4:
            for (y = 0; y < height; y++)
            {
                for (x = 0; x < width; x += 2)
                {
                    if (numcolours <= 256)
                    {
                        dst8[x >> 1] = lookup16[src8[x]] << 4 | lookup16[src8[x + 1]];
                    }
                    else
                    {
                        dst8[x >> 1] = lookup16[src16[x]] << 4 | lookup16[src16[x + 1]];
                    }
                }

                dst8 += ((width + 15) & 0xff0) >> 1;
                src8 += width;
                src16 += width;
            }

            return (((width + 15) & 0xff0) >> 1) * height;
    }

    return 0;
}


/**
 * For every second row, swap the bytes within that row.
 *
 * For textures with 32-bit colour values (in GBI format), swap every pair
 * within each word. For all other textures, swap every byte within each pair.
 */
void texSwapAltRowBytes(u8 *dst, s32 width, s32 height, s32 format)
{
	s32 x;
	s32 y;
	s32 alignedwidth;
	u32 *row = (u32 *)dst;
	s32 tmp;

	switch (format) {
	case TEXFORMAT_RGBA32:
	case TEXFORMAT_RGB24:
		alignedwidth = (width + 3) & 0xffc;
		break;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_RGB15:
	case TEXFORMAT_IA16:
		alignedwidth = ((width + 3) & 0xffc) >> 1;
		break;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
	case TEXFORMAT_RGBA16_CI8:
	case TEXFORMAT_IA16_CI8:
		alignedwidth = ((width + 7) & 0xff8) >> 2;
		break;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
	case TEXFORMAT_RGBA16_CI4:
	case TEXFORMAT_IA16_CI4:
		alignedwidth = ((width + 0xf) & 0xff0) >> 3;
		break;
	}

	row += alignedwidth;

	if (format == TEXFORMAT_RGBA32 || format == TEXFORMAT_RGB24) {
		for (y = 1; y < height; y += 2) {
			for (x = 0; x < alignedwidth; x += 4) {
				tmp = row[x + 0];
				row[x + 0] = row[x + 2];
				row[x + 2] = tmp;

				tmp = row[x + 1];
				row[x + 1] = row[x + 3];
				row[x + 3] = tmp;
			}

			row += alignedwidth * 2;
		}
	} else {
		for (y = 1; y < height; y += 2) {
			for (x = 0; x < alignedwidth; x += 2) {
				tmp = row[x + 0];
				row[x + 0] = row[x + 1];
				row[x + 1] = tmp;
			}

			row += alignedwidth * 2;
		}
	}
}






/**
 * Blur the pixels in the image with the surrounding pixels.
 */
void texBlur(u8 *pixels, s32 width, s32 height, s32 method, s32 chansize)
{
	s32 x;
	s32 y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			s32 cur = pixels[y * width + x] + chansize * 2;
			s32 left = x > 0 ? pixels[y * width + x - 1] : 0;
			s32 above = y > 0 ? pixels[(y - 1) * width + x] : 0;
			s32 aboveleft = x > 0 && y > 0 ? pixels[(y - 1) * width + x - 1] : 0;

			switch (method) {
			case 0:
				pixels[y * width + x] = (cur + left) % chansize;
				break;
			case 1:
				pixels[y * width + x] = (cur + above) % chansize;
				break;
			case 2:
				pixels[y * width + x] = (cur + aboveleft) % chansize;
				break;
			case 3:
				pixels[y * width + x] = (cur + (left + above - aboveleft)) % chansize;
				break;
			case 4:
				pixels[y * width + x] = (cur + ((above - aboveleft) / 2 + left)) % chansize;
				break;
			case 5:
				pixels[y * width + x] = (cur + ((left - aboveleft) / 2 + above)) % chansize;
				break;
			case 6:
				pixels[y * width + x] = (cur + ((left + above) / 2)) % chansize;
				break;
			}
		}
	}
}

u8* img_curpos; u32 img_curdatatable; s32 img_bitcount;
void texSetBitstring(s32 pos) {
    img_curpos = pos;
    img_curdatatable = 0;
    img_bitcount = 0;
}



u32 texReadBits(s32 bitCount)
{
    if (img_bitcount < bitCount)
    {
        do
        {
            img_curdatatable = (*img_curpos | (img_curdatatable << 8));
            img_curpos++;
            img_bitcount = img_bitcount + 8;
        } while (img_bitcount < bitCount);
    }
    
    img_bitcount -= bitCount;
    return (img_curdatatable >> img_bitcount) & ((1 << bitCount) - 1);
}


//this definately isn't proper way this data was represented, but works for now
// rodata
//D:8005BF80
const u8 rz_header_bytes[40] = {
    0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00,
    0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00, 0x11, 0x72, 0x00, 0x00
};

struct aaa {
    u8 unk00;
    u8 unk01;
    u8 unk02;
    u8 unk03;
    u8 unk04;
    u8 unk05;
    u8 unk06;
    u8 unk07;
    u8 unk08;
    u8 unk09;
    u8 unk0A;
    u8 unk0B;
    s32 unk0C;
    s32 unk10;
    s32 unk14;
    u8 unk18;
};


u32 decompressdata(u8 *src, u8 *dst, struct huft *huffman_table)
{
  s32 header_check;
  struct aaa *p = &rz_header_bytes;
  struct aaa *header_alias;
    
  header_check = 0;
  rz_inbuf = src;
  header_alias = &(*p);
  rz_outbuf = dst;
  rz_hlist = huffman_table;
    
  if (((header_check = src[0]) != (*header_alias).unk00) || (src[1] != (*p).unk05))
  {
    header_check = src[-1];
    if ((header_check == p->unk08) != 0)
    {
      header_check = 2;
    }
    if (src[-2] == ((*header_alias).unk18 & 0xFF))
    {
      header_check = 1;

      // Weird but needed for matching.
      if (((*header_alias).unk18) && ((*header_alias).unk18));
    }
  }
    
  if (header_check);
    
  rz_inbuf = rz_inbuf + 2;
  rz_wp = 0;
  rz_inptr = 0;
    
  zlib_inflate();

  return rz_wp;
}


s32 rzipGetSomething(void) {
    return (rz_inbuf + rz_inptr);
}





/* this file comes from gzip (1.2.4, 1993-08-20 release), but 
 * inflate.c placed in public domain by Mark Adler */

#define GETBYTE()   (rz_inbuf[rz_inptr++])
#define NEXTBYTE()  (u8)GETBYTE()
#define NEEDBITS(n) {while(k<(n)){b|=((u32)NEXTBYTE())<<k;k+=8;}}
#define DUMPBITS(n) {b>>=(n);k-=(n);}

#define WSIZE 0x8000U
#define BMAX 16
#define N_MAX 288

//.bss

//bss:8008D350
u8 *rz_inbuf;
//bss:8008D354
u8 *rz_outbuf;
//bss:8008D358
u32 rz_inptr;
//bss:8008D35C
u32 rz_wp;
//bss:8008D360
struct huft *rz_hlist;
//bss:8008D364
u32 rz_bb;
//bss:8008D368
u32 rz_bk;
//bss:8008D36C
u32 rz_hufts;


//.data
u8 rz_border[0x14] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

u16 rz_cplens[0x20] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};

u8 rz_cplext[0x20] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99};

u16 rz_cpdist[0x1E] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};

u8 rz_cpdext[0x20] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
        12, 12, 13, 13};

u16 rz_mask_bits[] = {
    0, 1, 3, 7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF, 0
};

s32 rz_lbits = 9;
s32 rz_dbits = 6;

//.rodata


/* Given a list of code lengths and a maximum table size, make a set of
   tables to decode that set of codes.  Return zero on success, one if
   the given code set is incomplete (the tables are still built in this
   case), two if the input is invalid (all zero length codes or an
   oversubscribed set of lengths), and three if not enough memory. */
s32 zlib_huft_build(u32 *b, u32 n, u32 s, u16 *d, u8 *e, struct huft **t, s32 *m)
{
	u32 a;                   /* counter for codes of length k */
	u32 c[BMAX+1];           /* bit length count table */
	u32 f;                   /* i repeats in table every f entries */
	s32 g;                   /* maximum code length */
	s32 h;                   /* table level */
	register u32 i;          /* counter, current code */
	register u32 j;          /* counter */
	register s32 k;          /* number of bits in current code */
	s32 l;                   /* bits per table (returned in m) */
	register u32 *p;         /* pointer into c[], b[], or v[] */
	register struct huft *q; /* points to current table */
	struct huft r;           /* table entry for structure assignment */
	struct huft *u[BMAX];    /* table stack */
	u32 v[N_MAX];            /* values in order of bit length */
	register s32 w;          /* bits before this table == (l * h) */
	u32 x[BMAX+1];           /* bit offsets, then code stack */
	u32 *xp;                 /* pointer into x */
	s32 y;                   /* number of dummy codes added */
	u32 z;                   /* number of entries in current table */

	/* Generate counts for each bit length */
    bzero(c, sizeof(c));

	p = b;
	i = n;

	do {
		c[*p]++;                  /* assume all entries <= BMAX */
		p++;                      /* Can't combine with above line (Solaris bug) */
	} while (--i);

	if (c[0] == n) {              /* null input--all zero length codes */
		*t = NULL;
		*m = 0;
		return 0;
	}

	/* Find minimum and maximum length, bound *m by those */
	l = *m;

	for (j = 1; j <= BMAX; j++) {
		if (c[j]) {
			break;
		}
	}

	k = j;                        /* minimum code length */

	if (l < j) {
		l = j;
	}

	for (i = BMAX; i; i--) {
		if (c[i]) {
			break;
		}
	}

	g = i;                        /* maximum code length */

	if (l > i) {
		l = i;
	}

	*m = l;

	/* Adjust last length count to fill out codes, if needed */
	for (y = 1 << j; j < i; j++, y <<= 1) {
		y -= c[j];
	}

	y -= c[i];
	c[i] += y;

	/* Generate starting offsets into the value table for each length */
	x[1] = j = 0;
	p = c + 1;
	xp = x + 2;

	while (--i) {                 /* note that i == g from above */
		*xp++ = (j += *p++);
	}

	/* Make a table of values in order of bit lengths */
	p = b;
	i = 0;

	do {
		if ((j = *p++) != 0) {
			v[x[j]++] = i;
		}
	} while (++i < n);

	/* Generate the Huffman codes and for each, make the table entries */
	x[0] = i = 0;                 /* first Huffman code is zero */
	p = v;                        /* grab values in bit order */
	h = -1;                       /* no tables yet--level -1 */
	w = -l;                       /* bits decoded == (l * h) */
	u[0] = (struct huft *)NULL;   /* just to keep compilers happy */
	q = (struct huft *)NULL;      /* ditto */
	z = 0;                        /* ditto */

	/* go through the bit lengths (k already is bits in shortest code) */
	for (; k <= g; k++) {
		a = c[k];

		while (a--) {
			/* here i is the Huffman code of length k bits for value *p */
			/* make tables up to required level */
			while (k > w + l) {
				h++;
				w += l;                 /* previous table always l bits */

				/* compute minimum size table less than or equal to l bits */
				z = (z = g - w) > l ? l : z;  /* upper limit on table size */

				if ((f = 1 << (j = k - w)) > a + 1) {   /* try a k-w bit table */
				                          /* too few codes for k-w bit table */
					f -= a + 1;           /* deduct codes from patterns left */
					xp = c + k;

					while (++j < z) {     /* try smaller tables up to z bits */
						if ((f <<= 1) <= *++xp) {
							break;            /* enough codes to use up j bits */
						}

						f -= *xp;           /* else deduct codes from patterns */
					}
				}

				z = 1 << j;             /* table entries for j-bit table */

				/* allocate and link in new table */
				q = &rz_hlist[rz_hufts];

				rz_hufts += z + 1;         /* track memory usage */
				*t = q + 1;             /* link to list for huft_free() */
				*(t = &(q->v.t)) = (struct huft *)NULL;
				u[h] = ++q;             /* table starts after link */

				/* connect to last table, if there is one */
				if (h) {
					x[h] = i;             /* save pattern for backing up */
					r.b = l;              /* bits to dump before this table */
					r.e = 16 + j;         /* bits in this table */
					r.v.t = q;            /* pointer to this table */
					j = i >> (w - l);     /* (get around Turbo C bug) */
					u[h-1][j] = r;        /* connect to last table */
				}
			}

			/* set up table entry in r */
			r.b = (k - w);

			if (p >= v + n) {
				r.e = 99;               /* out of values--invalid code */
			} else if (*p < s) {
				r.e = (*p < 256 ? 16 : 15);    /* 256 is end-of-block code */
				r.v.n = *p;             /* simple code is just the value */
				p++;                    /* one compiler does not like *p++ */
			} else {
				r.e = e[*p - s];   /* non-simple--look up in lists */
				r.v.n = d[*p++ - s];
			}

			/* fill code-like entries with r */
			f = 1 << (k - w);

			for (j = i >> w; j < z; j += f) {
				q[j] = r;
			}

			/* backwards increment the k-bit code i */
			for (j = 1 << (k - 1); i & j; j >>= 1) {
				i ^= j;
			}

			i ^= j;

			/* backup over finished tables */
			while ((i & ((1 << w) - 1)) != x[h]) {
				h--;                    /* don't need to update q */
				w -= l;
			}
		}
	}

	/* Return true (1) if we were given an incomplete table */
	return y != 0 && g != 1;
}




s32 zlib_inflate_codes(struct huft *tl, struct huft *td, s32 bl, s32 bd)
{
    register u32 e;    /* table entry flag/number of extra bits */
    u32 n;          /* length and index for copy */
    u32 d;
    u32 w;             /* current window position */
    struct huft *t;    /* pointer to table entry */
    u32 ml;        /* masks for bl and bd bits */
    u32 md;
    s32 padding;
    register u32 k;    /* number of bits in bit buffer */
    register u32 b;    /* bit buffer */

    /* make local copies of globals */
    b = rz_bb;                       /* initialize bit buffer */
    k = rz_bk;
    w = rz_wp;                       /* initialize window position */

    /* inflate the coded data */
    ml = rz_mask_bits[bl];
    md = rz_mask_bits[bd];         /* precompute masks for speed */
    
    for (;;)                      /* do until end of block */
    {
        NEEDBITS((u32)bl)

        if ((e = (t = tl + ((u32)b & ml))->e) > 16)
        {
            do
            {
                DUMPBITS(t->b)
                e -= 16;
                NEEDBITS(e)

            } while ((e = (t = t->v.t + ((u32)b & rz_mask_bits[e]))->e) > 16);
        }

        DUMPBITS(t->b)

        if (e == 16)                /* then it's a literal */
        {
            if ((u32)(s32)&rz_outbuf[w] >= (u32)(s32)&rz_inbuf[rz_inptr])
            {
                if ((u32)((s32)&rz_outbuf[w] - (s32)&rz_inbuf[rz_inptr]) < WSIZE)
                {
                    while(1){}              
                }
            }
            
            rz_outbuf[w++] = (u8)t->v.n;
        }
        else                        /* it's an EOB or a length */
        {
            /* exit if end of block */
            if (e == 15)
            {
                break;
            }

            NEEDBITS(e)
            /* get length of block to copy */
            n = t->v.n + ((u32)b & rz_mask_bits[e]);
            DUMPBITS(e)

            /* decode distance of block to copy */
            NEEDBITS((u32)bd)

            if ((e = (t = td + ((u32)b & md))->e) > 16)
            {
                do
                {
                    DUMPBITS(t->b)
                    e -= 16;
                    NEEDBITS(e)

                } while ((e = (t = t->v.t + ((u32)b & rz_mask_bits[e]))->e) > 16);
            }

            DUMPBITS(t->b)
            NEEDBITS(e)
            d = w - t->v.n - ((u32)b & rz_mask_bits[e]);
            DUMPBITS(e)

            /* do the copy */
            do
            {
                e = n;
				n = 0;
                
                if (w - d >= e)         /* (this test assumes unsigned comparison) */
                {
                    if ((u32)(s32)&rz_outbuf[w+e-1] >= (u32)(s32)&rz_inbuf[rz_inptr])
                    {
                        if ((u32)((s32)&rz_outbuf[w+e-1] - (s32)&rz_inbuf[rz_inptr]) < WSIZE)
                        {
                            while(1){}              
                        }
                    }

                    memcpy(&rz_outbuf[w], &rz_outbuf[d], e);
                    w += e;
                    d += e;
                }
                else                      /* do it slow to avoid memcpy() overlap */
                {
                    do
                    {
                        if ((u32)(s32)&rz_outbuf[w] >= (u32)(s32)&rz_inbuf[rz_inptr])
                        {
                            if ((u32)((s32)&rz_outbuf[w] - (s32)&rz_inbuf[rz_inptr]) < WSIZE)
                            {
                                while(1){}              
                            }
                        }

                        rz_outbuf[w++] = rz_outbuf[d++];
                    } while (--e);
                }
            } while (n);
        }
    }

    /* restore the globals from the locals */
    rz_wp = w;                       /* restore global window pointer */
    rz_bb = b;                       /* restore global bit buffer */
    rz_bk = k;

    /* done */
    return 0;
}





s32 zlib_inflate_stored(void)
{
	u32 n;           /* number of bytes in block */
	u32 w;           /* current window position */
	register u32 b;  /* bit buffer */
	register u32 k;  /* number of bits in bit buffer */
    
	/* make local copies of globals */
	b = rz_bb;                       /* initialize bit buffer */
	k = rz_bk;
	w = rz_wp;                       /* initialize window position */

	/* go to byte boundary */
	n = k & 7;
	DUMPBITS(n);

	/* get the length and its complement */
	NEEDBITS(16)
	n = (b & 0xffff);
	DUMPBITS(16)

	NEEDBITS(16)
    if (n != (unsigned)((~b) & 0xffff))
    {
        // removed
    }
	DUMPBITS(16)
       
	/* read and output the compressed data */
	while (n--)
    {
		NEEDBITS(8)
        
        if ((u32)(s32)&rz_outbuf[w] >= (u32)(s32)&rz_inbuf[rz_inptr])
        {
            if ((u32)((s32)&rz_outbuf[w] - (s32)&rz_inbuf[rz_inptr]) < WSIZE)
            {
                while(1){}              
            }
        }
        
        rz_outbuf[w++] = (u8)b;
        
        DUMPBITS(8)
	}

	/* restore the globals from the locals */
	rz_wp = w;                       /* restore global window pointer */
	rz_bb = b;                       /* restore global bit buffer */
	rz_bk = k;

	return 0;
}




s32 zlib_inflate_fixed(void)
{
	s32 i;                /* temporary variable */
	struct huft *tl;      /* literal/length code table */
	struct huft *td;      /* distance code table */
	s32 bl;               /* lookup bits for tl */
	s32 bd;               /* lookup bits for td */
	u32 l[288];           /* length list for zlib_huft_build */

	/* set up literal table */
	for (i = 0; i < 144; i++) {
		l[i] = 8;
	}
	for (; i < 256; i++) {
		l[i] = 9;
	}
	for (; i < 280; i++) {
		l[i] = 7;
	}
	for (; i < 288; i++) {
		l[i] = 8;
	}

	bl = 7;

	zlib_huft_build(l, 288, 257, rz_cplens, rz_cplext, &tl, &bl);

	/* set up distance table */
	for (i = 0; i < 30; i++) {
		l[i] = 5;
	}

	bd = 5;

	/* decompress until an end-of-block code */
	zlib_huft_build(l, 30, 0, rz_cpdist, rz_cpdext, &td, &bd);

	zlib_inflate_codes(tl, td, bl, bd);

	return 0;
}


s32 zlib_inflate_dynamic(void)
{
	s32 i;                /* temporary variables */
	u32 j;
	u32 l;           /* last length */
	u32 m;           /* mask for bit lengths table */
	u32 n;           /* number of lengths to get */
	struct huft *tl;      /* literal/length code table */
	struct huft *td;      /* distance code table */
	s32 bl;               /* lookup bits for tl */
	s32 bd;               /* lookup bits for td */
	u32 nb;          /* number of bit length codes */
	u32 nl;          /* number of literal/length codes */
	u32 nd;          /* number of distance codes */
	register u32 k;  /* number of bits in bit buffer */
	register u32 b;  /* bit buffer */
	u32 ll[286+30];  /* literal/length and distance code lengths */

	/* make local bit buffer */
	b = rz_bb;
	k = rz_bk;

	/* read in table lengths */
	NEEDBITS(5)
	nl = 257 + (b & 0x1f);      /* number of literal/length codes */
	DUMPBITS(5)
	NEEDBITS(5)
	nd = 1 + (b & 0x1f);        /* number of distance codes */
	DUMPBITS(5)
	NEEDBITS(4)
	nb = 4 + (b & 0xf);         /* number of bit length codes */
	DUMPBITS(4)

	/* read in bit-length-code lengths */
	for (j = 0; j < nb; j++)
    {
		NEEDBITS(3)
		ll[rz_border[j]] = b & 7;
		DUMPBITS(3)
	}

	for (; j < 19; j++)
    {
		ll[rz_border[j]] = 0;
	}

	/* build decoding table for trees--single level, 7 bit lookup */
	bl = 7;

	zlib_huft_build(ll, 19, 19, NULL, NULL, &tl, &bl);

	/* read in literal and distance code lengths */
	n = nl + nd;
	m = rz_mask_bits[bl];
	i = l = 0;

	while (i < n) {
		NEEDBITS(bl)
		j = (td = tl + (b & m))->b;
		DUMPBITS(j)

		j = td->v.n;

		if (j < 16) {                 /* length of code in bits (0..15) */
			ll[i++] = l = j;          /* save last length in l */
		} else if (j == 16) {         /* repeat last length 3 to 6 times */
			NEEDBITS(2)
			j = 3 + (b & 3);
			DUMPBITS(2)

			while (j--) {
				ll[i++] = l;
			}
		} else if (j == 17) {         /* 3 to 10 zero length codes */
			NEEDBITS(3)
			j = 3 + (b & 7);
			DUMPBITS(3)

			while (j--) {
				ll[i++] = 0;
			}

			l = 0;
		} else {                      /* j == 18: 11 to 138 zero length codes */
			NEEDBITS(7)
			j = 11 + (b & 0x7f);
			DUMPBITS(7)

			while (j--) {
				ll[i++] = 0;
			}

			l = 0;
		}
	}

	/* restore the global bit buffer */
	rz_bb = b;
	rz_bk = k;

	/* build the decoding tables for literal/length and distance codes */
	bl = rz_lbits;

	zlib_huft_build(ll, nl, 257, rz_cplens, rz_cplext, &tl, &bl);

	bd = rz_dbits;

	zlib_huft_build(ll + nl, nd, 0, rz_cpdist, rz_cpdext, &td, &bd);

	/* decompress until an end-of-block code */
	zlib_inflate_codes(tl, td, bl, bd);

	return 0;
}


s32 zlib_inflate_block(s32 *e)
{
	u32 t;                   /* block type */
	register u32 b = rz_bb;  /* bit buffer */
	register u32 k = rz_bk;  /* number of bits in bit buffer */

	/* make local bit buffer */
	b = rz_bb;
	k = rz_bk;

	/* read in last block bit */
	NEEDBITS(1)
	*e = b & 1;
	DUMPBITS(1)

	/* read in last block type */
	NEEDBITS(2)
	t = b & 3;
	DUMPBITS(2)

	/* restore the global bit buffer */
	rz_bb = b;
	rz_bk = k;

	/* inflate that block type */
	if (t == 2)
    {
		return zlib_inflate_dynamic();
	}

	if (t == 0)
    {
		return zlib_inflate_stored();
	}

	if (t == 1)
    {
		return zlib_inflate_fixed();
	}

	/* bad block type */
	return 2;
}



int zlib_inflate(void)
{
    /* last block flag */
    int e;

    /* result code */
    int r;

    /* maximum struct huft's malloc'ed */
    unsigned h;

    /* initialize window, bit buffer */
    rz_wp = 0;
    rz_bk = 0;
    rz_bb = 0;

    /* decompress until the last block */
    h = 0;

    do
    {
        rz_hufts = 0;
        
        if ((r = zlib_inflate_block(&e)) != 0)
        {
            return r;
        }
        
        if (rz_hufts > h)
        {
            h = rz_hufts;
        }
    } while (!e);

    /* Undo too much lookahead. The next read will be byte aligned so we
    * can discard unused bits in the last meaningful byte.
    */
    while (rz_bk >= 8)
    {
        rz_bk -= 8;
        rz_inptr--;
    }

    return 0;
}
