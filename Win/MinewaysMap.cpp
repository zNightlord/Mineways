/*
Copyright (c) 2010, Sean Kasun
Parts Copyright (c) 2010, Ryan Hitchman
All rights reserved.
Modified by Eric Haines, copyright (c) 2011.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/


// MinewaysMap.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "biomes.h"
#include <assert.h>
#include <string.h>

static unsigned char* draw(WorldGuide* pWorldGuide, int bx, int bz, int topy, int mapMaxY, Options* pOpts,
    ProgressCallback callback, float percent, int* hitsFound, int mcVersion, int versionID, int& retCode);
static void blit(unsigned char* block, unsigned char* bits, int px, int py,
    double zoom, int w, int h);
static int createBlockFromSchematic(WorldGuide* pWorldGuide, int cx, int cz, WorldBlock* block);
static void initColors();


static int gColorsInited = 0;
static unsigned int gBlockColors[NUM_BLOCKS_DEFINED * 16];
static unsigned char gEmptyR, gEmptyG, gEmptyB;
static unsigned char gBlankTile[16 * 16 * 4];
static unsigned char gBlankHighlitTile[16 * 16 * 4];
static unsigned char gBlankTransitionTile[16 * 16 * 4];

static unsigned short gColormap = 0;
static long long gMapSeed;

static int gBoxHighlightUsed = 0;
static int gBoxMinX;
static int gBoxMinY;
static int gBoxMinZ;
static int gBoxMaxX;
static int gBoxMaxY;
static int gBoxMaxZ;
static int gDirtyBoxMinX = INT_MAX;
static int gDirtyBoxMinZ = INT_MAX;
static int gDirtyBoxMaxX = INT_MIN;
static int gDirtyBoxMaxZ = INT_MIN;

// highlight blend factor and color
static double gHalpha = 0.3;
static double gHalphaBorder = 0.8;
static int gHred = 205;
static int gHgreen = 50;
static int gHblue = 255;
static int gHighlightID = 0;

// was an unknown block read in?
static int gUnknownBlock = 0;
static int gPerformUnknownBlockCheck = 1;

static wchar_t gSeparator[3];

void SetSeparatorMap(const wchar_t* separator)
{
    wcscpy_s(gSeparator, 3, separator);
}

void SetHighlightState(int on, int minx, int miny, int minz, int maxx, int maxy, int maxz, int mapMinHeight, int mapMaxHeight)
{
    // we don't really require one to be min or max, we take the range
    if (minx > maxx) swapint(minx, maxx);
    if (miny > maxy) swapint(miny, maxy);
    if (minz > maxz) swapint(minz, maxz);

    // clean up by clamping
    miny = clamp(miny, mapMinHeight, mapMaxHeight);
    maxy = clamp(maxy, mapMinHeight, mapMaxHeight);

    // now convert into map Y space. The map Y values are all from 0 on up, -64 is not used. But Mineways.cpp and ObjFileManip.cpp do use these
    // values in world space. We adjust at this point to go from -64 to 319 space, to 0 to 383 space
    miny -= mapMinHeight;
    maxy -= mapMinHeight;

    // has highlight state changed?
    if (gBoxHighlightUsed != on ||
        gBoxMinX != minx ||
        gBoxMinY != miny ||
        gBoxMinZ != minz ||
        gBoxMaxX != maxx ||
        gBoxMaxY != maxy ||
        gBoxMaxZ != maxz)
    {
        // state has changed, so invalidate rendering caches by changing highlight ID
        gHighlightID++;
        gBoxHighlightUsed = on;
        gBoxMinX = minx;
        gBoxMinY = miny;
        gBoxMinZ = minz;
        gBoxMaxX = maxx;
        gBoxMaxY = maxy;
        gBoxMaxZ = maxz;
        if (on)
        {
            // increase dirty rectangle by new bounds
            if (gDirtyBoxMinX > minx)
                gDirtyBoxMinX = minx;
            if (gDirtyBoxMinZ > minz)
                gDirtyBoxMinZ = minz;
            if (gDirtyBoxMaxX < maxx)
                gDirtyBoxMaxX = maxx;
            if (gDirtyBoxMaxZ < maxz)
                gDirtyBoxMaxZ = maxz;
        }
    }
}


//static long long randomSeed;
//static void javaRandomSetSeed(long long seed){
//  randomSeed = (seed ^ 0x5DEECE66DL) & ((1LL << 48) - 1);
//}

//static long long javaRandomNext(int bits) {
//  long long r = randomSeed;
//  r = (r * 0x5DEECE66DL + 0xBL) & ((1LL << 48) - 1);
//  return (long long)(r >> (48 - bits));
//}
//static int javaRandomNextInt(int n) {
//    long long bits,val;
//   if ((n & -n) == n)  // i.e., n is a power of 2
//       return (int)((n * (long long)javaRandomNext(31)) >> 31);
//   do {
//       bits = javaRandomNext(31);
//       val = bits % n;
//   } while(bits - val + (n-1) < 0);
//   return (int)val;
//}

//static long long getChunkSeed(int xPosition, int zPosition){
//    return (gMapSeed + (long long) (xPosition * xPosition * 0x4c1906) + (long long) (xPosition * 0x5ac0db) +
//             (long long) (zPosition * zPosition) * 0x4307a7L + (long long) (zPosition * 0x5f24f)) ^ 0x3ad8025f;
//}

//static int isSlimeChunk(int x, int z){
//    long long nextSeed = getChunkSeed(x, z);
//    javaRandomSetSeed(nextSeed);
//    return javaRandomNextInt(10)==0;
//}

void GetHighlightState(int* on, int* minx, int* miny, int* minz, int* maxx, int* maxy, int* maxz, int mapMinHeight)
{
    // inside MinewaysMap, we go from 0 to 383, not -64 to 319. Conversion is done here.
    *on = gBoxHighlightUsed;
    *minx = gBoxMinX;
    *miny = gBoxMinY + mapMinHeight;
    *minz = gBoxMinZ;
    *maxx = gBoxMaxX;
    *maxy = gBoxMaxY + mapMinHeight;
    *maxz = gBoxMaxZ;
}

//world = path to world saves
//cx = center x world
//cz = center z world
//y = start depth
//w = output width
//h = output height
//zoom = zoom amount (1.0 = 100%)
//bits = byte array for output
//opts = bitmasks of render options (see MinewaysMap.h)
int DrawMap(WorldGuide* pWorldGuide, double cx, double cz, int topy, int mapMaxY, int w, int h, double zoom, unsigned char* bits, Options* pOpts, int* hitsFound, ProgressCallback callback, int mcVersion, int versionID)
{
    /* We're converting between coordinate systems:
    *
    * X     -world z N  -screen y
    * screen origin  |
    *                |
    *                |
    *                |
    *  -world x      |(cx,cz)   +world x
    * W--------------+----------------E
    *  -screen x     |          +screen x
    *                |
    *                |
    *                |
    *      +world z  | +screen y
    *                S
    */

    unsigned char* blockbits;
    int z, x, px, py;
    int blockScale = (int)(16 * zoom);

    // number of blocks to fill the screen (plus 2 blocks for floating point inaccuracy)
    int hBlocks = (w + blockScale * 2) / blockScale;
    int vBlocks = (h + blockScale * 2) / blockScale;

    // cx/cz is the center, so find the upper left corner from that
    double startx = cx - (double)w / (2 * zoom);
    double startz = cz - (double)h / (2 * zoom);
    int startxblock = (int)(startx / 16);
    int startzblock = (int)(startz / 16);
    int shiftx = (int)((startx - startxblock * 16) * zoom);
    int shifty = (int)((startz - startzblock * 16) * zoom);

    int sumRetCode = 0;
    int retCode;

    if (shiftx < 0)
    {
        // essentially the floor function
        startxblock--;
        shiftx += blockScale;
    }
    if (shifty < 0)
    {
        // essentially the floor function
        startzblock--;
        shifty += blockScale;
    }

    if (!gColorsInited)
        initColors();

    // x increases south, decreases north
    for (z = 0, py = -shifty; z <= vBlocks; z++, py += blockScale)
    {
        // z increases west, decreases east
        for (x = 0, px = -shiftx; x <= hBlocks; x++, px += blockScale)
        {
            blockbits = draw(pWorldGuide, startxblock + x, startzblock + z, topy, mapMaxY, pOpts, callback, (float)(z * hBlocks + x) / (float)(vBlocks * hBlocks), hitsFound, mcVersion, versionID, retCode);
            if (retCode < 0) {
                // preserve the error code, which will (mysteriously) be displayed
                sumRetCode = retCode;
            }
            else if (sumRetCode >= 0)
            {
                // warnings can chained together
                sumRetCode |= retCode;
            } // else sumRetCode has an error code, so don't touch it
            blit(blockbits, bits, px, py, zoom, w, h);
        }
    }
    // clear dirty rectangle, if any
    if (gBoxHighlightUsed)
    {
        // box is set to current rectangle
        // TODO: this isn't quite right, as if you select a large rect, scroll it offscreen
        // then select new and scroll back, you'll see the highlight.
        gDirtyBoxMinX = gBoxMinX;
        gDirtyBoxMinZ = gBoxMinZ;
        gDirtyBoxMaxX = gBoxMaxX;
        gDirtyBoxMaxZ = gBoxMaxZ;
    }
    else
    {
        // empty
        gDirtyBoxMinX = gDirtyBoxMinZ = INT_MAX;
        gDirtyBoxMaxX = gDirtyBoxMaxZ = INT_MIN;
    }

    return sumRetCode;
}

//image = 3 bytes per pixel, w*h*zoom^2
//cx = upper left x world
//cz = upper left z world
//topy = height maximum
//w = output width
//h = output height
//zoom = zoom amount (1.0 = 100%, 1 texel per pixel)
//bits = byte array for output
//opts = bitmasks of render options (see MinewaysMap.h)
int DrawMapToArray(unsigned char* image, WorldGuide* pWorldGuide, int cx, int cz, int topy, int mapMaxY, int w, int h, int zoom, Options* pOpts, int* hitsFound, ProgressCallback callback, int mcVersion, int versionID)
{
    unsigned char* blockbits;
    int z, x, px, pz;
    int chunkSize = 16;
    int sumRetCode = 0;
    int retCode;

    assert(zoom >= 1);

    // number of blocks to fill the screen, rounded up to nearest 16
    int hBlocks = (int)floor((float)(cx + w - 1) / 16.0) - (int)floor((float)cx / 16.0) + 1;
    int vBlocks = (int)floor((float)(cz + h - 1) / 16.0) - (int)floor((float)cz / 16.0) + 1;

    int startxblock = (int)(cx / chunkSize);
    int startzblock = (int)(cz / chunkSize);
    int shiftx = cx - startxblock * chunkSize;
    int shifty = cz - startzblock * chunkSize;

    if (shiftx < 0)
    {
        // essentially the floor function
        startxblock--;
        shiftx += chunkSize;
    }
    if (shifty < 0)
    {
        // essentially the floor function
        startzblock--;
        shifty += chunkSize;
    }

    if (!gColorsInited)
        initColors();

    // x increases south, decreases north
    int iblockxstart, b2ix, iblockxend, iblockzstart, b2iz, iblockzend;
    for (z = 0, pz = -shifty; z < vBlocks; z++, pz += chunkSize)
    {
        int wblockzmin = (startzblock + z) * chunkSize;
        int wblockzmax = wblockzmin + chunkSize;

        if (wblockzmin < cz) {
            // beginning of column
            b2iz = wblockzmin - cz;
            iblockzstart = -b2iz;
        }
        else {
            b2iz = ((int)(wblockzmin / chunkSize)) * chunkSize - cz;
            iblockzstart = wblockzmin % chunkSize;
        }
        if (wblockzmax > cz + h) {
            // end of row, number from 1 to 16
            iblockzend = cz + h - wblockzmax + chunkSize;
        }
        else {
            iblockzend = chunkSize;
        }
        assert(iblockzstart < iblockzend);
        assert(iblockzstart >= 0 && iblockzstart < chunkSize);
        assert(iblockzend > 0 && iblockzend <= chunkSize);

        int nextLine = zoom * w * 3;

        // z increases west, decreases east
        for (x = 0, px = -shiftx; x < hBlocks; x++, px += chunkSize)
        {
            blockbits = draw(pWorldGuide, startxblock + x, startzblock + z, topy, mapMaxY, pOpts, callback, (float)(z * hBlocks + x) / (float)(vBlocks * hBlocks), hitsFound, mcVersion, versionID, retCode);
            sumRetCode |= retCode;

            // world space of block:
            int wblockxmin = (startxblock + x) * chunkSize;
            int wblockxmax = wblockxmin + chunkSize;

            if (wblockxmin < cx) {
                // beginning of row, number from 0 to 15
                b2ix = wblockxmin - cx;
                iblockxstart = -b2ix;
            }
            else {
                b2ix = ((int)(wblockxmin / chunkSize)) * chunkSize - cx;
                iblockxstart = wblockxmin % chunkSize;
            }
            if (wblockxmax > cx + w) {
                // end of row, number from 1 to 16
                iblockxend = cx + w - wblockxmax + chunkSize;
            }
            else {
                iblockxend = chunkSize;
            }

            // now walk through these, grabbing from the bits
            assert(iblockxstart < iblockxend);
            assert(iblockxstart >= 0 && iblockxstart < chunkSize);
            assert(iblockxend > 0 && iblockxend <= chunkSize);

            // copy over the data
            for (int iz = iblockzstart; iz < iblockzend; iz++) {
                unsigned char* curImg = &image[((iz + b2iz) * zoom * w + (iblockxstart + b2ix)) * zoom * 3];
                unsigned char* curBits = &blockbits[(iz * chunkSize + iblockxstart) * 4];
                for (int ix = iblockxstart; ix < iblockxend; ix++) {
                    // make sure in range
                    //assert(((iz + b2iz) * w + (ix + b2ix)) >= 0 && ((iz + b2iz) * w + (ix + b2ix)) <= w * h);
                    if (zoom == 1) {
                        *curImg++ = *curBits++;
                        *curImg++ = *curBits++;
                        *curImg++ = *curBits++;
                        curBits++;
                    }
                    else {
                        // loop and fill in image
                        unsigned char r = *curBits++;
                        unsigned char g = *curBits++;
                        unsigned char b = *curBits++;
                        curBits++;
                        unsigned char* curImgLine = curImg;
                        for (int imgz = 0; imgz < zoom; imgz++) {
                            unsigned char* curImgLoc = curImgLine;
                            for (int imgx = 0; imgx < zoom; imgx++) {
                                *curImgLoc++ = r;
                                *curImgLoc++ = g;
                                *curImgLoc++ = b;
                                //assert(curImgLoc - image <= zoom * zoom * w * h * 3);
                            }
                            curImgLine += nextLine;
                        }
                        // next pixel start location in line
                        curImg += zoom * 3;
                    }
                }
            }
        }
    }
    return sumRetCode;
}

//bx = x coord of pixel
//by = y coord of pixel
//cx = center x world
//cz = center z world
//w = output width
//h = output height
//zoom = zoom amount (1.0 = 100%)
//ox = world x at mouse
//oz = world z at mouse
//type is block type
//biome is biome found
const char* IDBlock(int bx, int by, double cx, double cz, int w, int h, int yOffset, double zoom, int* ox, int* oy, int* oz, int* type, int* dataVal, int* biome, bool schematic)
{
    //WARNING: keep this code in sync with draw()
    WorldBlock* block;
    int x, y, z, px, py, xoff, zoff;
    int blockScale = (int)(16 * zoom);

    // cx/cz is the center, so find the upper left corner from that
    double startx = cx - (double)w / (2 * zoom);
    double startz = cz - (double)h / (2 * zoom);
    // TODO: I suspect these want to be floors, not ints; int
    // rounds towards 0, floor takes -4.5 and goes to -5.
    int startxblock = (int)(startx / 16);
    int startzblock = (int)(startz / 16);
    int shiftx = (int)((startx - startxblock * 16) * zoom);
    int shifty = (int)((startz - startzblock * 16) * zoom);
    // someone could be more than 10000 blocks from spawn, so don't assert
    //assert(cz < 10000);
    //assert(cz > -10000);

    // initialize to "not set"
    *dataVal = 0;

    if (shiftx < 0)
    {
        startxblock--;
        shiftx += blockScale;
    }
    if (shifty < 0)
    {
        startzblock--;
        shifty += blockScale;
    }

    // Adjust bx and by so they can be negative.
    // Note that things are a bit weird with numbers here.
    // I check if the mouse location is unreasonably high, which means
    // that it's meant to be a negative number instead.
    if (bx > 0x7000)
        bx -= 0x8000;
    if (by > 0x7000)
        by -= 0x8000;

    // if off window above
    // Sean's fix, but makes the screen go empty if I scroll off top of window
    //if (by<0) return "";

    x = (bx + shiftx) / blockScale;
    px = x * blockScale - shiftx;
    z = (by + shifty) / blockScale;
    py = z * blockScale - shifty;

    xoff = (int)((bx - px) / zoom);
    zoff = (int)((by - py) / zoom);

    *ox = (startxblock + x) * 16 + xoff;
    *oz = (startzblock + z) * 16 + zoff;

    block = (WorldBlock*)Cache_Find(startxblock + x, startzblock + z);

    if (block == NULL)
    {
        *oy = EMPTY_HEIGHT;
        *type = BLOCK_UNKNOWN;
        return "Unknown";
    }

    y = block->heightmap[xoff + zoff * 16];
    *oy = y + yOffset;
    *biome = block->biome[xoff + zoff * 16];

    // Note that when "hide obscured" is on, blocks can be empty because
    // they were solid from the current level on down.
    if (y == EMPTY_HEIGHT)
    {
        *oy = EMPTY_HEIGHT;
        if (schematic) {
            // act like the pixel is not there, vs. a block that has an empty location
            *biome = -1;
            *type = BLOCK_UNKNOWN;
            return "Unknown";
        }
        else {
            *type = BLOCK_AIR;
            return "Empty";  // nothing was rendered here
        }
    }

    // there's a bug in the original code, sometimes xoff is negative.
    // For now, assert when I see it, and return empty - better than crashing.
    // TODO - can this still happen?
    //assert( y+(zoff+xoff*16)*128 >= 0 );
    // 65536 = 256 * 16 * 16
    if (y * 256 + zoff * 16 + xoff < 0 || y * 256 + zoff * 16 + xoff >= 256 * block->maxHeight) {
        *type = BLOCK_AIR;
        *biome = -1;
        return "(off map)";
    }

    *type = block->grid[xoff + zoff * 16 + y * 256];
    *dataVal = block->data[xoff + zoff * 16 + y * 256];

    // 1.13+ fun: move topmost dataVal value to type - note that BLOCK_HEAD and BLOCK_FLOWER_POT are "reserved" and the high-bit version is not used
    // Here is where the high data bit gets masked off and moved to the type bit.
    if (block->mcVersion >= 13) {
        if ((*dataVal & HIGH_BIT) && (*type != BLOCK_HEAD) && (*type != BLOCK_FLOWER_POT)) {
            *dataVal &= 0x7F;
            *type |= 0x100;
        }
    }

    return RetrieveBlockSubname(*type, *dataVal); //, block), xoff, y, zoff);
}


char gConcatString[100];

static struct {
    char* name;
} gColorNames[] = {
    { "White" },
    { "Orange" },
    { "Magenta" },
    { "Light" },
    { "Yellow" },
    { "Lime" },
    { "Pink" },
    { "Gray" },
    { "Light" },
    { "Cyan" },
    { "Purple" },
    { "Blue" },
    { "Brown" },
    { "Green" },
    { "Red" },
    { "Black" }
};

static struct {
    char* name;
} gCoralNames[] = {
    { "Tube" },
    { "Brain" },
    { "Bubble" },
    { "Fire" },
    { "Horn" },
};

