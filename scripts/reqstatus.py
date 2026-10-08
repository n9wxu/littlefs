#!/usr/bin/env python3
#
# Count the requirements in REQUIREMENTS.md by area, status, level and
# When, and check the commits their Status fields cite.
#
# Each requirement is a "#### LFS3-<AREA>-<NN>" heading followed by
# "- **Field:**" items. The first words of the Status field give its
# status: Tested, Partly tested, Untested, Not implemented, Open question
# or Known defect. The first word of Source gives the level, and When the
# schedule.
#
# With -c/--commits, every hex string of 7 to 12 characters in the Status
# fields that names a commit must be an ancestor of the given ref.
#
# Example:
# ./scripts/reqstatus.py REQUIREMENTS.md
# ./scripts/reqstatus.py REQUIREMENTS.md -c v3-integration
#
# Copyright (c) 2022, The littlefs authors.
# SPDX-License-Identifier: BSD-3-Clause
#

# prevent local imports
if __name__ == "__main__":
    __import__('sys').path.pop(0)

import collections as co
import re
import subprocess as sp
import sys


STATUSES = [
    ('T', 'Tested'),
    ('P', 'Partly tested'),
    ('U', 'Untested'),
    ('N', 'Not implemented'),
    ('Q', 'Open question'),
    ('K', 'Known defect'),
]

LEVELS = ['Stated', 'Derived', 'Proposal']
WHENS = ['every CI run', 'nightly', 'before v3-beta']


def parse(path):
    reqs = []
    names = {}
    req = None
    field = None
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            # area names come from the section headings, "### 6.1 Name (GEN)"
            m = re.match(r'### 6\.\d+ (.*) \(([A-Z]+)\)\s*$', line)
            if m:
                names[m.group(2)] = m.group(1)
            m = re.match(r'#### (LFS3-([A-Z]+)-\d+)\s*$', line)
            if m:
                req = {'id': m.group(1), 'area': m.group(2)}
                reqs.append(req)
                field = None
                continue
            if line.startswith('#'):
                req = None
                continue
            if req is None:
                continue
            m = re.match(r'- \*\*([A-Za-z ]+):\*\*\s*(.*)', line)
            if m:
                field = m.group(1)
                req[field] = m.group(2)
            elif field and line.startswith('  '):
                req[field] += ' ' + line.strip()
            else:
                field = None
    return reqs, names

def status(req):
    s = req.get('Status', '')
    for k, name in STATUSES:
        if s.startswith(name):
            # "Tested" must not swallow "Partly tested" and friends
            return k
    return None

def main(path, *, commits=None):
    reqs, names = parse(path)
    errs = 0

    # count by area, keeping the document's order
    areas = co.OrderedDict()
    for req in reqs:
        k = status(req)
        if k is None:
            print('%s: unknown status: %r' % (req['id'], req.get('Status')),
                    file=sys.stderr)
            errs += 1
            continue
        areas.setdefault(req['area'], co.Counter())[k] += 1

    keys = [k for k, _ in STATUSES]
    print('| Area | Code | Total | %s |' % ' | '.join(keys))
    print('|---|---|---|%s' % ('---|'*len(keys)))
    total = co.Counter()
    for area, c in areas.items():
        print('| %s | %s | %d | %s |' % (
                names.get(area, area), area, sum(c.values()),
                ' | '.join(str(c[k]) for k in keys)))
        total.update(c)
    print('| **All** | | **%d** | %s |' % (
            sum(total.values()),
            ' | '.join('**%d**' % total[k] for k in keys)))
    print()

    levels = co.Counter()
    whens = co.Counter()
    for req in reqs:
        src = req.get('Source', '')
        levels[next((l for l in LEVELS if src.startswith(l)), '?')] += 1
        when = req.get('When', '').rstrip('.')
        whens[next((w for w in WHENS if when.startswith(w)), when)] += 1
    print('By level: %s.' % ', '.join(
            '%d %s' % (n, l.lower()) for l, n in levels.items()))
    print('By When: %s.' % ', '.join(
            '%d %s' % (n, w) for w, n in whens.items()))

    # check the commits each Status cites
    if commits:
        seen = {}
        for req in reqs:
            for h in re.findall(r'\b[0-9a-f]{7,12}\b',
                    req.get('Status', '')):
                if h not in seen:
                    iscommit = sp.run(
                            ['git', 'cat-file', '-e', h+'^{commit}'],
                            stderr=sp.DEVNULL).returncode == 0
                    seen[h] = (not iscommit
                            or sp.run(['git', 'merge-base', '--is-ancestor',
                                h, commits]).returncode == 0)
                    # digits only may just be a number
                    if not iscommit and re.search('[a-f]', h):
                        print('%s: %s is not a commit' % (req['id'], h),
                                file=sys.stderr)
                        errs += 1
                if not seen[h]:
                    print('%s: %s is not on %s' % (req['id'], h, commits),
                            file=sys.stderr)
                    errs += 1

    return 1 if errs else 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(
            description="Count requirements by area and status.",
            allow_abbrev=False)
    parser.add_argument(
            'path',
            nargs='?',
            default='REQUIREMENTS.md',
            help="Requirements document. Defaults to REQUIREMENTS.md.")
    parser.add_argument(
            '-c', '--commits',
            help="Check that every commit a Status cites is an ancestor "
                "of this ref.")
    sys.exit(main(**{k: v
            for k, v in vars(parser.parse_args()).items()
            if v is not None}))
