"""Inspect an ESP-IDF full flash dump; optionally extract raw partitions. No device writes."""
import argparse, hashlib, json, re, struct
from pathlib import Path

def number(s):
    return int(s, 0)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image', type=Path)
    p.add_argument('--table-offset', type=number, default=0x8000)
    p.add_argument('--extract', type=Path)
    args = p.parse_args()
    data = args.image.read_bytes()
    start = args.table_offset
    entries = []
    checksum = 'absent'
    for pos in range(start, min(start + 0xC00, len(data)) - 31, 32):
        raw = data[pos:pos+32]
        magic = struct.unpack_from('<H', raw)[0]
        if magic == 0xEBEB:
            checksum = 'OK' if hashlib.md5(data[start:pos]).digest() == raw[16:32] else 'INVALID'
            break
        if raw == b'\xff' * 32:
            break
        if magic != 0x50AA:
            raise SystemExit(f'Invalid partition entry at 0x{pos:X}; check dump base and table offset.')
        _, typ, sub, offset, size, label, flags = struct.unpack('<HBBII16sI', raw)
        name = label.split(b'\0')[0].decode('utf-8', errors='replace')
        entry = dict(name=name, type=typ, subtype=sub, offset=offset, size=size, flags=flags,
                     available=offset + size <= len(data))
        if size == 0 or offset < start + 0x1000:
            raise SystemExit('Invalid partition bounds')
        entries.append(entry)
    if not entries:
        raise SystemExit('No partition table found')
    ordered = sorted(entries, key=lambda e: e['offset'])
    for a, b in zip(ordered, ordered[1:]):
        if a['offset'] + a['size'] > b['offset']:
            raise SystemExit('Overlapping partitions')
    print(f'File: {args.image.name} | bytes: {len(data)} | table MD5: {checksum}')
    print('NAME                 TYPE SUB  OFFSET      SIZE        KiB       COMPLETE')
    for e in entries:
        print(f"{e['name']:<20} {e['type']:02X}   {e['subtype']:02X}   0x{e['offset']:08X}  0x{e['size']:08X}  {e['size']/1024:8.0f}  {e['available']}")
    if checksum == 'INVALID':
        raise SystemExit('Checksum mismatch; extraction refused')
    if args.extract:
        if not all(e['available'] for e in entries):
            raise SystemExit('Dump is incomplete: use full backup for extraction')
        args.extract.mkdir(parents=True, exist_ok=False)
        for i, e in enumerate(entries):
            name = re.sub(r'[^A-Za-z0-9_-]', '_', e['name'])
            payload = data[e['offset']:e['offset']+e['size']]
            filename = f"{i:02d}_{name}_0x{e['offset']:08X}.bin"
            (args.extract / filename).write_bytes(payload)
            e.update(file=filename, sha256=hashlib.sha256(payload).hexdigest())
        (args.extract / 'partition_table.bin').write_bytes(data[start:start+0x1000])
        (args.extract / 'manifest.json').write_text(json.dumps(dict(source=args.image.name,
            source_sha256=hashlib.sha256(data).hexdigest(), table_md5=checksum, partitions=entries), indent=2), encoding='utf-8')
        print(f'Extracted to: {args.extract}')

if __name__ == '__main__':
    main()
