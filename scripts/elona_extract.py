"""Bounded, non-executing extraction of the ElonaPlus 2.335 HSP carrier.

Format references: Ruin0x11/borscht exe2ax/{dpm,crypt,ax}.rs at
03c8be3398a59d9df445fd5a0c7850b3f2188869 and OpenHSP HED_LIBDAT /
HED_STRUCTDAT. This is a narrow PE32/DPMX/HSP3 legacy reader, not a VM.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile


VERSION = "2.335"
EXE_SHA256 = "7d372ac439788de5288cacf200d46d4352d036aa74ae5715f654aaeab56a1e4e"
AX_SHA256 = "f1b1404060faa01dc1d3e46b9a58ba0c39cfd185afd33bdafc2287ee3e921d25"
MAX_FILE_BYTES = 64 * 1024 * 1024
HSP_HEADER_BYTES = 96


class ExtractError(ValueError):
    """Malformed or unsupported input, or a write that could clobber data."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _range(offset: int, size: int, total: int, label: str) -> tuple[int, int]:
    if offset < 0 or size < 0 or offset > total or size > total - offset:
        raise ExtractError(f"{label}: range {offset}+{size} exceeds {total} bytes")
    return offset, offset + size


def _unpack(fmt: str, data: bytes, offset: int, label: str) -> tuple:
    _range(offset, struct.calcsize(fmt), len(data), label)
    return struct.unpack_from(fmt, data, offset)


def _nonoverlap(ranges: list[tuple[int, int, str]]) -> None:
    previous_end = -1
    previous_name = ""
    for start, end, name in sorted(ranges):
        if start == end:
            continue
        if start < previous_end:
            raise ExtractError(f"overlap between {previous_name} and {name}")
        previous_end, previous_name = end, name


def _portable_name(raw: bytes) -> str:
    value, _, padding = raw.partition(b"\0")
    if any(padding):
        raise ExtractError("DPM member name has nonzero bytes after NUL")
    try:
        name = value.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ExtractError("non-ASCII DPM member names are unsupported") from exc
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,15}", name):
        raise ExtractError(f"unsafe DPM member name: {name!r}")
    reserved = {"CON", "PRN", "AUX", "NUL"}
    reserved.update(f"{prefix}{i}" for prefix in ("COM", "LPT") for i in range(1, 10))
    if name.endswith(".") or name.split(".")[0].upper() in reserved:
        raise ExtractError(f"non-portable DPM member name: {name!r}")
    return name


def read_dpm(exe: bytes) -> tuple[dict, list[dict]]:
    """Return validated directory entries; all offsets refer to the PE file."""
    if len(exe) > MAX_FILE_BYTES:
        raise ExtractError("PE input exceeds the 64 MiB inspection limit")
    if exe[:2] != b"MZ":
        raise ExtractError("input is not a DOS/PE executable")
    pe_offset, = _unpack("<I", exe, 0x3C, "DOS e_lfanew")
    if pe_offset < 0x40:
        raise ExtractError("PE header overlaps DOS header")
    signature, machine, count, _, _, _, optional_size, _ = _unpack(
        "<4sHHIIIHH", exe, pe_offset, "PE header")
    if signature != b"PE\0\0" or machine != 0x14C:
        raise ExtractError("only an i386 PE32 carrier is supported")
    if not 1 <= count <= 96 or optional_size < 96:
        raise ExtractError("invalid PE section count or optional header size")
    optional = pe_offset + 24
    _range(optional, optional_size, len(exe), "PE optional header")
    magic, = _unpack("<H", exe, optional, "PE optional magic")
    if magic != 0x10B:
        raise ExtractError("only PE32 optional headers are supported")
    table = optional + optional_size
    _, headers_end = _range(table, 40 * count, len(exe), "PE section table")
    sections = []
    for i in range(count):
        size, offset = _unpack("<II", exe, table + i * 40 + 16, "PE raw section")
        if not size:
            continue
        start, end = _range(offset, size, len(exe), f"PE section {i}")
        if start < headers_end:
            raise ExtractError("PE raw section overlaps headers")
        sections.append((start, end, f"PE section {i}"))
    if not sections:
        raise ExtractError("PE has no file-backed sections")
    _nonoverlap(sections)
    dpm_start = max(end for _, end, _ in sections)
    magic, data_offset, file_count, reserved = _unpack("<4sIII", exe, dpm_start, "DPM header")
    if magic != b"DPMX":
        raise ExtractError("no DPMX directory at end of PE sections")
    if not 1 <= file_count <= 4096:
        raise ExtractError("invalid DPM member count")
    if reserved != 0 or data_offset != 16 + file_count * 32:
        raise ExtractError("unsupported DPM directory layout")
    _range(dpm_start, data_offset, len(exe), "DPM directory")
    data_start = dpm_start + data_offset
    seen: set[str] = set()
    entries = []
    member_ranges = []
    for i in range(file_count):
        raw, unknown, key, relative_offset, size = _unpack(
            "<16sIIII", exe, dpm_start + 16 + i * 32, "DPM entry")
        name = _portable_name(raw)
        if name.casefold() in seen:
            raise ExtractError(f"duplicate DPM member: {name}")
        seen.add(name.casefold())
        start, end = _range(data_start + relative_offset, size, len(exe), f"DPM {name}")
        member_ranges.append((start, end, name))
        entries.append({"name": name, "offset": start, "bytes": size,
                        "encrypted": bool(key), "directory_unknown": unknown,
                        "stored_sha256": digest(exe[start:end])})
    _nonoverlap(member_ranges)
    last_end = max(end for _, end, _ in member_ranges)
    if last_end != len(exe):
        raise ExtractError("unexplained bytes after last DPM member")
    return {"offset": dpm_start, "data_offset": data_start, "member_count": file_count}, entries


