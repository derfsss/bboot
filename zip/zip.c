/*
 * BBoot boot loader zip support
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stddef.h>
#include <string.h>
#include "bboot.h"
#include "junzip.h"
#include "puff.h"
#include "zip.h"

int zip_openBuffer(zip_t *z, const char *zipdata, unsigned long ziplen)
{
    long i;
    JZEndRecord *er;

    /* no state or too small file to be a zip */
    if (!z || ziplen <= sizeof(JZEndRecord)) return Z_ERROR;
    for(i = ziplen - sizeof(JZEndRecord); i >= 0; i--) {
        er = (JZEndRecord *)(zipdata + i);
        if(BE32(er->signature) == 0x504B0506)
            break;
    }
    if(i < 0) return Z_ERROR; /* no end record found */
    if(er->diskNumber || er->centralDirectoryDiskNumber ||
       er->numEntries != er->numEntriesThisDisk) {
        /* multi file zip not supported */
        return Z_ERROR;
    }
    z->data = zipdata;
    z->len = ziplen;
    z->er = er;
    z->gfh = NULL;
    return Z_OK;
}

unsigned long zip_numEntries(zip_t *z)
{
    JZEndRecord *er = z->er;
    return LE16(er->numEntries);
}

int zip_readdir(zip_t *z)
{
    if (!z || !z->er) return Z_ERROR;
    unsigned long offs;
    JZGlobalFileHeader *gfh = z->gfh;
    if (!gfh) {
        JZEndRecord *er = z->er;
        offs = LE32(er->centralDirectoryOffset);
    } else {
        offs = (char *)gfh - z->data;
        offs += sizeof(JZGlobalFileHeader);
        offs += LE16(gfh->fileNameLength);
        offs += LE16(gfh->extraFieldLength);
    }
    if (offs >= z->len) return Z_ERROR; /* invalid offset */
    z->gfh = (void *)(z->data + offs);
    gfh = z->gfh;
    if (gfh->signature != BE32(0x504B0102)) {
        z->gfh = NULL;
        return Z_ERROR;
    }
    return Z_OK;
}

unsigned long zip_file_compressedSize(zip_t *z)
{
    JZGlobalFileHeader *gfh = z->gfh;
    return LE32(gfh->compressedSize);
}

unsigned long zip_file_uncompressedSize(zip_t *z)
{
    JZGlobalFileHeader *gfh = z->gfh;
    return LE32(gfh->uncompressedSize);
}

unsigned long zip_file_fileNameLength(zip_t *z)
{
    JZGlobalFileHeader *gfh = z->gfh;
    return LE16(gfh->fileNameLength);
}

int zip_file_get_name(zip_t *z, char *buf)
{
    if (!z || !z->gfh) return Z_ERROR;
    unsigned long len = zip_file_fileNameLength(z);
    memcpy(buf, (char *)z->gfh + sizeof(JZGlobalFileHeader), len);
    buf[len] = '\0';
    return Z_OK;
}

int zip_file_find(zip_t *z, const char *name)
{
    if (!z || !z->er || !name) return Z_ERROR;
    z->gfh = NULL;
    char fnbuf[256];
    unsigned long i, n, len = strlen(name);
    n = zip_numEntries(z);
    for (i = 0; i < n; i++) {
        if (zip_readdir(z)) break;
        unsigned long fnlen = zip_file_fileNameLength(z);
        if (fnlen > sizeof(fnbuf) - 1) return Z_ERROR;
        if (fnlen == len) {
            zip_file_get_name(z, fnbuf);
            if (!strcmp(fnbuf, name)) return Z_OK;
        }
    }
    return Z_ERROR;
}

int zip_file_extract(zip_t *z, void *buffer)
{
    if (!z || !z->gfh || !buffer) return Z_ERROR;
    JZGlobalFileHeader *gfh = z->gfh;
    JZLocalFileHeader *lfh = (JZLocalFileHeader *)(z->data +
                             LE32(gfh->relativeOffsetOflocalHeader));
    if (lfh->signature != BE32(0x504B0304)) return Z_ERROR;
    unsigned char *bits = (unsigned char *)lfh + sizeof(*lfh) +
                          LE16(lfh->fileNameLength) + LE16(lfh->extraFieldLength);
    switch(LE16(lfh->compressionMethod)) {
    case 0: /* store */
        if (gfh->compressedSize != gfh->uncompressedSize) return Z_ERROR;
        memcpy(buffer, bits, LE32(gfh->uncompressedSize));
        break;
    case 8: /* deflate */
    {
        unsigned long dstlen = LE32(gfh->uncompressedSize),
                      srclen = LE32(gfh->compressedSize);
        if (puff(buffer, &dstlen, bits, &srclen)) return Z_ERROR;
        break;
    }
    default:
        return Z_ERROR;
    }
    return Z_OK;
}
