/* zone_host_test.c: the timezone lookup gives way to a computer using the drive.
 *
 * The lookup reads TZMAP.BIN through the same flash chip a connected computer reads the drive
 * through. Stock waited inside the lookup until the computer's reads stopped. A computer reading for
 * longer than the watchdog's 8 s (one pass over TZMAP.BIN takes 16 s) then reset the clock. The
 * lookup now gives up instead, and this checks what giving up has to guarantee:
 *
 *   - it returns, whenever the computer starts and however long it goes on;
 *   - it reads nothing more from the drive once the computer has started;
 *   - it never returns a wrong zone made from the polygons it had seen so far;
 *   - it frees what it allocated;
 *   - the next lookup, with the computer gone, gives the right answer.
 *
 * It builds the firmware's own zonedetect.c (nothing else) over a map held in memory, and has the
 * computer start at every drive read a lookup makes, one run for each. Not part of the firmware
 * build; build and run on a host, from mk4-time/:
 *
 *   cc -std=gnu99 -O1 -I test/zone_host_test_stubs -I Core/Inc test/zone_host_test.c -lm -o /tmp/zone_host_test
 *   /tmp/zone_host_test ../qspi/output/tzmap.bin
 *
 * Against a zonedetect.c that waits, the second case never returns and the alarm reports it.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* ---- what zonedetect.c takes from the rest of the firmware ---------------------------------- */
volatile uint32_t qspi_write_time = 0, qspi_usb_read_time = 0;   /* the storage handlers set these */

static long live = 0;                                           /* allocations not yet freed */
static void *t_malloc(size_t n){ void *p = malloc(n); if (p) live++; return p; }
static void *t_realloc(void *p, size_t n){ void *q = realloc(p, n); if (q && !p) live++; return q; }
static void  t_free(void *p){ if (p) live--; free(p); }

#include "fatfs.h"
static const uint8_t *map; static uint32_t map_n;
static long reads = 0;                    /* drive reads the lookup has made */
static long host_at = -1;                 /* the computer starts as this many reads complete; -1: never */
static int  host_writes = 0;              /* ...with a write, not a read */
static long host_began = -1;              /* reads made when it started; -1: it has not */
int f_lseek(FIL *fp, uint32_t ofs){ fp->fptr = ofs; return 0; }
int f_read(FIL *fp, void *buff, UINT btr, UINT *br){
  uint32_t n = 0;
  if (fp->fptr < map_n) { n = map_n - fp->fptr; if (n > btr) n = btr; memcpy(buff, map + fp->fptr, n); }
  fp->fptr += n; *br = n;
  reads++;
  if (host_at >= 0 && reads == host_at) {
    if (host_writes) qspi_write_time = 1; else qspi_usb_read_time = 1;
    host_began = reads;
  }
  return 0;
}
int f_close(FIL *fp){ (void)fp; return 0; }

#define malloc  t_malloc
#define realloc t_realloc
#define free    t_free
#ifndef ZD_SRC
#define ZD_SRC "../Core/Src/zonedetect.c"
#endif
#include ZD_SRC
#undef malloc
#undef realloc
#undef free

/* ---- the test ------------------------------------------------------------------------------- */
static int fails = 0, total = 0;
static const char *doing = "";
#define CHECK(cond, ...) do { total++; if (!(cond)) { fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void on_alarm(int sig){
  (void)sig;
  printf("  FAIL %s: the lookup did not return. It is waiting for the computer's reads to stop.\n", doing);
  printf("\nFAILED: the stall. The rest was not run.\n");
  fflush(stdout);
  _exit(1);
}

/* a zone string is allocated in zonedetect.c and freed by its caller: count that too */
static void host_off(void){ host_at = -1; host_writes = 0; host_began = -1; qspi_write_time = 0; qspi_usb_read_time = 0; }

/* one lookup, the way the main loop does it: open the map, look up, close */
static char *lookup(float lat, float lon){
  FIL f; memset(&f, 0, sizeof f); f.size = map_n;
  char *zone = NULL;
  ZoneDetect *zdb = ZDOpenDatabase(&f);
  if (zdb) { zone = ZDHelperSimpleLookupString(zdb, lat, lon); ZDCloseDatabase(zdb); }
  return zone;
}

static const struct { const char *name; float lat, lon; const char *zone; } place[] = {
  { "Bath",      51.4044f,   -2.3235f, "Europe/London" },
  { "New York",  40.7128f,  -74.0060f, "America/New_York" },
  { "Tokyo",     35.6762f,  139.6503f, "Asia/Tokyo" },
  { "Sydney",   -33.8688f,  151.2093f, "Australia/Sydney" },
  { "Maseru",   -29.3100f,   27.4800f, "Africa/Maseru" },        /* a zone that is a hole in another */
};
#define NPLACE (int)(sizeof place / sizeof place[0])

