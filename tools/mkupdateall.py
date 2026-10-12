#!/usr/bin/env python3
"""Writes picoGalagino's sets into pico-bootLoader's updateAll.json.

galagino/gal_games.c is the single source of truth for the ROM sets: for every
game it lists the folder, the zips (MAME's parent set first for a clone) and the
files with their sizes and CRC32s. This script turns that into the `cores`
entries of updateAll.json:

    tools/mkupdateall.py ../pico-bootLoader/updateAll/updateAll.json

An entry with the game's destination folder is updated in place and keeps its
id (pico-phoenix and pico-mooncresta share their folders and sets with
picoGalagino); a game without one gets a new entry, placed after the last of
the others. Every other entry is left as it is. With --print the entries are
printed instead of written.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAMES_C = os.path.join(HERE, '..', 'galagino', 'gal_games.c')
ROMROOT = '/roms/arcade'


def parse_games(path):
    src = open(path, encoding='utf-8').read()
    arrays = {}
    for m in re.finditer(r'/\* (MAME (?:[^*]|\*(?!/))*?)\*/\s*static const gal_romfile_t (\w+)\[\] = \{(.*?)\n\};', src, re.S):
        # the comment's first sentence, up to a colon: "MAME mspacman: ..."
        desc = re.sub(r'\s*\n\s*\*\s*', ' ', m.group(1)).strip()
        desc = desc.split(':')[0].strip().rstrip('.')
        var, body = m.group(2), m.group(3)
        files = [(n, int(c, 16), int(s, 16)) for n, c, s in
                 re.findall(r'\{"([^"]+)", 0x([0-9a-fA-F]{8}), 0x([0-9a-fA-F]+),', body)]
        arrays[var] = (desc, files)
    games = []
    for m in re.finditer(r'\[GAL_GAME_(\w+)\] = \{"((?:[^"\\]|\\.)*)", "([^"]+)", "([^"]+)", \{([^}]*)\}, (\w+),', src):
        enum, title, folder, mameset, zips, var = m.groups()
        desc, files = arrays[var]
        games.append({
            'id': enum.lower(),
            'name': title.replace('\\"', '"'),
            'set': desc,
            'zips': [z.strip().strip('"') for z in zips.split(',') if z.strip()],
            'destination': ROMROOT + '/' + folder,
            'files': [{'name': n, 'size': s, 'crc': '%08x' % c} for n, c, s in files],
        })
    if not games:
        raise SystemExit('no games found in ' + path)
    return games


def render_entry(e, indent='    '):
    """One entry as updateAll.json writes it: a file per line, columns aligned."""
    i2 = indent + '  '
    lines = [indent + '{']
    lines.append('%s"id": %s,' % (i2, json.dumps(e['id'])))
    lines.append('%s"name": %s,' % (i2, json.dumps(e['name'], ensure_ascii=False)))
    lines.append('%s"set": %s,' % (i2, json.dumps(e['set'], ensure_ascii=False)))
    lines.append('%s"zips": [%s],' % (i2, ', '.join(json.dumps(z) for z in e['zips'])))
    lines.append('%s"destination": %s,' % (i2, json.dumps(e['destination'])))
    lines.append('%s"files": [' % i2)
    names = ['"name": %s,' % json.dumps(f['name']) for f in e['files']]
    sizes = ['"size": %d,' % f['size'] for f in e['files']]
    wn = max(len(n) for n in names)
    ws = max(len(s) for s in sizes)
    for k, f in enumerate(e['files']):
        comma = ',' if k < len(e['files']) - 1 else ''
        lines.append('%s  { %s %s "crc": "%s" }%s' % (i2, names[k].ljust(wn), sizes[k].ljust(ws), f['crc'], comma))
    lines.append(i2 + ']')
    lines.append(indent + '}')
    return '\n'.join(lines)


def render_file(doc):
    out = ['{']
    out.append('  "databases": [%s],' % ', '.join(json.dumps(d) for d in doc['databases']))
    out.append('  "cores": [')
    out.append(',\n'.join(render_entry(e) for e in doc['cores']))
    out.append('  ]')
    out.append('}')
    return '\n'.join(out) + '\n'


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    games = parse_games(GAMES_C)
    if '--print' in sys.argv or not args:
        print(',\n'.join(render_entry(g) for g in games))
        return
    path = args[0]
    text = open(path, encoding='utf-8').read()
    doc = json.loads(text)
    if render_file(doc) != text:
        raise SystemExit('%s is not in the layout this script writes; not touching it' % path)

    cores = doc['cores']
    where = {c['destination']: i for i, c in enumerate(cores)}
    last = max((where[g['destination']] for g in games if g['destination'] in where), default=len(cores) - 1)
    added = 0
    for g in games:
        if g['destination'] in where:
            old = cores[where[g['destination']]]
            g = dict(g, id=old['id'])  # keep ids others rely on
            cores[where[g['destination']]] = g
        else:
            last += 1
            cores.insert(last, g)
            where = {c['destination']: i for i, c in enumerate(cores)}
            added += 1
    ids = [c['id'] for c in cores]
    dup = set(i for i in ids if ids.count(i) > 1)
    if dup:
        raise SystemExit('duplicate ids: ' + ', '.join(sorted(dup)))
    new_text = render_file(doc)
    open(path, 'w', encoding='utf-8').write(new_text)
    print('%s: %d games, %d added, %d entries in all' % (path, len(games), added, len(cores)))


if __name__ == '__main__':
    main()
