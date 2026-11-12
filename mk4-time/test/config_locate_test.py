#!/usr/bin/env python3
"""config_locate_test.py: does the firmware find config.txt's real sectors and directory entry?

config_write.h rewrites config.txt's own flash sectors. Its cw_locate works out where they are
through FATFS, and where the directory entry is by reading the root directory. This builds that
function with the firmware's own ff.c and ffconf.h (test/config_locate_test.c), makes FAT12 and
FAT16 images with 4096-byte sectors laid out every awkward way, and compares what cw_locate
reports with where things really are. "Really" is decided twice: by the builder, which placed
them, and by parse(), a FAT reader written here that shares nothing with FATFS.

It also checks the refusals: a file over 32 KB, a volume with more than one sector a cluster,
no config.txt at all.

    python3 test/config_locate_test.py            (from mk4-time/)
    python3 test/config_locate_test.py IMAGE...   also check these images (a real drive's, say)
"""
import json, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK4 = os.path.dirname(HERE)
SS = 4096


def build_harness(out):
    stubs = os.path.join(os.path.dirname(out), "stubs")
    os.makedirs(stubs, exist_ok=True)
    for h in ("main.h", "stm32l4xx_hal.h"):          # ffconf.h includes them; nothing in them is needed here
        open(os.path.join(stubs, h), "w").write("/* empty: host build of ff.c */\n")
    ff = os.path.join(MK4, "Middlewares/Third_Party/FatFs/src")
    cmd = ["cc", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-I", stubs,
           "-I", os.path.join(MK4, "FATFS/Target"), "-I", ff, "-I", os.path.join(MK4, "Core/Src"),
           os.path.join(HERE, "config_locate_test.c"), os.path.join(ff, "ff.c"), "-o", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-3000:])
        sys.exit("could not build the harness")


