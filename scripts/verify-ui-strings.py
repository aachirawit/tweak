"""Check that nothing is drawn to screen in English without a translation.

verify-translations.py checks the table outwards: every key has a call site.
This checks inwards: every string handed to a text-drawing call goes through
i18n::tr() first. A literal that never reaches the table is invisible to the
other check, because there is nothing in the table to notice is unused - which
is how the Dashboard, the sign-in screen and the About page each stayed in
English under a Thai interface for a while.

Product nouns are the deliberate exception and are listed below. They are what
the rest of the ecosystem calls these things, and translating them would make
the app harder to follow alongside a guide, not easier.

Usage:  python scripts/verify-ui-strings.py [repo-root]
"""
import glob
import io
import os
import re
import sys

# Calls whose arguments end up on screen.
DRAWING_CALLS = (
    'draw_text', 'draw_text_wrapped', 'draw_text_ellipsis', 'draw_text_tracked',
    'draw_text_tabular', 'draw_text_blur', 'row_label', 'empty_state', 'toast',
    'badge', 'pill', 'aside_head', 'heading', 'action', 'stateful_button_update',
)

# Calls that take an ImGui id first: that argument is never read by anyone.
ID_FIRST = {'badge', 'pill', 'action'}

# Names that stay in English on purpose - see the module comment.
PRODUCT_NOUNS = {
    'NVIDIA Profile Inspector',
    'ReShade',
    'FiveM',
    'numbanine',
    'CitizenFX.ini',
    'Windows',
    'NVIDIA',
    'AMD',
    # The name the power plan tweak writes into Windows. Translating the toast
    # would report a plan under a name the Control Panel does not use.
    'numbanine powerplan',
}

LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

# Not prose: format fragments, identifiers, punctuation, anything too short to
# be a sentence or a label.
NOT_PROSE = re.compile(r'^(?:[\s%.,:/|+-]*|[A-Za-z0-9_.-]{1,3}|%[sdfl].*|.{0,2})$')


def is_prose(text):
    if text in PRODUCT_NOUNS or NOT_PROSE.match(text):
        return False
    if not re.search(r'[A-Za-z]{3}', text):
        return False
    return not text.startswith(('##', '0x'))


def arguments_at(src, open_paren):
    """The text between a call's parentheses."""
    i, depth = open_paren, 0
    while i < len(src):
        if src[i] == '(':
            depth += 1
        elif src[i] == ')':
            depth -= 1
            if depth == 0:
                return src[open_paren + 1:i]
        i += 1
    return ''


def scan(path):
    src = io.open(path, encoding='utf-8', errors='replace').read()
    hits = []

    for name in DRAWING_CALLS:
        for m in re.finditer(r'(?<![A-Za-z0-9_])' + re.escape(name) + r'\s*\(', src):
            args = arguments_at(src, m.end() - 1)
            literals = LITERAL.findall(args)

            if name in ID_FIRST and literals:
                literals = literals[1:]

            untranslated = [lit for lit in literals if is_prose(lit)]
            if not untranslated:
                continue

            # A call that translates something may still have missed one, so
            # only the strings that are not wrapped anywhere in it are reported
            # - which is coarse, but errs towards looking rather than away.
            if 'i18n::tr' in args:
                wrapped = LITERAL.findall(''.join(
                    arguments_at(args, t.end() - 1) for t in re.finditer(r'i18n::tr\s*\(', args)))
                untranslated = [lit for lit in untranslated if lit not in wrapped]
                if not untranslated:
                    continue

            line = src.count('\n', 0, m.start()) + 1
            for lit in untranslated:
                hits.append((line, name, lit))

    return sorted(hits)


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(argv[0])))
    os.chdir(root)

    files = sorted(glob.glob('src/**/*.cpp', recursive=True))
    total = 0
    for path in files:
        hits = scan(path)
        if not hits:
            continue
        if total == 0:
            print('FAILED: text drawn to screen without going through i18n::tr().')
            print('Add the string to k_table and wrap the call, or list it as a product noun.')
        print('\n  %s' % path.replace(os.sep, '/'))
        for line, name, lit in hits:
            print('    %5d  %-20s %s' % (line, name, lit[:66]))
        total += len(hits)

    if total:
        print('\n%d untranslated string(s).' % total)
        return 1

    print('UI strings verified: every drawn literal is translated.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
