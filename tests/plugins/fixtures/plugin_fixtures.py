#!/usr/bin/env python3
"""Original-DLL reference fixtures for the native plugin replacements.

Subcommands (run from the repository root):
  stage   --tests-bin build/plugins-macos/elona_plugins_tests
          Writes work/windows/plugins-claude/input/ (CP932 HSP sources, data
          files, hash-checked original DLL copies, manifest.json).
  upload  SFTP the staged input to reference-build/plugins-claude-input.
  run     --run plugins-claude-NN   Run RunPluginFixtures.ps1 remotely.
  fetch   --run plugins-claude-NN   SFTP the run folder back to work/windows/plugins-claude/.
  winbuild --run plugins-claude-x64-NN   Build and run the native plugin tests
          with MSVC x64 in %USERPROFILE%/ElonaPlusNative-2.335/build/<run>.
  collect --run plugins-claude-NN   Convert the observed x86 outputs into
          tests/plugins/reference/* consumed by elona_plugins_tests.

Only synthetic data is used. The original game, its data and saves are never
touched. Outputs stay under work/ (and tests/plugins/reference for the small
recorded expectations).
"""
import argparse
import gzip
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
STAGE = ROOT / "work/windows/plugins-claude"
REMOTE_BASE = "ElonaPlusNative-2.335/reference-build"
REMOTE_INPUT = REMOTE_BASE + "/plugins-claude-input"
HOST = "home/windows"
PINNED = {
    "hmm.dll": "5eed0341f3399b833d56402bf4b9c67bae6469e099ee0b802e9ebee428550565",
    "z.hpi": "92aa68357593af607ae813b978298484003befac59cc5b4d66b31380210c9bee",
    "exrand.dll": "fb99c1443797646428c705740ae0a93aacfe9cd76e3341b0e24970fb061a0b23",
    "hspda.dll": "a1fcd341def560bd1aa57e63708d487b722a4fc95f2d8edd14e7a5461ea5eb08",
}

# Legacy numeric declarations exactly as restored for 2.335 (subid -5).
DECL_HMM = '''#uselib "hmm.dll"
#func HMMBITON "_HMMBITON@16" $1
#func HMMBITOFF "_HMMBITOFF@16" $1
#func HMMBITCHECK "_HMMBITCHECK@16" $0
'''
DECL_EXRAND = '''#uselib "exrand.dll"
#func exrand_randomize "_exrand_randomize@16" $0
#func exrand_rnd "_exrand_rnd@16" $1
'''
DECL_Z = '''#uselib "z.hpi"
#func global zOpen "_zOpen@16" $5
#func global zRead "_zRead@16" $1
#func global zWrite "_zWrite@16" $1
#func global zClose "_zClose@16" $0
'''
DECL_HSPDA = '''#uselib "hspda.dll"
#func sortval_ "_sortval@16" $202
#func sortnote_ "_sortnote@16" $202
#func xnotesel "_xnotesel@16" $83
#func xnoteadd "_xnoteadd@16" $6
'''
HEX_MODULE = '''#module hexmod
#defcfunc hexs var v, int n
	hs = ""
	repeat n
		hs += strf("%02x", peek(v, cnt))
	loop
	if n == 0 {
		hs = "-"
	}
	return hs
#global
'''
PATTERN_MODULE = '''#module patmod
#deffunc patfill var v, int n, int base
	repeat n
		i = base + cnt
		poke v, cnt, (i * 7 + i / 251) & 255
	loop
	return
#deffunc rndfill var v, int n, int base
	repeat n
		i = base + cnt
		hh = i * 1103515245 + 12345
		hh = hh ^ (hh >> 15)
		poke v, cnt, (hh >> 7) & 255
	loop
	return
#defcfunc rndcheck var v, int n, int base
	bad = 0
	repeat n
		i = base + cnt
		hh = i * 1103515245 + 12345
		hh = hh ^ (hh >> 15)
		if peek(v, cnt) != ((hh >> 7) & 255) {
			bad++
		}
	loop
	return bad
#defcfunc patcheck var v, int n, int base
	bad = 0
	repeat n
		i = base + cnt
		if peek(v, cnt) != ((i * 7 + i / 251) & 255) {
			bad++
		}
	loop
	return bad
#global
'''


LOADED_DECL = '''#uselib "kernel32.dll"
#func global elonaGetModuleHandleA "GetModuleHandleA" str
#func global elonaGetModuleFileNameA "GetModuleFileNameA" int, var, int
'''


def loaded_probe(dll, sink="mes"):
    """HSP lines printing the path of the module actually loaded as `dll`."""
    emit = 'mes "loaded {0} " + modpath'.format(dll) if sink == "mes" else 'out += "loaded {0} " + modpath + "\\n"'.format(dll)
    return (f'elonaGetModuleHandleA "{dll}"\nmodh = stat\nsdim modpath, 1024\n'
            f'if modh != 0 {{\n\telonaGetModuleFileNameA modh, modpath, 1000\n}} else {{\n\tmodpath = "NOT-LOADED"\n}}\n{emit}\n')


def with_probe(source, dll, sink="mes"):
    marker = 'mes "end"\nend\n' if sink == "mes" else 'out += "end\\n"'
    assert marker in source, dll
    return LOADED_DECL + source.replace(marker, loaded_probe(dll, sink) + marker, 1)


