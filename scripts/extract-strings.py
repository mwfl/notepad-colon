#!/usr/bin/env python3
"""List every translatable interface string in the application sources.

Translatable strings are the arguments of Tr(L"...") and the English titles
passed to mwfl::Command(id, L"..."). With --check, the script also verifies
that the Simplified Chinese catalog (src/strings_zh_cn.inc) covers all of
them, keeps mnemonics and ellipses, and contains no stale entries.

usage: extract-strings.py [--check] [--missing]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LITERAL = r'L"((?:[^"\\]|\\.)*)"'
TR = re.compile(r'\bTr\(\s*((?:' + LITERAL + r'\s*)+)\)')
COMMAND = re.compile(r'mwfl::Command\(\s*[^,]+,\s*' + LITERAL)
PAIR = re.compile(r'^\{' + LITERAL + r',\s*' + LITERAL + r'\},?\s*$')
CODE_PAGE = re.compile(r'LegacyCodePage\{\w+,\s*' + LITERAL + r'\}')


def source_strings():
    found = {}
    for path in sorted((ROOT / 'src' / 'app').glob('*.cpp')):
        if 'self_test' in path.name:
            continue
        text = path.read_text(encoding='utf-8')
        for match in TR.finditer(text):
            value = ''.join(re.findall(LITERAL, match.group(1)))
            found.setdefault(value, path.name)
        for match in COMMAND.finditer(text):
            found.setdefault(match.group(1), path.name)
    detection = (ROOT / 'src' / 'encoding_detection.cpp').read_text(encoding='utf-8')
    for match in CODE_PAGE.finditer(detection):
        found.setdefault(match.group(1), 'encoding_detection.cpp')
    found.setdefault('Plain Text', 'language.cpp')
    return found


def catalog():
    entries = {}
    path = ROOT / 'src' / 'strings_zh_cn.inc'
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = PAIR.match(line)
        if not match:
            raise SystemExit(f'{path.name}:{number}: malformed entry: {line}')
        if match.group(1) in entries:
            raise SystemExit(f'{path.name}:{number}: duplicate entry: {match.group(1)}')
        entries[match.group(1)] = match.group(2)
    return entries


def main():
    strings = source_strings()
    if '--check' not in sys.argv and '--missing' not in sys.argv:
        for value in strings:
            print(value)
        return 0
    translated = catalog()
    missing = [value for value in strings if value not in translated]
    if '--missing' in sys.argv:
        for value in missing:
            print(value)
        return 0
    problems = [f'missing translation: "{value}" ({strings[value]})' for value in missing]
    problems += [f'stale catalog entry: "{value}"' for value in translated if value not in strings]
    for english, chinese in translated.items():
        if not chinese:
            problems.append(f'empty translation: "{english}"')
        if ('&' in english.replace('&&', '')) != ('&' in chinese.replace('&&', '')):
            problems.append(f'mnemonic mismatch: "{english}"')
        if english.endswith('...') != chinese.endswith('...'):
            problems.append(f'ellipsis mismatch: "{english}"')
        if english.count('\\n') != chinese.count('\\n'):
            problems.append(f'line-break mismatch: "{english}"')
        if english.startswith(' ') != chinese.startswith(' ') and not english.strip() == '':
            pass  # leading spaces are a word-separator convention, not required in Chinese
    for problem in problems:
        print(problem)
    print(f'{len(strings)} strings, {len(translated)} catalog entries, {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
