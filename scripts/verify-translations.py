"""Check that every key in the translation table can actually be reached.

A key nothing looks up is a string the user still reads in English while the
rest of the interface switches to Thai, and it is invisible in review: the
table looks complete, because the translation is right there. The Dashboard
carried five of these for a while.

Keys fall into three buckets:

  direct    the literal is written inside an i18n::tr(...) call
  indirect  the literal lives in a table that reaches tr() through a variable
            - nav labels, tab strips, the FiveM feature cards. Listed with
            where the literal is, so a human can confirm it
  orphaned  the literal appears nowhere else in src at all, so nothing can be
            looking it up: a typo, or a call site that was removed

Only orphaned keys fail the run. Whether an indirect key really reaches tr()
cannot be decided without following the variable, so those are reported and
left to the reader rather than guessed at.

Usage:  python scripts/verify-translations.py [repo-root]
"""
import glob
import io
import os
import sys

QUOTE = '"'
BACKSLASH = chr(92)

TABLE_PATH = 'src/core/i18n.cpp'
TABLE_BEGIN = 'constexpr entry k_table[]'
TABLE_END = 'const std::unordered_map'
LOOKUP = 'i18n::tr('


def literals_from(text, at):
    """Read a run of adjacent C++ string literals starting at `at`.

    Adjacent literals concatenate in C++, so a wrapped string is one key. Scans
    by hand rather than by regex so an escaped quote cannot end a literal early.
    Returns (joined contents, index past the run), or (None, at) if there is no
    literal there.
    """
    parts = []
    i = at
    while True:
        while i < len(text) and text[i] in ' \t\r\n':
            i += 1
        if i >= len(text) or text[i] != QUOTE:
            break
        i += 1
        piece = []
        while i < len(text) and text[i] != QUOTE:
            if text[i] == BACKSLASH and i + 1 < len(text):
                piece.append(text[i:i + 2])
                i += 2
                continue
            piece.append(text[i])
            i += 1
        i += 1  # past the closing quote
        parts.append(''.join(piece))
    if not parts:
        return None, at
    return ''.join(parts), i


def table_keys(path):
    """The English side of every entry in k_table."""
    text = io.open(path, encoding='utf-8').read()
    body = text[text.index(TABLE_BEGIN):text.index(TABLE_END)]

    keys, i = [], 0
    while i < len(body):
        if body[i] != '{':
            i += 1
            continue
        key, after = literals_from(body, i + 1)
        if key is None:
            i += 1
            continue
        keys.append(key)
        i = after
    return keys


def direct_lookups(paths):
    """Every string written literally inside an i18n::tr(...) call."""
    found = set()
    for path in paths:
        text = io.open(path, encoding='utf-8', errors='replace').read()
        at = 0
        while True:
            at = text.find(LOOKUP, at)
            if at < 0:
                break
            literal, _ = literals_from(text, at + len(LOOKUP))
            if literal is not None:
                found.add(literal)
            at += len(LOOKUP)
    return found


def literal_runs(paths):
    """Every concatenated string literal in the sources, mapped to file:line.

    Whole runs, not single literals: a long key is wrapped across several lines
    at the call site and joins back into one string, and the wrap points do not
    have to match the ones in the table. Comparing line by line would report
    every wrapped key as missing.
    """
    seen = {}
    for path in paths:
        if path.replace(os.sep, '/').endswith(TABLE_PATH):
            continue
        text = io.open(path, encoding='utf-8', errors='replace').read()
        i = 0
        while i < len(text):
            if text[i] != QUOTE:
                i += 1
                continue
            joined, after = literals_from(text, i)
            if joined is None:
                i += 1
                continue
            seen.setdefault(joined, '%s:%d'
                            % (path.replace(os.sep, '/'), text.count('\n', 0, i) + 1))
            i = max(after, i + 1)
    return seen


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(argv[0])))
    os.chdir(root)

    if not os.path.exists(TABLE_PATH):
        print('verify-translations: %s not found under %s' % (TABLE_PATH, root))
        return 2

    keys = table_keys(TABLE_PATH)
    sources = sorted(glob.glob('src/**/*.cpp', recursive=True) +
                     glob.glob('src/**/*.h', recursive=True))
    direct = direct_lookups(sources)
    elsewhere = literal_runs(sources)

    indirect, orphaned = [], []
    for key in keys:
        if key in direct:
            continue
        where = elsewhere.get(key)
        (indirect if where else orphaned).append((key, where))

    print('Translations verified: %d keys, %d looked up directly, %d through a variable.'
          % (len(keys), len(keys) - len(indirect) - len(orphaned), len(indirect)))

    if indirect:
        print('\nReached through a variable - confirm each still has a call site:')
        for key, where in indirect:
            print('  %-48s %s' % (key[:48], where))

    if orphaned:
        print('\nFAILED: %d key(s) appear nowhere in src, so nothing can look them up.'
              % len(orphaned))
        print('Either the call site was removed, or the key does not match it exactly:')
        for key, _ in orphaned:
            print('  -', key[:88])
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
