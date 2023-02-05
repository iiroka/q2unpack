/*
* Copyright (C) 1997-2001 Id Software, Inc.
* Copyright (C) 2019      Iiro Kaihlaniemi
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at
* your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
* 02111-1307, USA.
*
*/
#include <iostream>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <png.h>
#include "files.h"

typedef struct
{
    char name[256];
    int size;
    int offset;     /* Ignored in PK3 files. */
} fsPackFile_t;

typedef struct
{
    char name[256];
    int numFiles;
    FILE *pak;
    fsPackFile_t *files;
} fsPack_t;

#define LittleLong(x) x

typedef struct
{
    char name[256];
    FILE *file;
    int offset;
    long length;
} fileEntry;

static std::vector<fileEntry> entries;
static uint32_t d_8to24table[256];

/*
 * Takes an explicit (not game tree related) path to a pak file.
 *
 * Loads the header and directory, adding the files at the beginning of the
 * list so they override previous pack files.
 */
fsPack_t *
FS_LoadPAK(const char *packPath)
{
    int i; /* Loop counter. */
    int numFiles; /* Number of files in PAK. */
    FILE *handle; /* File handle. */
    fsPackFile_t *files; /* List of files in PAK. */
    fsPack_t *pack; /* PAK file. */
    dpackheader_t header; /* PAK file header. */
    dpackfile_t info[MAX_FILES_IN_PACK]; /* PAK info. */

    handle = fopen(packPath, "rb");

    if (handle == NULL)
    {
        fprintf(stderr, "FS_LoadPAK: Cannot open '%s'\n", packPath);
        return NULL;
    }

    fread(&header, 1, sizeof(dpackheader_t), handle);

    if (LittleLong(header.ident) != IDPAKHEADER)
    {
        fclose(handle);
        fprintf(stderr, "FS_LoadPAK: '%s' is not a pack file\n", packPath);
        return nullptr;
    }

    header.dirofs = LittleLong(header.dirofs);
    header.dirlen = LittleLong(header.dirlen);

    numFiles = header.dirlen / sizeof(dpackfile_t);

    if ((numFiles > MAX_FILES_IN_PACK) || (numFiles == 0))
    {
        fclose(handle);
        fprintf(stderr, "FS_LoadPAK: '%s' has %i files\n",
                  packPath, numFiles);
        return nullptr;
    }

    files = (fsPackFile_t *)malloc(numFiles * sizeof(fsPackFile_t));

    fseek(handle, header.dirofs, SEEK_SET);
    fread(info, 1, header.dirlen, handle);

    /* Parse the directory. */
    for (i = 0; i < numFiles; i++)
    {
        strncpy(files[i].name, info[i].name, sizeof(files[i].name));
        files[i].offset = LittleLong(info[i].filepos);
        files[i].size = LittleLong(info[i].filelen);
    }

    pack = (fsPack_t *)malloc(sizeof(fsPack_t));
    strncpy(pack->name, packPath, sizeof(pack->name));
    pack->pak = handle;
    pack->numFiles = numFiles;
    pack->files = files;

    printf("Added packfile '%s' (%i files).\n", pack->name, numFiles);

    return pack;
}

/*
 * Create entries for contents of PAK file.
 */
static bool loadPak(const char *name) {
    fsPack_t *pak = FS_LoadPAK(name);
    if (pak == nullptr) {
        return false;
    }

    for (int i = 0; i < pak->numFiles; i++) {
        fileEntry entry;
        strcpy(entry.name, pak->files[i].name);
        entry.file = pak->pak;
        entry.offset = pak->files[i].offset;
        entry.length = pak->files[i].size;
        entries.push_back(entry);
    }

    return true;
}

static void strtolower(char *str) {
    for (int i = 0; i < strlen(str); i++) {
        str[i] = tolower(str[i]);
    }
}

/*
 * Read a quake2 directory an create entries for files.
 */