const char* RetrieveBlockSubname(int type, int dataVal) // , WorldBlock* block), int xoff, int y, int zoff)
{
    ///////////////////////////////////
    // give a better name if possible
    switch (type)
    {
    case BLOCK_LEAVES:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break; //return concatStrings(OAK_NAME, LEAVES_NAME);
        case 1:	// spruce
            return "Spruce Leaves";
        case 2:	// birch
            return "Birch Leaves";
        case 3:	// jungle
            return "Jungle Leaves";
        }
        break;

    case BLOCK_AD_LEAVES:
        switch (dataVal & 0x1)
        {
        default:
            assert(0);
            break;
        case 0:
            break; //return concatStrings(ACACIA_NAME, LEAVES_NAME);
        case 1:	// dark oak
            return "Dark Oak Leaves";
        }
        break;

    case BLOCK_GRASS:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0: // dead bush
            return "Dead Bush";
        case 1:	// tall grass - really, the default name Grass
            break; // return "TALL_GRASS";
        case 2:	// fern
            return "Fern";
        case 3:
            return "Nether Sprouts";
        case 4:
            return "Crimson Roots";
        case 5:
            return "Warped Roots";
        }
        break;

    case BLOCK_WOOL:
    case BLOCK_STAINED_GLASS:
    case BLOCK_STAINED_GLASS_PANE:
    case BLOCK_CARPET:
    case BLOCK_CONCRETE:
    case BLOCK_CONCRETE_POWDER:
        // someday, when I add beds with colors: case BLOCK_BED - and we'll probably need to shift the data value, since the lower bits are used for top/bottom etc.
        sprintf_s(gConcatString, 100, "%s %s", gColorNames[dataVal & 0xf].name, gBlockDefinitions[type].name);
        return gConcatString;

    case BLOCK_COLORED_TERRACOTTA:
        sprintf_s(gConcatString, 100, "%s Terracotta", gColorNames[dataVal & 0xf].name);
        return gConcatString;

    case BLOCK_REDSTONE_WIRE:
        sprintf_s(gConcatString, 100, "%s power %d", gBlockDefinitions[type].name, (dataVal & 0xf));
        return gConcatString;

    case BLOCK_PUMPKIN:
        if (!(dataVal & 0x4)) {
            return "Carved Pumpkin";
        }
        break;

    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
        sprintf_s(gConcatString, 100, "%s age %d", gBlockDefinitions[type].name, (dataVal & 0x7));
        return gConcatString;

    case BLOCK_FLOWER_POT:
        switch (dataVal)
        {
        default:
            assert(0);
            break;
        case 0:	// "Flower Pot" (the default - empty)
            break;
        case 2:
        case YELLOW_FLOWER_FIELD | 0:
            return "Potted Dandelion";
        case 1:
        case RED_FLOWER_FIELD | 0:
            return "Potted Poppy";
        case RED_FLOWER_FIELD | 1:
            return "Potted Blue Orchid";
        case RED_FLOWER_FIELD | 2:
            return "Potted Allium";
        case RED_FLOWER_FIELD | 3:
            return "Potted Azure Bluet";
        case RED_FLOWER_FIELD | 4:
            return "Potted Red Tulip";
        case RED_FLOWER_FIELD | 5:
            return "Potted Orange Tulip";
        case RED_FLOWER_FIELD | 6:
            return "Potted White Tulip";
        case RED_FLOWER_FIELD | 7:
            return "Potted Pink Tulip";
        case RED_FLOWER_FIELD | 8:
            return "Potted Oxeye Daisy";
        case RED_FLOWER_FIELD | 9:
            return "Potted Cornflower";
        case RED_FLOWER_FIELD | 10:
            return "Potted Lily of the Valley";
        case RED_FLOWER_FIELD | 11:
            return "Potted Wither Rose";
        case RED_FLOWER_FIELD | 12:
            return "Potted Crimson Fungus";
        case RED_FLOWER_FIELD | 13:
            return "Potted Warped Fungus";
        case RED_FLOWER_FIELD | 14:
            return "Potted Crimson Root";
        case RED_FLOWER_FIELD | 15:
            return "Potted Warped Root";
        case SAPLING_FIELD | 0:
        case 3:
            return "Potted Oak Sapling";
        case SAPLING_FIELD | 1:
        case 4:
            return "Potted Spruce Sapling";
        case SAPLING_FIELD | 2:
        case 5:
            return "Potted Birch Sapling";
        case SAPLING_FIELD | 3:
        case 6:
            return "Potted Jungle Sapling";
        case SAPLING_FIELD | 4:
        case 12:
            return "Potted Acacia Sapling";
        case SAPLING_FIELD | 5:
        case 13:
            return "Potted Dark Oak Sapling";
        case RED_MUSHROOM_FIELD | 0:
        case 7:
            return "Potted Red Mushroom";
        case BROWN_MUSHROOM_FIELD | 0:
        case 8:
            return "Potted Brown Mushroom";
        case TALLGRASS_FIELD | 2:	// yes, weirdly, there's a 2 here but no 0 or 1
        case 11:
            return "Potted Fern";
        case DEADBUSH_FIELD | 0:
        case 10:
            return "Potted Dead Bush";
        case CACTUS_FIELD | 0:
        case 9:
            return "Potted Cactus";
        case BAMBOO_FIELD | 0:
            return "Potted Bamboo";
        }
        break;

    case BLOCK_POPPY:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0: // poppy
            break;
        case 1:	// blue orchid
            return "Blue Orchid";
        case 2:	// allium
            return "Allium";
        case 3:	// azure bluet
            return "Azure Bluet";
        case 4:	// red tulip
            return "Red Tulip";
        case 5:	// orange tulip
            return "Orange Tulip";
        case 6:	// white tulip
            return "White Tulip";
        case 7:	// pink tulip
            return "Pink Tulip";
        case 8:	// oxeye daisy
            return "Oxeye Daisy";
        case 9:
            return "Cornflower";
        case 10:
            return "Lily of the Valley";
        case 11:
            return "Wither Rose";
        case 12:
            return "Crimson Fungus";
        case 13:
            return "Warped Fungus";
        case 14:
            return "Crimson Root";
        case 15:
            return "Warped Root";
        }
        break;

    case BLOCK_DOUBLE_FLOWER:
        // subtract 256, one Y level, as we need to look at the bottom of the plant to ID its type.
        // This is just a safety net now - we actually shove the data value into the upper part of the plant nowadays, in extractChunk
        //if ((block != NULL) && (dataVal & 0x8)) {
        //    // can get name only when the block data is available
        //    dataVal = block->data[xoff + zoff * 16 + (y - 1) * 256];
        //}
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            break;
        case 0: // sunflower
            break;
        case 1:	// lilac
            return "Lilac";
        case 2:	// tall grass
            return "Double Tallgrass";
        case 3:	// large fern
            return "Large Fern";
        case 4:	// rose bush
            return "Rose Bush";
        case 5:	// peony
            return "Peony";
        }
        break;

    case BLOCK_OAK_PLANKS:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// spruce
            return "Spruce Wood Planks";
        case 2:	// birch
            return "Birch Wood Planks";
        case 3:	// jungle
            return "Jungle Wood Planks";
        case 4:	// acacia
            return "Acacia Wood Planks";
        case 5:	// dark oak
            return "Dark Oak Wood Planks";
        case 6:
            return "Crimson Planks";
        case 7:
            return "Warped Planks";
        }
        break;

    case BLOCK_STONE:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Granite";
        case 2:
            return "Polished Granite";
        case 3:
            return "Diorite";
        case 4:
            return "Polished Diorite";
        case 5:
            return "Andesite";
        case 6:
            return "Polished Andesite";
        case 7: // blackstone
            return "Blackstone";
        case 8: // chiseled_polished_blackstone
            return "Chiseled Polished Blackstone";
        case 9: // polished_blackstone
            return "Polished Blackstone";
        case 10: // gilded_blackstone
            return "Gilded Blackstone";
        case 11: // polished_blackstone_bricks
            return "Polished Blackstone Bricks";
        case 12: // cracked_polished_blackstone_bricks
            return "Cracked Polished Blackstone Bricks";
        case 13: // netherite_block
            return "Netherite Block";
        case 14: // ancient_debris
            return "Ancient Debris";
        case 15: // nether_gold_ore
            return "Nether Gold Ore";
        }
        break;

    case BLOCK_NETHER_BRICKS:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Chiseled Nether Bricks";
        case 2:
            return "Cracked Nether Bricks";
        }
        break;

    case BLOCK_SOUL_SAND:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Soul Soil";
        }
        break;

    case BLOCK_GLOWSTONE:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Shroomlight";
        }
        break;

    case BLOCK_NETHER_WART_BLOCK:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Warped Wart Block";
        }
        break;

    case BLOCK_DIRT:
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Coarse Dirt";
        case 2:
            return "Podzol";
        case 3:
            return "Crimson Nylium";
        case 4:
            return "Warped Nylium";
        }
        break;

    case BLOCK_SAPLING:
        // mask off the age_bit - specifies the sapling's growth stage.
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// spruce
            return "Spruce Sapling";
        case 2:	// birch
            return "Birch Sapling";
        case 3:	// jungle
            return "Jungle Sapling";
        case 4:	// acacia
            return "Acacia Sapling";
        case 5:	// dark oak
            return "Dark Oak Sapling";
        case 6:	// bamboo
            return "Bamboo Sapling";
        }
        break;

        // TODO: someday check if "double" and put "Double " at the front of each name returned.
        // Easier and nice to do with code than adding a bunch of names.
    case BLOCK_RED_SANDSTONE_DOUBLE_SLAB:
    case BLOCK_RED_SANDSTONE_SLAB:
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
        case 0:
            break;
        case 1:
            return "Cut Red Sandstone Slab";
        case 2:
            return "Smooth Red Sandstone Slab";
        case 3:
            return "Cut Sandstone Slab";
        case 4:
            return "Smooth Sandstone Slab";
        case 5:
            return "Granite Slab";
        case 6:
            return "Polished Granite Slab";
        case 7:
            return "Smooth Quartz Slab";
        }
        break;

    case BLOCK_PURPUR_DOUBLE_SLAB:
    case BLOCK_PURPUR_SLAB:
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
        case 0:
        case 1:
            break;
        case 2:
            return "Prismarine Slab";
        case 3:
            return "Prismarine Brick Slab";
        case 4:
            return "Dark Prismarine Slab";
        case 5:
            return "Red Nether Brick Slab";
        case 6:
            return "Mossy Stone Brick Slab";
        case 7:
            return "Mossy Cobblestone Slab";
        }
        break;

    case BLOCK_STRIPPED_OAK:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// spruce
            return "Stripped Spruce Log";
        case 2:	// birch
            return "Stripped Birch Log";
        case 3:	// jungle
            return "Stripped Jungle Log";
        }
        break;
    case BLOCK_STRIPPED_ACACIA:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// dark oak
            return "Stripped Dark Oak Log";
        case 2:
            return "Stripped Crimson Stem";
        case 3:
            return "Stripped Warped Stem";
        }
        break;
    case BLOCK_STRIPPED_OAK_WOOD:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// spruce
            return "Stripped Spruce Wood";
        case 2:	// birch
            return "Stripped Birch Wood";
        case 3:	// jungle
            return "Stripped Jungle Wood";
        }
        break;
    case BLOCK_STRIPPED_ACACIA_WOOD:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// dark oak
            return "Stripped Dark Oak Wood";
        case 2:
            return "Stripped Crimson Hyphae";
        case 3:	// jungle
            return "Stripped Warped Hyphae";
        }
        break;
    case BLOCK_SIGN_POST:
        switch (dataVal & (BIT_16 | BIT_32))
        {
        default:
            assert(0);
            break;
        case 0:
            return "Oak Standing Sign";
        case BIT_16:	// spruce
            return "Spruce Standing Sign";
        case BIT_32:	// birch
            return "Birch Standing Sign";
        case BIT_32 | BIT_16:	// jungle
            return "Jungle Standing Sign";
        }
        break;
    case BLOCK_ACACIA_SIGN_POST:
        switch (dataVal & (BIT_16 | BIT_32))
        {
        default:
            assert(0);
            break;
        case 0:
            return "Acacia Standing Sign";
        case BIT_16:	// dark oak
            return "Dark Oak Standing Sign";
        case BIT_32:	// dark oak
            return "Crimson Standing Sign";
        case BIT_32 | BIT_16:	// dark oak
            return "Warped Standing Sign";
        }
        break;
    case BLOCK_WALL_SIGN:
        switch (dataVal & (BIT_8 | BIT_16 | BIT_32))
        {
        default:
            assert(0);
            break;
        case 0:
            return "Oak Wall Sign";
        case BIT_8:	// spruce
            return "Spruce Wall Sign";
        case BIT_16:	// birch
            return "Birch Wall Sign";
        case BIT_16 | BIT_8:	// jungle
            return "Jungle Wall Sign";
        case BIT_32:	// acacia
            return "Acacia Wall Sign";
        case BIT_32 | BIT_8:	// dark oak
            return "Dark Oak Wall Sign";
        case BIT_32 | BIT_16:
            return "Crimson Wall Sign";
        case BIT_32 | BIT_16 | BIT_8:
            return "Warped Wall Sign";
        }
        break;

    case BLOCK_SMOOTH_STONE:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // sandstone
            return "Smooth Sandstone";
        case 2: // red sandstone
            return "Smooth Red Sandstone";
        case 3: // quartz
            return "Quartz";
        }
        break;

        // special, returns its own particular constructed name
    case BLOCK_CORAL_BLOCK:
    case BLOCK_CORAL:
    case BLOCK_CORAL_FAN:
    case BLOCK_CORAL_WALL_FAN:
    case BLOCK_DEAD_CORAL_BLOCK:
    case BLOCK_DEAD_CORAL:
    case BLOCK_DEAD_CORAL_FAN:
    case BLOCK_DEAD_CORAL_WALL_FAN:
        switch (type) {
        case BLOCK_CORAL_BLOCK:
            sprintf_s(gConcatString, 100, "%s Coral Block", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_CORAL:
            sprintf_s(gConcatString, 100, "%s Coral", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_CORAL_FAN:
            sprintf_s(gConcatString, 100, "%s Coral Fan", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_CORAL_WALL_FAN:
            sprintf_s(gConcatString, 100, "%s Coral Wall Fan", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_DEAD_CORAL_BLOCK:
            sprintf_s(gConcatString, 100, "Dead %s Coral Block", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_DEAD_CORAL:
            sprintf_s(gConcatString, 100, "Dead %s Coral", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_DEAD_CORAL_FAN:
            sprintf_s(gConcatString, 100, "Dead %s Coral Fan", gCoralNames[dataVal & 0x7].name);
            break;
        case BLOCK_DEAD_CORAL_WALL_FAN:
            sprintf_s(gConcatString, 100, "Dead %s Coral Wall Fan", gCoralNames[dataVal & 0x7].name);
            break;
        }
        return gConcatString;

    case BLOCK_ANDESITE_DOUBLE_SLAB:
    case BLOCK_ANDESITE_SLAB:
        // a little wasteful if the default is returned after all
        strcpy_s(gConcatString, 100, (type == BLOCK_ANDESITE_DOUBLE_SLAB) ? "Double " : "");
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            return gBlockDefinitions[type].name;
        case 0:
            return gBlockDefinitions[type].name;
        case 1:
            strcat_s(gConcatString, 100, "Polished Andesite Slab");
            break;
        case 2:
            strcat_s(gConcatString, 100, "Diorite Slab");
            break;
        case 3:
            strcat_s(gConcatString, 100, "Polished Diorite Slab");
            break;
        case 4:
            strcat_s(gConcatString, 100, "End Stone Brick Slab");
            break;
        case 5:
            strcat_s(gConcatString, 100, "Stone Slab");
            break;
        }
        return gConcatString;

    case BLOCK_STONE_DOUBLE_SLAB:
    case BLOCK_STONE_SLAB:
        // a little wasteful if the default is returned after all
        strcpy_s(gConcatString, 100, (type == BLOCK_STONE_DOUBLE_SLAB) ? "Double " : "");
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            break;
        case 0:
            return gBlockDefinitions[type].name;
        case 1:
            // sandstone
            strcat_s(gConcatString, 100, "Sandstone Slab");
            break;
        case 2:
            // wooden
            strcat_s(gConcatString, 100, "Petrified Oak Slab");
            break;
        case 3:
            // cobblestone
            strcat_s(gConcatString, 100, "Cobblestone Slab");
            break;
        case 4:
            // brick
            strcat_s(gConcatString, 100, "Brick Slab");
            break;
        case 5:
            // stone brick
            strcat_s(gConcatString, 100, "Stone Brick Slab");
            break;
        case 6:
            // nether brick
            strcat_s(gConcatString, 100, "Nether Brick Slab");
            break;
        case 7:
            // quartz with distinctive sides and bottom
            strcat_s(gConcatString, 100, "Quartz Slab");
            break;
        }
        return gConcatString;

    case BLOCK_WOODEN_DOUBLE_SLAB:
    case BLOCK_WOODEN_SLAB:
        // a little wasteful if the default is returned after all
        strcpy_s(gConcatString, 100, (type == BLOCK_WOODEN_DOUBLE_SLAB) ? "Double " : "");
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            return gBlockDefinitions[type].name;
        case 0: // normal log
            return gBlockDefinitions[type].name;
        case 1: // spruce (dark)
            strcat_s(gConcatString, 100, "Spruce Slab");
            break;
        case 2: // birch
            strcat_s(gConcatString, 100, "Birch Slab");
            break;
        case 3: // jungle
            strcat_s(gConcatString, 100, "Jungle Slab");
            break;
        case 4: // acacia
            strcat_s(gConcatString, 100, "Acacia Slab");
            break;
        case 5: // dark oak
            strcat_s(gConcatString, 100, "Dark Oak Slab");
            break;
        case 6:
            strcat_s(gConcatString, 100, "Crimson Slab");
            break;
        case 7:
            strcat_s(gConcatString, 100, "Warped Slab");
            break;
        }
        return gConcatString;

    case BLOCK_CRIMSON_DOUBLE_SLAB:
    case BLOCK_CRIMSON_SLAB:
        // a little wasteful if the default is returned after all
        strcpy_s(gConcatString, 100, (type == BLOCK_CRIMSON_DOUBLE_SLAB) ? "Double " : "");
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            return gBlockDefinitions[type].name;
        case 0: // crimson
            return gBlockDefinitions[type].name;
        case 1:
            strcat_s(gConcatString, 100, "Warped Slab");
            break;
        case 2:
            strcat_s(gConcatString, 100, "Blackstone Slab");
            break;
        case 3:
            strcat_s(gConcatString, 100, "Polished Blackstone Slab");
            break;
        case 4:
            strcat_s(gConcatString, 100, "Polished Blackstone Brick Slab");
            break;
        }
        return gConcatString;

    case BLOCK_WEEPING_VINES:
        // note we ignore BIT_32, which is top and bottom
        switch (dataVal & 0xf) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Twisting Vines";
        }
        break;

    case BLOCK_COBBLESTONE_WALL:
        switch (dataVal & 0xf) {
        default:
            assert(0);
            break;
        case 0:
            // no change, default cobblestone is fine
            break;
        case 1: // mossy cobblestone
            return "Mossy Cobblestone Wall";
        case 2: // brick wall
            return "Brick Wall";
        case 3: // granite wall
            return "Granite Wall";
        case 4: // diorite wall
            return "Diorite Wall";
        case 5: // andesite wall
            return "Andesite Wall";
        case 6: // prismarine wall
            return "Prismarine Wall";
        case 7: // stone brick wall
            return "Stone Brick Wall";
        case 8: // mossy stone brick wall
            return "Mossy Stone Brick Wall";
        case 9: // end stone brick wall
            return "End Stone Brick Wall";
        case 10: // nether brick wall
            return "Nether Brick Wall";
        case 11: // red nether brick wall
            return "Red Nether Brick Wall";
        case 12: // sandstone wall
            return "Sandstone Wall";
        case 13: // red sandstone wall
            return "Red Sandstone Wall";
        case 14:
            return "Blackstone Wall";
        case 15:
            return "Polished Blackstone Wall";
        case 16:
            return "Polished Blackstone Brick Wall";
        }
        break;

    case BLOCK_PRISMARINE:
        switch (dataVal & 0x7)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // bricks
            return "Prismarine Bricks";
        case 2: // dark
            return "Dark Prismarine";
        }
        break;
    case BLOCK_FURNACE:
    case BLOCK_BURNING_FURNACE:
        switch (dataVal & (BIT_32 | BIT_16)) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case BIT_16:	// loom
            return "Loom";
        case BIT_32:	// smoker
            return "Smoker";
        case BIT_32 | BIT_16:	// blast furnace
            return "Blast Furnace";
        }
        break;
    case BLOCK_CRAFTING_TABLE:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// cartography
            return "Cartography Table";
        case 2:	// fletching
            return "Fletching Table";
        case 3:	// smithing
            return "Smithing Table";
        case 4:
            return "Lodestone";
        }
        break;

    case BLOCK_SAND:
        switch (dataVal & 0x1)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:	// red sand
            return "Red Sand";
        }
        break;

    case BLOCK_TNT:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Target";
        }
        break;

    case BLOCK_RED_MUSHROOM:
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Crimson Fungus";
        case 2:
            return "Warped Fungus";
        }
        break;

    case BLOCK_LANTERN:
        switch (dataVal & 0x2)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 2:
            return "Soul Lantern";
        }
        break;

    case BLOCK_CAMPFIRE:
        switch (dataVal & 0x8)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 8:
            return "Soul Campfire";
        }
        break;

    case BLOCK_FIRE:
        switch (dataVal & BIT_16)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case BIT_16:
            return "Soul Fire";
        }
        break;

    case BLOCK_LOG:
        if (dataVal & BIT_16) {
            switch (dataVal & 0x3)
            {
            default:
                assert(0);
                break;
            case 0:
                return "Oak Wood";
            case 1:	// spruce
                return "Spruce Wood";
            case 2:	// birch
                return "Birch Wood";
            case 3:	// jungle
                return "Jungle Wood";
            }
        }
        else {
            switch (dataVal & 0x3)
            {
            default:
                assert(0);
                break;
            case 0:
                break;
            case 1:	// spruce
                return "Spruce Log";
            case 2:	// birch
                return "Birch Log";
            case 3:	// jungle
                return "Jungle Log";
            }
        }
        break;
    case BLOCK_AD_LOG:
        if (dataVal & BIT_16) {
            switch (dataVal & 0x3)
            {
            default:
                assert(0);
                break;
            case 0:
                return "Acacia Wood";
            case 1:	// dark oak
                return "Dark Oak Wood";
            case 2:
                return "Crimson Hyphae";
            case 3:
                return "Warped Hyphae";
            }
        }
        else {
            switch (dataVal & 0x3)
            {
            default:
                assert(0);
                break;
            case 0:
                break;
            case 1:	// dark oak
                return "Dark Oak Log";
            case 2:
                return "Crimson Stem";
            case 3:
                return "Warped Stem";
            }
        }
        break;
    case BLOCK_STATIONARY_WATER:
        if (dataVal & BIT_16)
        {
            // bubble column
            return "Bubble Column";
        }
        break;
    case BLOCK_SPONGE:
        switch (dataVal & 0x1)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Wet Sponge";
        }
        break;
    case BLOCK_BONE_BLOCK:
        switch (dataVal & 0x3)
        {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1:
            return "Basalt";
        case 2:
            return "Polished Basalt";
        }
        break;

    case BLOCK_SANDSTONE:
        switch (dataVal & 0x3) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // chiseled
            return "Chiseled Sandstone";
        case 2: // smooth
            return "Cut Sandstone";
        }
        break;
    case BLOCK_RED_SANDSTONE:
        switch (dataVal & 0x3) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // chiseled
            return "Chiseled Red Sandstone";
        case 2: // smooth
            return "Cut Red Sandstone";
        }
        break;
    case BLOCK_STONE_BRICKS:
        switch (dataVal & 0x3) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // mossy - small color difference, so this isn't added to color table
            return "Mossy Stone Bricks";
        case 2: // cracked
            return "Cracked Stone Bricks";
        case 3: // chiseled
            return "Chiseled Stone Bricks";
        }
        break;
    case BLOCK_INFESTED_STONE:
        switch (dataVal & 0x7) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // cobblestone
            return "Infested Cobblestone";
        case 2: // stone brick
            return "Infested Stone Bricks";
        case 3: // mossy - small color difference, so this isn't added to color table
            return "Infested Mossy Stone Bricks";
        case 4: // cracked
            return "Infested Cracked Stone Bricks";
        case 5: // chiseled
            return "Infested Chiseled Stone Bricks";
        }
        break;
    case BLOCK_QUARTZ_BLOCK:
        switch (dataVal & 0x7) {
        default:
            assert(0);
            break;
        case 0:
            break;
        case 1: // chiseled quartz block
            return "Chiseled Quartz Block";
        case 2: // quartz pillar
        case 3: // quartz pillar
        case 4: // quartz pillar - different directions
            return "Quartz Pillar";
        case 5:
            return "Quartz Brick";
        }
        break;

    case BLOCK_HEAD:
        switch (dataVal & 0x70) {
        default:
            assert(0);
            break;
        case 0:
            sprintf_s(gConcatString, 100, "Skeleton %sSkull", (dataVal & 0x80) ? "" : "Wall ");
            break;
        case 1 << 4:
            sprintf_s(gConcatString, 100, "Wither Skeleton %sSkull", (dataVal & 0x80) ? "" : "Wall ");
            break;
        case 2 << 4:
            sprintf_s(gConcatString, 100, "Zombie %sHead", (dataVal & 0x80) ? "" : "Wall ");
            break;
        case 3 << 4:
            sprintf_s(gConcatString, 100, "Player %sHead", (dataVal & 0x80) ? "" : "Wall ");
            break;
        case 4 << 4:
            sprintf_s(gConcatString, 100, "Creeper %sHead", (dataVal & 0x80) ? "" : "Wall ");
            break;
        case 5 << 4:
            sprintf_s(gConcatString, 100, "Dragon %sHead", (dataVal & 0x80) ? "" : "Wall ");
            break;
        }
        return gConcatString;

    case BLOCK_ANVIL:
        switch (dataVal & 0xC)
        {
        default:
            assert(0);
            break;
        case 0: // as is
            break;
        case 4:
            return "Chipped Anvil";
        case 8:
            return "Damaged Anvil";
        }
        break;
    case BLOCK_BEE_NEST:
        if (dataVal & BIT_32)
        {
            return "Beehive";
        }
    }

    return gBlockDefinitions[type].name;
}

