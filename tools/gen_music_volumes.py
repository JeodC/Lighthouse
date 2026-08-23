import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'src', 'core1', 'audio_instruments.c')
YAMLS = ['us/rev0', 'us/rev1', 'pal/rev0', 'jp/rev0']
KEY = 'music_volumes'


def read_table():
    text = open(SOURCE, encoding='utf-8', errors='replace').read()
    body = re.search(r'MusicTrackMeta\s+musicTrackInfo\[\d+\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if body is None:
        sys.exit('musicTrackInfo not found in %s' % SOURCE)
    rows = re.findall(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\d+)\s*\}', body.group(1))
    if not rows:
        sys.exit('musicTrackInfo parsed to nothing')
    return [int(v) for _, v in rows]


def rewrite(path, volumes):
    lines = open(path, encoding='utf-8').read().split('\n')
    out = []
    skipping = False
    for line in lines:
        if skipping:
            if line.startswith('    ') or line.strip() == '':
                continue
            skipping = False
        if line.strip() == '%s:' % KEY:
            skipping = True
            continue
        out.append(line)
    end = next((i for i, line in enumerate(out) if i > 0 and line and not line.startswith(' ')), len(out))
    while end > 0 and not out[end - 1].strip():
        end -= 1
    block = ['  %s:' % KEY] + ['    %d: %d' % (i, v) for i, v in enumerate(volumes)]
    out[end:end] = block
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))


volumes = read_table()
for rel in YAMLS:
    path = os.path.join(ROOT, 'assets', 'yaml', *rel.split('/'), 'assets.yaml')
    if not os.path.exists(path):
        print('skipped %s (not present)' % rel)
        continue
    rewrite(path, volumes)
    print('%-9s %d volumes' % (rel, len(volumes)))
