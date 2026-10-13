#!/usr/bin/env python3
"""Checks a dynafix.log written by logtest.exe / logtest_l.exe.
   check_log.py long <log> [launcher]      long strings: cut, CRLF-terminated, next line intact
   check_log.py mp <log> <id:count>...     several writers: no torn lines, nothing lost after the last trim"""
import re, sys

TS = rb'\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} '


def read_lines(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\r\n')
    return parts[:-1], d.endswith(b'\r\n'), d


def check_long(path, mode):
    launcher = mode == 'launcher'
    ls, ok, d = read_lines(path)
    bad = []
    if not ok:
        bad.append('file does not end with CRLF')
    if b'\n' in d.replace(b'\r\n', b'') or b'\r' in d.replace(b'\r\n', b''):
        bad.append('stray CR or LF inside a line')
    for l in ls:
        if not re.match(TS, l):
            bad.append('line without timestamp: %r' % l[:60])
        if len(l) + 2 > 1000:
            bad.append('line longer than 1000 bytes: %d' % (len(l) + 2))
    text = b'\n'.join(ls)
    need = [b'NEXT%d fine' % n for n in (10, 200, 500, 970, 1000, 1100, 2000, 5000)] + [b'LONG-DONE 1 2']
    if not launcher:
        need += [b'NEXT-SHEXEC fine', b'NEXT-STRIP fine'] + ([] if mode == 'old' else [b'W2U len '])
    for n in need:
        if not any(re.match(TS + re.escape(n), l) for l in ls):
            bad.append('line missing or damaged: %s' % n.decode())
    for n in (1100, 2000, 5000):
        l = [x for x in ls if re.match(TS + b'LONG%d ' % n, x)]
        if len(l) != 1 or not l[0].endswith(b'...'):
            bad.append('LONG%d: expected one line ending in "..."' % n)
    for n in (10, 200, 500):
        l = [x for x in ls if re.match(TS + b'LONG%d ' % n, x)]
        if len(l) != 1 or not l[0].endswith(b' tail'):
            bad.append('LONG%d: short line must be kept whole' % n)
    if not launcher:
        # an over-long Setup_ path is not the first-run helper (setup_match.h): it is passed through, nothing is logged
        sh = [x for x in ls if b'setup helper' in x]
        if sh:
            bad.append('ShellExecute: a path that is not the helper must not be logged as one')
        w = [x for x in ls if b'W2U len' in x] if mode != 'old' else [b'last-nonzero 1']
        if len(w) != 1 or b'last-nonzero 1' not in w[0]:
            bad.append('w2u did not terminate: %r' % w)
    print('lines %d, longest %d bytes' % (len(ls), max(len(l) + 2 for l in ls)))
    return bad


def check_mp(path, writers):
    ls, ok, d = read_lines(path)
    bad = []
    if not ok:
        bad.append('file does not end with CRLF')
    pat = re.compile(TS + rb'(?:PRE L\d{8} x{50}|W(\d+) L(\d{8}) x{50}) END$')
    seen = {}
    for n, l in enumerate(ls):
        m = pat.match(l)
        if not m:
            bad.append('malformed line %d: %r' % (n, l[:90]))
        elif m.group(1) is not None:
            seen.setdefault(int(m.group(1)), []).append(int(m.group(2)))
    for wid, count in writers:
        v = seen.get(wid, [])
        if not v:
            bad.append('writer %d: no lines at all' % wid)
            continue
        if v[-1] != count - 1:
            bad.append('writer %d: last line is %d, expected %d' % (wid, v[-1], count - 1))
        gaps = sum(1 for a, b in zip(v, v[1:]) if b != a + 1)
        if gaps:
            bad.append('writer %d: %d gaps/duplicates in the middle (lines lost)' % (wid, gaps))
        print('writer %d: %d lines kept, first kept L%d of %d' % (wid, len(v), v[0], count))
    print('file %d bytes, %d lines, %d malformed' % (len(d), len(ls), sum(1 for b in bad if b.startswith('malformed'))))
    return bad


if __name__ == '__main__':
    if sys.argv[1] == 'long':
        bad = check_long(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else '')
    else:
        bad = check_mp(sys.argv[2], [tuple(int(x) for x in a.split(':')) for a in sys.argv[3:]])
    for b in bad[:20]:
        print('FAIL:', b)
    if len(bad) > 20:
        print('... %d more' % (len(bad) - 20))
    print('RESULT:', 'FAIL' if bad else 'PASS')
    sys.exit(1 if bad else 0)