static bool readDir(const char *basePath, const char *relPath) {

    DIR* dir = opendir(basePath);
    if (dir == NULL) {
        fprintf(stderr, "Cannot open dir %s\n", basePath);
        return false;
    }

    dirent* dp;
    while ((dp = readdir(dir)) != NULL) {
        if (dp->d_namlen == 0 || dp->d_name[0] == '.') continue;

        char fullPath[4096];
        sprintf(fullPath, "%s/%s", basePath, dp->d_name);
        char fullrelPath[4096];
        if (strlen(relPath) == 0) {
            strcpy(fullrelPath, dp->d_name);
        } else {
            sprintf(fullrelPath, "%s/%s", relPath, dp->d_name);
        }

        if (dp->d_type == DT_DIR) {
            if (!readDir(fullPath, fullrelPath)) {
                closedir(dir);
                return false;
            }
        } else if (dp->d_type == DT_REG) {

            if (dp->d_namlen > 4 && strcmp(&dp->d_name[dp->d_namlen-4], ".pak") == 0) {
                if (!loadPak(fullPath)) {
                    closedir(dir);
                    return false;
                }
            } else if (dp->d_namlen > 6 && strcmp(&dp->d_name[dp->d_namlen-6], ".dylib") == 0) {
                // ignored
            } else {
                FILE *f = fopen(fullPath, "rb");
                fseek(f, 0, SEEK_END);
                long l = ftell(f);
                fseek(f, 0, SEEK_SET);
                fileEntry entry;
                strcpy(entry.name, fullrelPath);
                entry.file = f;
                entry.offset = 0;
                entry.length = l;
                entries.push_back(entry);
            }

        } else {
            fprintf(stderr, "Skipping unknown file: %s\n", dp->d_name);
        }
    }
    closedir(dir);
    return true;
}

/*
 * Find an entry for specific file by name.
 */
static fileEntry* findEntry(const char *path)
{
    for (int i = 0; i < entries.size(); i++) {
        if (strcmp(entries[i].name, path) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

/*
 * Load palette from pcx file.
 */
static bool loadPalette(const char *path, const char *outpath, const char *outfile)
{
    fileEntry *entry = findEntry(path);
    if (entry == NULL) {
        fprintf(stderr, "Failed to find entry\n");
        return false;
    }
    fseek(entry->file, entry->offset, SEEK_SET);
    pcx_t pcx;
    if (fread(&pcx, sizeof(pcx), 1, entry->file) != 1) {
        fprintf(stderr, "Failed to read entry\n");
        return false;
    }

    if ((pcx.manufacturer != 0x0a) || (pcx.version != 5) ||
        (pcx.encoding != 1) || (pcx.bits_per_pixel != 8)) {
        fprintf(stderr, "Bad pcx file %s\n", path);
        return false;
    }

    byte palette[768];
    fseek(entry->file, entry->offset + entry->length - 768, SEEK_SET);
    if (fread(palette, 768, 1, entry->file) != 1) {
        fprintf(stderr, "Failed to read palette\n");
        return false;
    }

    for (int i = 0; i < 256; i++) {
        uint32_t r = palette[i * 3 + 0];
        uint32_t g = palette[i * 3 + 1];
        uint32_t b = palette[i * 3 + 2];

        uint32_t v = (255 << 24) + (r << 0) + (g << 8) + (b << 16);
        d_8to24table[i] = LittleLong(v);
    }

    d_8to24table[255] &= LittleLong(0xffffff); /* 255 is transparent */

    char fullpath[256];
    snprintf(fullpath, 256, "%s/%s", outpath, outfile);

    FILE *ofile = fopen(fullpath, "wb");
    if (!ofile) {
        fprintf(stderr, "Failed to create %s\n", fullpath);
        return false;
    }

    if (fwrite(palette, 768, 1, ofile) != 1) {
        fprintf(stderr, "Failed to write %s\n", fullpath);
        fclose(ofile);
        return false;
    }

    fclose(ofile);
    return true;
}

/*
 * Split entry to filename and path.
 */
static void splitPath(const fileEntry& entry, const char *outPath, char *path, char *filename)
{
    strcpy(path, outPath);
    char bfr[64];
    strncpy(bfr, entry.name, 56);
    const char *s = &bfr[0];
    const char *start = s;
    while (*s) {
        if (*s == '/') {
            strncat(path, start, s - start);
            mkdir(path, 0777);
            start = s;
        }
        s++;
    }
    strcat(path, "/");
    strcpy(filename, start + (*start == '/' ? 1 : 0));
}

/*
 * Create a PNG from pixel data.
 */
static bool writePng(const char *name, int width, int height, const uint32_t *data)
{
    FILE *ofile = fopen(name, "wb");
    if (!ofile) {
        fprintf(stderr, "Failed to create %s\n", name);
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        fprintf(stderr, "Could not allocate write struct\n");
        fclose(ofile);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        fprintf(stderr, "Could not allocate info struct\n");
        fclose(ofile);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error during png creation\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(ofile);
        return false;
    }

    png_init_io(png_ptr, ofile);

    // Write header (8 bit colour depth)
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error during writing bytes\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(ofile);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error during end of write\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(ofile);
        return false;
    }

    png_bytep *row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
    for (int i = 0; i < height; i++) {
        row_pointers[i] = (png_bytep)&data[i * width];
    }

    png_write_image(png_ptr, row_pointers);

    png_write_end(png_ptr, NULL);

    free(row_pointers);
    fclose(ofile);
    return true;
}

// Just one to one copy
static bool copyFile(const fileEntry& entry, const char *outPath) {
    char fullpath[4096];
    char fname[32];
    splitPath(entry, outPath, fullpath, fname);
    strcat(fullpath, fname);
    strtolower(fullpath);

    FILE *ofile = fopen(fullpath, "wb");
    if (!ofile) {
        fprintf(stderr, "Failed to create %s\n", fullpath);
        return false;
    }
    fseek(entry.file, entry.offset, SEEK_SET);
    for (long i = 0; i < entry.length;) {
        int len = (entry.length - i) > 4096 ? 4096 : int(entry.length - i);
        size_t l = fread(fullpath, 1, len, entry.file);
        if (l == 0) {
            fprintf(stderr, "Failed to read %s\n", entry.name);
            fclose(ofile);
            return false;
        }
        if (fwrite(fullpath, 1, l, ofile) != l) {
            fprintf(stderr, "Failed to write %s\n", entry.name);
            fclose(ofile);
            return false;
        }
        i += l;
    }

    fclose(ofile);
    return true;
}

typedef struct
{
    short x, y;
} floodfill_t;

/* must be a power of 2 */
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy)    \
    { \
        if (pos[off] == fillcolor) \
        { \
            pos[off] = 255;    \
            fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
            inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
        } \
        else if (pos[off] != 255) \
        { \
            fdc = pos[off];    \
        } \
    }

/*
 * Fill background pixels so mipmapping doesn't have haloes
 */
static void FloodFillSkin(byte *skin, int skinwidth, int skinheight) {
    byte fillcolor = *skin; /* assume this is the pixel to fill */
    floodfill_t fifo[FLOODFILL_FIFO_SIZE];
    int inpt = 0, outpt = 0;
    int filledcolor = -1;
    int i;

    if (filledcolor == -1)
    {
        filledcolor = 0;

        /* attempt to find opaque black */
        for (i = 0; i < 256; ++i)
        {
            if (LittleLong(d_8to24table[i]) == (255 << 0)) /* alpha 1.0 */
            {
                filledcolor = i;
                break;
            }
        }
    }

    /* can't fill to filled color or to transparent color (used as visited marker) */
    if ((fillcolor == filledcolor) || (fillcolor == 255))
    {
        return;
    }

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt)
    {
        int x = fifo[outpt].x, y = fifo[outpt].y;
        int fdc = filledcolor;
        byte *pos = &skin[x + skinwidth * y];

        outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

        if (x > 0)
        {
            FLOODFILL_STEP(-1, -1, 0);
        }

        if (x < skinwidth - 1)
        {
            FLOODFILL_STEP(1, 1, 0);
        }

        if (y > 0)
        {
            FLOODFILL_STEP(-skinwidth, 0, -1);
        }

        if (y < skinheight - 1)
        {
            FLOODFILL_STEP(skinwidth, 0, 1);
        }

        skin[x + skinwidth * y] = fdc;
    }
}

