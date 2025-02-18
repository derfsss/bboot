/*
 * BBoot boot loader zip support
 * (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ZIP_H
#define ZIP_H

#define Z_OK 0
#define Z_ERROR -1

typedef struct {
    const char *data;
    unsigned long len;
    void *er;
    void *gfh;
} zip_t;

int zip_openBuffer(zip_t *z, const char *zibdata, unsigned long ziplen);
unsigned long zip_numEntries(zip_t *z);
int zip_readdir(zip_t *z);
unsigned long zip_file_compressedSize(zip_t *z);
unsigned long zip_file_uncompressedSize(zip_t *z);
unsigned long zip_file_fileNameLength(zip_t *z);
int zip_file_get_name(zip_t *z, char *buf);
int zip_file_find(zip_t *z, const char *name);
int zip_file_extract(zip_t *z, void *buffer);

#endif