//copy block to bits at px,py at zoom.  bits is wxh
static void blit(unsigned char* block, unsigned char* bits, int px, int py,
    double zoom, int w, int h)
{
    int x, y, yofs, bitofs;
    int skipx = 0, skipy = 0;
    int bw = (int)(16 * zoom);
    int bh = (int)(16 * zoom);
    if (px < 0) skipx = -px;
    if (px + bw >= w) bw = w - px;
    if (bw <= 0) return;
    if (py < 0) skipy = -py;
    if (py + bh >= h) bh = h - py;
    if (bh <= 0) return;
    bits += py * w * 4;
    bits += px * 4;
    for (y = 0; y < bh; y++, bits += w << 2)
    {
        if (y < skipy) continue;
        yofs = ((int)(y / zoom)) << 6;
        bitofs = 0;
        if (zoom == 1.0 && skipx == 0 && bw == 16) {
            memcpy(bits + bitofs, block + yofs, 16 * 4);
        }
        else {
            for (x = 0; x < bw; x++, bitofs += 4)
            {
                if (x < skipx) continue;
                memcpy(bits + bitofs, block + yofs + (((int)(x / zoom)) << 2), 4);
            }
        }
    }
}

void CloseAll()
{
    Cache_Empty();
}

static unsigned short retrieveType(WorldBlock* block, unsigned int voxel)
{
    unsigned short type = block->grid[voxel];
    if (block->mcVersion >= 13) {
        if ((block->data[voxel] & 0x80) && (type != BLOCK_HEAD) && (type != BLOCK_FLOWER_POT)) {
            type |= 0x100;
        }
    }
    return type;
}

static unsigned int scaleColor(unsigned int color, float scale)
{
    unsigned int r = color >> 16;
    unsigned int g = (color >> 8) & 0xff;
    unsigned int b = color & 0xff;
    r = (unsigned int)(r * scale + 0.5f);
    g = (unsigned int)(g * scale + 0.5f);
    b = (unsigned int)(b * scale + 0.5f);
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;
    return (r << 16) | (g << 8) | b;
}

