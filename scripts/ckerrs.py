#!/usr/bin/env python3
#
# Check the error codes the tests saw against the codes lfs3.h documents.
#
# Run the tests with TEST_ERRS=<file> and the test runner records each
# (function, error code) pair a public function returns, see
# runners/test_errs.h. Each public function in lfs3.h ends its comment
# with a paragraph starting "Returns" that names every LFS3_ERR_* code it
# can return. This script fails if a test saw a code its function doesn't
# list, or a code that isn't a public error code at all.
#
# Example:
# TEST_ERRS=errs.txt ./scripts/test.py -j
# ./scripts/ckerrs.py errs.txt
#
# Copyright (c) 2022, The littlefs authors.
# SPDX-License-Identifier: BSD-3-Clause
#

# prevent local imports
if __name__ == "__main__":
    __import__('sys').path.pop(0)

import collections as co
import re
import sys


HEADER_PATH = 'lfs3.h'
HOOK_PATH = 'runners/test_errs.h'

# codes that exist but must never reach the application
INTERNAL = {'LFS3_ERR_UNKNOWN', 'LFS3_ERR_RANGE'}


# find the error codes in lfs3.h
def parse_codes(header):
    m = re.search(r'enum\s+lfs3_err\s*{(.*?)}', header, re.S)
    if not m:
        print('error: no enum lfs3_err in lfs3.h', file=sys.stderr)
        sys.exit(-1)
    codes = co.OrderedDict()
    for name, value in re.findall(
            r'(LFS3_ERR_\w+)\s*=\s*(-?\w+)', m.group(1)):
        codes[name] = int(value, 0)
    return codes

# find the public functions in lfs3.h, and the codes each one documents
def parse_funcs(header):
    lines = header.splitlines()
    funcs = co.OrderedDict()
    for i, line in enumerate(lines):
        m = re.match(
                r'(?:int|lfs3_ssize_t|lfs3_soff_t|lfs3_sblock_t)\s+'
                    r'(lfs3_\w+)\(',
                line)
        if not m:
            continue

        # find the comment, skipping any preprocessor lines
        j = i-1
        while j >= 0 and lines[j].startswith('#'):
            j -= 1
        comment = []
        while j >= 0 and lines[j].startswith('//'):
            comment.append(lines[j][2:].strip())
            j -= 1
        comment.reverse()

        # the codes are named in the paragraph starting "Returns", to the
        # end of the comment
        for k, c in enumerate(comment):
            if c.startswith('Returns'):
                returns = ' '.join(comment[k:])
                break
        else:
            returns = None

        funcs[m.group(1)] = (
                i+1,
                set(re.findall(r'LFS3_ERR_\w+', returns))
                    if returns is not None
                    else None)
    return funcs

# find the functions our hook wraps
def parse_hooks(hook):
    return set(re.findall(r'#define\s+(lfs3_\w+)\(\.\.\.\)', hook))

# read the pairs the runner recorded, "function error case" per line
def parse_errs(paths):
    errs = co.OrderedDict()
    for path in paths:
        try:
            f = open(path)
        except FileNotFoundError:
            print('error: no recordings in %s, did the tests run with '
                        'TEST_ERRS set?' % path,
                    file=sys.stderr)
            sys.exit(-1)
        with f:
            for lineno, line in enumerate(f, 1):
                fields = line.split()
                if not fields:
                    continue
                if len(fields) < 2:
                    print('%s:%d: error: can\'t parse %r' % (
                                path, lineno, line.rstrip()),
                            file=sys.stderr)
                    sys.exit(-1)
                func, err = fields[0], int(fields[1])
                case = fields[2] if len(fields) > 2 else '-'
                errs.setdefault((func, err), set()).add(case)
    return errs


def main(errs_paths, *,
        header_path=HEADER_PATH,
        hook_path=HOOK_PATH,
        verbose=False):
    with open(header_path) as f:
        header = f.read()
    with open(hook_path) as f:
        hook = f.read()

    codes = parse_codes(header)
    names = {v: k for k, v in codes.items()}
    funcs = parse_funcs(header)
    hooks = parse_hooks(hook)
    errs = parse_errs(errs_paths)

    failures = 0
    def fail(msg):
        nonlocal failures
        print('error: %s' % msg)
        failures += 1

    # every function must say what it returns, and only public codes
    for func, (lineno, documented) in funcs.items():
        if documented is None:
            fail('%s:%d: %s documents no "Returns" paragraph' % (
                    header_path, lineno, func))
            continue
        for code in sorted(documented):
            if code not in codes:
                fail('%s:%d: %s documents %s, which isn\'t in '
                        'enum lfs3_err' % (
                            header_path, lineno, func, code))
            elif code in INTERNAL:
                fail('%s:%d: %s documents %s, which is internal' % (
                        header_path, lineno, func, code))

    # every function must be hooked, or we can't see its codes
    for func in funcs:
        if func not in hooks:
            fail('%s: %s isn\'t hooked' % (hook_path, func))
    for func in sorted(hooks - set(funcs)):
        fail('%s: %s isn\'t in %s' % (hook_path, func, header_path))

    # check every code the tests saw
    for (func, err), cases in errs.items():
        where = ', '.join(sorted(cases))
        if func not in funcs:
            fail('%s returned %d, but isn\'t in %s (%s)' % (
                    func, err, header_path, where))
        elif err not in names:
            fail('%s returned %d, which isn\'t in enum lfs3_err (%s)' % (
                    func, err, where))
        elif names[err] in INTERNAL:
            fail('%s returned %s, which is internal (%s)' % (
                    func, names[err], where))
        elif funcs[func][1] is not None \
                and names[err] not in funcs[func][1]:
            fail('%s returned %s, which %s:%d doesn\'t list (%s)' % (
                    func, names[err], header_path, funcs[func][0], where))

    # show which documented codes no test saw
    seen = {(func, names.get(err)) for func, err in errs}
    unseen = [(func, code)
            for func, (_, documented) in funcs.items()
            for code in sorted(documented or [])
            if (func, code) not in seen]
    if verbose:
        for func, code in unseen:
            print('unseen: %s %s' % (func, code))

    print('errs: %d functions, %d pairs seen, %d documented pairs unseen, '
                '%d errors' % (
                len(funcs),
                len(errs),
                len(unseen),
                failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(
            description="Check the error codes the tests saw against the "
                "codes lfs3.h documents.",
            allow_abbrev=False)
    parser.add_argument(
            'errs_paths',
            nargs='+',
            help="Files of \"function error case\" lines written by the "
                "test runner with TEST_ERRS.")
    parser.add_argument(
            '-H', '--header',
            dest='header_path',
            default=HEADER_PATH,
            help="Header that documents the codes. Defaults to %r." % (
                HEADER_PATH))
    parser.add_argument(
            '--hook',
            dest='hook_path',
            default=HOOK_PATH,
            help="Header that hooks the public functions. Defaults to "
                "%r." % HOOK_PATH)
    parser.add_argument(
            '-v', '--verbose',
            action='store_true',
            help="Also list the documented codes no test saw.")
    sys.exit(main(**{k: v
            for k, v in vars(parser.parse_args()).items()
            if v is not None}))