/*
 * Load PCX and write PNG.
 */
static bool convertPcx(const fileEntry& entry, const char *outPath, bool isSkin) {
    char fullpath[1024];
    char fname[32];
    splitPath(entry, outPath, fullpath, fname);

    fseek(entry.file, entry.offset, SEEK_SET);
    pcx_t pcx;
    if (fread(&pcx, sizeof(pcx), 1, entry.file) != 1) {
        fprintf(stderr, "Failed to pcx header\n");
        return false;
    }

    int pcx_width = pcx.xmax - pcx.xmin;
    int pcx_height = pcx.ymax - pcx.ymin;

    if ((pcx.manufacturer != 0x0a) || (pcx.version != 5) ||
        (pcx.encoding != 1) || (pcx.bits_per_pixel != 8) ||
        (pcx_width >= 4096) || (pcx_height >= 4096)) {
        fprintf(stderr, "Bad pcx file %s\n", entry.name);
        return false;
    }

    int datalen = int(entry.length - sizeof(pcx));
    byte *raw_b = (byte *)malloc(datalen);
    if (fread(raw_b, datalen, 1, entry.file) != 1) {
        fprintf(stderr, "Failed to pcx data\n");
        free(raw_b);
        return false;
    }


    int full_size = (pcx_height + 1) * (pcx_width + 1);
    uint8_t *out1 = (uint8_t *)malloc(full_size);
    const byte *raw = raw_b;

    uint8_t *pix = out1;
    for (int y = 0; y <= pcx_height; y++, pix += pcx_width + 1) {
        for (int x = 0; x <= pcx_width; ) {
            byte dataByte = *raw++;
            byte runLength = 1;
            if ((dataByte & 0xC0) == 0xC0) {
                runLength = dataByte & 0x3F;
                dataByte = *raw++;
            }

            while (runLength-- > 0) {
                pix[x++] = dataByte;
            }
        }
    }
    free(raw_b);

    if (isSkin) {
        FloodFillSkin(out1, pcx_width + 1, pcx_height + 1);
    }

    uint32_t *out = (uint32_t *)malloc(full_size * 4);
    for (int i = 0; i < full_size; i++) {
        out[i] = d_8to24table[out1[i]];
    }

    strcat(fullpath, fname);
    strtolower(fullpath);
    int l = strlen(fullpath);
    strcpy(&fullpath[l - 4], ".png");

    bool r = writePng(fullpath, pcx_width + 1, pcx_height + 1, out);
    free(out);
    return r;
}