static unsigned int checkSpecialBlockColor(WorldBlock* block, unsigned int voxel, unsigned short type, int light, char useBiome, char useElevation)
{
    unsigned int color = 0xFFFFFF;
    unsigned int r, g, b;
    unsigned char dataVal;
    bool lightComputed = false;
    float alpha;
    bool alphaComputed = false;
    int affectedByBiome = 0;

    switch (type)
    {
    case BLOCK_WOOL:
    case BLOCK_CARPET:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
            // I picked the color from the tile location 2 from the left, 3 down.
        default:
            assert(0);
        case 0:
            lightComputed = true;
            //color = 0xEEEEEE;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:
            color = 0xDA8248;
            break;
        case 2:
            color = 0xBA5EC2;
            break;
        case 3:
            color = 0x7B96CD;
            break;
        case 4:
            color = 0xC1B52A;
            break;
        case 5:
            color = 0x46BA3A;
            break;
        case 6:
            color = 0xD597A7;
            break;
        case 7:
            color = 0x434343;
            break;
        case 8:
            color = 0xA6ACAC;
            break;
        case 9:
            color = 0x307592;
            break;
        case 10:
            color = 0x8643BF;
            break;
        case 11:
            color = 0x2E3B97;
            break;
        case 12:
            color = 0x53351F;
            break;
        case 13:
            color = 0x384B1B;
            break;
        case 14:
            color = 0xA23732;
            break;
        case 15:
            color = 0x1D1818;
            break;
        }
        break;

    case BLOCK_COLORED_TERRACOTTA:
        // from upper left corner
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
            // I picked the color from the tile location 2 from the left, 3 down.
        default:
            assert(0);
        case 0:
            lightComputed = true;
            //color = 0xCEAE9E;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:
            color = 0x9D5021;
            break;
        case 2:
            color = 0x925469;
            break;
        case 3:
            color = 0x6D6987;
            break;
        case 4:
            color = 0xB6801F;
            break;
        case 5:
            color = 0x647230;
            break;
        case 6:
            color = 0x9D4A4B;
            break;
        case 7:
            color = 0x362621;
            break;
        case 8:
            color = 0x84665D;
            break;
        case 9:
            color = 0x535758;
            break;
        case 10:
            color = 0x734253;
            break;
        case 11:
            color = 0x473858;
            break;
        case 12:
            color = 0x4A2F21;
            break;
        case 13:
            color = 0x484F27;
            break;
        case 14:
            color = 0x8B392B;
            break;
        case 15:
            color = 0x21120D;
            break;
        }
        break;

    case BLOCK_STAINED_GLASS:
    case BLOCK_STAINED_GLASS_PANE:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
            // from 2 down, 2 to the right, basically upper left inside the frame
        default:
            assert(0);
        case 0:
            lightComputed = true;
            //color = 0xEFEFEF;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:
            color = 0xDFBB9D;
            break;
        case 2:
            color = 0xCFA7DF;
            break;
        case 3:
            color = 0xB1C5DF;
            break;
        case 4:
            color = 0xE3E39D;
            break;
        case 5:
            color = 0xBBD995;
            break;
        case 6:
            color = 0xE9BBCB;
            break;
        case 7:
            color = 0xA7A7A7;
            break;
        case 8:
            color = 0xC5C5C5;
            break;
        case 9:
            color = 0xA7BBC5;
            break;
        case 10:
            color = 0xBBA1CF;
            break;
        case 11:
            color = 0x9DA7CF;
            break;
        case 12:
            color = 0xB1A79D;
            break;
        case 13:
            color = 0xB1BB9D;
            break;
        case 14:
            color = 0xC59D9D;
            break;
        case 15:
            color = 0x959595;
            break;
        }
        // now premultiply by alpha
        r = color >> 16;
        g = (color >> 8) & 0xff;
        b = color & 0xff;
        alpha = gBlockDefinitions[type].alpha;
        r = (unsigned char)(r * alpha);
        g = (unsigned char)(g * alpha);
        b = (unsigned char)(b * alpha);

        color = (r << 16) | (g << 8) | b;

        break;

    case BLOCK_OAK_PLANKS:
    case BLOCK_WOODEN_DOUBLE_SLAB:
    case BLOCK_WOODEN_SLAB:
        dataVal = block->data[voxel];
        // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
        switch (dataVal & 0x7)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// spruce
            color = gBlockDefinitions[BLOCK_SPRUCE_WOOD_STAIRS].pcolor;
            break;
        case 2:	// birch
            color = gBlockDefinitions[BLOCK_BIRCH_WOOD_STAIRS].pcolor;
            break;
        case 3:	// jungle
            color = gBlockDefinitions[BLOCK_JUNGLE_WOOD_STAIRS].pcolor;
            break;
        case 4:	// acacia
            color = gBlockDefinitions[BLOCK_ACACIA_WOOD_STAIRS].pcolor;
            break;
        case 5:	// dark oak
            color = gBlockDefinitions[BLOCK_DARK_OAK_WOOD_STAIRS].pcolor;
            break;
        }
        break;

    case BLOCK_CRIMSON_DOUBLE_SLAB:
    case BLOCK_CRIMSON_SLAB:
        dataVal = block->data[voxel];
        // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
        switch (dataVal & 0x7)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// warped
            color = 0x2D6D68;
            break;
        case 2:	// blackstone top
            color = 0x2D282F;
            break;
        case 3:	// polished blackstone
            color = 0x37333D;
            break;
        case 4:	// polished blackstone brick
            color = 0x322E36;
            break;
        }
        break;

    case BLOCK_WEEPING_VINES:
        dataVal = block->data[voxel];
        // The 0x1 is the type
        switch (dataVal & 0x1)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// twisting
            color = 0x148C7C;
            break;
        }
        break;

    case BLOCK_STONE:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// granite
            color = 0xA77562;
            break;
        case 2:	// polished granite
            color = 0x946251;
            break;
        case 3:	// diorite
            color = 0x9B9B9E;
            break;
        case 4:	// polished diorite
            color = 0xC9C9CD;
            break;
        case 5:	// andesite
            color = gBlockDefinitions[BLOCK_ANDESITE_SLAB].pcolor;
            break;
        case 6:	// polished andesite
            color = 0x7F7F84;
            break;
        case 7: // blackstone
            color = 0x2D282F;
            break;
        case 8: // chiseled_polished_blackstone
            color = 0x39353E;
            break;
        case 9: // polished_blackstone
            color = 0x37333D;
            break;
        case 10: // gilded_blackstone
            color = 0x4A392D;
            break;
        case 11: // polished_blackstone_bricks
            color = 0x322E36;
            break;
        case 12: // cracked_polished_blackstone_bricks
            color = 0x2F2B32;
            break;
        case 13: // netherite_block
            color = 0x444042;
            break;
        case 14: // ancient_debris
            color = 0x67504A;
            break;
        case 15: // nether_gold_ore
            color = 0x7E4E31;
            break;
        }
        break;

    case BLOCK_NETHER_BRICKS:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// chiseled
            color = 0x331A1E;
            break;
        case 2:	// cracked
            color = 0x2B1519;
            break;
        }
        break;

    case BLOCK_SOUL_SAND:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// soul soil
            color = 0x4E3B30;
            break;
        }
        break;

    case BLOCK_GLOWSTONE:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// shroomlight
            color = 0xF29C5E;
            break;
        }
        break;

    case BLOCK_DIRT:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// coarse dirt
            color = 0x7D5A3F;
            break;
        case 2:	// podzol
            color = 0x5F4118;
            break;
        case SNOWY_BIT | 2:	// podzol with snow
            color = 0xFCFFFF;
            break;
        case 3:	// crimson nylium
            color = 0x852727;
            break;
        case 4:	// warped nylium
            color = 0x347568;
            break;
        }
        break;

    case BLOCK_NETHER_WART_BLOCK:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// warped wart block
            color = 0x177A7A;
            break;
        }
        break;

    case BLOCK_SAND:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// red sand
            color = 0xA85420;
            break;
        }
        break;

    case BLOCK_TNT:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// target
            color = 0xE7C7BE;
            break;
        }
        break;

    case BLOCK_RED_MUSHROOM:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:
            color = 0x9D3F2B;
            break;
        case 2:
            color = 0x777965;
            break;
        }
        break;

    case BLOCK_FIRE:
        dataVal = block->data[voxel];
        switch (dataVal & BIT_16)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case BIT_16:	// soul fire
            color = 0x6BD0D5;
            break;
        }
        break;

    case BLOCK_LOG:
    case BLOCK_STRIPPED_OAK:
    case BLOCK_STRIPPED_OAK_WOOD:
        dataVal = block->data[voxel];
        switch (dataVal & 0x3)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// spruce
            color = 0x291806;
            break;
        case 2:	// birch
            color = 0xE2E8DF;
            break;
        case 3:	// jungle
            color = 0x584419;
            break;
        }
        break;

    case BLOCK_BONE_BLOCK:
        dataVal = block->data[voxel];
        switch (dataVal & 0x3)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// basalt
            color = 0x565659;
            break;
        case 2:	// polished basalt
            color = 0x676667;
            break;
        }
        break;

    case BLOCK_LEAVES:
        dataVal = block->data[voxel];
        switch (dataVal & 0x3)
        {
        default:
        case 0:	// oak
        case 3:	// jungle
            // if the default color is in use, use something more visible
            if (gBlockDefinitions[BLOCK_LEAVES].read_color == gBlockDefinitions[BLOCK_LEAVES].color)
            {
                // NOTE: considerably darker than what is stored:
                // the stored value is used to affect only the output color, not the map color.
                // This oak leaf color (and jungle, below) makes the trees easier to pick out.

                // jungle and oak
                color = dataVal ? 0x46AD19 : 0x3A7F1B;
            }
            else
            {
                lightComputed = true;
                color = gBlockColors[type * 16 + light];
            }
            affectedByBiome = 2;
            break;
        case 1:	// spruce
            color = 0x3D623D;
            break;
        case 2:	// birch
            color = 0x6B8D46;
            break;
        }
        break;

    case BLOCK_AD_LEAVES:
        affectedByBiome = 2;
        // if the default color is in use, use something more visible
        if (gBlockDefinitions[BLOCK_LEAVES].read_color == gBlockDefinitions[BLOCK_LEAVES].color)
        {
            // NOTE: considerably darker than what is stored:
            // the stored value is used to affect only the output color, not the map color.
            // This oak leaf color (and jungle, below) makes the trees easier to pick out.

            // acacia and 
            dataVal = block->data[voxel];
            // dark oak and acacia
            color = dataVal ? 0x2C6F0F : 0x3D9A14;
        }
        else
        {
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
        }
        break;

    case BLOCK_GRASS:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        case 0: // dead bush
            color = 0x946428;
            break;
        default:
            assert(0);
        case 1:	// grass
        case 2:	// fern
            // by default, color is used for grass and ferns, which are more common
            affectedByBiome = 1;
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            // fern should be dimmer, 128 vs. 148 - we dim it further, so it'll look different than a grass block
            if ((dataVal & 0x3) == 2) {
                color = scaleColor(color, 118.0f / 148.0f);
            }
            break;
        case 3:	// nether sprouts
            color = 0x149985;
            break;
        case 4:	// crimson roots
            color = 0x83092B;
            break;
        case 5:	// warped roots
            color = 0x148E7E;
            break;
        }
        break;

    case BLOCK_GRASS_BLOCK:
        dataVal = block->data[voxel];
        affectedByBiome = 1;
        if (dataVal & SNOWY_BIT) {
            color = 0xFCFFFF;
        }
        else {
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
        }
        break;

    case BLOCK_VINES:
        affectedByBiome = 1;
        lightComputed = true;
        color = gBlockColors[type * 16 + light];
        break;

    case BLOCK_AD_LOG:
    case BLOCK_STRIPPED_ACACIA:
    case BLOCK_STRIPPED_ACACIA_WOOD:
        dataVal = block->data[voxel];
        switch (dataVal & 0x3)
        {
        default:	// acacia
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// dark oak
            color = 0x342816;
            break;
        case 2:	// crimson
            color = 0x7B3953;
            break;
        case 3:	// warped
            color = 0x35837F;
            break;
        }
        break;

    case BLOCK_STONE_DOUBLE_SLAB:
    case BLOCK_STONE_SLAB:
        dataVal = block->data[voxel];
        alphaComputed = true;
        switch (dataVal & 0xf)
        {
        default:
        case 8:	// full stone
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// sandstone
        case 9:	// full sandstone
            color = gBlockDefinitions[BLOCK_SANDSTONE].pcolor;
            break;
        case 2:	// wooden
            color = gBlockDefinitions[BLOCK_OAK_PLANKS].pcolor;
            break;
        case 3:	// cobblestone
        case 11:	// cobblestone
            color = gBlockDefinitions[BLOCK_COBBLESTONE].pcolor;
            break;
        case 4:	// bricks
        case 12:	// bricks
            color = gBlockDefinitions[BLOCK_BRICK].pcolor;
            break;
        case 5:	// stone brick
        case 13:	// stone brick
            color = gBlockDefinitions[BLOCK_STONE_BRICKS].pcolor;
            break;
        case 6:	// nether brick
        case 14:	// nether brick
            color = gBlockDefinitions[BLOCK_NETHER_BRICKS].pcolor;
            break;
        case 7:	// quartz
        case 15:	// quartz
            color = gBlockDefinitions[BLOCK_QUARTZ_BLOCK].pcolor;
            break;
        case 10:	// tile quartz or upper wooden slab - what? Some old weirdness...
            color = gBlockDefinitions[(type == BLOCK_STONE_DOUBLE_SLAB) ? BLOCK_QUARTZ_BLOCK : BLOCK_OAK_PLANKS].pcolor;
            break;
        }
        break;

    case BLOCK_RED_SANDSTONE_DOUBLE_SLAB:
    case BLOCK_RED_SANDSTONE_SLAB:
        dataVal = block->data[voxel];
        alphaComputed = true;
        switch (dataVal & 0x7)
        {
        default:
        case 1:	// cut red sandstone
        case 2:	// smooth red sandstone
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 3: // cut sandstone
        case 4: // smooth sandstone
            color = gBlockDefinitions[BLOCK_SANDSTONE].pcolor;
            break;
        case 5:	// granite
            color = 0xA77562;
            break;
        case 6:	// polished granite
            color = 0x946251;
            break;
        case 7:	// smooth quartz
            color = gBlockDefinitions[BLOCK_QUARTZ_BLOCK].pcolor;
            break;
        }
        break;

    case BLOCK_ANDESITE_DOUBLE_SLAB:
    case BLOCK_ANDESITE_SLAB:
        dataVal = block->data[voxel];
        alphaComputed = true;
        switch (dataVal & 0x7)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// polished andesite
            color = 0x7F7F84;
            break;
        case 2:	// diorite
            color = 0x9B9B9E;
            break;
        case 3: // polished diorite slab
            color = 0xC9C9CD;
            break;
        case 4: // end stone brick slab
            color = gBlockDefinitions[BLOCK_END_BRICKS].pcolor;
            break;
        case 5:	// stone slab
            color = gBlockDefinitions[BLOCK_STONE].pcolor;
            break;
        }
        break;

    case BLOCK_POPPY:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:	// poppy
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// blue orchid
            color = 0x26ABF8;
            break;
        case 2:	// allium
            color = 0xB562F8;
            break;
        case 3:	// azure bluet
            color = 0xE1E7EF;
            break;
        case 4:	// red tulip
            color = 0xC02905;
            break;
        case 5:	// orange tulip
            color = 0xDE6E20;
            break;
        case 6:	// white tulip
            color = 0xE4E4E4;
            break;
        case 7:	// pink tulip
            color = 0xE7BBE7;
            break;
        case 8:	// oxeye daisy
            color = 0xE7D941;
            break;
        case 9: // cornflower
            color = 0x547CAB;
            break;
        case 10: // lily of the valley
            color = 0x93B588;
            break;
        case 11: // wither rose
            color = 0x2D3119;
            break;
        }
        break;

    case BLOCK_DOUBLE_FLOWER:
        // Subtract 256, one Y level, as we need to look at the bottom of the plant to ID its type.
        // Guard against a negative voxel value. Use the top half if the bottom half doesn't exist;
        // this is entirely bogus, as we really need the bottom half to get the right bits, but perhaps
        // some modded data uses the bottom three bits in this way...
        // This is just a safety net now - we actually shove the data value into the upper part of the plant nowadays, in extractChunk
        dataVal = (voxel >= 256) ? block->data[voxel - 256] : block->data[voxel];
        // masking just in case it's a top half (and probably bogus)
        switch (dataVal & 0x7)
        {
        default:
        case 0:	// sunflower
            color = 0xEAD31F;
            break;
        case 1:	// lilac
            color = 0xB79ABB;
            break;
        case 2:	// tall grass
            // we use color as the grass multiplier color
            affectedByBiome = 1;
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 3:	// large fern
            // we use color as the grass multiplier color
            affectedByBiome = 1;
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 4:	// rose bush
            color = 0xF4210B;
            break;
        case 5:	// peony
            color = 0xE3BCF4;
            break;
        }
        break;

    case BLOCK_SPONGE:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// wet sponge
            color = 0x999829;
            break;
        }
        break;

    case BLOCK_WATER:
        color = gBlockDefinitions[BLOCK_WATER].color;
        affectedByBiome = 3;	// by swamp only
        break;

    case BLOCK_STATIONARY_WATER:
        color = gBlockDefinitions[BLOCK_STATIONARY_WATER].color;
        affectedByBiome = 3;
        break;

    case BLOCK_CONCRETE:
        // from upper left corner
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:
            color = 0xE06101;
            break;
        case 2:
            color = 0xA9309F;
            break;
        case 3:
            color = 0x2489C7;
            break;
        case 4:
            color = 0xF1AF15;
            break;
        case 5:
            color = 0x5EA919;
            break;
        case 6:
            color = 0xD6658F;
            break;
        case 7:
            color = 0x373A3E;
            break;
        case 8:
            color = 0x7D7D73;
            break;
        case 9:
            color = 0x157788;
            break;
        case 10:
            color = 0x64209C;
            break;
        case 11:
            color = 0x2D2F8F;
            break;
        case 12:
            color = 0x603C20;
            break;
        case 13:
            color = 0x495B24;
            break;
        case 14:
            color = 0x8E2121;
            break;
        case 15:
            color = 0x080A0F;
            break;
        }
        break;

    case BLOCK_CONCRETE_POWDER:
        // from upper left corner
        dataVal = block->data[voxel];
        switch (dataVal & 0xf)
        {
        default:
            assert(0);
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:
            color = 0xE38423;
            break;
        case 2:
            color = 0xC155B9;
            break;
        case 3:
            color = 0x4BB5D6;
            break;
        case 4:
            color = 0xE9C739;
            break;
        case 5:
            color = 0x7EBD2B;
            break;
        case 6:
            color = 0xE59AB6;
            break;
        case 7:
            color = 0x4D5155;
            break;
        case 8:
            color = 0x9B9B95;
            break;
        case 9:
            color = 0x25959D;
            break;
        case 10:
            color = 0x8438B2;
            break;
        case 11:
            color = 0x474AA7;
            break;
        case 12:
            color = 0x7E5536;
            break;
        case 13:
            color = 0x61782D;
            break;
        case 14:
            color = 0xA93633;
            break;
        case 15:
            color = 0x1B1C21;
            break;
        }
        break;

    case BLOCK_PURPUR_DOUBLE_SLAB:
    case BLOCK_PURPUR_SLAB:
        dataVal = block->data[voxel];
        alphaComputed = true;
        switch (dataVal & 0x7)
        {
        default:
        case 0:	// full stone
        case 1:	// purpur, just in case
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 2:	// prismarine
            color = gBlockDefinitions[BLOCK_PRISMARINE].pcolor;
            break;
        case 3:	// prismarine block
            color = gBlockDefinitions[BLOCK_PRISMARINE_BRICK_STAIRS].pcolor;
            break;
        case 4:	// dark prismarine
            color = gBlockDefinitions[BLOCK_DARK_PRISMARINE_STAIRS].pcolor;
            break;
        case 5:	// red nether brick
            color = gBlockDefinitions[BLOCK_RED_NETHER_BRICK].pcolor;
            break;
        case 6:	// mossy stone brick
            color = 0x767B6E;
            break;
        case 7:	// mossy cobblestone
            color = gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].pcolor;
            break;
        }
        break;

    case BLOCK_CORAL_BLOCK:
    case BLOCK_CORAL:
    case BLOCK_CORAL_FAN:
    case BLOCK_CORAL_WALL_FAN:
        dataVal = block->data[voxel];
        switch (dataVal & 0x7)
        {
        default:
        case 0:	// tube coral - as is
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1:	// brain
            color = 0xC85D9B;
            break;
        case 2:	// bubble
            color = 0xA61EA2;
            break;
        case 3:	// fire
            color = 0xAC282F;
            break;
        case 4:	// horn
            color = 0xD2BE40;
            break;
        }
        break;

    case BLOCK_SIGN_POST:
        dataVal = block->data[voxel];
        switch (dataVal & (BIT_16 | BIT_32))
        {
        default:
            assert(0);
            break;
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case BIT_16:	// spruce
            color = 0x745632;
            break;
        case BIT_32:	// birch
            color = 0xC2B17A;
            break;
        case BIT_32 | BIT_16:	// jungle
            color = 0xA37654;
            break;
        }
        break;

    case BLOCK_ACACIA_SIGN_POST:
        dataVal = block->data[voxel];
        switch (dataVal & (BIT_16 | BIT_32))
        {
        default:
            assert(0);
            break;
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case BIT_16:	// dark oak
            color = 0x442C15;
            break;
        case BIT_32:	// crimson
            color = 0x7B3953;
            break;
        case BIT_32 | BIT_16:	// warped
            color = 0x35837F;
            break;
        }
        break;

    case BLOCK_WALL_SIGN:
        dataVal = block->data[voxel];
        switch (dataVal & (BIT_8 | BIT_16 | BIT_32))
        {
        default:
            assert(0);
            break;
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case BIT_8:	// spruce
            color = 0x745632;
            break;
        case BIT_16:	// birch
            color = 0xC2B17A;
            break;
        case BIT_16 | BIT_8:	// jungle
            color = 0xA37654;
            break;
        case BIT_32:	// acacia
            color = 0xA95B33;
            break;
        case BIT_32 | BIT_8:	// dark oak
            color = 0x442C15;
            break;
        case BIT_32 | BIT_16:
            color = 0x7B3953;
            break;
        case BIT_32 | BIT_16 | BIT_8:
            color = 0x35837F;
            break;
        }
        break;

    case BLOCK_SMOOTH_STONE:
        dataVal = block->data[voxel];
        switch (dataVal & 0x3)
        {
        default:
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1: // smooth sandstone
            color = gBlockDefinitions[BLOCK_SANDSTONE].pcolor;
            break;
        case 2: // red sandstone
            color = gBlockDefinitions[BLOCK_RED_SANDSTONE].pcolor;
            break;
        case 3: // quartz
            color = gBlockDefinitions[BLOCK_QUARTZ_BLOCK].pcolor;
            break;
        }
        break;

    case BLOCK_COBBLESTONE_WALL:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf) {
        default:
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1: // mossy cobblestone
            color = gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].pcolor;
            break;
        case 2: // brick wall
            color = gBlockDefinitions[BLOCK_BRICK].pcolor;
            break;
        case 3: // granite wall
            color = 0xA77562;
            break;
        case 4: // diorite wall
            color = 0x9B9B9E;
            break;
        case 5: // andesite wall
            color = gBlockDefinitions[BLOCK_ANDESITE_SLAB].pcolor;
            break;
        case 6: // prismarine wall
            color = gBlockDefinitions[BLOCK_PRISMARINE].pcolor;
            break;
        case 7: // stone brick wall
            color = gBlockDefinitions[BLOCK_STONE_BRICKS].pcolor;
            break;
        case 8: // mossy stone brick wall
            color = 0x767B6E;
            break;
        case 9: // end stone brick wall
            color = 0xDBE2A4;
            break;
        case 10: // nether brick wall
            color = gBlockDefinitions[BLOCK_NETHER_BRICKS].pcolor;
            break;
        case 11: // red nether brick wall
            color = gBlockDefinitions[BLOCK_RED_NETHER_BRICK].pcolor;
            break;
        case 12: // sandstone wall
            color = gBlockDefinitions[BLOCK_SANDSTONE].pcolor;
            break;
        case 13: // red sandstone wall
            color = gBlockDefinitions[BLOCK_RED_SANDSTONE].pcolor;
            break;
        case 14: // blackstone wall
            color = 0x2D272E;
            break;
        case 15: // polished blackstone wall
            color = 0x37333D;
            break;
        case 16: // polished blackstone brick wall
            color = 0x322E36;
            break;
        }
        break;

    case BLOCK_PRISMARINE:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf) {
        default:
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1: // bricks
            color = gBlockDefinitions[BLOCK_PRISMARINE_BRICK_STAIRS].pcolor;
            break;
        case 2:	// dark prismarine
            color = gBlockDefinitions[BLOCK_DARK_PRISMARINE_STAIRS].pcolor;
            break;
        }
        break;

    case BLOCK_FURNACE:
    case BLOCK_BURNING_FURNACE:
        dataVal = block->data[voxel];
        switch (dataVal & (BIT_32 | BIT_16)) {
        default:
        case 0x0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case BIT_16:	// loom
            color = 0x9A836C;
            break;
        case BIT_32:	// smoker
            color = 0x5C5A59;
            break;
        case BIT_32 | BIT_16:	// blast furnace
            color = 0x535253;
            break;
        }
        break;

    case BLOCK_CRAFTING_TABLE:
        dataVal = block->data[voxel];
        switch (dataVal & 0xf) {
        default:
        case 0:
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        case 1: // cartography
            color = 0x81756D;
            break;
        case 2:	// fletching
            color = 0xC8B78C;
            break;
        case 3:	// smithing
            color = 0x3B3C49;
            break;
        case 4:	// lodestone
            color = 0x7D7E80;
            break;
        }
        break;
    case BLOCK_BEE_NEST:
        dataVal = block->data[voxel];
        if (dataVal & BIT_32)
        {
            // beehive
            color = 0xB5935B;
        }
        else {
            lightComputed = true;
            color = gBlockColors[type * 16 + light];
            break;
        }
        break;

    default:
        // Everything else
        lightComputed = true;
        color = gBlockColors[type * 16 + light];
    }

    // if biome affects color, then look up color and use it
    if (useBiome && affectedByBiome)
    {
        // get the biome
        unsigned char biome = block->biome[voxel & 0xff];	// x and z location
        int elevation;
        if (useElevation)
        {
            elevation = (voxel >> 8) - 64;	// y location
        }
        else
        {
            // not used
            elevation = 0;
        }

        switch (affectedByBiome)
        {
        default:
        case 1:
            // grass
            // We'll have to compute the effect of light, alpha, etc.
            // so turn on this flag.
            lightComputed = false;
            if (elevation)
            {
                color = ComputeBiomeColor(biome, elevation, 1);
            }
            else
            {
                color = gBiomes[biome].grass;
            }
            break;
        case 2:
            // trees
            // We'll have to compute the effect of light, alpha, etc.
            // so turn on this flag.
            lightComputed = false;
            if (elevation)
            {
                color = ComputeBiomeColor(biome, elevation, 0);
            }
            else
            {
                color = gBiomes[biome].foliage;
            }
            break;
        case 3:
            // water, in swamp
            if ((biome & 0x7f) == SWAMPLAND_BIOME)
            {
                // We'll have to compute the effect of light, alpha, etc.
                // so turn on this flag.
                lightComputed = false;
                color = BiomeSwampRiverColor(color);
            }
            else
            {
                // normal water color
                lightComputed = true;
                color = gBlockColors[type * 16 + light];
            }
            break;
        }
    }

    // did we factor in the effect of the light? light == 15 means
    // fully lit, so nothing further needs to be done.
    if (!lightComputed)
    {
        // alphaComputed is true if we got the color directly from the pcolor;
        // in other words, alpha is already folded in. If not, we need to multiply by alpha.
        // It is examined only when lightComputed is false; when lightComputed
        // is true, alpha is already folded in.
        if (!alphaComputed && (gBlockDefinitions[type].alpha != 1.0))
        {
            r = (unsigned char)((color >> 16) & 0xff);
            g = (unsigned char)((color >> 8) & 0xff);
            b = (unsigned char)color & 0xff;
            alpha = gBlockDefinitions[type].alpha;
            r = (unsigned char)(r * alpha); //premultiply alpha
            g = (unsigned char)(g * alpha);
            b = (unsigned char)(b * alpha);
            color = (r << 16) | (g << 8) | b;
        }
        if (light != 15)
        {
            // compute effect of light
            double y, u, v;
            r = color >> 16;
            g = (color >> 8) & 0xff;
            b = color & 0xff;
            //we'll use YUV to darken the blocks.. gives a nice even
            //coloring
            y = 0.299 * r + 0.587 * g + 0.114 * b;
            u = (b - y) * 0.565;
            v = (r - y) * 0.713;

            y *= (double)light / 15.0;
            r = (unsigned int)clamp(y + 1.403 * v, 0, 255);
            g = (unsigned int)clamp(y - 0.344 * u - 0.714 * v, 0, 255);
            b = (unsigned int)clamp(y + 1.770 * u, 0, 255);
            color = (r << 16) | (g << 8) | b;
        }
    }

    return color;
}

