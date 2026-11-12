/* config_locate_test.c: cw_locate against the real FATFS, on a disk image.
 *
 * config_write.h rewrites config.txt's own flash sectors, so it must know exactly where they are, and
 * where the file's directory entry is. A wrong address would write over something else on the drive.
 * The emulator cannot check this: its file system is a stand-in with no sectors. This builds the
 * firmware's own cw_locate (and nothing else of that file) with the firmware's own ff.c and ffconf.h,
 * mounts a disk image held in memory, and prints what cw_locate found. test/config_locate_test.py
 * makes the images, knows where everything really is, and compares.
 *
 *   cc ... -o config_locate_test test/config_locate_test.c  (see config_locate_test.py for the line)
 *   ./config_locate_test image.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ff.h"
#include "diskio.h"

static uint8_t *img; static size_t img_n;

/* the disk: the image, 4096-byte sectors */
DSTATUS disk_initialize(BYTE pdrv){ (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv){ (void)pdrv; return 0; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count){
  (void)pdrv;
  if (((size_t)sector + count) * 4096u > img_n) return RES_PARERR;
  memcpy(buff, img + (size_t)sector * 4096u, (size_t)count * 4096u);
  return RES_OK;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff){
  (void)pdrv;
  if (cmd == GET_SECTOR_SIZE) { *(WORD*)buff = 4096; return RES_OK; }
  if (cmd == GET_SECTOR_COUNT) { *(DWORD*)buff = (DWORD)(img_n / 4096u); return RES_OK; }
  return RES_OK;
}

/* what config_write.h's locator uses from the rest of the firmware */
#define EE_QSPI_PGSZ 4096u
#define EE_FLASH_END ((uint32_t)img_n)
#define CONFIG_FILENAME "/CONFIG.TXT"
static FATFS USERFatFS;
static _Bool ee_rd_fault = 0;
static void ee_rd(uint32_t a, void *d, uint32_t n){
  if ((size_t)a + n > img_n) { ee_rd_fault = 1; memset(d, 0, n); return; }
  memcpy(d, img + a, n);
}
#define CW_LOCATE_ONLY
#include "main_parts/config_write.h"

int main(int argc, char **argv){
  if (argc < 2) return 2;
  FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
  fseek(f, 0, SEEK_END); img_n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  img = malloc(img_n); if (!img || fread(img, 1, img_n, f) != img_n) return 2;
  fclose(f);
  if (f_mount(&USERFatFS, "", 1) != FR_OK) { printf("{\"mounted\":false}\n"); return 0; }
  int ok = cw_locate();
  printf("{\"mounted\":true,\"fs_type\":%d,\"csize\":%d,\"found\":%s", USERFatFS.fs_type, USERFatFS.csize, ok ? "true" : "false");
  if (ok) {
    printf(",\"size\":%lu,\"dir_addr\":%lu,\"dir_off\":%u,\"sectors\":[", (unsigned long)cw_size, (unsigned long)cw_dir_addr, (unsigned)cw_dir_off);
    for (unsigned i = 0; i < cw_nsect; i++) printf("%s%lu", i ? "," : "", (unsigned long)cw_sect[i]);
    printf("]");
  }
  printf("}\n");
  return 0;
}