int main(int argc, char **argv){
  const char *path = argc > 1 ? argv[1] : "../qspi/output/tzmap.bin";
  FILE *fh = fopen(path, "rb");
  if (!fh) { printf("cannot open %s\n", path); return 2; }
  fseek(fh, 0, SEEK_END); map_n = (uint32_t)ftell(fh); fseek(fh, 0, SEEK_SET);
  uint8_t *buf = malloc(map_n);
  if (!buf || fread(buf, 1, map_n, fh) != map_n) { printf("cannot read %s\n", path); return 2; }
  fclose(fh); map = buf;
  signal(SIGALRM, on_alarm);

  /* 1. with no computer: the right zone, nothing left allocated */
  long need[NPLACE];
  printf("1. no computer on the drive\n");
  for (int i = 0; i < NPLACE; i++) {
    host_off(); reads = 0; long before = live;
    doing = place[i].name; alarm(20);
    char *z = lookup(place[i].lat, place[i].lon);
    alarm(0);
    need[i] = reads;
    printf("   %-9s %-18s %ld drive reads\n", place[i].name, z ? z : "(none)", reads);
    CHECK(z && !strcmp(z, place[i].zone), "%s: expected %s", place[i].name, place[i].zone);
    t_free(z);
    CHECK(live == before, "%s: %ld allocations left after a lookup", place[i].name, live - before);
  }
  if (fails) { printf("\nFAILED: the map or the places are wrong. The rest was not run.\n"); return 1; }

  /* 2. the computer is already reading, and goes on reading */
  printf("2. the computer is reading before the lookup starts, and does not stop\n");
  for (int i = 0; i < NPLACE; i++) {
    host_off(); qspi_usb_read_time = 1; reads = 0; long before = live;
    doing = place[i].name; alarm(5);
    char *z = lookup(place[i].lat, place[i].lon);
    alarm(0);
    CHECK(z == NULL, "%s: a zone came back (%s)", place[i].name, z);
    CHECK(reads == 0, "%s: %ld drive reads made while the computer was reading", place[i].name, reads);
    t_free(z);
    CHECK(live == before, "%s: %ld allocations left", place[i].name, live - before);
  }
  printf("   every lookup returned at once, with no zone and no drive reads: %s\n", fails ? "NO" : "yes");

  /* 3. the computer starts part-way: at every drive read of the lookup, once with a read and once with
        a write, and keeps going. Then, with the computer gone, the next lookup must be right. */
  for (int w = 0; w < 2; w++) {
    long runs = 0, gave_up = 0, wrong = 0, read_on = 0, leaked = 0, next_wrong = 0;
    for (int i = 0; i < NPLACE; i++) {
      for (long k = 1; k < need[i]; k++) {
        host_off(); host_at = k; host_writes = w; reads = 0; long before = live;
        doing = place[i].name; alarm(5);
        char *z = lookup(place[i].lat, place[i].lon);
        alarm(0);
        runs++;
        if (!z) gave_up++;
        if (z && strcmp(z, place[i].zone)) wrong++;              /* a zone from part of the map */
        if (host_began >= 0 && reads != host_began) read_on++;   /* a drive read after the computer started */
        t_free(z);
        if (live != before) { leaked++; live = before; }
        host_off(); reads = 0;
        char *again = lookup(place[i].lat, place[i].lon);
        if (!again || strcmp(again, place[i].zone)) next_wrong++;
        t_free(again);
      }
    }
    printf("3%c. the computer starts a %s part-way through: %ld runs, %ld gave the lookup up\n",
           w ? 'b' : 'a', w ? "write" : "read", runs, gave_up);
    CHECK(gave_up == runs, "%ld runs carried on to an answer", runs - gave_up);
    CHECK(wrong == 0,      "%ld runs returned a wrong zone", wrong);
    CHECK(read_on == 0,    "%ld runs read the drive after the computer started", read_on);
    CHECK(leaked == 0,     "%ld runs left memory allocated", leaked);
    CHECK(next_wrong == 0, "%ld times the next lookup, with the computer gone, was wrong", next_wrong);
  }

  /* 4. given up stays given up: the computer's read ending does not start the lookup reading again,
        until a new lookup opens the map */
  printf("4. the computer stops reading while a given-up lookup is still unwinding\n");
  {
    FIL f; memset(&f, 0, sizeof f); f.size = map_n;
    host_off(); reads = 0;
    ZoneDetect *zdb = ZDOpenDatabase(&f);
    CHECK(zdb != NULL, "the map did not open");
    uint32_t far1 = map_n / 2, far2 = map_n / 3, far3 = map_n / 4;   /* nowhere near the cached block */
    long r0 = reads;
    qspi_usb_read_time = 1;
    uint8_t a = readMapFile(far1);
    CHECK(a == 0 && reads == r0 && zd_abort, "a read with the computer reading was not given up");
    qspi_usb_read_time = 0;
    uint8_t b = readMapFile(far2), c = readMapFileReverse(far3);
    CHECK(b == 0 && c == 0 && reads == r0, "the lookup read the drive again after giving up (%ld reads)", reads - r0);
    ZDCloseDatabase(zdb);
    zdb = ZDOpenDatabase(&f);
    CHECK(zdb != NULL && !zd_abort, "a new lookup did not start clean");
    r0 = reads;
    (void)readMapFile(far1);
    CHECK(reads == r0 + 1, "a new lookup could not read the drive");
    ZDCloseDatabase(zdb);
  }

  printf("\n%s: %d of %d checks failed\n", fails ? "FAILED" : "PASSED", fails, total);
  return fails ? 1 : 0;
}