def sfn(name):
    base, _, ext = name.upper().partition(".")
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def make_image(total_sectors, files, spc=1, root_entries=512, extra_dir=()):
    """files: list of (name, content bytes, cluster chain, directory index). extra_dir: (index, 32 raw bytes)."""
    reserved, nfats = 1, 2
    root_secs = root_entries * 32 // SS
    fat_sz = 1
    while True:                                       # the FAT size depends on the cluster count, and the other way round
        data = total_sectors - reserved - nfats * fat_sz - root_secs
        clusters = data // spc
        bits = 12 if clusters < 4085 else 16
        need = -(-((clusters + 2) * bits // 8 + 1) // SS)
        if need <= fat_sz:
            break
        fat_sz = need
    img = bytearray(total_sectors * SS)
    bpb = bytearray(SS)
    bpb[0:3] = b"\xEB\x3C\x90"; bpb[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", bpb, 11, SS, spc, reserved, nfats, root_entries,
                     total_sectors if total_sectors < 65536 else 0, 0xF8, fat_sz, 63, 255, 0,
                     0 if total_sectors < 65536 else total_sectors)
    bpb[36] = 0x80; bpb[38] = 0x29; bpb[39:43] = b"\x12\x34\x56\x78"; bpb[43:54] = b"CLOCK      "
    bpb[54:62] = b"FAT12   " if bits == 12 else b"FAT16   "
    bpb[510] = 0x55; bpb[511] = 0xAA
    img[0:SS] = bpb
    fat = [0] * (clusters + 2)
    fat[0] = 0xFF8 if bits == 12 else 0xFFF8
    fat[1] = 0xFFF if bits == 12 else 0xFFFF
    root_start = reserved + nfats * fat_sz
    data_start = root_start + root_secs
    truth = {}
    for name, content, chain, idx in files:
        assert len(chain) * spc * SS >= len(content) and all(2 <= c < clusters + 2 and fat[c] == 0 for c in chain), name
        for a, b in zip(chain, chain[1:]):
            fat[a] = b
        if chain:
            fat[chain[-1]] = 0xFFF if bits == 12 else 0xFFFF
        for i, c in enumerate(chain):
            part = content[i * spc * SS:(i + 1) * spc * SS]
            off = (data_start + (c - 2) * spc) * SS
            img[off:off + len(part)] = part
        e = bytearray(32); e[0:11] = sfn(name); e[11] = 0x20
        struct.pack_into("<HHH", e, 22, 0x4321, 0x5B6C, chain[0] if chain else 0)
        struct.pack_into("<I", e, 28, len(content))
        img[root_start * SS + idx * 32: root_start * SS + idx * 32 + 32] = e
        truth[name.upper()] = {"size": len(content), "sectors": [(data_start + (c - 2) * spc + k) * SS for c in chain for k in range(spc)],
                                "dir_addr": (root_start + idx * 32 // SS) * SS, "dir_off": idx * 32 % SS}
    for idx, raw in extra_dir:
        img[root_start * SS + idx * 32: root_start * SS + idx * 32 + 32] = raw
    fb = bytearray(fat_sz * SS)
    if bits == 12:
        for i, v in enumerate(fat):
            o = i * 3 // 2
            if i & 1:
                fb[o] |= (v << 4) & 0xF0; fb[o + 1] = v >> 4
            else:
                fb[o] = v & 0xFF; fb[o + 1] |= (v >> 8) & 0x0F
    else:
        for i, v in enumerate(fat):
            struct.pack_into("<H", fb, i * 2, v)
    for k in range(nfats):
        img[(reserved + k * fat_sz) * SS:(reserved + (k + 1) * fat_sz) * SS] = fb
    return bytes(img), truth, bits


def parse(img, want=b"CONFIG  TXT"):
    """An independent reading of a FAT12/16 image: where is the file, and where is its directory entry?"""
    bps, spc, reserved, nfats, root_entries, tot16, _media, fat_sz = struct.unpack_from("<HBHBHHBH", img, 11)
    total = tot16 or struct.unpack_from("<I", img, 32)[0]
    root_start = reserved + nfats * fat_sz
    root_secs = -(-root_entries * 32 // bps)
    data_start = root_start + root_secs
    clusters = (total - data_start) // spc
    bits = 12 if clusters < 4085 else 16
    fat0 = reserved * bps

    def nxt(c):
        if bits == 16:
            return struct.unpack_from("<H", img, fat0 + c * 2)[0]
        v = struct.unpack_from("<H", img, fat0 + c * 3 // 2)[0]
        return v >> 4 if c & 1 else v & 0xFFF
    for i in range(root_entries):
        o = root_start * bps + i * 32
        e = img[o:o + 32]
        if e[0] == 0:
            break
        if e[0:11] != want or e[11] & 0x18 or e[11] == 0x0F:
            continue
        cl, size = struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
        chain, c = [], cl
        while 2 <= c < (0xFF8 if bits == 12 else 0xFFF8) and len(chain) < 4096:
            chain.append(c); c = nxt(c)
        used = -(-size // (spc * bps)) if size else 0                 # clusters the size says the file has
        return {"bps": bps, "spc": spc, "bits": bits, "size": size,
                "sectors": [(data_start + (c - 2) * spc + k) * bps for c in chain[:used] for k in range(spc)],
                "dir_addr": (o // bps) * bps, "dir_off": o % bps}
    return {"bps": bps, "spc": spc, "bits": bits, "size": None}


def text(n):
    lines, i = [], 0
    while sum(len(l) + 2 for l in lines) < n:
        lines.append("# line %05d ........................................" % i); i += 1
    return ("\r\n".join(lines) + "\r\n").encode()[:n]


def lfn_entry(seq, chars):                            # a long-name entry: attribute 0x0F, never a file's own entry
    e = bytearray(32); e[0] = seq; e[11] = 0x0F
    u = chars.encode("utf-16-le").ljust(26, b"\xFF")
    e[1:11] = u[0:10]; e[14:26] = u[10:22]; e[28:32] = u[22:26]
    return bytes(e)


def main():
    work = tempfile.mkdtemp(prefix="cfglocate-")
    exe = os.path.join(work, "config_locate_test")
    build_harness(exe)
    results = []

    def check(name, ok):
        results.append(ok); print(("PASS  " if ok else "FAIL  ") + name)

    def run(img):
        p = os.path.join(work, "img.bin"); open(p, "wb").write(img)
        return json.loads(subprocess.run([exe, p], capture_output=True, text=True).stdout)

    def agrees(tag, img, truth):
        got, ref = run(img), parse(img)
        t = truth["CONFIG.TXT"]
        same_ref = ref["size"] == t["size"] and ref["sectors"] == t["sectors"] and ref["dir_addr"] == t["dir_addr"] and ref["dir_off"] == t["dir_off"]
        check(f"{tag}: the independent reader agrees with where the builder put things", same_ref)
        ok = got.get("found") and got["size"] == t["size"] and got["sectors"] == t["sectors"] and got["dir_addr"] == t["dir_addr"] and got["dir_off"] == t["dir_off"]
        check(f"{tag}: cw_locate reports the same sectors and the same directory entry ({len(t['sectors'])} sectors, entry at {t['dir_addr']:#x}+{t['dir_off']})", bool(ok))
        if ok:
            e = img[got["dir_addr"] + got["dir_off"]: got["dir_addr"] + got["dir_off"] + 32]
            body = b"".join(img[a:a + SS] for a in got["sectors"])[:got["size"]]
            check(f"{tag}: those addresses hold config.txt's entry and config.txt's bytes", e[0:11] == b"CONFIG  TXT" and body == truth["_content"])

    label = bytearray(32); label[0:11] = b"CLOCK      "; label[11] = 0x08
    deleted = bytearray(32); deleted[0:11] = b"\xE5ONFIG  TXT"; deleted[11] = 0x20; struct.pack_into("<HI", deleted, 26, 77, 500)
    folder = bytearray(32); folder[0:11] = b"CONFIG  TXT"; folder[11] = 0x10
    gone = bytearray(32); gone[0] = 0xE5; gone[1:11] = b"LDFILE TMP"; gone[11] = 0x20
    c = text(15710)                                   # the size of a real config.txt

    def others(spc):                                  # the files that share the drive: SETTINGS.BIN is 16 KB
        return [("SETTINGS.BIN", b"\xFF" * 16384, list(range(2, 2 + -(-4 // spc))), 1), ("TZRULES.BIN", text(9000), list(range(8, 8 + -(-3 // spc))), 2)]

    def case(tag, total, spc, chain, idx, content=c, extra=()):
        img, truth, bits = make_image(total, others(spc) + [("CONFIG.TXT", content, chain, idx)], spc=spc, extra_dir=[(0, bytes(label))] + list(extra))
        truth["_content"] = content
        # cw_locate reports every sector of the file's clusters, 32 KB at most, in file order
        agrees(f"{tag} (FAT{bits}, {spc} sector{'s' if spc > 1 else ''} a cluster)", img, truth)

    # The clock's drive: 16 MB, four sectors a cluster. config.txt is then one cluster.
    case("the clock's layout, 16 MB", 4096, 4, [20], 3)
    # Its entry in the second sector of the root directory, after a deleted entry of the same name,
    # long-name entries and a run of deleted files. (A folder of the same name cannot be there too: a
    # directory holds a name once, and FATFS would open the first.)
    fill = [(i, bytes(gone)) for i in range(5, 138)]
    extra = [(3, bytes(gone)), (4, bytes(deleted)), (138, lfn_entry(0x42, "g.txt")), (139, lfn_entry(0x01, "a long confi"))] + fill
    case("the clock's layout, entry in the directory's second sector", 4096, 4, [777], 140, extra=extra)
    # Two clusters, not next to each other.
    case("two clusters apart", 4096, 4, [300, 31], 3, content=text(20000))
    # Other shapes a drive could have.
    case("one sector a cluster, clusters scattered", 4096, 1, [900, 31, 2500, 44], 3)
    case("two sectors a cluster", 4096, 2, [50, 7], 3)
    case("a 24 MB volume, scattered", 6000, 1, [5000, 4999, 30, 5001], 3)
    for n, chain in ((1, [20]), (4096, [20]), (4097, [20]), (16384, [20]), (16385, [20, 9]), (32768, [40, 41])):
        case(f"config.txt of {n} bytes", 4096, 4, chain, 3, content=text(n))

    # The refusals.
    img, _, _ = make_image(4096, others(4) + [("CONFIG.TXT", text(32769), [40, 41, 42], 3)], spc=4, extra_dir=[(0, bytes(label))])
    check("a config.txt over 32 KB is refused", run(img).get("found") is False)
    img, _, _ = make_image(4096, others(4), spc=4, extra_dir=[(0, bytes(label)), (5, bytes(folder))])
    check("no config.txt (only a folder of that name) is refused", run(img).get("found") is False)

    for path in sys.argv[1:]:                         # real images, checked against the independent reader only
        img = open(path, "rb").read()
        got, ref = run(img), parse(img)
        ok = ref["size"] is not None and got.get("found") and got["size"] == ref["size"] and got["sectors"] == ref["sectors"] and got["dir_addr"] == ref["dir_addr"] and got["dir_off"] == ref["dir_off"]
        check(f"{os.path.basename(path)}: FAT{ref['bits']}, config.txt of {ref['size']} bytes in {len(ref.get('sectors', []))} sectors: cw_locate agrees with the independent reader", bool(ok))

    print("\nALL PASS" if all(results) else "\n%d FAIL" % results.count(False))
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