def decrypt_ax(encrypted: bytes) -> bytes:
    """Decode HSP's byte-delta/XOR/add obfuscation without executing it."""
    if not HSP_HEADER_BYTES <= len(encrypted) <= MAX_FILE_BYTES:
        raise ExtractError("encrypted AX size is outside the supported range")
    magic = b"HSP3"
    delta = bytes((value - (magic[i - 1] if i else 0)) & 255 for i, value in enumerate(magic))
    candidates = []
    for xor_then_add in (False, True):
        for add in range(128):
            xor = (((encrypted[0] - add) & 255) ^ delta[0] if xor_then_add
                   else encrypted[0] ^ ((delta[0] + add) & 255))
            def encode(value: int) -> int:
                return (((value ^ xor) + add) & 255 if xor_then_add
                        else ((value + add) & 255) ^ xor)
            if bytes(encode(value) for value in delta) == encrypted[:4]:
                candidates.append((xor_then_add, add, xor))
    valid = []
    for xor_then_add, add, xor in candidates:
        def decode(source: bytes) -> bytes:
            result = bytearray(len(source))
            previous = 0
            for i, value in enumerate(source):
                delta_byte = (((value - add) & 255) ^ xor if xor_then_add
                              else ((value ^ xor) - add) & 255)
                previous = (previous + delta_byte) & 255
                result[i] = previous
            return bytes(result)
        # Four magic bytes alone can admit distinct transforms. Check the
        # declared version/length before decoding and validating the whole AX.
        prefix = decode(encrypted[:20])
        version, variables, declared_size, code_offset = struct.unpack_from("<4i", prefix, 4)
        if (version != 0x301 or variables < 0 or declared_size != len(encrypted)
                or code_offset != HSP_HEADER_BYTES):
            continue
        result = decode(encrypted)
        try:
            inspect_ax(result)
        except ExtractError:
            continue
        if result not in valid:
            valid.append(result)
    if len(valid) != 1:
        raise ExtractError(f"expected one structurally valid HSP3 decoding, found {len(valid)}")
    return valid[0]


