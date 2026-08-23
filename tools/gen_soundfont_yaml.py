#!/usr/bin/env python
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
ENUMS = os.path.join(ROOT, 'include', 'enums.h')
INS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'BK_InstrumentNames.ins')
REVISIONS = {
    os.path.join('us', 'rev0'): ((0xD846C0, 0xD954B0), (0xEA3EB0, 0xEADE60)),
    os.path.join('us', 'rev1'): ((0xD87CA0, 0xD98A90), (0xEA7490, 0xEB1440)),
    os.path.join('pal', 'rev0'): ((0xDA8DF0, 0xDB9BE0), (0xEC85E0, 0xED2590)),
    os.path.join('jp', 'rev0'): ((0xDA80A0, 0xDB8E90), (0xEC7890, 0xED1840)),
}
CTL_SIZE = {1: 0x10DF0, 2: 0x9FB0}
SFX_BASE = {1: 0x000, 2: 0x3E9}
SFX_COUNT = {1: 402, 2: 61}

def sfx_enum():
    """enumerator name -> id."""
    text = open(ENUMS, encoding='utf-8', errors='replace').read()
    start = text.index('enum sfx_e')
    body = text[start:text.index('};', start)]
    out = {}
    cur = 0
    for line in body.splitlines():
        line = line.split('//')[0].strip().rstrip(',')
        m = re.match(r'^(SFX_[0-9A-Fa-f]+_?\w*)\s*(?:=\s*(0[xX][0-9A-Fa-f]+|\d+))?$', line)
        if not m:
            continue
        cur = int(m.group(2), 0) if m.group(2) else cur
        out[m.group(1)] = cur
        cur += 1
    return out

def short_names(enum):
    """id -> name with the SFX_<id>_ prefix stripped."""
    out = {}
    for name, sfx_id in enum.items():
        short = re.sub(r'^SFX_[0-9A-Fa-f]+_?', '', name)
        out[sfx_id] = short if short else 'UNNAMED'
    return out

def sfx_users(enum):
    """id -> sorted source basenames that name it."""
    token = re.compile(r'\bSFX_[0-9A-Fa-f]+_?\w*\b')
    users = defaultdict(set)
    for root, _dirs, files in os.walk(SRC):
        for fn in files:
            if not fn.endswith('.c'):
                continue
            text = open(os.path.join(root, fn), encoding='utf-8', errors='replace').read()
            for tok in set(token.findall(text)):
                if tok in enum:
                    users[enum[tok]].add(fn[:-2])
    return {k: sorted(v) for k, v in users.items()}

def instrument_names():
    text = open(INS, encoding='utf-8', errors='replace').read()
    out = {}
    for line in text.splitlines():
        match = re.match(r'\s*(\d+)\s*=\s*(.+?)\s*$', line)
        if not match:
            continue
        name = match.group(2).split('(')[0]
        name = re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_').lower()
        if name:
            out[int(match.group(1))] = name
    return out

HEADER = ''':config:
  directory: assets
  sfx_names:
{names}
  instrument_names:
{instruments}
  sfx_users:
{users}
'''

ENTRY = '''{key}:
  type: BK64:SOUNDFONT
  offset: 0x{ctl:X}
  size: 0x{size:X}
  tbl_offset: 0x{tbl:X}
  sfx_path: sfx
  inst_path: {inst_path}
  sfx_base: 0x{sfx_base:X}
'''

def main():
    enum = sfx_enum()
    names = short_names(enum)
    users = sfx_users(enum)
    instruments = instrument_names()
    inst_lines = ['    %d: %s' % (i, n) for i, n in sorted(instruments.items())]
    name_lines, user_lines = [], []
    for bank in (1, 2):
        base, count = SFX_BASE[bank], SFX_COUNT[bank]
        for i in range(count):
            sfx_id = base + i
            name_lines.append('    0x%03X: %s' % (sfx_id, names.get(sfx_id, 'UNNAMED')))
            if sfx_id in users:
                user_lines.append('    0x%03X: %d' % (sfx_id, len(users[sfx_id])))
    written = 0
    for rev, ((ctl1, tbl1), (ctl2, tbl2)) in REVISIONS.items():
        out = HEADER.format(names='\n'.join(name_lines),
                            instruments='\n'.join(inst_lines),
                            users='\n'.join(user_lines))
        out += ENTRY.format(key='sfx_bank', ctl=ctl1, size=CTL_SIZE[1], tbl=tbl1,
                            inst_path='sfx', sfx_base=SFX_BASE[1])
        out += ENTRY.format(key='instrument_bank', ctl=ctl2, size=CTL_SIZE[2],
                            tbl=tbl2, inst_path='instruments', sfx_base=SFX_BASE[2])
        path = os.path.join(ROOT, 'assets', 'yaml', rev, 'soundfont.yaml')
        open(path, 'w', newline='\n').write(out)
        written += 1
        print('wrote', os.path.relpath(path, ROOT))
    referenced = sum(1 for b in (1, 2) for i in range(SFX_COUNT[b]) if SFX_BASE[b] + i in users)
    total = SFX_COUNT[1] + SFX_COUNT[2]
    print('%d files | %d of %d sounds named in code | %d instruments named'
          % (written, referenced, total, len(instruments)))

if __name__ == '__main__':
    sys.exit(main())