// Draw a block at chunk bx,bz
// opts is a bitmask representing render options (see MinewaysMap.h)
// returns 16x16 set of block colors to use to render map.
// colors are adjusted by height, transparency, etc.
static unsigned char* draw(WorldGuide* pWorldGuide, int bx, int bz, int maxHeight, int mapMaxY, Options* pOpts, ProgressCallback callback, float percent, int* hitsFound, int mcVersion, int versionID, int& retCode)
{
    WorldBlock* block, * prevblock;
    int ofs = 0, prevy, prevSely, blockSolid, saveHeight;
    unsigned int voxel;
    //int hasSlime = 0;
    int x, z, i;
    unsigned int color, viewFilterFlags;
    unsigned short type;
    unsigned char r, g, b, seenempty;
    double alpha, blend;

    char useBiome, useElevation, cavemode, showobscured, depthshading, lighting, transparentWater, mapGrid, showAll;
    unsigned char* bits;

    retCode = 0;

    //    if ((pOpts->worldType&(HELL|ENDER|SLIME))==SLIME)
    //            hasSlime = isSlimeChunk(bx, bz);

    useBiome = !!(pOpts->worldType & BIOMES);
    cavemode = !!(pOpts->worldType & CAVEMODE);
    showobscured = !(pOpts->worldType & HIDEOBSCURED);
    useElevation = !!(pOpts->worldType & DEPTHSHADING);
    transparentWater = !!(pOpts->worldType & TRANSPARENT_WATER);
    mapGrid = !!(pOpts->worldType & MAP_GRID);
    showAll = !!(pOpts->worldType & SHOWALL);
    // use depthshading only if biome shading is off
    //depthshading= !useBiome && useElevation;
    depthshading = useElevation;
    lighting = !!(pOpts->worldType & LIGHTING);
    viewFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN |   // what's visible
        (showAll ? (BLF_FLATTEN_SMALL | BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD) : 0x0);

    block = (WorldBlock*)Cache_Find(bx, bz);

    if (block == NULL)
    {
        wcsncpy_s(pWorldGuide->directory, MAX_PATH_AND_FILE, pWorldGuide->world, MAX_PATH_AND_FILE - 1);
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, gSeparator);
        if (pOpts->worldType & HELL)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM-1/");
        }
        if (pOpts->worldType & ENDER)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM1/");
        }

        block = LoadBlock(pWorldGuide, bx, bz, mcVersion, versionID, retCode);
        if ((block == NULL) || (block->blockType == 2)) //blank tile
        {
            // highlighting off, or fully outside real area? Use blank tile.
            if (!gBoxHighlightUsed ||
                (bx * 16 + 15 < gBoxMinX) || (bx * 16 > gBoxMaxX) ||
                (bz * 16 + 15 < gBoxMinZ) || (bz * 16 > gBoxMaxZ))
                return gBlankTile;

            // fully inside? Use precomputed highlit area
            static int flux = 0;
            if ((bx * 16 > gBoxMinX) && (bx * 16 + 15 < gBoxMaxX) &&
                (bz * 16 > gBoxMinZ) && (bz * 16 + 15 < gBoxMaxZ))
                return gBlankHighlitTile;

            // draw the highlighted area
            memcpy(gBlankTransitionTile, gBlankTile, 16 * 16 * 4);
            // z increases south, decreases north
            for (z = 0; z < 16; z++)
            {
                // x increases west, decreases east
                for (x = 0; x < 16; x++)
                {
                    int offset = (z * 16 + x) * 4;
                    // make selected area slightly red
                    if (bx * 16 + x >= gBoxMinX && bx * 16 + x <= gBoxMaxX &&
                        bz * 16 + z >= gBoxMinZ && bz * 16 + z <= gBoxMaxZ)
                    {
                        // blend in highlight color
                        blend = gHalpha;
                        // are we on a border? If so, change blend factor
                        if (bx * 16 + x == gBoxMinX || bx * 16 + x == gBoxMaxX ||
                            bz * 16 + z == gBoxMinZ || bz * 16 + z == gBoxMaxZ)
                        {
                            blend = gHalphaBorder;
                        }
                        gBlankTransitionTile[offset++] = (unsigned char)((double)gBlankTransitionTile[offset] * (1.0 - blend) + blend * (double)gHred);
                        gBlankTransitionTile[offset++] = (unsigned char)((double)gBlankTransitionTile[offset] * (1.0 - blend) + blend * (double)gHgreen);
                        gBlankTransitionTile[offset] = (unsigned char)((double)gBlankTransitionTile[offset] * (1.0 - blend) + blend * (double)gHblue);
                    }
                }
            }
            return gBlankTransitionTile;
        }

        //let's only update the progress bar if we're loading
        if (callback)
            callback(percent);

        Cache_Add(bx, bz, block);
    }

    // At this point the block is loaded.

    // Is the block partially or fully inside the dirty area?
    bool isOnOrInside = (bx * 16 + 15 >= gDirtyBoxMinX && bx * 16 <= gDirtyBoxMaxX &&
        bz * 16 + 15 >= gDirtyBoxMinZ && bz * 16 <= gDirtyBoxMaxZ);

    // already rendered?
    if (block->rendery == maxHeight && block->renderopts == pOpts->worldType && block->colormap == gColormap)
    {
        if (block->rendermissing // wait, the last render was incomplete
            && Cache_Find(bx, bz + block->rendermissing) != NULL) {
            ; // we can do a better render now that the missing block is loaded
        }
        else {
            // Yes, it's been rendered, but now we need to check if the highlight number is OK:
            // If the area is inside the highlighted region, renderhilitID==gHighlightID.
            // If the area is outside the hightlighted region, renderhilitID==0.
            // Else the area should be redrawn.
            // final check, is highlighting state OK?
            if (((block->renderhilitID == gHighlightID) && isOnOrInside) ||
                ((block->renderhilitID == 0) && !isOnOrInside))
            {
                // there's no need to re-render, use cached image already generated
                return block->rendercache;
            }
            // else re-render, to clean up previous highlight
        }
    }

    block->rendery = maxHeight;
    block->renderopts = pOpts->worldType;
    // if the block to be drawn is inside, note the ID, else note it's "clean" of highlighting;
    // when we come back next time, the code above will note the rendering is OK.
    block->renderhilitID = isOnOrInside ? gHighlightID : 0;
    block->rendermissing = 0;
    block->colormap = gColormap;

    bits = block->rendercache;

    // find the block to the west, so we can use its heightmap for shading
    prevblock = (WorldBlock*)Cache_Find(bx - 1, bz);

    if (prevblock == NULL)
        block->rendermissing = 1; //note no loaded block to west
    else if (prevblock->rendery != maxHeight || prevblock->renderopts != pOpts->worldType) {
        block->rendermissing = 1; //note improperly rendered block to west
        prevblock = NULL; //block was rendered at a different y level, ignore
    }
    // z increases south, decreases north
    for (z = 0; z < 16; z++)
    {
        // prevy is the height of the block to the left (west) of the current block, for shadowing.
        // Note it is set to the previous y height for the loop below.
        if (prevblock != NULL)
            prevy = prevblock->heightmap[15 + z * 16];
        else
            prevy = -1;

        // x increases west, decreases east
        for (x = 0; x < 16; x++)
        {
            prevSely = -1;
            saveHeight = -1; // the "not found" value.
            bool hitGrid = false;

            voxel = ((maxHeight * 16 + z) * 16 + x);
            r = gEmptyR;
            g = gEmptyG;
            b = gEmptyB;
            // if we start at the top of the world, seenempty is set to 1 (there's air above), else 0
            // The idea here is that if you're delving into cave areas, "hide obscured" will treat all
            // blocks at the topmost layer as empty, until a truly empty block is hit, at which point
            // the next solid block is then shown. If it's solid all the way down, the block will be
            // drawn as "empty"
            seenempty = (maxHeight == mapMaxY ? 1 : 0);
            alpha = 0.0;
            // go from top down through all voxels, looking for the first one visible.
            for (i = maxHeight; i >= 0; i--, voxel -= 16 * 16)
            {
                type = retrieveType(block, voxel);
                // if block is air or something very small, or water when transparent water is flagged, note it's empty and continue to next voxel
                if ((type == BLOCK_AIR) ||
                    !(gBlockDefinitions[type].flags & viewFilterFlags) ||
                    (transparentWater && (type == BLOCK_STATIONARY_WATER || type == BLOCK_WATER)))
                {
                    seenempty = 1;
                    continue;
                }

                // special selection height: we want to be able to select water
                float currentAlpha = gBlockDefinitions[type].alpha;
                // water is considered "solid"
                blockSolid = (currentAlpha != 0.0f); // ((type<NUM_BLOCKS_MAP) || (type ==255)) && 
                if ((showobscured || seenempty) && blockSolid)
                    if (prevSely == -1)
                        prevSely = i;

                // non-flowing water does not count when finding the displayed height, so that we can reveal what is
                // underneath the water.
                if (type == BLOCK_STATIONARY_WATER)
                    seenempty = 1;

                // if showobscured is on, or voxel is air or water (seenempty)
                // AND the voxel id is valid (in our array of known values)
                // AND it's not entirely transparent, then process it
                if ((showobscured || seenempty) && blockSolid)
                {
                    int light = 12;
                    if (lighting)
                    {
                        if (i < mapMaxY)
                        {
                            light = block->light[voxel / 2];
                            if (voxel & 1) light >>= 4;
                            light &= 0xf;
                        }
                        else
                        {
                            light = 0;
                        }
                    }
                    // if it's the first voxel visible (i.e., there was no block at all to the west), note this depth.
                    if (prevy == -1)
                        prevy = i;
                    else if (prevy < i)   // fully lit on west side of block?
                        light += 2;
                    else if (prevy > i)   // in shadow?
                        light -= 5;
                    light = clamp(light, 1, 15);

                    // Here is where the color of the block is retrieved.
                    // First we check if there's a special color for this block,
                    // such as for wool, terracotta, carpet, etc. If not, then
                    // we can look the quick lookup value from the table.
                    color = checkSpecialBlockColor(block, voxel, type, light, useBiome, useElevation);

                    // is this the first block encountered?
                    if (alpha == 0.0)
                    {
                        // yes; since there's no accumulated alpha, simply substitute the values into place;
                        // note that semi-transparent values already have their alpha multiplied in.
                        saveHeight = i;
                        alpha = currentAlpha;
                        r = (unsigned char)(color >> 16);
                        g = (unsigned char)((color >> 8) & 0xff);
                        b = (unsigned char)(color & 0xff);
                    }
                    else
                    {
                        // Else need to blend in this color with the previous.
                        // This is an "under" operation, putting the new color under the previous
                        // accumulated alpha
                        r += (unsigned char)((1.0 - alpha) * (color >> 16));
                        g += (unsigned char)((1.0 - alpha) * ((color >> 8) & 0xff));
                        b += (unsigned char)((1.0 - alpha) * (color & 0xff));
                        alpha += currentAlpha * (1.0 - alpha);
                    }
                    // if the current block's color is fully opaque, finish.
                    if (currentAlpha == 1.0f)
                        break;
                }
            }

            // The solid location (or none at all, in which case -1 is set) is saved here.
            // If everything is visible, then the height map will store the highest object found,
            // transparent or solid. This will tend to darken the water, since it will now "shadow"
            // itself. Note that prevy is used in the methods below, but for showAll it's good to have
            // this higher height for these purposes.
            prevy = showAll ? saveHeight : i;

            if (depthshading && prevy >= 0) // darken deeper blocks
            {
                // 50 kicks up the minimum darkness returned, so that it's not black.
                // Note that setting the upper height of the selection box affects this view.
                int num = prevy + 50 - (256 - maxHeight) / 5;
                int denom = maxHeight + 50 - (256 - maxHeight) / 5;

                r = (unsigned char)(r * num / denom);
                g = (unsigned char)(g * num / denom);
                b = (unsigned char)(b * num / denom);
            }

            //if (depthshading) // add contours on natural blocks
            //{
            //	if ( prevy % 5 == 0 )
            //	{
            //		alpha = 0.7;
            //		r=(unsigned char)(255.0 * alpha + r*(1.0 - alpha));
            //		g=(unsigned char)(255.0 * alpha + g*(1.0 - alpha));
            //		b=(unsigned char)(255.0 * alpha + b*(1.0 - alpha));
            //	}
            //}

            //if(hasSlime > 0){
            //    // before 0.9 Pre 5 it was 16, see http://www.minecraftwiki.net/wiki/Slime
            //    //if(maxHeight<=16){
            //    if(maxHeight<=40){
            //        g=clamp(g+20,mapMinHeight,mapMaxHeight);
            //    }else{
            //        if(x%15==0 || z%15==0){
            //            g=clamp(g+20,mapMinHeight,mapMaxHeight);
            //        }
            //    }
            //}

            if (cavemode && prevy >= 0)
            {
                seenempty = 0;
                type = retrieveType(block, voxel);

                if (type == BLOCK_LEAVES || type == BLOCK_LOG || type == BLOCK_AD_LEAVES || type == BLOCK_AD_LOG) //special case surface trees
                    for (; i >= 1; i--, voxel -= 16 * 16, type = retrieveType(block, voxel))
                        if (!(type == BLOCK_LOG || type == BLOCK_LEAVES || type == BLOCK_AD_LEAVES || type == BLOCK_AD_LOG || type == BLOCK_AIR))
                            break; // skip leaves, wood, air

                for (; i >= 1; i--, voxel -= 16 * 16)
                {
                    type = retrieveType(block, voxel);
                    if (type == BLOCK_AIR)
                    {
                        seenempty = 1;
                        continue;
                    }
                    if (seenempty && gBlockDefinitions[type].alpha != 0.0) // ((type<NUM_BLOCKS_MAP) || (type ==255)) &&
                    {
                        r = (unsigned char)(r * (prevy - i + 10) / 138);
                        g = (unsigned char)(g * (prevy - i + 10) / 138);
                        b = (unsigned char)(b * (prevy - i + 10) / 138);
                        break;
                    }
                }
            }

            // all the above is needed for various reasons, but blast in map grid if needed
            if (mapGrid && (x == 0 || z == 0)) {
                if (((bx % 32) == 0 && x == 0) || ((bz % 32) == 0 && z == 0)) {
                    // bright MCA line
                    r = 0;
                    g = 255;
                    b = 255;
                }
                else {
                    // normal chunk line
                    r = 0;
                    g = 200;
                    b = 200;
                }
                hitGrid = true;
            }

            if (gBoxHighlightUsed) {
                // make selected area slightly red, if at right heightmap range
                if (bx * 16 + x >= gBoxMinX && bx * 16 + x <= gBoxMaxX &&
                    bz * 16 + z >= gBoxMinZ && bz * 16 + z <= gBoxMaxZ)
                {
                    // test and save minimum height found
                    if (prevSely >= 0 && prevSely < hitsFound[3])
                    {
                        // the minimum visible selected height found so far
                        hitsFound[3] = prevSely;
                    }

                    // in bounds, is the height good?
                    // First case is for if we hit nothing, all void, so it's black:
                    // always highlight that area, just for readability.
                    if ((prevSely == -1) || (prevSely >= gBoxMinY && prevSely <= gBoxMaxY))
                    {
                        hitsFound[1] = 1;
                        // blend in highlight color
                        blend = gHalpha;
                        // are we on a border? If so, change blend factor
                        if (prevSely == gBoxMinY || prevSely == gBoxMaxY ||
                            bx * 16 + x == gBoxMinX || bx * 16 + x == gBoxMaxX ||
                            bz * 16 + z == gBoxMinZ || bz * 16 + z == gBoxMaxZ)
                        {
                            blend = gHalphaBorder;
                        }
                        r = (unsigned char)((double)r * (1.0 - blend) + blend * (double)gHred);
                        g = (unsigned char)((double)g * (1.0 - blend) + blend * (double)gHgreen);
                        b = (unsigned char)((double)b * (1.0 - blend) + blend * (double)gHblue);
                    }
                    else if (prevSely < gBoxMinY)
                    {
                        hitsFound[0] = 1;
                        // lower than selection box, so if exactly on border, dim
                        if (bx * 16 + x == gBoxMinX || bx * 16 + x == gBoxMaxX ||
                            bz * 16 + z == gBoxMinZ || bz * 16 + z == gBoxMaxZ)
                        {
                            double dim = 0.5;
                            r = (unsigned char)((double)r * dim);
                            g = (unsigned char)((double)g * dim);
                            b = (unsigned char)((double)b * dim);
                        }
                    }
                    else
                    {
                        hitsFound[2] = 1;
                        // higher than selection box, so if exactly on border, brighten
                        // - I don't think it's actually possible to hit this condition,
                        // as the area above the selection box should never be seen (the
                        // slider sets the maximum), but just in case things change...
                        if (bx * 16 + x == gBoxMinX || bx * 16 + x == gBoxMaxX ||
                            bz * 16 + z == gBoxMinZ || bz * 16 + z == gBoxMaxZ)
                        {
                            double brighten = 0.5;
                            r = (unsigned char)((double)r * (1.0 - brighten) + brighten);
                            g = (unsigned char)((double)g * (1.0 - brighten) + brighten);
                            b = (unsigned char)((double)b * (1.0 - brighten) + brighten);
                        }
                    }
                }
            }

            if (prevy == -1 && !hitGrid) {
                // empty, so make it background color to start
                unsigned char* clr = &gBlankTile[(x + z * 16) * 4];
                r = *clr++;
                g = *clr++;
                b = *clr; // ++ if you add alpha
                // highlight the block if in selected area, as otherwise it looks like it's missing with schematics.
                // Make selected area slightly red
                if (gBoxHighlightUsed &&
                    bx * 16 + x >= gBoxMinX && bx * 16 + x <= gBoxMaxX &&
                    bz * 16 + z >= gBoxMinZ && bz * 16 + z <= gBoxMaxZ)
                {
                    // blend in highlight color
                    blend = gHalpha;
                    // are we on a border? If so, change blend factor
                    if (bx * 16 + x == gBoxMinX || bx * 16 + x == gBoxMaxX ||
                        bz * 16 + z == gBoxMinZ || bz * 16 + z == gBoxMaxZ)
                    {
                        blend = gHalphaBorder;
                    }
                    r = (unsigned char)((double)r * (1.0 - blend) + blend * (double)gHred);
                    g = (unsigned char)((double)g * (1.0 - blend) + blend * (double)gHgreen);
                    b = (unsigned char)((double)b * (1.0 - blend) + blend * (double)gHblue);
                }
            }

            // to make the map look like a heightfield instead
            //#define HEIGHTMAP
#ifdef HEIGHTMAP
            if (depthshading) {
                r = g = b = (unsigned char)((prevy >= 0) ? prevy : 0);
            }
#endif

            bits[ofs++] = r;
            bits[ofs++] = g;
            bits[ofs++] = b;
            bits[ofs++] = 0xff;

            // heightmap determines what value is displayed on status and for shadowing. If "show all" is on,
            // save any semi-visible thing, else save the first solid thing (or possibly nothing == -1).
            block->heightmap[x + z * 16] = (prevy < 0) ? EMPTY_HEIGHT : (short)prevy;
        }
    }
    return bits;
}

