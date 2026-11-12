#!/usr/bin/env python3
# objeq.py A.o B.o: are two ARM relocatable objects the same code and data, whatever the order?
#
# For checking that a rearrangement of main.c and its sections (Core/Src/main_parts) moved code
# without changing it. Each function and each variable has its own section (-ffunction-sections
# -fdata-sections). Two objects are equivalent when they hold the same set of sections, each with the
# same bytes and the same relocations, where a relocation is compared by what it points AT (a named
# symbol, the string it addresses, or the object inside a shared section) and not by where that is.
# Compiler-numbered names (static locals "x.12", clones "f.part.0") are compared with the number
# removed. Exit status 0 and "EQUIVALENT", or 1 and the sections that differ.
#
#   python3 test/objeq.py before/main.o Release/Core/Src/main.o
import sys, struct, re, collections
def load(path):
    b=open(path,'rb').read()
    assert b[:4]==b'\x7fELF' and b[4]==1 and b[5]==1
    shoff,=struct.unpack_from('<I',b,0x20); shentsize,shnum,shstrndx=struct.unpack_from('<HHH',b,0x2E)
    sh=[]
    for i in range(shnum):
        name,typ,flags,addr,off,size,link,info,align,entsize=struct.unpack_from('<10I',b,shoff+i*shentsize)
        sh.append(dict(name=name,type=typ,flags=flags,off=off,size=size,link=link,info=info,entsize=entsize))
    strtab=lambda sec,o: b[sec['off']+o:b.index(b'\0',sec['off']+o)].decode()
    for s in sh: s['n']=strtab(sh[shstrndx],s['name'])
    symsec=[s for s in sh if s['type']==2][0]; symstr=sh[symsec['link']]
    syms=[]
    for i in range(symsec['size']//16):
        nm,val,size,info,other,shndx=struct.unpack_from('<IIIBBH',b,symsec['off']+i*16)
        syms.append(dict(name=strtab(symstr,nm),value=val,size=size,type=info&15,bind=info>>4,shndx=shndx))
    return b,sh,syms
norm=lambda n: re.sub(r'\.\d+(?=$|\.)','.#',n)
def describe(path):
    b,sh,syms=load(path)
    rels=collections.defaultdict(list)
    for s in sh:
        if s['type']==9:                                   # SHT_REL
            for i in range(s['size']//8):
                off,info=struct.unpack_from('<II',b,s['off']+i*8); rels[s['info']].append((off,info&0xff,info>>8))
    out=collections.Counter(); sizes=collections.Counter()
    for idx,s in enumerate(sh):
        if not (s['flags']&2): continue                     # SHF_ALLOC only
        if s['flags']&0x10 and s['flags']&0x20: continue    # merged strings: compared through what points at them
        if s['size']==0 and not rels.get(idx): continue
        data=bytearray(b[s['off']:s['off']+s['size']]) if s['type']!=8 else bytearray()
        rl=[]
        for off,typ,symi in sorted(rels.get(idx,[])):
            sy=syms[symi]
            if sy['type']==3:                               # section symbol: an address inside another section
                tgt=sh[sy['shndx']]
                if typ in (2,38):                           # R_ARM_ABS32 / R_ARM_TARGET1: the addend is the word itself
                    add,=struct.unpack_from('<I',data,off); data[off:off+4]=b'\0\0\0\0'
                else: add=None
                if tgt['flags']&0x10 and tgt['flags']&0x20 and add is not None:
                    base=tgt['off']+add; desc=('str',b[base:b.index(b'\0',base)])
                else:
                    # an address inside a section several objects share (a named section such as RAM2):
                    # name the object it points into, and how far in, not the position in the section
                    desc=('sec',norm(tgt['n']),add)
                    if add is not None:
                        inside=[q for q in syms if q['shndx']==sy['shndx'] and q['type'] in (1,2) and q['value']<=add<q['value']+max(q['size'],1)]
                        if len(inside)==1: desc=('obj',norm(tgt['n']),norm(inside[0]['name']),add-inside[0]['value'])
            else:
                desc=('sym',norm(sy['name']))
                if sy['shndx'] not in (0,0xfff1,0xfff2) and sy['bind']==0: desc=('loc',norm(sy['name']),norm(sh[sy['shndx']]['n']))
            rl.append((off,typ,desc))
        key=(norm(s['n']),s['type'],s['flags'],s['size'],bytes(data),tuple(rl))
        out[key]+=1; sizes[norm(s['n'])]+=s['size']
    return out,sizes
a,asz=describe(sys.argv[1]); c,csz=describe(sys.argv[2])
only_a=a-c; only_c=c-a
print(f'sections compared: {sum(a.values())} and {sum(c.values())}; bytes {sum(asz.values())} and {sum(csz.values())}')
if not only_a and not only_c: print('EQUIVALENT: every function and variable is identical'); sys.exit(0)
names=sorted({k[0] for k in only_a}|{k[0] for k in only_c})
print(f'DIFFERENT in {len(names)} section name(s):')
for n in names[:40]:
    sa=[k for k in only_a if k[0]==n]; sc=[k for k in only_c if k[0]==n]
    why='only in first' if not sc else 'only in second' if not sa else ('size %d -> %d'%(sa[0][3],sc[0][3]) if sa[0][3]!=sc[0][3] else 'bytes' if sa[0][4]!=sc[0][4] else 'relocations')
    print('  ',n,'—',why)
sys.exit(1)