def inspect_ax(ax: bytes) -> dict:
    """Inspect legacy serialized declarations, not reachable/executed calls."""
    if len(ax) > MAX_FILE_BYTES:
        raise ExtractError("AX exceeds the 64 MiB inspection limit")
    header = _unpack("<4s23i", ax, 0, "legacy HSP header")
    if header[0] != b"HSP3" or header[1] != 0x301 or header[4] != HSP_HEADER_BYTES:
        raise ExtractError("only HSP3 object format 0x301 with a 96-byte header is supported")
    if header[2] < 0 or header[3] != len(ax):
        raise ExtractError("invalid variable count or AX declared size mismatch")
    segments = {}
    ranges = [(0, HSP_HEADER_BYTES, "HSP header")]
    for name, index, stride in (("code", 4, 2), ("data", 6, 1), ("labels", 8, 4),
                                ("debug", 10, 1), ("libraries", 12, 16),
                                ("functions", 14, 28), ("parameters", 16, 8),
                                ("functions2", 18, 1)):
        offset, size = header[index:index + 2]
        start, end = _range(offset, size, len(ax), f"HSP {name}")
        if size and (offset < HSP_HEADER_BYTES or size % stride):
            raise ExtractError(f"invalid HSP {name} alignment/size")
        segments[name] = {"offset": offset, "bytes": size}
        ranges.append((start, end, f"HSP {name}"))
    # Header offset 84 packs two uint16 values, not one signed int.
    hpi_count, variable_plugin_count = _unpack("<HH", ax, 84, "HSP plugin counts")
    hpi_start, hpi_end = _range(header[20], hpi_count * 16, len(ax), "HSP plugin table")
    ranges.append((hpi_start, hpi_end, "HSP plugins"))
    if hpi_count or variable_plugin_count or segments["functions2"]["bytes"]:
        raise ExtractError("extended plugin or secondary function tables are unsupported")
    _nonoverlap(ranges)
    data = ax[header[6]:header[6] + header[7]]

    def string_at(offset: int, context: str) -> str:
        if not 0 <= offset < len(data):
            raise ExtractError(f"{context}: name offset outside data segment")
        end = data.find(b"\0", offset)
        if end < 0:
            raise ExtractError(f"{context}: unterminated name")
        try:
            return data[offset:end].decode("cp932")
        except UnicodeDecodeError as exc:
            raise ExtractError(f"{context}: name is not valid CP932") from exc

    labels = list(struct.iter_unpack("<i", ax[header[8]:header[8] + header[9]]))
    if any(not 0 <= label < header[5] // 2 for label, in labels):
        raise ExtractError("HSP label target outside code segment")
    libraries = []
    for i, offset in enumerate(range(header[12], header[12] + header[13], 16)):
        flag, name_offset, stored_handle, clsid = struct.unpack_from("<4i", ax, offset)
        if flag not in (0, 1, 2, 3, 4):
            raise ExtractError(f"library {i}: unknown declaration flag {flag}")
        libraries.append({"index": i, "name": string_at(name_offset, f"library {i}"),
                          "flag": flag, "name_data_offset": name_offset,
                          "stored_handle": stored_handle, "clsid_raw": clsid})
    functions = []
    parameter_count = header[17] // 8
    for i, offset in enumerate(range(header[14], header[14] + header[15], 28)):
        library, subid, first, count, name_offset, stack_bytes, target, flags = struct.unpack_from("<hh6i", ax, offset)
        if not -3 <= library < len(libraries):
            raise ExtractError(f"function {i}: invalid library/function kind index")
        if first < 0 or count < 0 or first + count > parameter_count or stack_bytes < 0:
            raise ExtractError(f"function {i}: invalid parameter range or stack size")
        if library < 0 and not 0 <= target < len(labels):
            raise ExtractError(f"function {i}: invalid label table target")
        parameters = []
        for j in range(first, first + count):
            kind, owner, stack_offset = struct.unpack_from("<hhi", ax, header[16] + j * 8)
            parameters.append({"type": kind, "subid": owner, "offset": stack_offset})
        functions.append({"index": i, "name": string_at(name_offset, f"function {i}"),
                          "library_index": library if library >= 0 else None,
                          "kind": "external" if library >= 0 else {-1: "function", -2: "cfunction", -3: "struct"}[library],
                          "subid": subid, "first_parameter": first, "parameters": parameters,
                          "stack_bytes": stack_bytes, "target_or_flags": target, "function_flags": flags})
    return {"magic": "HSP3", "object_format_version": "0x301", "header_bytes": HSP_HEADER_BYTES,
            "bytes": len(ax), "sha256": digest(ax), "variable_count": header[2],
            "boot_options": header[22], "runtime_file_offset": header[23],
            "segments": segments, "label_count": len(labels), "parameter_count": parameter_count,
            "library_count": len(libraries), "function_count": len(functions),
            "external_function_count": sum(f["kind"] == "external" for f in functions),
            "libraries": libraries, "functions": functions,
            "limitations": ["Declarations do not prove invocation or runtime reachability.",
                            "Object format 0x301 is not identification of the exact HSP compiler release.",
                            "Code opcodes, debug records and game behavior are not decoded or executed.",
                            "Only the observed legacy header layout and serialized declaration sizes are supported."]}


def extract(exe: bytes, *, require_baseline: bool = True) -> tuple[dict[str, bytes], dict]:
    source_hash = digest(exe)
    if require_baseline and source_hash != EXE_SHA256:
        raise ExtractError("input SHA-256 is not the pinned ElonaPlus 2.335 executable")
    dpm, entries = read_dpm(exe)
    files = {}
    ax_inventory = None
    for entry in entries:
        if entry["name"].casefold() in {"manifest.json", "inventory.json"}:
            raise ExtractError("DPM member conflicts with a generated report filename")
        start = entry["offset"]
        payload = exe[start:start + entry["bytes"]]
        if entry["encrypted"]:
            if entry["name"] != "start.ax":
                raise ExtractError(f"unsupported encrypted member {entry['name']}")
            payload = decrypt_ax(payload)
        if entry["name"] == "start.ax":
            ax_inventory = inspect_ax(payload)
            if require_baseline and digest(payload) != AX_SHA256:
                raise ExtractError("decoded start.ax SHA-256 differs from the pinned baseline")
        files[entry["name"]] = payload
        entry["output_sha256"] = digest(payload)
        entry["output_bytes"] = len(payload)
    if ax_inventory is None:
        raise ExtractError("no start.ax member found")
    inventory_data = (json.dumps(ax_inventory, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    files["inventory.json"] = inventory_data
    manifest = {"schema_version": 1, "game_version": VERSION if require_baseline else "unverified-fixture",
                "source": {"file": "elonaplus.exe", "bytes": len(exe), "sha256": source_hash},
                "format_reference": "https://github.com/Ruin0x11/borscht/tree/03c8be3398a59d9df445fd5a0c7850b3f2188869",
                "dpm": dpm, "members": entries,
                "inventory": {"file": "inventory.json", "sha256": digest(inventory_data)},
                "summary": {key: ax_inventory[key] for key in ("sha256", "variable_count", "label_count", "library_count", "function_count", "external_function_count")},
                "scope": "Static extraction and declaration inventory only; no game execution."}
    files["manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    return files, manifest


def write_outputs(output: Path, files: dict[str, bytes], *, original_dir: Path) -> bool:
    """Publish a new directory, or verify an identical one; never overwrite files."""
    original = original_dir.resolve(strict=True)
    for name in files:
        try:
            encoded_name = name.encode("ascii")
        except UnicodeEncodeError as exc:
            raise ExtractError("output filenames must be portable ASCII") from exc
        _portable_name(encoded_name)
    if output.is_symlink():
        raise ExtractError("output directory must not be a symlink")
    resolved = output.resolve()
    if resolved == original or original in resolved.parents or resolved in original.parents:
        raise ExtractError("output must be separate from the original game directory")
    if output.exists():
        if not output.is_dir() or {p.name for p in output.iterdir()} != set(files):
            raise ExtractError("existing output directory has unexpected contents; choose a new output path")
        for name, data in files.items():
            path = output / name
            if path.is_symlink() or not path.is_file() or path.read_bytes() != data:
                raise ExtractError(f"existing output differs: {name}; choose a new output path")
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=f".{output.name}-", dir=output.parent))
    try:
        for name, data in files.items():
            # Names originate in the validated directory, or fixed internal filenames.
            with (stage / name).open("xb") as stream:
                stream.write(data)
        if output.exists() or output.is_symlink():
            raise ExtractError("output appeared during extraction; retry to verify it")
        os.rename(stage, output)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return True