#define BLOCK_INDEX(x,topy,z) (  ((topy)*256)+ \
    ((z)*16) + \
    (x)  )

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Generate test blocks for test world.
// testBlock checks to see whether this block and data value exists and should be output,
// and whether any neighboring blocks should be output for testing. Sometimes the data value is
// used as a guide for where to put neighbors instead of as a data value, so that testing is more thorough.
void testBlock(WorldBlock* block, int origType, int y, int dataVal)
{
    int bi = 0;
    int trimVal;
    int addBlock = 0;

    assert(dataVal < 16);
    int finalDataVal = dataVal;

    int type = origType;
    if (origType > 255) {
        // how we signal a block type is > 255.
        //finalDataVal |= HIGH_BIT; - now done at end
        type &= 0xFF;
    }

    switch (origType)
    {
    default:
        if (dataVal == 0)
        {
            //block->grid[BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8)] = (unsigned char)type;
            addBlock = 1;
        }
        break;
    case BLOCK_SAND:
    case BLOCK_TNT:
    case BLOCK_WOODEN_PRESSURE_PLATE:
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_SPRUCE_PRESSURE_PLATE:
    case BLOCK_BIRCH_PRESSURE_PLATE:
    case BLOCK_JUNGLE_PRESSURE_PLATE:
    case BLOCK_ACACIA_PRESSURE_PLATE:
    case BLOCK_DARK_OAK_PRESSURE_PLATE:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY:
    case BLOCK_CRIMSON_PRESSURE_PLATE:
    case BLOCK_WARPED_PRESSURE_PLATE:
    case BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE:
    case BLOCK_AD_LEAVES:
    case BLOCK_SPONGE:
    case BLOCK_SOUL_SAND:
    case BLOCK_GLOWSTONE:
    case BLOCK_NETHER_WART_BLOCK:
        // uses 0-1
        if (dataVal < 2)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_FIRE:
        // uses 0-1
        if (dataVal < 2)
        {
            addBlock = 1;
            if (dataVal == 1) {
                // soul fire
                finalDataVal = BIT_16;
            }
        }
        break;

    case BLOCK_GRASS_BLOCK:
        // uses 0,8 - snowy
        if ((dataVal & 0x7) == 0)
        {
            addBlock = 1;
        }
        break;

    case BLOCK_SANDSTONE:
    case BLOCK_RED_SANDSTONE:
    case BLOCK_PRISMARINE:
    case BLOCK_NETHER_BRICKS:
    case BLOCK_RED_MUSHROOM:
        // uses 0-2
        if (dataVal < 3)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_LEAVES:
    case BLOCK_NETHER_WART:
    case BLOCK_STONE_BRICKS:
    case BLOCK_CAULDRON:
    case BLOCK_FROSTED_ICE:
    case BLOCK_STRUCTURE_BLOCK:
    case BLOCK_GLAZED_TERRACOTTA:
    case BLOCK_GLAZED_TERRACOTTA + 1:
    case BLOCK_GLAZED_TERRACOTTA + 2:
    case BLOCK_GLAZED_TERRACOTTA + 3:
    case BLOCK_GLAZED_TERRACOTTA + 4:
    case BLOCK_GLAZED_TERRACOTTA + 5:
    case BLOCK_GLAZED_TERRACOTTA + 6:
    case BLOCK_GLAZED_TERRACOTTA + 7:
    case BLOCK_GLAZED_TERRACOTTA + 8:
    case BLOCK_GLAZED_TERRACOTTA + 9:
    case BLOCK_GLAZED_TERRACOTTA + 10:
    case BLOCK_GLAZED_TERRACOTTA + 11:
    case BLOCK_GLAZED_TERRACOTTA + 12:
    case BLOCK_GLAZED_TERRACOTTA + 13:
    case BLOCK_GLAZED_TERRACOTTA + 14:
    case BLOCK_GLAZED_TERRACOTTA + 15:
    case BLOCK_SMOOTH_STONE:
    case BLOCK_SWEET_BERRY_BUSH:
    case BLOCK_STONECUTTER:
    case BLOCK_LECTERN:
        // uses 0-3
        if (dataVal < 4)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_BEETROOT_SEEDS:
        // uses 0-3, put farmland beneath it
        if (dataVal < 4)
        {
            addBlock = 1;
            // add farmland underneath
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y - 1, 4 + (dataVal % 2) * 8)] = BLOCK_FARMLAND;
        }
        break;
    case BLOCK_HEAD:
        // uses 2-5
        if (dataVal >= 2 && dataVal <= 5)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_CRAFTING_TABLE:
    case BLOCK_PUMPKIN:
    case BLOCK_JACK_O_LANTERN:
    case BLOCK_CORAL_BLOCK:
    case BLOCK_DEAD_CORAL_BLOCK:
    case BLOCK_CRIMSON_DOUBLE_SLAB:
    case BLOCK_RESPAWN_ANCHOR:
        // uses 0-4
        if (dataVal < 5)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_CORAL:
    case BLOCK_CORAL_FAN:
    case BLOCK_DEAD_CORAL_FAN:
    case BLOCK_DEAD_CORAL:
        // uses 0-9
        if (dataVal < 10)
        {
            addBlock = 1;
            if (dataVal >= 5) {
                finalDataVal = (dataVal-5) | WATERLOGGED_BIT;	// waterlogged
            }
        }
        break;
    case BLOCK_DIRT:
        // uses 0-4, 10 for podzol with snow
        if (dataVal < 5 || dataVal == 10)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_GRASS:
    case BLOCK_WOODEN_DOUBLE_SLAB:
    case BLOCK_CAKE:
    case BLOCK_QUARTZ_BLOCK:
    case BLOCK_INFESTED_STONE:
    case BLOCK_END_ROD:
    case BLOCK_CHORUS_FLOWER:
    case BLOCK_OBSERVER:	// could also have top bit "fired", but no graphical effect
    case BLOCK_ANDESITE_DOUBLE_SLAB:
    case BLOCK_BAMBOO:
    case BLOCK_JIGSAW:
        // uses 0-5 - could use more for 1.16 orientations, TODO
        if (dataVal < 6)
        {
            addBlock = 1;
            // for just chorus flower, put endstone below
            if (type == BLOCK_CHORUS_FLOWER)
            {
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y - 1, 4 + (dataVal % 2) * 8)] = BLOCK_END_STONE;
            }
        }
        break;
    case BLOCK_CRIMSON_SLAB:
        // uses 0-4 and 8-12 for different slab types + lower or upper
        if (dataVal < 5 || (dataVal >= 8 && dataVal <= 12))
        {
            addBlock = 1;
        }
        break;
    case BLOCK_WOODEN_SLAB:
    case BLOCK_ANDESITE_SLAB:
        // uses 0-5 and 8-13 for different slab types + lower or upper
        if (dataVal < 6 || (dataVal >= 8 && dataVal <= 13))
        {
            addBlock = 1;
        }
        break;
    case BLOCK_DOUBLE_FLOWER:
        // uses 0-5, put flower head above it
        if (dataVal < 6)
        {
            addBlock = 1;
            // add flower above
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = BLOCK_DOUBLE_FLOWER;
            // not entirely sure about this number, but 10 seems to be the norm,
            // but https://minecraft.fandom.com/wiki/Flower#Data_values says to use 8
            // Note that the top half is made to be the dataVal, too, which is the modern
            // way that nbt.cpp reads in this data. 1.12 and earlier would not have this set,
            // but this artificial map is considered modern.
            block->data[bi] = (unsigned char)(dataVal | 8);
        }
        break;
    case BLOCK_SAPLING:	// now with bamboo
        // uses 0-6
        if (dataVal < 7)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_OAK_PLANKS:
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
    case BLOCK_OAK_WOOD_STAIRS:
    case BLOCK_SPRUCE_WOOD_STAIRS:
    case BLOCK_BIRCH_WOOD_STAIRS:
    case BLOCK_JUNGLE_WOOD_STAIRS:
    case BLOCK_QUARTZ_STAIRS:
    case BLOCK_SNOW:
    case BLOCK_END_PORTAL_FRAME:
    case BLOCK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
    case BLOCK_CRIMSON_FENCE_GATE:
    case BLOCK_WARPED_FENCE_GATE:
    case BLOCK_FARMLAND:
    case BLOCK_BREWING_STAND:
    case BLOCK_ACACIA_WOOD_STAIRS:
    case BLOCK_DARK_OAK_WOOD_STAIRS:
    case BLOCK_RED_SANDSTONE_STAIRS:
    case BLOCK_PURPUR_STAIRS:
    case BLOCK_PRISMARINE_STAIRS:
    case BLOCK_PRISMARINE_BRICK_STAIRS:
    case BLOCK_DARK_PRISMARINE_STAIRS:
    case BLOCK_RED_SANDSTONE_DOUBLE_SLAB:
    case BLOCK_PURPUR_DOUBLE_SLAB:
    case BLOCK_COBBLESTONE_STAIRS:
    case BLOCK_BRICK_STAIRS:
    case BLOCK_STONE_BRICK_STAIRS:
    case BLOCK_NETHER_BRICK_STAIRS:
    case BLOCK_SANDSTONE_STAIRS:
    case BLOCK_STONE_STAIRS:
    case GRANITE_STAIRS:
    case POLISHED_GRANITE_STAIRS:
    case SMOOTH_QUARTZ_STAIRS:
    case DIORITE_STAIRS:
    case POLISHED_DIORITE_STAIRS:
    case END_STONE_BRICK_STAIRS:
    case ANDESITE_STAIRS:
    case POLISHED_ANDESITE_STAIRS:
    case RED_NETHER_BRICK_STAIRS:
    case MOSSY_STONE_BRICK_STAIRS:
    case MOSSY_COBBLESTONE_STAIRS:
    case SMOOTH_SANDSTONE_STAIRS:
    case SMOOTH_RED_SANDSTONE_STAIRS:
    case CRIMSON_STAIRS:
    case WARPED_STAIRS:
    case BLACKSTONE_STAIRS:
    case POLISHED_BLACKSTONE_STAIRS:
    case POLISHED_BLACKSTONE_BRICK_STAIRS:
        // uses 0-7 - TODO we could someday add more blocks to neighbor the others, in order to show the "step block trim" feature of week 39
        if (dataVal < 8)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_WHEAT:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES:
        // uses 0-7, put farmland beneath it
        if (dataVal < 8)
        {
            addBlock = 1;
            // add farmland underneath
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y - 1, 4 + (dataVal % 2) * 8)] = BLOCK_FARMLAND;
        }
        break;
    case BLOCK_COMPOSTER:
        // uses 0-8
        if (dataVal < 9)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_STONE_DOUBLE_SLAB:
        // uses 0-7, F (15)
        if (dataVal < 8 || dataVal == 15)
        {
            addBlock = 1;
        }
        break;

    case BLOCK_HUGE_BROWN_MUSHROOM:
    case BLOCK_HUGE_RED_MUSHROOM:
        // uses 0-10
        if (dataVal < 11)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_POPPY:
        // uses 0-11
        if (dataVal < 12)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_FLOWER_POT:
        // uses 0-11 for old-style 1.7 flower pots; 12 and 13 for acacia and dark oak saplings, whenever those were added
        if (dataVal < 14)
        {
            addBlock = 1;
        }
        // add new style diagonally SE of original
        {
            int neighborIndex = BLOCK_INDEX(5 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
            int neighborIndex2 = BLOCK_INDEX(6 + (type % 2) * 8, y, 6 + (dataVal % 2) * 8);
            switch (dataVal) {
            case 1:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = SAPLING_FIELD | 0;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 7;
                break;
            case 2:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = SAPLING_FIELD | 1;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 8;
                break;
            case 3:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = SAPLING_FIELD | 2;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_MUSHROOM_FIELD | 0;
                break;
            case 4:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = SAPLING_FIELD | 3;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = BROWN_MUSHROOM_FIELD | 0;
                break;
            case 5:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = SAPLING_FIELD | 4;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = DEADBUSH_FIELD | 0;
                break;
            case 6:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = SAPLING_FIELD | 5;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = CACTUS_FIELD | 0;
                break;
            case 7:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = TALLGRASS_FIELD | 2;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 9;
                break;
            case 8:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = YELLOW_FLOWER_FIELD | 0;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 10;
                break;
            case 9:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 0;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 11;
                break;
            case 10:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 1;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = BAMBOO_FIELD | 0;
                break;
            case 11:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 2;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 12;
                break;
            case 12:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 3;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 13;
                break;
            case 13:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 4;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 14;
                break;
            case 14:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 5;
                block->grid[neighborIndex2] = BLOCK_FLOWER_POT;
                block->data[neighborIndex2] = RED_FLOWER_FIELD | 15;
                break;
            case 15:
                block->grid[neighborIndex] = BLOCK_FLOWER_POT;
                block->data[neighborIndex] = RED_FLOWER_FIELD | 6;
                break;
            }
        }
        break;
    case BLOCK_ANVIL:
    case BLOCK_TURTLE_EGG: // number is 0-3, hatch is 0-2, total of 12
        // uses 0-11
        if (dataVal < 12)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_BONE_BLOCK:
        // uses 0,1,2 for low bits in 0x3
        if ((dataVal & 0x3) < 3)
        {
            addBlock = 1;
        }
        break;

        // TODO: fan in different directions
    case BLOCK_CORAL_WALL_FAN:
    case BLOCK_DEAD_CORAL_WALL_FAN:
        // uses 0-15 - no waterlogging (no room!)
    {
        int coralVal = dataVal % 5;	// 0-4 types of coral - lowest three bits 123 (4 is unused)
        int rotVal = (dataVal - coralVal) / 5; // rotate 0-3
        finalDataVal = (0x80 | coralVal | (((rotVal + 3) % 4) << 4));	// put into bits 56
        addBlock = 1;
        // add attached block
        switch (rotVal)
        {
        case 3:	// not actually used
            // put block to south
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 1:
            // put block to north
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 2:
            // put block to east
            block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 0:
            // put block to west
            block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        }
    }
    break;
    case BLOCK_STONE:
    case BLOCK_LOG:	// really just 12, but we pay attention to directionless
    case BLOCK_STRIPPED_OAK:
    case BLOCK_STRIPPED_OAK_WOOD:
    case BLOCK_AD_LOG:
    case BLOCK_STRIPPED_ACACIA:
    case BLOCK_STRIPPED_ACACIA_WOOD:
    case BLOCK_STONE_SLAB:
    case BLOCK_REDSTONE_REPEATER_OFF:
    case BLOCK_REDSTONE_REPEATER_ON:
    case BLOCK_REDSTONE_COMPARATOR:
    case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
    case BLOCK_COLORED_TERRACOTTA:
    case BLOCK_CARPET:
    case BLOCK_STAINED_GLASS:
    case BLOCK_STANDING_BANNER:
    case BLOCK_WOOL:
    case BLOCK_CONCRETE:
    case BLOCK_CONCRETE_POWDER:
    case BLOCK_ORANGE_BANNER:
    case BLOCK_MAGENTA_BANNER:
    case BLOCK_LIGHT_BLUE_BANNER:
    case BLOCK_YELLOW_BANNER:
    case BLOCK_LIME_BANNER:
    case BLOCK_PINK_BANNER:
    case BLOCK_GRAY_BANNER:
    case BLOCK_LIGHT_GRAY_BANNER:
    case BLOCK_CYAN_BANNER:
    case BLOCK_PURPLE_BANNER:
    case BLOCK_BLUE_BANNER:
    case BLOCK_BROWN_BANNER:
    case BLOCK_GREEN_BANNER:
    case BLOCK_RED_BANNER:
    case BLOCK_BLACK_BANNER:
    case BLOCK_RED_SANDSTONE_SLAB:
    case BLOCK_PURPUR_SLAB:
    case BLOCK_CAMPFIRE:
        // uses all bits, 0-15
        addBlock = 1;
        break;
    case BLOCK_SIGN_POST:
    case BLOCK_ACACIA_SIGN_POST:
        // uses all bits, 0-15, with variations to show other styles
        addBlock = 1;
        {
            // add new style diagonally SE of original
            int neighborIndex = BLOCK_INDEX(5 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
            block->grid[neighborIndex] = (unsigned char)type;
            block->data[neighborIndex] = (unsigned char)finalDataVal | BIT_16;

            int neighborIndex2 = BLOCK_INDEX(6 + (type % 2) * 8, y, 6 + (dataVal % 2) * 8);
            block->grid[neighborIndex2] = (unsigned char)type;
            block->data[neighborIndex2] = (unsigned char)finalDataVal | BIT_32;

            int neighborIndex3 = BLOCK_INDEX(7 + (type % 2) * 8, y, 7 + (dataVal % 2) * 8);
            block->grid[neighborIndex3] = (unsigned char)type;
            block->data[neighborIndex3] = (unsigned char)finalDataVal | BIT_32 | BIT_16;
        }
        break;
    case BLOCK_WATER:
    case BLOCK_STATIONARY_WATER:
    case BLOCK_LAVA:
    case BLOCK_STATIONARY_LAVA:
        // water has no data value normally (bubble column is only exception)
        finalDataVal = 0;
        // uses 0-8, with 8 giving one above
        if (dataVal <= 8)
        {
            addBlock = 1;

            if (dataVal == 8)
            {
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = (unsigned char)type;
            }
            else if (dataVal > 0)
            {
                int x = type % 2;
                int z = !x;
                block->grid[BLOCK_INDEX(x + 4 + (type % 2) * 8, y, z + 4 + (dataVal % 2) * 8)] = (unsigned char)type;
            }
        }
        else if (type == BLOCK_STATIONARY_WATER) {
            // for stationary water, bubble column is put at bottom
            if (dataVal == 15) {
                addBlock = 1;
                finalDataVal = BIT_16 | 8;
                // put block above, if we want the block below to go fully to the top:
                //block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = (unsigned char)type;
            }
        }
        break;
    case BLOCK_ENDER_CHEST:
        // uses 2-5
        if (dataVal >= 2 && dataVal <= 5)
        {
            addBlock = 1;
        }
        else if (dataVal >= 6 && dataVal <= 9)
        {
            addBlock = 1;
            finalDataVal = (dataVal - 4) | WATERLOGGED_BIT;	// waterlogged
        }
        break;
    case BLOCK_FURNACE:
        // uses 0-15, remapping dataVal to the four facings of the furnace, loom, smoker, and blast furnace 
        addBlock = 1;
        finalDataVal = ((dataVal & 0xC) << 2) + (dataVal & 0x3) + 2;
        break;
    case BLOCK_BURNING_FURNACE:
        // uses 0-3 and 8-15, remapping dataVal to the four facings of the furnace, loom, smoker, and blast furnace
        if (dataVal < 4 || dataVal >= 8) {
            addBlock = 1;
            finalDataVal = ((dataVal & 0xC) << 2) + (dataVal & 0x3) + 2;
        }
        break;
    case BLOCK_DISPENSER:
    case BLOCK_DROPPER:
        if (dataVal <= 5)
        {
            addBlock = 1;
            switch (dataVal)
            {
            case 0:
            case 1:
                // make the block itself be up by two, so we can examine its top and bottom
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 2, 4 + (dataVal % 2) * 8);
                block->grid[bi] = (unsigned char)type;
                block->data[bi] = (unsigned char)dataVal;
                addBlock = 0;
                break;
            }
        }
        break;

    case BLOCK_HOPPER:
        // uses 0,1, 2-5
        if (dataVal <= 5)
        {
            addBlock = 1;
            switch (dataVal)
            {
            case 0:
                // make the block itself be up by two, so we can examine its top and bottom
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 2, 4 + (dataVal % 2) * 8);
                block->grid[bi] = (unsigned char)type;
                block->data[bi] = (unsigned char)dataVal;
                addBlock = 0;
                break;
            case 1:
                // put block above
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 2:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            }
        }
        break;

    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
    case BLOCK_SOUL_TORCH:
        if (dataVal >= 1 && dataVal <= 5)
        {
            addBlock = 1;
            switch (dataVal)
            {
            case 1:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 2:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            default:
                // do nothing - on ground
                break;
            }
        }
        break;
    case BLOCK_LADDER:
        if ((dataVal & 0x7) >= 2 && (dataVal & 0x7) <= 5)
        {
            addBlock = 1;
            switch (dataVal & 0x7)
            {
            case 2:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            }
            finalDataVal = (dataVal & 0x7) | ((dataVal >= 8) ? WATERLOGGED_BIT : 0x0);
        }
        break;
    case BLOCK_WALL_BANNER:
    case BLOCK_ORANGE_WALL_BANNER:
    case BLOCK_MAGENTA_WALL_BANNER:
    case BLOCK_LIGHT_BLUE_WALL_BANNER:
    case BLOCK_YELLOW_WALL_BANNER:
    case BLOCK_LIME_WALL_BANNER:
    case BLOCK_PINK_WALL_BANNER:
    case BLOCK_GRAY_WALL_BANNER:
    case BLOCK_LIGHT_GRAY_WALL_BANNER:
    case BLOCK_CYAN_WALL_BANNER:
    case BLOCK_PURPLE_WALL_BANNER:
    case BLOCK_BLUE_WALL_BANNER:
    case BLOCK_BROWN_WALL_BANNER:
    case BLOCK_GREEN_WALL_BANNER:
    case BLOCK_RED_WALL_BANNER:
    case BLOCK_BLACK_WALL_BANNER:
        if (dataVal >= 2 && dataVal <= 5)
        {
            addBlock = 1;
            switch (dataVal)
            {
            case 2:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            }
        }
        break;

    case BLOCK_WALL_SIGN:
        // there are now 6 materials for wall signs. Rather than going absolutely nuts, we change the dataVal for each
        if ((dataVal & 0x7) <= 5)
        {
            addBlock = 1;
            // set higher bits BIT_8 and BIT_16
            finalDataVal = ((dataVal % 6) << 3) | (dataVal & 0x7);
            switch (dataVal & 0x7)
            {
                // do all the wood types
            case 0:
            case 1:
            case 2:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            }
        }
        break;

    case BLOCK_RAIL:
        if (dataVal >= 6 && dataVal <= 9)
        {
            addBlock = 1;
            break;
        }
        // test if too high - if so, ignore
        else if (dataVal > 9)
        {
            break;
        } // else:
        // falls through on 0 through 5, since these are handled below for all rails
    case BLOCK_POWERED_RAIL:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        trimVal = dataVal & 0x7;
        if (trimVal <= 5)
        {
            addBlock = 1;
            switch (trimVal)
            {
            case 2:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            default:
                // do nothing - on ground
                break;
            }
        }
        break;
    case BLOCK_LEVER:
        trimVal = dataVal & 0x7;
        addBlock = 1;
        switch (dataVal & 0x7)
        {
        case 1:
            // put block to west
            block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 2:
            // put block to east
            block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 3:
            // put block to north
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 4:
            // put block to south
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 7:
        case 0:
            // put block above
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        default:
            // do nothing - on ground
            break;
        }
        break;
    case BLOCK_WOODEN_DOOR:
    case BLOCK_IRON_DOOR:
    case BLOCK_SPRUCE_DOOR:
    case BLOCK_BIRCH_DOOR:
    case BLOCK_JUNGLE_DOOR:
    case BLOCK_DARK_OAK_DOOR:
    case BLOCK_ACACIA_DOOR:
    case BLOCK_CRIMSON_DOOR:
    case BLOCK_WARPED_DOOR:
        bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
        block->grid[bi] = (unsigned char)type;
        block->data[bi] = (unsigned char)((dataVal & 0x7) | (finalDataVal & HIGH_BIT));
        if (dataVal < 8)
        {
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)(8 | (finalDataVal & HIGH_BIT));
        }
        else
        {
            // other direction door (for double doors)
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)(9 | (finalDataVal & HIGH_BIT));
        }
        break;
    case BLOCK_BED:
        if (dataVal < 8)
        {
            addBlock = 1;
            switch (dataVal & 0x3)
            {
            case 0:
                // put head to south
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
                block->grid[bi] = (unsigned char)type;
                block->data[bi] |= (unsigned char)(dataVal | 0x8);
                break;
            case 1:
                // put head to west
                bi = BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
                block->grid[bi] = (unsigned char)type;
                block->data[bi] |= (unsigned char)(dataVal | 0x8);
                break;
            case 2:
                // put head to north
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8);
                block->grid[bi] = (unsigned char)type;
                block->data[bi] |= (unsigned char)(dataVal | 0x8);
                break;
            case 3:
                // put head to east
                bi = BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
                block->grid[bi] = (unsigned char)type;
                block->data[bi] |= (unsigned char)(dataVal | 0x8);
                break;
            }
        }
        break;
    case BLOCK_STONE_BUTTON:
    case BLOCK_WOODEN_BUTTON:
    case BLOCK_SPRUCE_BUTTON:
    case BLOCK_BIRCH_BUTTON:
    case BLOCK_JUNGLE_BUTTON:
    case BLOCK_ACACIA_BUTTON:
    case BLOCK_DARK_OAK_BUTTON:
    case BLOCK_CRIMSON_BUTTON:
    case BLOCK_WARPED_BUTTON:
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
        trimVal = dataVal & 0x7;
        if (trimVal <= 5)
        {
            addBlock = 1;
            switch (trimVal)
            {
            case 0:
                // put block above
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_OBSIDIAN;
                break;
            case 1:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_OBSIDIAN;
                break;
            case 2:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_OBSIDIAN;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_OBSIDIAN;
                break;
            case 4:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_OBSIDIAN;
                break;
            case 5:
                // put block below
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y - 1, 4 + (dataVal % 2) * 8)] = BLOCK_OBSIDIAN;
            }
        }
        break;
    case BLOCK_TRAPDOOR:
    case BLOCK_IRON_TRAPDOOR:
    case BLOCK_SPRUCE_TRAPDOOR:
    case BLOCK_BIRCH_TRAPDOOR:
    case BLOCK_JUNGLE_TRAPDOOR:
    case BLOCK_ACACIA_TRAPDOOR:
    case BLOCK_DARK_OAK_TRAPDOOR:
    case BLOCK_CRIMSON_TRAPDOOR:
    case BLOCK_WARPED_TRAPDOOR:
        // use all 0-15
        addBlock = 1;

        trimVal = dataVal & 0x3;
        switch (trimVal)
        {
        case 3:
            // put block to west
            block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 2:
            // put block to east
            block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 1:
            // put block to north
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 0:
            // put block to south
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        }
        break;
    case BLOCK_PISTON:
    case BLOCK_STICKY_PISTON:
        trimVal = dataVal & 0x7;
        if (trimVal < 6)
        {
            addBlock = 1;

            // is piston extended?
            if (dataVal & 0x8)
            {
                int bx = 0;
                int by = 0;
                int bz = 0;

                switch (trimVal)
                {
                case 0: // pointing down
                    bx = 4 + (type % 2) * 8;
                    by = y;
                    bz = 4 + (dataVal % 2) * 8;
                    // increase y by 1 so piston is one block higher
                    y++;
                    break;
                case 1: // pointing up
                    bx = 4 + (type % 2) * 8;
                    by = y + 1;
                    bz = 4 + (dataVal % 2) * 8;
                    break;
                case 2: // pointing north
                    bx = 4 + (type % 2) * 8;
                    by = y;
                    bz = 3 + (dataVal % 2) * 8;
                    break;
                case 3: // pointing south
                    bx = 4 + (type % 2) * 8;
                    by = y;
                    bz = 5 + (dataVal % 2) * 8;
                    break;
                case 4: // pointing west
                    bx = 3 + (type % 2) * 8;
                    by = y;
                    bz = 4 + (dataVal % 2) * 8;
                    break;
                case 5: // pointing east
                    bx = 5 + (type % 2) * 8;
                    by = y;
                    bz = 4 + (dataVal % 2) * 8;
                    break;
                default:
                    assert(0);
                    break;
                }
                bi = BLOCK_INDEX(bx, by, bz);
                block->grid[bi] = BLOCK_PISTON_HEAD;
                // sticky or not, plus direction
                block->data[bi] |= (unsigned char)(trimVal | ((type == BLOCK_STICKY_PISTON) ? 0x8 : 0x0));
            }
        }
        break;
    case BLOCK_PISTON_HEAD:
        // uses bits 0-5 and 8-13
        if ((dataVal & 0x7) < 6)
        {
            if ((dataVal & 0x7) != 1)
            {
                // add glass so that when 3D printing it's not deleted;
                // it will be deleted when pointing up.
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
                block->grid[bi] = BLOCK_GLASS_PANE;
            }
            // make it float above ground, to avoid asserts and to test.
            y++;
            addBlock = 1;
        }
        break;
    case BLOCK_VINES:
        // uses all bits, 0-15
        // TODO: really should place vines under stuff, but this is a pain
        if (dataVal > 0)
        {
            addBlock = 1;
        }
        bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
        block->grid[bi] = (unsigned char)type;
        block->data[bi] = (unsigned char)dataVal;

        block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 2, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
        break;
    case BLOCK_FENCE:
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_NETHER_BRICK_FENCE:
    case BLOCK_CRIMSON_FENCE:
    case BLOCK_WARPED_FENCE:
    case BLOCK_IRON_BARS:
    case BLOCK_GLASS_PANE:
    case BLOCK_CHORUS_PLANT:
        // this one is specialized: dataVal says where to put neighbors, NSEW
        addBlock = 1;
        bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
        //block->grid[bi] = (unsigned char)type;
        //block->data[bi] = (unsigned char)finalDataVal;

        // put block above, too, for every fifth one, just to see it's working
        if ((dataVal % 5) == 4) {
            finalDataVal |= (type == BLOCK_CHORUS_PLANT) ? BIT_32 : 0;
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // just a post
            block->data[bi] = (unsigned char)(type == BLOCK_CHORUS_PLANT)? BIT_16 : 0;
        }

        // for just chorus plant, put endstone below
        if (type == BLOCK_CHORUS_PLANT)
        {
            finalDataVal |= BIT_16;
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y - 1, 4 + (dataVal % 2) * 8)] = BLOCK_END_STONE;
            // half the time also put chorus flower above
            if (dataVal & 0x1) {
                finalDataVal |= BIT_32;
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_CHORUS_FLOWER;
            }
        }

        if (dataVal & 0x4)
        {
            // put block to north
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)0x1;
        }
        if (dataVal & 0x8)
        {
            // put block to east
            bi = BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)0x2;
        }
        if (dataVal & 0x1)
        {
            // put block to south
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)0x4;
        }
        if (dataVal & 0x2)
        {
            // put block to west
            bi = BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)0x8;
        }
        break;
    case BLOCK_STAINED_GLASS_PANE:	// color AND neighbors!
        // this one is specialized: incoming dataVal chooses where to put neighbors, NSEW
        // *and* what color to use. Unlike the "clear" glass pane, above, the 4 bits
        // in the final dataVal are the color, not the neighbors. :( - need more bits
        bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
        block->grid[bi] = (unsigned char)type;
        block->data[bi] = (unsigned char)dataVal;

        if (dataVal & 0x1)
        {
            // alternate between wall and mossy wall - we set mossy wall if odd
            block->data[bi] |= (unsigned char)0x1;

            // put block to north
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] = (unsigned char)dataVal;
        }
        if (dataVal & 0x2)
        {
            // put block to east
            bi = BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] = (unsigned char)dataVal;
        }
        if (dataVal & 0x4)
        {
            // put block to south
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] = (unsigned char)dataVal;
        }
        if (dataVal & 0x8)
        {
            // put block to west
            bi = BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] = (unsigned char)dataVal;
        }
        break;
    case BLOCK_COBBLESTONE_WALL:
        // this one is specialized: dataVal just says where to put neighbors, NSEW
        bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
        block->grid[bi] = (unsigned char)type;

        // put block above, too, for every seventh one, just to see it's working
        if ((dataVal % 7) == 5)
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = (unsigned char)type;

        if (dataVal & 0x1)
        {
            // alternate between wall and mossy wall - we set mossy wall if odd
            block->data[bi] |= (unsigned char)0x1;

            // put block to north
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] |= (unsigned char)(dataVal % 2);
        }
        if (dataVal & 0x2)
        {
            // put block to east
            bi = BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] |= (unsigned char)(dataVal % 2);
        }
        if (dataVal & 0x4)
        {
            // put block to south
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] |= (unsigned char)(dataVal % 2);
        }
        if (dataVal & 0x8)
        {
            // put block to west
            bi = BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[bi] |= (unsigned char)(dataVal % 2);
        }
        if (dataVal > 0 && dataVal < 14) {
            // add neighbor of different material, to see it
            int neighborIndex = BLOCK_INDEX(7 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
            block->grid[neighborIndex] = (unsigned char)type;
            block->data[neighborIndex] = (unsigned char)dataVal;
            neighborIndex = BLOCK_INDEX(7 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
            block->grid[neighborIndex] = (unsigned char)type;
            block->data[neighborIndex] = (unsigned char)dataVal;
            neighborIndex = BLOCK_INDEX(7 + (type % 2) * 8, y, 6 + (dataVal % 2) * 8);
            block->grid[neighborIndex] = (unsigned char)type;
            block->data[neighborIndex] = (unsigned char)dataVal;
        }
        break;
    case BLOCK_REDSTONE_WIRE:
        // this one is specialized: dataVal just says where to put neighbors, NSEW
        bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
        block->grid[bi] = (unsigned char)type;

        if (dataVal & 0x1)
        {
            // put block to north
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 3 + (dataVal % 2) * 8)] = (unsigned char)type;
        }
        if (dataVal & 0x2)
        {
            // put block to east
            block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = (unsigned char)type;
        }
        if (dataVal & 0x4)
        {
            // put block to south
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 5 + (dataVal % 2) * 8)] = (unsigned char)type;
        }
        if (dataVal & 0x8)
        {
            // put block to west, redstone atop it
            block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = (unsigned char)type;
        }
        break;
    case BLOCK_CACTUS:
        // put on sand
        if (dataVal == 0)
        {
            addBlock = 1;
            // put sand below
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y - 1, 4 + (dataVal % 2) * 8)] = BLOCK_SAND;
        }
        break;
    case BLOCK_CHEST:
    case BLOCK_TRAPPED_CHEST:
        // uses 2-5, we add an extra chest on 0x8
        trimVal = dataVal & 0x7;
        if (trimVal >= 2 && trimVal <= 5)
        {
            // Note that we use trimVal here, different than the norm
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (trimVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] |= (unsigned char)trimVal;
        }
        // double-chest on 0x8 (for mapping - in Minecraft chests have just 2,3,4,5)
        // - locked chests (April Fool's joke) don't really have doubles, but whatever
        switch (dataVal)
        {
        case 0x8 | 2:
        case 0x8 | 3:
            // north/south, so put one to west (-1 X)
            bi = BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (trimVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] |= (unsigned char)trimVal;
            break;
        case 0x8 | 4:
        case 0x8 | 5:
            // west/east, so put one to north (-1 Z)
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (trimVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] |= (unsigned char)trimVal;
            break;
        default:
            break;
        }
        break;
    case BLOCK_LILY_PAD:
        if (dataVal == 0)
        {
            int wrow, wcol;
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = (unsigned char)type;
            for (wrow = 3; wrow <= 5; wrow++)
                for (wcol = 3; wcol <= 5; wcol++)
                    block->grid[BLOCK_INDEX(wrow + (type % 2) * 8, y - 1, wcol + (dataVal % 2) * 8)] = BLOCK_STATIONARY_WATER;
        }
        break;
    case BLOCK_COCOA_PLANT:
        if (dataVal < 12)
        {
            addBlock = 1;
            switch (dataVal & 0x3)
            {
            case 0:
                // put block to south
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8);
                break;
            case 1:
                // put block to west
                bi = BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
                break;
            case 2:
                // put block to north
                bi = BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8);
                break;
            case 3:
                // put block to east
                bi = BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8);
                break;
            }
            block->grid[bi] = BLOCK_LOG;
            block->data[bi] |= 3;	// jungle
        }
        break;
    case BLOCK_TRIPWIRE_HOOK:
        addBlock = 1;
        switch (dataVal & 0x3)
        {
        case 0:
            // put block to north
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_OAK_PLANKS;
            break;
        case 1:
            // put block to east
            block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_OAK_PLANKS;
            break;
        case 2:
            // put block to south
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_OAK_PLANKS;
            break;
        case 3:
            // put block to west
            block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_OAK_PLANKS;
            break;
        }
        break;

    case BLOCK_HAY:
    case BLOCK_PURPUR_PILLAR:
        // uses 0,4,8
        if ((dataVal == 0) || (dataVal == 4) || (dataVal == 8)) {
            addBlock = 1;
        }
        break;
    case BLOCK_TALL_SEAGRASS:
        if (dataVal < 1)
        {
            addBlock = 1;
            // add leaves above
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)(BLOCK_TALL_SEAGRASS & 0xFF);
            block->data[bi] = (unsigned char)(8 | HIGH_BIT);	// like flower, add 8
        }
        break;
    case BLOCK_WEEPING_VINES:
        if (dataVal < 2)
        {
            addBlock = 1;
            // add leaves above
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)(BLOCK_WEEPING_VINES & 0xFF);
            if (dataVal == 0) {
                finalDataVal = BIT_32;
                block->data[bi] = (unsigned char)HIGH_BIT;
            }
            else {
                // twisting vines are 0x1
                block->data[bi] = (unsigned char)(HIGH_BIT | BIT_32 | 0x1);
            }
        }
        break;
    case BLOCK_KELP:
        if (dataVal < 1)
        {
            addBlock = 1;
            // add leaves above
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)(BLOCK_KELP & 0xFF);
            // not entirely sure about this number, but 10 seems to be the norm
            block->data[bi] = (unsigned char)(1 | HIGH_BIT);	// just add 1 for top
        }
        break;
    case BLOCK_SEA_PICKLE:
        // uses 0-3, but also with waterlogged
        if (dataVal < 8)
        {
            addBlock = 1;
            if (dataVal >= 4) {
                finalDataVal = (dataVal & 0x3) | WATERLOGGED_BIT;	// waterlogged
            }
        }
        break;
    case BLOCK_CHAIN:
        // uses 0-2 to mean 0,4,8, but also with waterlogged
        if (dataVal < 6)
        {
            addBlock = 1;
            finalDataVal = ((dataVal % 3) * 4) | ((dataVal >= 3) ? WATERLOGGED_BIT : 0);	// waterlogged
        }
        break;
    case BLOCK_CONDUIT:
        // also with waterlogged
        if (dataVal < 2)
        {
            addBlock = 1;
            finalDataVal = (dataVal >= 1) ? WATERLOGGED_BIT : 0;	// waterlogged
        }
        break;
    case BLOCK_BARREL:
        // uses bits 0-5 and 8-13
        if ((dataVal & 0x7) < 6)
        {
            addBlock = 1;
        }
        break;
    case BLOCK_BELL:
        addBlock = 1;
        switch (dataVal & 0xc)
        {
        case 0x4:	// ceiling
            // put block above
            block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            break;
        case 0x8:	// single wall
            switch (dataVal & 0x3)
            {
            case 0:
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 1:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 2:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            }
            break;
        case 0xc:	// double wall
            switch (dataVal & 0x3)
            {
            case 0:
            case 2:
                // put block to west
                block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                // put block to east
                block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            case 1:
            case 3:
                // put block to south
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                // put block to north
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            }
            break;
        default:	// floor
            // do nothing - on ground
            break;
        }
        break;
    case BLOCK_GRINDSTONE:
        if (dataVal < 12) {
            addBlock = 1;
            switch (dataVal & 0xc)
            {
            case 0x4:	// wall
                switch (dataVal & 0x3)
                {
                case 0:
                    // put block to west
                    block->grid[BLOCK_INDEX(3 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                    break;
                case 1:
                    // put block to north
                    block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 3 + (dataVal % 2) * 8)] = BLOCK_STONE;
                    break;
                case 2:
                    // put block to east
                    block->grid[BLOCK_INDEX(5 + (type % 2) * 8, y, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                    break;
                case 3:
                    // put block to south
                    block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y, 5 + (dataVal % 2) * 8)] = BLOCK_STONE;
                    break;
                }
                break;
            case 0x8:	// ceiling
                // put block above
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
                break;
            default:	// floor
                // do nothing - on ground
                break;
            }
        }
        break;
    case BLOCK_LANTERN:
        // uses lowest bit 0 for hanging, 1 for soul lantern, plus waterlogging
        if ((dataVal & 0xf) < 8)
        {
            addBlock = 1;
            if (dataVal & 0x1) {
                // put block above lantern
                block->grid[BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8)] = BLOCK_STONE;
            }
            finalDataVal = (dataVal & 0x3) | ((dataVal >= 4) ? WATERLOGGED_BIT : 0);
        }
        break;
    case BLOCK_SCAFFOLDING:
        // uses only bit 0, but put three of them up, and waterlog
        if (dataVal < 4)
        {
            addBlock = 1;
            // put block above
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 4 + (dataVal % 2) * 8);
            block->grid[bi] = BLOCK_SCAFFOLDING & 0xff;
            block->data[bi] = (unsigned char)HIGH_BIT;
            // put block to south, above, floating
            bi = BLOCK_INDEX(4 + (type % 2) * 8, y + 1, 5 + (dataVal % 2) * 8);
            block->grid[bi] = BLOCK_SCAFFOLDING & 0xff;
            block->data[bi] = (unsigned char)(HIGH_BIT | 0x1);
            finalDataVal = (dataVal % 2) | ((dataVal >= 2) ? WATERLOGGED_BIT : 0);
        }
        break;
    case BLOCK_BEE_NEST:
        addBlock = 1;
        // use BIT_32 on or off (beehive/bee_nest), honey_level 5 or 0, facing 0 1 2 3
        finalDataVal = 0x80 | ((dataVal & 0x8) ? BIT_32 : 0) |	// beehive / bee_nest
            ((dataVal & 0x4) ? 5 << 2 : 0) |	// honey level
            (dataVal & 0x3);	// facing
        break;

        // don't show special blocks to users
