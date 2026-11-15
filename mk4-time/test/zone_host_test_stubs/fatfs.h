/* fatfs.h for the host build of zonedetect.c (test/zone_host_test.c): the file calls it makes, over a
 * map held in memory. Not the firmware's FATFS; test/config_locate_test.py is the test that uses that. */
#ifndef ZONE_HOST_TEST_FATFS_H
#define ZONE_HOST_TEST_FATFS_H
#include <stdint.h>

typedef unsigned int UINT;
typedef struct { unsigned char err; uint32_t size, fptr; } FIL;

int f_lseek(FIL *fp, uint32_t ofs);
int f_read(FIL *fp, void *buff, UINT btr, UINT *br);
int f_close(FIL *fp);
#define f_size(fp) ((fp)->size)

#endif