/*
* Load WAL and write PNG.
*/

static bool convertWal(const fileEntry& entry, const char *outPath) {
    char fullpath[1024];
    char fname[32];
    splitPath(entry, outPath, fullpath, fname);

    fseek(entry.file, entry.offset, SEEK_SET);
    miptex_t mt;
    if (fread(&mt, sizeof(miptex_t), 1, entry.file) != 1) {
        fprintf(stderr, "Failed to mip header\n");
        return false;
    }

    if ((mt.offsets[0] <= 0) || (mt.width <= 0) || (mt.height <= 0) ||
        (((entry.length - mt.offsets[0]) / mt.height) < mt.width)) {
        fprintf(stderr, "Bad mip file %s\n", entry.name);
        return false;
    }

    strcat(fullpath, fname);
    int l = strlen(fullpath);
    strtolower(fullpath);
    strcpy(&fullpath[l - 4], ".png");

    int fullsize = mt.width * mt.height;
    byte *raw = (byte *)malloc(fullsize);
    fseek(entry.file, entry.offset + mt.offsets[0], SEEK_SET);
    if (fread(raw, fullsize, 1, entry.file) != 1) {
        fprintf(stderr, "Failed to read mt data\n");
        free(raw);
        return false;
    }

    uint32_t *out = (uint32_t *)malloc(fullsize * 4);
    for (int i = 0; i < fullsize; i++) {
        out[i] = d_8to24table[raw[i]];
    }
    free(raw);

    bool r = writePng(fullpath, mt.width, mt.height, out);
    free(out);
    return r;
}

int main(int argc, const char * argv[]) {

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage q2unpack [-nc] inpath outpath\n");
        fprintf(stderr, " -nc: Do not convert to imagess\n");
        return 1;
    }

    int arg_index = 1;
    bool convert = true;
    if (argc == 4)
    {
        if (strcmp(argv[1], "-nc") != 0) {
            fprintf(stderr, "Usage q2unpack [-nc] inpath outpath\n");
            fprintf(stderr, " -nc: Do not convert to imagess\n");
            return 1;
        }
        arg_index = 2;
        convert = false;
    }

    char path[1024];
    strncpy(path, argv[arg_index + 1], 1024);
    char *d = &path[strlen(path)-1];
    if (*d != '/') {
        *(++d) = '/';
        *(++d) = 0;
    }
    mkdir(argv[arg_index + 1], 0777);

    char picspath[1024];
    sprintf(picspath,"%spics", path);

    if (!readDir(argv[arg_index], "")) {
        return 1;
    }

    printf("Files: %lu\n", entries.size());
    if (convert && !loadPalette("pics/colormap.pcx", picspath, "colormap.bin")) {
        return 1;
    }

    for (const fileEntry& entry : entries) {
        int len = int(strlen(entry.name));
        if (convert) {
            if (strcmp(entry.name, "pics/colormap.pcx") == 0) { // We already handled this one
            } else if (len > 4 && strcmp(&entry.name[len - 4], ".pcx") == 0) {
                bool isSkin = strncmp(entry.name, "models", 6) == 0 || strncmp(entry.name, "players", 7) == 0;
                if (!convertPcx(entry, path, isSkin)) {
                    return 1;
                }
            } else if (len > 4 && strcmp(&entry.name[len - 4], ".wal") == 0) {
                if (!convertWal(entry, path)) {
                    return 1;
                }
            } else if (len > 4 && strcmp(&entry.name[len - 4], ".tga") == 0) {
                // TODO!!!!
                printf("TGA %s\n", entry.name);
            } else {
                // Just copy the rest of the files
                if (!copyFile(entry, path)) {
                    return 1;
                }
            }
        } else {
            if (!copyFile(entry, path)) {
                return 1;
            }
        }
    }

    entries.clear();
    return 0;
}