#ifndef _DEBUG
    case BLOCK_UNKNOWN:
    case BLOCK_FAKE:
        break;
#endif
    }

    // if we want to do a normal sort of thing
    if (addBlock)
    {
        finalDataVal |= (origType > 255) ? HIGH_BIT : 0x0;
        bi = BLOCK_INDEX(4 + (origType % 2) * 8, y, 4 + (dataVal % 2) * 8);
        block->grid[bi] = (unsigned char)type;
        block->data[bi] = (unsigned char)finalDataVal;
#ifdef _DEBUG
        static bool extraBlock = false;
        if (extraBlock)
        {
            // optional: put neighbor to south, so we can test for what happens at borders when splitting occurs;
            // note: this will generate two assertions with pistons. Ignore them.
            bi = BLOCK_INDEX(4 + (origType % 2) * 8, y, 5 + (dataVal % 2) * 8);
            block->grid[bi] = (unsigned char)type;
            block->data[bi] = (unsigned char)finalDataVal;
        }
#endif
    }
}

void testNumeral(WorldBlock* block, int type, int y, int digitPlace, int outType)
{
    int i;
    int shiftedNumeral = type;
    int numeral;

    i = digitPlace;
    while (i > 0)
    {
        shiftedNumeral /= 10;
        i--;
    }
    numeral = shiftedNumeral % 10;
    if ((type < NUM_BLOCKS_DEFINED) && shiftedNumeral > 0)
    {
        int dots[50][2];
        int doti = 0;
        switch (numeral)
        {
        default:
        case 0:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 0; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 1:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 3; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 1;
            dots[doti][0] = 2; dots[doti++][1] = 2;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 1; dots[doti++][1] = 4;
            dots[doti][0] = 2; dots[doti++][1] = 4;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 2:
            dots[doti][0] = 0; dots[doti++][1] = 0;
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 3; dots[doti++][1] = 0;
            dots[doti][0] = 1; dots[doti++][1] = 1;
            dots[doti][0] = 2; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 3:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 0; dots[doti++][1] = 5;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 4:
            dots[doti][0] = 3; dots[doti++][1] = 0;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 2;
            dots[doti][0] = 2; dots[doti++][1] = 2;
            dots[doti][0] = 4; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 4;
            break;
        case 5:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 0; dots[doti++][1] = 3;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 0; dots[doti++][1] = 5;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            break;
        case 6:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 0; dots[doti++][1] = 3;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            break;
        case 7:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 1; dots[doti++][1] = 1;
            dots[doti][0] = 1; dots[doti++][1] = 2;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 0; dots[doti++][1] = 5;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            break;
        case 8:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 9:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        }
        for (i = 0; i < doti; i++)
        {
            block->grid[BLOCK_INDEX(2 + dots[i][0] + (type % 2) * 8, y - 1, 6 - dots[i][1] + ((digitPlace + 1) % 2) * 8)] = (unsigned char)outType;
        }
    }
}