def pattern(size, base=0):
    return bytes(((i * 7 + i // 251) & 0xFF) for i in range(base, base + size))


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def rnd_pattern(size, base=0):
    out = bytearray()
    for i in range(base, base + size):
        h = s32(i * 1103515245 + 12345)
        h = s32(h ^ (h >> 15))
        out.append((h >> 7) & 0xFF)
    return bytes(out)


def hmm_source():
    return DECL_HMM + '''dim init, 5
init = 0, 1, -1, -2147483647 - 1, 305419896
dim bits, 13
bits = -33, -32, -1, 0, 1, 30, 31, 32, 33, 63, 64, 2147483647, -2147483647 - 1
dim a, 3
repeat 5
	i = cnt
	repeat 13
		b = bits(cnt)
		a(0) = 286331153 : a(1) = init(i) : a(2) = 572662306
		HMMBITON a(1), b : s1 = stat : v1 = a(1)
		HMMBITCHECK a(1), b : c1 = stat
		HMMBITOFF a(1), b : s2 = stat : v2 = a(1)
		HMMBITCHECK a(1), b : c2 = stat
		HMMBITCHECK init(i), b : c3 = stat
		mes "hmm " + init(i) + " " + b + " " + s1 + " " + v1 + " " + c1 + " " + s2 + " " + v2 + " " + c2 + " " + c3 + " " + a(0) + " " + a(2)
	loop
loop
v = 0
repeat 3
	HMMBITON v, 5
	s = stat
	mes "repeat_on " + v + " " + s
loop
repeat 2
	HMMBITOFF v, 5
	s = stat
	mes "repeat_off " + v + " " + s
loop
mes "end"
end
'''


EXRAND_SEEDS = [0, 1, 12345, 5489, 1999, 2147483647, 20261117]
EXRAND_RANGES = [0, 1, 2, 7, 100, 32768, 100000, 1000000, 2147483647]
# Inputs where one 53-bit rounding, exact floor and 24-bit rounding disagree.
EXRAND_PRECISION = [(12345, 68925423), (1, 1278798073), (2024, 302762703)]


def exrand_source():
    lines = [DECL_EXRAND]
    calls = []
    for seed in EXRAND_SEEDS:
        for rng in EXRAND_RANGES:
            calls.append((seed, [rng] * 12))
    for seed, rng in EXRAND_PRECISION:
        calls.append((seed, [rng, rng, rng]))
    calls.append((12345, [0, 32768, 0, 0, 32768]))  # range 0 consumes a draw
    calls.append((12345, [7, 100000, 2147483647, 1, 32768, 5]))  # mixed ranges
    for seed, ranges in calls:
        lines.append(f"exrand_randomize {seed}\nst = stat\nln_ = \"{seed} {len(ranges)}\"\n")
        for rng in ranges:
            lines.append(f"exrand_rnd v, {rng}\nst += stat\nln_ += \" {rng} \" + v\n")
        lines.append("if st != 0 {\n\tln_ += \" STAT\" + st\n}\nmes ln_\n")
    lines.append("mes \"end\"\nend\n")
    return "".join(lines)


def exrand_unseeded_source():
    # Seed marker -1: no exrand_randomize before the first draw.
    return DECL_EXRAND + '''ln_ = "-1 6"
repeat 6
	exrand_rnd v, 2147483647
	ln_ += " 2147483647 " + v
loop
mes ln_
mes "end"
end
'''


def error_source(decl, body):
    return decl + '''onerror goto *on_error
''' + body + '''
mes "noerror stat=" + stat
end
*on_error
mes "error err=" + err
end
'''


def gz_source():
    sizes = [0, 1, 4, 16384, 16385, 100000]
    return DECL_Z + HEX_MODULE + PATTERN_MODULE + '''#module readmod
#deffunc readcheck str fname, int chunkn, int mode
	sdim rb, chunkn + 1
	h = -7
	zOpen h, fname, 0
	ln_ = "r " + fname + " " + chunkn + " open=" + stat + " hnz=" + (h != 0)
	if h == 0 {
		mes ln_
		return
	}
	ps_ = 0
	bad = 0
	repeat 400
		zRead rb, h, chunkn
		st = stat
		ln_ += " " + st
		if st <= 0 {
			break
		}
		if mode == 0 {
			bad += patcheck(rb, st, ps_)
		} else {
			bad += rndcheck(rb, st, ps_)
		}
		ps_ += st
	loop
	zRead rb, h, chunkn
	ln_ += " again=" + stat
	zClose h
	ln_ += " close=" + stat + " total=" + ps_ + " bad=" + bad
	mes ln_
	return
#global
dim sizes, 6
sizes = ''' + ", ".join(map(str, sizes)) + '''
sdim chunk, 4100
repeat 6
	size = sizes(cnt)
	fname = "orig_" + size + ".gz"
	h = -7
	zOpen h, fname, 1, 3
	ln_ = "w " + fname + " " + size + " open=" + stat + " hnz=" + (h != 0)
	ps_ = 0
	repeat
		n = size - ps_
		if n > 4096 {
			n = 4096
		}
		patfill chunk, n, ps_
		zWrite chunk, h, n
		ln_ += " " + stat
		ps_ += n
		if ps_ >= size {
			break
		}
	loop
	zClose h
	ln_ += " close=" + stat
	mes ln_
loop
sdim big, 100001
patfill big, 100000, 0
zOpen h, "orig_whole_100000.gz", 1, 3
ln_ = "w orig_whole_100000.gz 100000 open=" + stat
zWrite big, h, 100000
ln_ += " " + stat
zClose h
ln_ += " close=" + stat
mes ln_
sdim big, 100001
rndfill big, 100000, 0
zOpen h, "orig_rand_100000.gz", 1, 3
ln_ = "w orig_rand_100000.gz 100000 open=" + stat
ps_ = 0
repeat
	n = 100000 - ps_
	if n > 4096 {
		n = 4096
	}
	memcpy chunk, big, n, 0, ps_
	zWrite chunk, h, n
	ln_ += " " + stat
	ps_ += n
	if ps_ >= 100000 {
		break
	}
loop
zClose h
ln_ += " close=" + stat
mes ln_
readcheck "orig_rand_100000.gz", 4096, 1
readcheck "orig_rand_100000.gz", 16385, 1
readcheck "native_rand_100000.gz", 4096, 1
readcheck "native_rand_100000.gz", 16385, 1
readcheck "native_rand_100000.gz", 100000, 1
; read back own files and native files
repeat 6
	readcheck "orig_" + sizes(cnt) + ".gz", 4096
	readcheck "native_" + sizes(cnt) + ".gz", 4096
	readcheck "native_" + sizes(cnt) + ".gz", 16385
loop
readcheck "orig_4.gz", 1
readcheck "native_4.gz", 1
readcheck "orig_whole_100000.gz", 100000
readcheck "crc.gz", 50000
readcheck "truncated.gz", 50000
readcheck "plain.bin", 3000
readcheck "empty.bin", 10
readcheck "cat.gz", 1024
readcheck "header.gz", 7777
readcheck "badmethod.gz", 100
readcheck "rand_crc.gz", 4096, 1
readcheck "rand_trunc.gz", 4096, 1
; contract cases
h = 55
zOpen h, "missing_dir\\\\none.gz", 0
mes "missing open=" + stat + " h=" + h
mkdir "subdir"
h = 55
zOpen h, "subdir", 0
mes "dir open=" + stat + " h=" + h
sdim rb, 65
h0 = 0
zRead rb, h0, 10
mes "null read=" + stat
zClose h0
mes "null close=" + stat
zOpen h, "wr.gz", 1, 3
ln_ = "wr open=" + stat
zRead rb, h, 8
ln_ += " read_on_w=" + stat
zWrite rb, h, 0
ln_ += " write0=" + stat
rb = "abcdefgh"
zWrite rb, h, 8
ln_ += " write8=" + stat
zClose h
ln_ += " close=" + stat
mes ln_
zOpen h, "wr.gz", 0
ln_ = "rd open=" + stat
zRead rb, h, 0
ln_ += " read0=" + stat
sdim rb, 65
zRead rb, h, 64
ln_ += " read64=" + stat + " data=" + hexs(rb, 8)
zRead rb, h, 64
ln_ += " again=" + stat
zRead rb, h, 64
ln_ += " again2=" + stat
zClose h
ln_ += " close=" + stat
mes ln_
sdim lv, 5001
patfill lv, 5000, 0
dim levels, 4
levels = 54, 56, -48, 259
repeat 4
	fname = "lv_" + cnt + ".gz"
	zOpen h, fname, 1, levels(cnt)
	ln_ = "lv " + levels(cnt) + " open=" + stat
	zWrite lv, h, 5000
	ln_ += " write=" + stat
	zClose h
	ln_ += " close=" + stat
	mes ln_
	readcheck fname, 1000
loop
mes "end"
end
'''


def hspda_source():
    sortval_cases = [
        (0, [5, 3, 9, 1, 7]), (1, [5, 3, 9, 1, 7]), (0, [2, 2, 1, 3, 1]), (1, [2, 2, 1, 3, 1]),
        (0, [-5, 100, 0, -100, 7]), (1, [-5, 100, 0, -100, 7]), (1, [4]), (0, [1, 2]), (1, [1, 2]),
        (1, [13, 12, 11, 10, 9]), (1, [3, 3, 3, 3, 3]), (0, [9, 9, 1]),
    ]
    state = 1
    rnd = []
    for _ in range(60):
        state = (state * 1103515245 + 12345) & 0xFFFFFFFF
        rnd.append(((state >> 8) % 2001) - 1000)
    sortval_cases += [(0, rnd), (1, rnd)]
    out = [DECL_HSPDA, HEX_MODULE]
    for order, values in sortval_cases:
        n = len(values)
        out.append(f"dim sv, {n}\n")
        for i, v in enumerate(values):
            out.append(f"sv({i}) = {v}\n")
        out.append(f"ln_ = \"sortval {order} {n} {n}\"\nrepeat {n}\n\tln_ += \" \" + sv(cnt)\nloop\n")
        out.append(f"sortval_ sv, {order}\nst = stat\nln_ += \" =>\"\nrepeat {n}\n\tln_ += \" \" + sv(cnt)\nloop\n")
        out.append("mes ln_ + \" stat=\" + st\n")
    # 2-D array: only len[1] elements from the start are sorted.
    out.append('''dim m2, 3, 2
m2(0, 0) = 3 : m2(1, 0) = 1 : m2(2, 0) = 2 : m2(0, 1) = 9 : m2(1, 1) = 7 : m2(2, 1) = 8
ln_ = "sortval 0 3 6 3 1 2 9 7 8 =>"
sortval_ m2, 0
st = stat
ln_ += " " + m2(0, 0) + " " + m2(1, 0) + " " + m2(2, 0) + " " + m2(0, 1) + " " + m2(1, 1) + " " + m2(2, 1)
mes ln_ + " stat=" + st
sdim ss, 64
ss = "x"
sortval_ ss, 0
mes "sortval_str stat=" + stat
''')
    notes = [
        (1, b"b.npc\r\na.npc\r\nc.npc\r\n"), (0, b"b.npc\r\na.npc\r\nc.npc\r\n"), (0, b"b\r\na\r\nc"),
        (0, b""), (1, b"b\na\rc"), (0, b"apple\r\nBanana\r\napple\r\nAPPLE\r\nbanana"),
        (1, b"apple\r\nBanana\r\napple\r\nAPPLE\r\nbanana"), (0, b"a-b\r\nab\r\na'b\r\na_b\r\na b\r\nA-B"),
        (0, b"10\r\n9\r\n1\r\n_1\r\n-1\r\n~\r\n+"), (1, b"npc_01.npc\r\nNPC_02.npc\r\nzz.npc\r\naa-1.npc\r\n"),
        (0, b"z\r\ny\r\nx\r\nw\r\nv\r\nu\r\nt"), (0, b"\r\n\r\na\r\n"),
    ]
    out.append("sdim sn, 4096\n")
    for order, text in notes:
        out.append("sdim sn, 4096\n")
        for i, byte in enumerate(text):
            out.append(f"poke sn, {i}, {byte}\n")
        hexin = text.hex() if text else "-"
        out.append(f"sortnote_ sn, {order}\nst = stat\n")
        out.append(f"mes \"sortnote {order} {hexin} \" + hexs(sn, strlen(sn)) + \" stat=\" + st\n")
    out.append('mes "end"\nend\n')
    return "".join(out)


SORTNOTE_EDGES = [b"a", b"a\r\n", b"\r\n", b"a\n", b"a\r", b"\r", b"\n", b"a\r\n\r\n", b"b\r\na\r\n\r\n",
                  b"\r\nb\r\na", b"b\na\n", b"b\ra\r", b"b\r\n\na", b"c\r\nb\r\na", b"x", b"\r\n\r\n",
                  b"b\r\n\r\na", b"c\r\n\r\nb\r\n\r\na\r\n", b"B\r\nb\r\nA\r\na", b"b\r\na\r\n "]


def sortnote_edge_source():
    body = DECL_HSPDA + HEX_MODULE
    for order in (0, 1):
        for text in SORTNOTE_EDGES:
            body += "sdim sn, 4096\n" + "".join(f"poke sn, {i}, {b}\n" for i, b in enumerate(text))
            body += (f"sortnote_ sn, {order}\nst = stat\n"
                     f"mes \"sortnote {order} {text.hex() or '-'} \" + hexs(sn, strlen(sn)) + \" stat=\" + st\n")
    return body + "mes \"end\"\nend\n"


DECL_HMM_AUDIO = '''#uselib "hmm.dll"
#func DSINIT "_DSINIT@16" $2
#func DSEND "_DSEND@16" $0
#func DSRELEASE "_DSRELEASE@16" $0
#func DSPLAY "_DSPLAY@16" $0
#func DSSTOP "_DSSTOP@16" $0
#func DSSETVOLUME "_DSSETVOLUME@16" $0
#func DSGETMASTERVOLUME "_DSGETMASTERVOLUME@16" $0
#func CHECKPLAY "_CHECKPLAY@16" $0
#func DMINIT "_DMINIT@16" $2
#func DMEND "_DMEND@16" $0
#func DMPLAY "_DMPLAY@16" $0
#func DMSTOP "_DMSTOP@16" $2
#func DIINIT "_DIINIT@16" $2
#func DIGETJOYNUM "_DIGETJOYNUM@16" $0
'''


def hmm_audio_calls(prefix):
    # Only channel numbers / volumes the game itself uses; no pointers.
    calls = ["DSSTOP 13", "DSSTOP 1025", "DSRELEASE 13", "DSRELEASE 1025", "DSSETVOLUME 13, 50", "DSSETVOLUME 13, 101",
             "CHECKPLAY 7", "CHECKPLAY 32", "DSPLAY 13, 0", "DMSTOP", "DMPLAY 1, 0", "DIGETJOYNUM"]
    return "".join(f'{c}\nout += "{prefix} {c.replace(chr(34), "")} stat=" + stat + "\\n"\n' for c in calls)


def hmm_audio_uninit_source():
    return (DECL_HMM_AUDIO + "sdim out, 8192\n" + hmm_audio_calls("uninit") +
            'DSEND\nout += "uninit DSEND stat=" + stat + "\\n"\nDMEND\nout += "uninit DMEND stat=" + stat + "\\n"\n'
            'out += "end\\n"\nnotesel out\nnotesave "out.txt"\nend\n')


def hmm_audio_mastervol_source():
    return (DECL_HMM_AUDIO + 'sdim out, 1024\nonerror goto *on_error\nDSGETMASTERVOLUME\n'
            'out = "noerror stat=" + stat + "\\n"\nnotesel out\nnotesave "out.txt"\nend\n'
            '*on_error\nout = "error err=" + err + "\\n"\nnotesel out\nnotesave "out.txt"\nend\n')


def hmm_audio_init_source():
    # Session-0 environment observation: real DirectSound/DirectMusic/DirectInput init.
    return (DECL_HMM_AUDIO + "sdim out, 8192\n"
            'DSINIT\nds = stat\nout += "env DSINIT stat=" + ds + "\\n"\n'
            'DMINIT\nout += "env DMINIT stat=" + stat + "\\n"\n'
            'DIINIT\nout += "env DIINIT stat=" + stat + "\\n"\n'
            'if ds == 1 {\n\tDSGETMASTERVOLUME\n\tout += "env DSGETMASTERVOLUME stat=" + stat + "\\n"\n}\n' +
            hmm_audio_calls("env") +
            'DSEND\nout += "env DSEND stat=" + stat + "\\n"\nDMEND\nout += "env DMEND stat=" + stat + "\\n"\n'
            'out += "end\\n"\nnotesel out\nnotesave "out.txt"\nend\n')


def xnote_source():
    # GUI runtime (hsp3.exe): xnoteadd's $6 declaration passes the current
    # BMSCR, which the console runtime cannot provide. Output goes to out.txt.
    body = """sdim xb, 256
xnotesel xb
out += "xnote sel1 " + stat + " " + hexs(xb, strlen(xb)) + "\\n"
xnoteadd "sword"
out += "xnote add1 " + stat + " " + hexs(xb, strlen(xb)) + "\\n"
xnoteadd "shield"
out += "xnote add2 " + stat + " " + hexs(xb, strlen(xb)) + "\\n"
xnoteadd "sword"
out += "xnote add3 " + stat + " " + hexs(xb, strlen(xb)) + "\\n"
xnoteadd ""
out += "xnote add4 " + stat + " " + hexs(xb, strlen(xb)) + "\\n"
xnoteadd ""
out += "xnote add5 " + stat + " " + hexs(xb, strlen(xb)) + "\\n"
sdim xb2, 64
poke xb2, 0, 97 : poke xb2, 1, 10 : poke xb2, 2, 98
xnotesel xb2, 4
out += "xnote sel2 " + stat + " " + hexs(xb2, strlen(xb2)) + "\\n"
xnoteadd "c"
out += "xnote add6 " + stat + " " + hexs(xb2, strlen(xb2)) + "\\n"
sdim xb3, 256
xb3 = "old"
xnotesel xb3
xb3 = ""
xnoteadd "game"
out += "xnote add7 " + stat + " " + hexs(xb3, strlen(xb3)) + "\\n"
xnoteadd "game"
out += "xnote add8 " + stat + " " + hexs(xb3, strlen(xb3)) + "\\n"
dim iv, 4
xnotesel iv
out += "xnote sel3 " + stat + " -\\n"
out += "end\\n"
notesel out
notesave "out.txt"
end
"""
    return DECL_HSPDA + HEX_MODULE + "sdim out, 8192\n" + body


def gzip_member(data, header_flags=0, extra=b"", name=b"", comment=b"", hcrc=False):
    head = bytearray(b"\x1f\x8b\x08" + bytes([header_flags]) + b"\x00\x00\x00\x00\x00\x03")
    if header_flags & 4:
        head += struct.pack("<H", len(extra)) + extra
    if header_flags & 8:
        head += name + b"\x00"
    if header_flags & 16:
        head += comment + b"\x00"
    if hcrc:
        head += struct.pack("<H", zlib.crc32(bytes(head)) & 0xFFFF)
    comp = zlib.compressobj(6, zlib.DEFLATED, -15)
    body = comp.compress(data) + comp.flush()
    return bytes(head) + body + struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF)


def data_files(tests_bin):
    files = {}
    native_dir = STAGE / "native-samples"
    if native_dir.exists():
        shutil.rmtree(native_dir)
    subprocess.run([str(tests_bin), "--write-native-samples", str(native_dir)], check=True)
    for line in (native_dir / "manifest.txt").read_text().split("\n"):
        if line.strip():
            name = line.split()[0]
            files[name] = (native_dir / name).read_bytes()
    good = gzip_member(pattern(40000))
    crc = bytearray(good)
    crc[-8] ^= 0xFF
    files["crc.gz"] = bytes(crc)
    files["truncated.gz"] = good[: len(good) // 2]
    files["plain.bin"] = pattern(40000)
    files["empty.bin"] = b""
    files["cat.gz"] = gzip_member(pattern(3000)) + gzip_member(pattern(5000, 3000))
    files["header.gz"] = gzip_member(pattern(40000), 4 | 8 | 16 | 2, b"\x01\x02\x03", b"name.bin", b"comment", True)
    bad = bytearray(gzip_member(pattern(100)))
    bad[2] = 7
    files["badmethod.gz"] = bytes(bad)
    rgood = gzip_member(rnd_pattern(100000))
    rcrc = bytearray(rgood)
    rcrc[-5] ^= 0x01
    files["rand_crc.gz"] = bytes(rcrc)
    files["rand_trunc.gz"] = rgood[: len(rgood) // 2]
    return files


def stage(args):
    inp = STAGE / "input"
    if inp.exists():
        shutil.rmtree(inp)
    (inp / "src").mkdir(parents=True)
    (inp / "dll").mkdir()
    (inp / "data").mkdir()
    game = ROOT / "elonaplus2.335"
    for name, digest in PINNED.items():
        raw = (game / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            sys.exit(f"{name} is not the pinned 2.335 original")
        (inp / "dll" / name).write_bytes(raw)  # copy only; the original folder is read-only input
    data = data_files(Path(args.tests_bin).resolve())
    for name, raw in data.items():
        (inp / "data" / name).write_bytes(raw)
    gz_data = sorted(data)
    fixtures = [
        ("hmm_bits", with_probe(hmm_source(), "hmm.dll"), ["hmm.dll"], [], False),
        ("exrand_vectors", with_probe(exrand_source(), "exrand.dll"), ["exrand.dll"], [], False),
        ("exrand_unseeded", exrand_unseeded_source(), ["exrand.dll"], [], False),
        ("exrand_err_seed", error_source(DECL_EXRAND, "exrand_randomize -1"), ["exrand.dll"], [], True),
        ("exrand_err_range", error_source(DECL_EXRAND, "exrand_randomize 5\nv = 77\nexrand_rnd v, -5"), ["exrand.dll"], [], True),
        ("gz_contract", with_probe(gz_source(), "z.hpi"), ["z.hpi"], gz_data, False),
        ("gz_err_write_null", error_source(DECL_Z, "sdim b, 16\nh = 0\nzWrite b, h, 4"), ["z.hpi"], [], True),
        ("gz_err_write_readhandle", error_source(DECL_Z, "sdim b, 16\nzOpen h, \"plain.bin\", 0\nzWrite b, h, 4"), ["z.hpi"], ["plain.bin"], True),
        ("hspda_cases", with_probe(hspda_source(), "hspda.dll"), ["hspda.dll"], [], False),
        ("hspda_sortnote_edges", with_probe(sortnote_edge_source(), "hspda.dll"), ["hspda.dll"], [], False),
    ]
    # xnoteadd's `$6` passes the current BMSCR, which needs the GUI runtime.
    # XnoteHarness.cs additionally calls the DLL directly with the same script.
    # (Runs up to plugins-claude-04 loaded the SDK's newer hspda.dll by mistake.)
    fixtures = [f + ("cl",) for f in fixtures] + [("hspda_xnote", with_probe(xnote_source(), "hspda.dll", "out"), ["hspda.dll"], [], False, "gui"),
        ("hmm_audio_uninit", with_probe(hmm_audio_uninit_source(), "hmm.dll", "out"), ["hmm.dll"], [], False, "gui"),
        ("hmm_audio_mastervol", hmm_audio_mastervol_source(), ["hmm.dll"], [], True, "gui"),
        ("hmm_audio_init", with_probe(hmm_audio_init_source(), "hmm.dll", "out"), ["hmm.dll"], [], False, "gui")]
    rows = []
    for name, source, plugins, datas, expect_error, runtime in fixtures:
        raw = source.replace("\n", "\r\n").encode("cp932")
        (inp / "src" / (name + ".hsp")).write_bytes(raw)
        rows.append({"name": name, "source": name + ".hsp", "sourceSha256": hashlib.sha256(raw).hexdigest(),
                     "plugins": plugins, "expectError": expect_error, "runtime": runtime,
                     "data": [{"name": d, "sha256": hashlib.sha256(data[d]).hexdigest()} for d in datas]})
    manifest = {"purpose": "Original x86 plugin reference fixtures; synthetic data only, never the game",
                "sourceEncoding": "CP932", "fixtures": rows, "pluginSha256": PINNED}
    (inp / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    here = Path(__file__).resolve().parent
    shutil.copy2(here / "RunPluginFixtures.ps1", inp / "RunPluginFixtures.ps1")
    shutil.copy2(here / "LstrcmpDump.cs", inp / "LstrcmpDump.cs")
    shutil.copy2(here / "XnoteHarness.cs", inp / "XnoteHarness.cs")
    print(f"staged {len(rows)} fixtures, {len(data)} data files -> {inp}")


def sftp(batch):
    result = subprocess.run(["sftp", "-o", "BatchMode=yes", "-b", "-", HOST], input=batch, text=True,
                            capture_output=True, timeout=600)
    if result.returncode != 0:
        sys.exit(result.stdout + result.stderr)
    return result.stdout


def powershell(script, timeout=900):
    sys.path.insert(0, str(ROOT / "scripts"))
    from windows_probe import run_powershell
    result = run_powershell(HOST, script, timeout=timeout)
    out = result.stdout.decode("utf-8", "replace") if isinstance(result.stdout, bytes) else result.stdout
    err = result.stderr.decode("utf-8", "replace") if isinstance(result.stderr, bytes) else result.stderr
    return result.returncode, out, err


def upload(_args):
    code, out, err = powershell(
        "$d = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build\\plugins-claude-input'\n"
        "if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }\n"
        "foreach ($s in @('', 'src', 'dll', 'data')) { $null = New-Item -ItemType Directory -Force -Path (Join-Path $d $s) }\n"
        "Write-Output ready")
    if code != 0 or "ready" not in out:
        sys.exit(out + err)
    inp = STAGE / "input"
    lines = []
    for path in sorted(inp.rglob("*")):
        if path.is_file():
            rel = path.relative_to(inp).as_posix()
            lines.append(f"put {path} {REMOTE_INPUT}/{rel}")
    sftp("\n".join(lines) + "\n")
    print(f"uploaded {len(lines)} files")


def run(args):
    script = ("$root = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\reference-build'\n"
              "$runner = [scriptblock]::Create([IO.File]::ReadAllText((Join-Path $root 'plugins-claude-input\\RunPluginFixtures.ps1')))\n"
              f"& $runner -RunName {args.run}\n")
    code, out, err = powershell(script)
    print(out[-4000:])
    if code != 0:
        sys.exit(err[-4000:])


def fetch(args):
    dest = STAGE / args.run
    if dest.exists():
        sys.exit(f"{dest} exists")
    STAGE.mkdir(parents=True, exist_ok=True)
    sftp(f"lcd {STAGE}\nget -R {REMOTE_BASE}/{args.run}\n")
    print(f"fetched {dest}")


def collect(args):
    run_dir = STAGE / args.run
    report = json.loads((run_dir / "report.json").read_text(encoding="utf-8-sig"))
    ref = ROOT / "tests/plugins/reference"
    (ref / "gz").mkdir(parents=True, exist_ok=True)
    outputs = {}
    problems = []
    for row in report["fixtures"]:
        raw = row.get("outHex") if row.get("runtime") == "gui" else row.get("stdoutHex")
        stdout = bytes.fromhex(raw or "").decode("cp932").replace("\r\n", "\n")
        outputs[row["name"]] = stdout
        ok = row.get("compileExit") == 0 and row.get("runExit") == 0 and not row.get("stderrHex")
        if row["expectError"]:
            ok = ok and stdout.startswith("error err=")
        else:
            ok = ok and stdout.rstrip("\n").endswith("end")
        if not ok:
            problems.append(row["name"])
    loaded = {}
    for row in report["fixtures"]:
        name = row["name"]
        keep = []
        for line in outputs[name].split("\n"):
            if line.startswith("loaded "):
                _, dll, path = line.split(" ", 2)
                loaded[name] = (dll, path)
            else:
                keep.append(line)
        outputs[name] = "\n".join(keep)
        hashes = {m["path"].lower(): m.get("sha256") for m in (row.get("loadedModules") or [])}
        if name in loaded:
            dll, path = loaded[name]
            case_dir = "\\" + args.run.lower() + "\\" + name.lower() + "\\"
            if case_dir not in path.lower() or hashes.get(path.lower()) != PINNED[dll]:
                problems.append(f"{name}: loaded {dll} from {path} sha256={hashes.get(path.lower())}")
    (run_dir / "outputs.json").write_text(json.dumps(outputs, ensure_ascii=False, indent=2) + "\n")
    (ref / "loaded_modules.txt").write_text(
        f"# Module actually loaded in each fixture process (GetModuleFileNameA), run {args.run}\n" +
        "".join(f"{n} {d} {PINNED[d]} {Path(p.replace(chr(92), '/')).name} case-local\n"
                for n, (d, p) in sorted(loaded.items())))
    print("loaded modules verified:", sorted(loaded))
    if problems:
        print("FIXTURE FAILURES:", problems)
    # exrand vectors
    ex = [l for name in ("exrand_vectors", "exrand_unseeded") for l in outputs[name].split("\n")
          if l and l != "end"]
    (ref / "exrand_vectors.txt").write_text(
        f"# Observed: original exrand.dll in HSP 3.4 x86 hsp3cl, run {args.run}\n"
        "# <seed|-1 unseeded> <k> (<range> <value>)*k ; seed reset per line\n" + "\n".join(ex) + "\n")
    # gzip files written by the original
    case = run_dir / "gz_contract"
    names = []
    for size in [0, 1, 4, 16384, 16385, 100000]:
        names.append((f"orig_{size}.gz", size))
    names.append(("orig_whole_100000.gz", 100000))
    names_r = [("orig_rand_100000.gz", 100000)]
    for i in range(4):
        names.append((f"lv_{i}.gz", 5000))
    with open(ref / "gz" / "manifest.txt", "w") as manifest:
        for name, size in names:
            shutil.copy2(case / name, ref / "gz" / name)
            manifest.write(f"{name} {size} p\n")
        for name, size in names_r:
            shutil.copy2(case / name, ref / "gz" / name)
            manifest.write(f"{name} {size} r\n")
    inputs_dir = ref / "gz_inputs"
    inputs_dir.mkdir(exist_ok=True)
    for path in sorted((STAGE / "input" / "data").iterdir()):
        shutil.copy2(path, inputs_dir / path.name)
    (ref / "gz_contract.txt").write_text(f"# Observed z.hpi stat lines, run {args.run}\n" + outputs["gz_contract"])
    (ref / "hmm_bits.txt").write_text(f"# Observed hmm.dll lines, run {args.run}\n" + outputs["hmm_bits"])
    errors = {name: outputs[name].strip() for name in outputs if report_row(report, name)["expectError"]}
    (ref / "errors.txt").write_text(f"# Observed legacy DLL error behaviour, run {args.run}\n" +
                                    "".join(f"{k} {v}\n" for k, v in sorted(errors.items())))
    # hspda
    (ref / "hspda_reference.txt").write_text(f"# Observed bundled hspda.dll, run {args.run}\n" + outputs["hspda_cases"] +
        "# xnote: XnoteHarness.cs direct x86 calls into the bundled hspda.dll\n" +
        (run_dir / "xnote.txt").read_text())
    (ref / "hmm_audio.txt").write_text(
        f"# Observed hmm.dll audio/input exports in HSP 3.4 hsp3.exe (session 0 over SSH), run {args.run}\n"
        "# uninit: no DSINIT/DMINIT/DIINIT call. env: real init in this Windows session (environment-dependent).\n" +
        outputs["hmm_audio_uninit"] + "mastervol_uninit " + outputs["hmm_audio_mastervol"].strip() + "\n" +
        outputs["hmm_audio_init"])
    (ref / "hspda_xnote_gui.txt").write_text(f"# Observed bundled hspda.dll xnote in HSP 3.4 hsp3.exe, run {args.run}\n" +
                                             outputs["hspda_xnote"])
    (ref / "hspda_sortnote_edges.txt").write_text(
        f"# Observed bundled hspda.dll sortnote edge inputs, HSP 3.4 hsp3cl, run {args.run}\n" +
        "".join(l + "\n" for l in outputs["hspda_sortnote_edges"].split("\n") if l.startswith("sortnote ")))
    # lstrcmpA dumps
    user = (run_dir / "lstrcmpa_user.txt").read_text().split("\n", 1)
    ja = (run_dir / "lstrcmpa_0411.txt").read_text().split("\n", 1)
    if user[1] != ja[1]:
        problems.append("lstrcmpA user locale differs from CompareStringA(0x0411) on ASCII")
    shutil.copy2(run_dir / "lstrcmpa_user.txt", ref / "lstrcmpa_ascii.txt")
    print("lstrcmpA(user) == CompareStringA(0x0411) on the ASCII corpus:", user[1] == ja[1])
    print("collected into", ref)
    if problems:
        print("PROBLEMS:", problems)
        sys.exit(1)


def winbuild(args):
    import zipfile
    if not args.run.startswith("plugins-claude-x64-"):
        sys.exit("run must start with plugins-claude-x64-")
    STAGE.mkdir(parents=True, exist_ok=True)
    archive = STAGE / (args.run + ".zip")
    members = [ROOT / "src/cmake/ElonaPlugins.cmake", ROOT / ".cache/downloads/zlib-1.3.1.tar.gz"]
    for base in (ROOT / "src/plugins", ROOT / "tests/plugins"):
        members += [p for p in sorted(base.rglob("*")) if p.is_file() and "__pycache__" not in p.parts
                    and "fixtures" not in p.relative_to(base).parts[:1]]
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in members:
            info = zipfile.ZipInfo(path.relative_to(ROOT).as_posix(), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, path.read_bytes())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    code, out, err = powershell(
        "$d = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\\build\\" + args.run + "'\n"
        "if (Test-Path -LiteralPath $d) { throw 'exists' }\n"
        "$null = New-Item -ItemType Directory -Path $d\nWrite-Output ready")
    if "ready" not in out:
        sys.exit(out + err)
    sftp(f"put {archive} ElonaPlusNative-2.335/build/{args.run}/src.zip\n")
    script = r"""$ErrorActionPreference = 'Stop'
$d = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\build\RUN'
$zip = Join-Path $d 'src.zip'
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'DIGEST') { throw 'zip hash mismatch' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($zip, (Join-Path $d 'src'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$src = Join-Path $d 'src'
$cmd = Join-Path $d 'build.cmd'
$lines = @('@echo off', ('call "{0}" >nul 2>&1' -f $vcvars), 'if errorlevel 1 exit /b 1',
  ('set "PATH={0};{1};%PATH%"' -f $cmakeDir,$ninjaDir), ('cd /d "{0}"' -f $src),
  'cmake -S tests/plugins -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug 2>&1', 'if errorlevel 1 exit /b 1',
  'cmake --build build 2>&1', 'if errorlevel 1 exit /b 1',
  'ctest --test-dir build --output-on-failure --no-tests=error 2>&1', 'if errorlevel 1 exit /b 1',
  'build\elona_plugins_tests.exe --work build\plugin-tests-direct 2>&1')
Set-Content -LiteralPath $cmd -Value $lines -Encoding Default
& $env:ComSpec /d /c $cmd 2>&1 | Tee-Object -FilePath (Join-Path $d 'build.log')
Write-Output ("exit=" + $LASTEXITCODE)
""".replace("RUN", args.run).replace("DIGEST", digest)
    code, out, err = powershell(script, timeout=1800)
    (STAGE / (args.run + ".log")).write_text(out + "\n" + err)
    print(out[-5000:])
    if "exit=0" not in out:
        sys.exit(err[-2000:])


def report_row(report, name):
    return next(r for r in report["fixtures"] if r["name"] == name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("stage")
    p.add_argument("--tests-bin", required=True)
    sub.add_parser("upload")
    for name in ("run", "fetch", "collect", "winbuild"):
        p = sub.add_parser(name)
        p.add_argument("--run", required=True)
    args = parser.parse_args()
    {"stage": stage, "upload": upload, "run": run, "fetch": fetch, "collect": collect, "winbuild": winbuild}[args.cmd](args)


if __name__ == "__main__":
    main()
