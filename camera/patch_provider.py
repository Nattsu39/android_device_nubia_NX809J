#!/usr/bin/env python3
"""Add the camera adapter to the pinned OEM provider's ELF dependencies.

The service enters a SELinux domain with AT_SECURE, which disables LD_PRELOAD.
An explicit DT_NEEDED dependency is loaded in that mode without changing the
service identity, capabilities, executable instructions or SELinux policy.

This transformer supports one verified ELF, rather than arbitrary binaries. Its
read-only segment has space for a copied string table; the writable RELRO segment
has space for the enlarged dynamic table. All existing addresses remain stable.
"""

import argparse
import hashlib
from pathlib import Path
import struct


ORIGINAL_SHA256 = '8149f32f1e756b21a3698f127db22c4f2ba5fc43e779ba1bd6a520e5319718ac'
LIBRARY = b'libnx809j_camera_provider.so\0'
PROGRAM_HEADER = struct.Struct('<IIQQQQQQ')
SECTION_HEADER = struct.Struct('<IIQQQQIIQQ')
DYNAMIC_ENTRY = struct.Struct('<qQ')
PT_LOAD, PT_DYNAMIC, PT_GNU_RELRO = 1, 2, 0x6474E552
DT_NULL, DT_NEEDED, DT_STRTAB, DT_STRSZ = 0, 1, 5, 10


def align(value, alignment=8):
    return (value + alignment - 1) & -alignment


def patch_provider(original):
    if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('OEM provider does not match the verified firmware input')
    output = bytearray(original)
    phoff, shoff = struct.unpack_from('<QQ', original, 32)
    phsize, phcount, shsize, shcount = struct.unpack_from('<HHHH', original, 54)
    if phsize != PROGRAM_HEADER.size or shsize != SECTION_HEADER.size:
        raise ValueError('Unexpected ELF header sizes')
    programs = [list(PROGRAM_HEADER.unpack_from(original, phoff + index * phsize))
                for index in range(phcount)]
    sections = [list(SECTION_HEADER.unpack_from(original, shoff + index * shsize))
                for index in range(shcount)]
    dynamic = next(header for header in programs if header[0] == PT_DYNAMIC)
    entries = [DYNAMIC_ENTRY.unpack_from(original, offset)
               for offset in range(dynamic[2], dynamic[2] + dynamic[5], DYNAMIC_ENTRY.size)]
    if entries[-1] != (DT_NULL, 0) or any(tag == DT_NULL for tag, value in entries[:-1]):
        raise ValueError('Unexpected dynamic table terminator')
    strings_address = next(value for tag, value in entries if tag == DT_STRTAB)
    strings_size = next(value for tag, value in entries if tag == DT_STRSZ)
    readonly = next(header for header in programs if header[0] == PT_LOAD and header[1] == 4)
    writable = next(header for header in programs
                    if header[0] == PT_LOAD and header[2] <= dynamic[2] < header[2] + header[5])
    relro = next(header for header in programs if header[0] == PT_GNU_RELRO)
    strings_offset = strings_address - readonly[3] + readonly[2]
    relocated_strings = align(readonly[2] + readonly[5])
    relocated_dynamic = align(writable[2] + writable[5])
    new_strings = original[strings_offset:strings_offset + strings_size] + LIBRARY
    new_strings_address = readonly[3] + relocated_strings - readonly[2]
    mapped_entries = [(DT_NEEDED, strings_size)] + [
        (tag, new_strings_address if tag == DT_STRTAB else
         len(new_strings) if tag == DT_STRSZ else value)
        for tag, value in entries
    ]
    new_dynamic = b''.join(DYNAMIC_ENTRY.pack(tag, value) for tag, value in mapped_entries)
    strings_end = relocated_strings + len(new_strings)
    dynamic_end = relocated_dynamic + len(new_dynamic)
    following_load = min(header[2] for header in programs
                         if header[0] == PT_LOAD and header[2] > readonly[2])
    if strings_end > following_load or dynamic_end > writable[2] + writable[6]:
        raise ValueError('Reserved ELF padding is too small')
    if any(original[readonly[2] + readonly[5]:strings_end]) or any(
            original[writable[2] + writable[5]:dynamic_end]):
        raise ValueError('Reserved ELF padding is not empty')
    output[relocated_strings:strings_end] = new_strings
    output[relocated_dynamic:dynamic_end] = new_dynamic
    readonly[5] = readonly[6] = strings_end - readonly[2]
    writable[5] = dynamic_end - writable[2]
    dynamic[2] = relocated_dynamic
    dynamic[3] = dynamic[4] = writable[3] + relocated_dynamic - writable[2]
    dynamic[5] = dynamic[6] = len(new_dynamic)
    relro[5] = dynamic_end - relro[2]
    for index, header in enumerate(programs):
        PROGRAM_HEADER.pack_into(output, phoff + index * phsize, *header)
    for index, section in enumerate(sections):
        if section[4] == strings_offset and section[5] == strings_size:
            section[3:6] = [new_strings_address, relocated_strings, len(new_strings)]
        elif section[1] == 6:  # SHT_DYNAMIC
            section[3:6] = [dynamic[3], relocated_dynamic, len(new_dynamic)]
        elif section[1] == 8 and section[4] == relocated_dynamic:  # RELRO NOBITS padding
            section[3] += len(new_dynamic)
            section[4] += len(new_dynamic)
            section[5] -= len(new_dynamic)
        SECTION_HEADER.pack_into(output, shoff + index * shsize, *section)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    patched = patch_provider(args.input.read_bytes())
    args.output.write_bytes(patched)
    print(f'{hashlib.sha256(patched).hexdigest()}  {args.output}')


if __name__ == '__main__':
    main()