// return NULL if no block loaded.
WorldBlock* LoadBlock(WorldGuide* pWorldGuide, int cx, int cz, int mcVersion, int versionID, int& retCode)
{
    // return negative value on error, 1 on read OK, 2 on read and it's empty, and higher bits than 1 or 2 are warnings
    retCode = 0;

    // if there's no world, simply return
    if (pWorldGuide->type == WORLD_UNLOADED_TYPE)
        return NULL;

    WorldBlock* block = block_alloc();

    // out of memory? If so, clear cache and cross fingers
    if (block == NULL)
    {
        Cache_Empty();
        block = block_alloc();
        if (block == NULL) {
            // oh well, out of luck
            return NULL;
        }
    }
    // always set
    block->rendery = -1; // force redraw
    block->mcVersion = mcVersion;
    block->versionID = versionID;
    // this version of 1.17 beta went to a height of 384;
    block->maxHeight = (versionID >= 2685) ? 384 : 256;

    if (pWorldGuide->type == WORLD_TEST_BLOCK_TYPE)
    {
        // synthetic world we populate - no need to go nuts here
        int type = cx * 2;
        // if directory starts with /, this is [Block Test World], a synthetic test world
        // made by the testBlock() method.
        int x, z;
        int bedrockHeight = 62;      // cppcheck-suppress 398
        int grassHeight = 64;
        int blockHeight = 65;

        memset(block->grid, 0, 16 * 16 * block->maxHeight);
        memset(block->data, 0, 16 * 16 * block->maxHeight);
        memset(block->biome, 1, 16 * 16);
        memset(block->light, 0xff, 16 * 16 * block->maxHeight/2);
        block->renderhilitID = 0;

        if (type >= 0 && type < NUM_BLOCKS_DEFINED && cz >= 0 && cz < 8)
        {
            // grass base
            for (x = 0; x < 16; x++)
            {
                for (z = 0; z < 16; z++)
                {
                    // make the grass two blocks thick, and then "impenetrable" below, for test border code
                    block->grid[BLOCK_INDEX(x, bedrockHeight, z)] = BLOCK_BEDROCK;
                    for (int y = bedrockHeight + 1; y < grassHeight; y++)
                        block->grid[BLOCK_INDEX(x, y, z)] = BLOCK_DIRT;
                    block->grid[BLOCK_INDEX(x, grassHeight, z)] = BLOCK_GRASS_BLOCK;
                }
            }

            // blocks: each 16x16 area has 4 blocks, every 8 spaces, so we call this to get 4 blocks.
            testBlock(block, type, blockHeight, cz * 2);
            testBlock(block, type, blockHeight, cz * 2 + 1);
            if (type + 1 < NUM_BLOCKS_DEFINED)
            {
                testBlock(block, type + 1, blockHeight, cz * 2);
                testBlock(block, type + 1, blockHeight, cz * 2 + 1);
            }
            return block;
        }
        // tick marks
        else if (type >= 0 && type < NUM_BLOCKS_DEFINED && (cz == -1 || cz == 8))
        {
            int i, j;

            // stone edge
            for (x = 0; x < 16; x++)
            {
                for (z = 0; z < 16; z++)
                {
                    block->grid[BLOCK_INDEX(x, grassHeight, z)] = (cz > 0) ? (unsigned char)BLOCK_OAK_PLANKS : (unsigned char)BLOCK_STONE;
                }
            }

            // blocks
            for (i = 0; i < 2; i++)
            {
                if (((type + i) % 10) == 0)
                {
                    if (type + i < NUM_BLOCKS_DEFINED)
                    {
                        for (j = 0; j <= (int)(cx / 8); j++)
                            block->grid[BLOCK_INDEX(4 + (i % 2) * 8, grassHeight, j)] = (((type + i) % 50) == 0) ? (unsigned char)BLOCK_WATER : (unsigned char)BLOCK_LAVA;
                    }
                }
            }
            return block;
        }
        // numbers (yes, I'm insane)
        else if (type >= 0 && type < NUM_BLOCKS_DEFINED && (cz <= -2 && cz >= -3))
        {
            int letterType = BLOCK_OBSIDIAN;
            if ((type >= NUM_BLOCKS_STANDARD) && (type != BLOCK_STRUCTURE_BLOCK))
            {
                // for unknown block, put a different font
                letterType = BLOCK_LAVA;
            }

            // white wool
            for (x = 0; x < 16; x++)
            {
                for (z = 0; z < 16; z++)
                {
                    block->grid[BLOCK_INDEX(x, grassHeight, z)] = BLOCK_WOOL;
                }
            }
            // blocks
            testNumeral(block, type, blockHeight, -cz * 2 - 3, letterType);
            testNumeral(block, type, blockHeight, -cz * 2 - 1 - 3, letterType);

            // second number on 16x16 tile
            letterType = BLOCK_OBSIDIAN;
            if (type + 1 < NUM_BLOCKS_DEFINED)
            {
                if ((type + 1 >= NUM_BLOCKS_STANDARD) && (type + 1 != BLOCK_STRUCTURE_BLOCK))
                {
                    letterType = BLOCK_LAVA;
                }
                testNumeral(block, type + 1, blockHeight, -cz * 2 - 3, letterType);
                testNumeral(block, type + 1, blockHeight, -cz * 2 - 1 - 3, letterType);
            }
            return block;
        }
        else
        {
            block_free(block);
            return NULL;
        }
    }
    else {
        // it's a real world or schematic or no world is loaded
        if (pWorldGuide->type == WORLD_LEVEL_TYPE) {
            // absolute insanely high maximum, just in case
            BlockEntity blockEntities[16 * 16 * 384];

            retCode = regionGetBlocks(pWorldGuide->directory, cx, cz, block->grid, block->data, block->light, block->biome, blockEntities, &block->numEntities, block->mcVersion, block->versionID, block->maxHeight);

            // values 1 and 2 are valid; 3's not used - higher bits are warnings
            if (retCode >= 0) {
                block->blockType = retCode & 0x3;
            }
            else {
                // negative means a serious read error, so store as-is
                block->blockType = retCode;
            }

            // for old-style chunks, there may be tile entities, such as flower and head types, which need to get transferred and used later
            if ((retCode >= 0) && (block->numEntities > 0)) {
                // transfer the relevant part of the BlockEntity array to permanent block storage
                block->entities = (BlockEntity*)malloc(block->numEntities * sizeof(BlockEntity));

                if (block->entities)
                    memcpy(block->entities, blockEntities, block->numEntities * sizeof(BlockEntity));
                else
                    // couldn't alloc data
                    return NULL;
            }
        }
        else {
            assert(pWorldGuide->type == WORLD_SCHEMATIC_TYPE);
            retCode = block->blockType = createBlockFromSchematic(pWorldGuide, cx, cz, block);
        }

        // is block valid? and not empty?
        if (retCode > 0) {

            // does block have anything in it other than air?
            if (block->blockType == 1) {
                int i;
                unsigned char* pBlockID = block->grid;
                for (i = 0; i < 16 * 16 * block->maxHeight; i++, pBlockID++)
                {
                    if ((*pBlockID >= NUM_BLOCKS_STANDARD) && (*pBlockID != BLOCK_STRUCTURE_BLOCK))
                    {
                        // some new version of Minecraft, block ID is unrecognized;
                        // turn this block into stone. dataVal will be ignored.
                        // flag assert only once
                        assert((gUnknownBlock == 1) || (gPerformUnknownBlockCheck == 0));	// note the program needs fixing
                        *pBlockID = BLOCK_UNKNOWN;
                        // note that we always clean up bad blocks;
                        // whether we flag that a bad block was found is optional.
                        // This gets turned off once the user has been warned, once, that his map has some funky data.
                        if (gPerformUnknownBlockCheck)
                            gUnknownBlock = 1;
                    }
                }
            }
            return block;
        }
    }

    block_free(block);
    return NULL;
}

// cx and cz are the chunk location - multiply by 16 to get the starting world location
// return 1 if real data is found, 0 if all empty
int createBlockFromSchematic(WorldGuide* pWorldGuide, int cx, int cz, WorldBlock* block)
{
    if (!pWorldGuide->sch.repeat) {
        // does block overlap the schematic?
        if ((cx * 16 > pWorldGuide->sch.width) || (cx < 0))
            return 0;
        if ((cz * 16 > pWorldGuide->sch.length) || (cz < 0))
            return 0;
    }

    // no biome, so that's easy
    memset(block->biome, 0, 16 * 16);
    // no light, so that's also easy
    memset(block->light, 0, 16 * 16 * block->maxHeight/2);

    // clear the rest, so we fill these in as found
    memset(block->grid, 0, 16 * 16 * block->maxHeight);
    memset(block->data, 0, 16 * 16 * block->maxHeight);

    // not sure why I made this a static int, but let's leave it be, shall we?
    static int border = 1;      // cppcheck-suppress 398
    if (pWorldGuide->sch.repeat) {
        // loop through local block locations, 0-15,0-15
        for (int y = 0; y < pWorldGuide->sch.height; y++) {
            int zWorld = cz * 16;
            for (int z = 0; z < 16; z++, zWorld++) {
                int zMod = zWorld % (pWorldGuide->sch.length + border);
                zMod = (zMod + pWorldGuide->sch.length + border) % (pWorldGuide->sch.length + border);
                // leave a border
                if (zMod < pWorldGuide->sch.length) {
                    int index = (y * 16 + z) * 16;
                    int xWorld = cx * 16;
                    for (int x = 0; x < 16; x++, index++, xWorld++) {
                        int xMod = xWorld % (pWorldGuide->sch.width + border);
                        xMod = (xMod + pWorldGuide->sch.width + border) % (pWorldGuide->sch.width + border);
                        if (xMod < pWorldGuide->sch.width) {
                            int schIndex = (y * pWorldGuide->sch.length + zMod) * pWorldGuide->sch.width + xMod;
                            assert(schIndex >= 0 && schIndex < pWorldGuide->sch.numBlocks);
                            block->grid[index] = pWorldGuide->sch.blocks[schIndex];
                            block->data[index] = pWorldGuide->sch.data[schIndex];
                        }
                    }
                }
            }
        }
    }
    else {
        // find the valid data's bounds inside the 16x16 area
        int xlength = (cx * 16 + 16 > pWorldGuide->sch.width) ? pWorldGuide->sch.width - cx * 16 : 16;
        int ylength = pWorldGuide->sch.height;
        int zlength = (cz * 16 + 16 > pWorldGuide->sch.length) ? pWorldGuide->sch.length - cz * 16 : 16;
        // the offset is how many 16x16 tiles into the schematic data itself we need to offset
        int offset = 16 * cz * pWorldGuide->sch.width + 16 * cx;

        // loop through local block locations, 0-15,0-15
        for (int y = 0; y < ylength; y++) {
            for (int z = 0; z < zlength; z++) {
                int index = (y * 16 + z) * 16;
                int schIndex = (y * pWorldGuide->sch.length + z) * pWorldGuide->sch.width + offset;
                assert(schIndex >= 0 && schIndex < pWorldGuide->sch.numBlocks);
                for (int x = 0; x < xlength; x++, index++, schIndex++) {

                    // TODO: we could test if the block is entirely empty, marking blockType == 2 if so. Common in schematics, and would draw faster.
                    block->grid[index] = pWorldGuide->sch.blocks[schIndex];
                    block->data[index] = pWorldGuide->sch.data[schIndex];
                }
            }
        }
    }
    return 1;
}

// Clear that an unknown block was encountered. Good to do when loading a new world.
void ClearBlockReadCheck()
{
    gUnknownBlock = 0;
}

// was an unknown block found during mapping? Will be set true only if CheckUnknownBlock is true (which it is by default)
int UnknownBlockRead()
{
    return gUnknownBlock;
}

// should we check for unknown blocks? If turned off, we won't assert and won't flag (but will still clean up) any unknown blocks found.
// Normally always on, could turn it off for debugging situations.
void CheckUnknownBlock(int check)
{
    gPerformUnknownBlockCheck = check;
}

// should not be needed, but there's some timing thread weirdness
int NeedToCheckUnknownBlock()
{
    return gPerformUnknownBlockCheck;
}

// 0 succeed, 1+ windows file open fail, -1 or less is some other read error from nbt
int GetSpawn(const wchar_t* world, int* x, int* y, int* z)
{
    bfFile bf;
    wchar_t filename[300];
    wcsncpy_s(filename, 300, world, wcslen(world) + 1);
    wcscat_s(filename, 300, gSeparator);
    wcscat_s(filename, 300, L"level.dat");
    int err = 0;
    bf = newNBT(filename, &err);
    if (bf.gz == 0x0) return err;
    int retcode = nbtGetSpawn(&bf, x, y, z);
    nbtClose(&bf);
    return retcode;
}
// 0 succeed, 1+ windows file open fail, -1 or less is some other read error from nbt
int GetFileVersion(const wchar_t* world, int* version, wchar_t* fileOpened, rsize_t size)
{
    bfFile bf;
    wcsncpy_s(fileOpened, size, world, wcslen(world) + 1);
    wcscat_s(fileOpened, size, gSeparator);
    wcscat_s(fileOpened, size, L"level.dat");
    int err = 0;
    bf = newNBT(fileOpened, &err);
    if (bf.gz == 0x0) return err;
    int retcode = nbtGetFileVersion(&bf, version);
    nbtClose(&bf);
    return retcode;
}
// 0 succeed, 1+ windows file open fail, -1 or less is some other read error from nbt
//  The NBT data version, which tells the MC release. See https://minecraft.gamepedia.com/Data_version
// 1444 is 1.13, 1901 is 1.14
int GetFileVersionId(const wchar_t* world, int* versionId)
{
    bfFile bf;
    wchar_t filename[300];
    wcsncpy_s(filename, 300, world, wcslen(world) + 1);
    wcscat_s(filename, 300, gSeparator);
    wcscat_s(filename, 300, L"level.dat");
    int err = 0;
    bf = newNBT(filename, &err);
    if (bf.gz == 0x0) return err;
    int retcode = nbtGetFileVersionId(&bf, versionId);
    nbtClose(&bf);
    return retcode;
}

/* currently not used
// 0 succeed, 1+ windows file open fail, -1 or less is some other read error from nbt
int GetFileVersionName(const wchar_t* world, char* versionName, int stringLength)
{
    bfFile bf;
    wchar_t filename[300];
    wcsncpy_s(filename, 300, world, wcslen(world) + 1);
    wcscat_s(filename, 300, gSeparator);
    wcscat_s(filename, 300, L"level.dat");
    int err = 0;
    bf = newNBT(filename, &err);
    if (bf.gz == 0x0) return err;
    int retcode = nbtGetFileVersionName(&bf, versionName, stringLength);
    nbtClose(&bf);
    return retcode;
}
*/

// 0 succeed, 1+ windows file open fail, -1 or less is some other read error from nbt
int GetLevelName(const wchar_t* world, char* levelName, int stringLength)
{
    bfFile bf;
    wchar_t filename[300];
    wcsncpy_s(filename, 300, world, wcslen(world) + 1);
    wcscat_s(filename, 300, gSeparator);
    wcscat_s(filename, 300, L"level.dat");
    int err = 0;
    bf = newNBT(filename, &err);
    if (bf.gz == 0x0) return err;
    int retcode = nbtGetLevelName(&bf, levelName, stringLength);
    nbtClose(&bf);
    return retcode;
}
//void GetRandomSeed(const wchar_t *world,long long *seed)
//{
//    bfFile bf;
//    wchar_t filename[300];
//    wcsncpy_s(filename,300,world,wcslen(world)+1);
//    wcscat_s(filename,300,L"/level.dat");
//    bf=newNBT(filename);
//    nbtGetRandomSeed(&bf,seed);
//    gMapSeed = *seed;
//    nbtClose(&bf);
//
//}
// 0 succeed, 1+ windows file open fail, -1 or less is some other read error from nbt
int GetPlayer(const wchar_t* world, int* px, int* py, int* pz)
{
    bfFile bf;
    wchar_t filename[300];
    wcsncpy_s(filename, 300, world, wcslen(world) + 1);
    wcscat_s(filename, 300, gSeparator);
    wcscat_s(filename, 300, L"level.dat");
    int err = 0;
    bf = newNBT(filename, &err);
    if (bf.gz == 0x0) return err;
    int retval = nbtGetPlayer(&bf, px, py, pz);
    nbtClose(&bf);
    return retval;
}

/////////////////////////// Schematic read file
// return 1 for OK, 0 for failure, -1 means schematic doesn't exist or can't be opened.
int GetSchematicWord(const wchar_t* schematic, char* field, int* value)
{
    bfFile bf;
    int err = 0;
    bf = newNBT(schematic, &err);
    if (bf.gz == 0x0) return -1;
    int retval = nbtGetSchematicWord(&bf, field, value);
    nbtClose(&bf);
    return retval;
}

// return 1 on success, 0 for failure, -1 means schematic doesn't exist or can't be opened.
int GetSchematicBlocksAndData(const wchar_t* schematic, int numBlocks, unsigned char* schematicBlocks, unsigned char* schematicBlockData)
{
    bfFile bf;
    int err = 0;
    bf = newNBT(schematic, &err);
    if (bf.gz == 0x0) return -1;
    int retval = nbtGetSchematicBlocksAndData(&bf, numBlocks, schematicBlocks, schematicBlockData);
    nbtClose(&bf);
    return retval;
}


//Sets the pcolor, the premultiplied colors, as these are a pain to precompute and put in the table.
// done from the saved colors, not from the color scheme
void SetMapPremultipliedColors(int start)
{
    for (int i = start; i < NUM_BLOCKS_DEFINED; i++)
    {
        unsigned int color = gBlockDefinitions[i].color = gBlockDefinitions[i].read_color;
        unsigned char r = (unsigned char)((color >> 16) & 0xff);
        unsigned char g = (unsigned char)((color >> 8) & 0xff);
        unsigned char b = (unsigned char)color & 0xff;
        float a = gBlockDefinitions[i].alpha = gBlockDefinitions[i].read_alpha;
        unsigned char ra = (unsigned char)(r * a); //premultiply alpha
        unsigned char ga = (unsigned char)(g * a);
        unsigned char ba = (unsigned char)(b * a);
        gBlockDefinitions[i].pcolor = (ra << 16) | (ga << 8) | ba;

        // reality check: every block except the first one (air) should have *some* sort of "it exists" flag set for it.
        assert(gBlockDefinitions[i].flags & BLF_CLASS_SET);
    }
}

//Sets the colors used.
//palette should be in RGBA format
// done from the color scheme, as possible
void SetMapPalette(unsigned int* palette, int num)
{
    gColormap++;
    for (int i = 0; i < num; i++)
    {
        unsigned char r = (unsigned char)(palette[i] >> 24);
        unsigned char g = (unsigned char)(palette[i] >> 16);
        unsigned char b = (unsigned char)(palette[i] >> 8);
        float a = ((float)(palette[i] & 0xff)) / 255.0f;
        unsigned char ra = (unsigned char)(r * a); //premultiply alpha
        unsigned char ga = (unsigned char)(g * a);
        unsigned char ba = (unsigned char)(b * a);
        gBlockDefinitions[i].color = (r << 16) | (g << 8) | b;
        gBlockDefinitions[i].pcolor = (ra << 16) | (ga << 8) | ba;
        gBlockDefinitions[i].alpha = a;
    }
    SetMapPremultipliedColors(num);
    initColors();
}

// for each block color, calculate light levels 0-15
static void initColors()
{
    gColorsInited = 1;
    for (unsigned int i = 0; i < NUM_BLOCKS_DEFINED; i++)
    {
        unsigned int color = gBlockDefinitions[i].pcolor;
        unsigned int r = color >> 16;
        unsigned int g = (color >> 8) & 0xff;
        unsigned int b = color & 0xff;
        //we'll use YUV to darken the blocks.. gives a nice even
        //coloring
        double y = 0.299 * r + 0.587 * g + 0.114 * b;
        double u = (b - y) * 0.565;
        double v = (r - y) * 0.713;
        double delta = y / 15;

        for (unsigned int shade = 0; shade < 16; shade++)
        {
            y = shade * delta;
            r = (unsigned int)clamp(y + 1.403 * v, 0, 255);
            g = (unsigned int)clamp(y - 0.344 * u - 0.714 * v, 0, 255);
            b = (unsigned int)clamp(y + 1.770 * u, 0, 255);
            gBlockColors[i * 16 + shade] = (r << 16) | (g << 8) | b;
        }
    }

    // set the Empty color, for initialization
    gEmptyR = (unsigned char)(gBlockColors[15] >> 16);
    gEmptyG = (unsigned char)((gBlockColors[15] >> 8) & 0xff);
    gEmptyB = (unsigned char)(gBlockColors[15] & 0xff);

    // also initialize the "missing tile" unknown graphic, a gray and black checkerboard
    for (int rx = 0; rx < 16; ++rx)
    {
        for (int ry = 0; ry < 16; ++ry)
        {
            int off = (rx + ry * 16) * 4;
            int tone = 150;
            if ((rx / 4) % 2 ^ (ry / 4) % 2)
                tone = 140;
            gBlankTile[off] = (unsigned char)tone;
            gBlankTile[off + 1] = (unsigned char)tone;
            gBlankTile[off + 2] = (unsigned char)tone;
            gBlankTile[off + 3] = (unsigned char)255;	// was 128 - why?

            // fully inside highlight box
            gBlankHighlitTile[off] = (unsigned char)((double)gBlankTile[off] * (1.0 - gHalpha) + gHalpha * (double)gHred);
            gBlankHighlitTile[off + 1] = (unsigned char)((double)gBlankTile[off + 1] * (1.0 - gHalpha) + gHalpha * (double)gHgreen);
            gBlankHighlitTile[off + 2] = (unsigned char)((double)gBlankTile[off + 2] * (1.0 - gHalpha) + gHalpha * (double)gHblue);
            gBlankHighlitTile[off + 3] = (unsigned char)255;
        }
    }
}
