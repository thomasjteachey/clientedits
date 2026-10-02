#!/usr/bin/env python3
"""Structural Lua 5.1 check (no interpreter here): tokens, block keywords, brackets.

Catches unbalanced function/if/for/while/do/repeat...end/until, if/elseif without
then, stray else, unbalanced (), [], {}, unterminated strings/comments and
characters Lua 5.1 does not have (!, &, |, ...). Not a full parser.

usage: lua_check.py file.lua [...]      also: file.xml -> well-formedness check
"""
import re
import sys
import xml.dom.minidom

KEYWORDS = {'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'if', 'in',
            'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while'}


def tokenize(src):
    i, line, n = 0, 1, len(src)
    long_open = re.compile(r'\[(=*)\[')
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c in ' \t\r':
            i += 1
            continue
        if src.startswith('--', i):
            m = long_open.match(src, i + 2)
            if m:
                close = ']' + m.group(1) + ']'
                j = src.find(close, m.end())
                if j < 0:
                    raise SyntaxError('line %d: unterminated long comment' % line)
                line += src.count('\n', i, j)
                i = j + len(close)
            else:
                j = src.find('\n', i)
                i = n if j < 0 else j
            continue
        m = long_open.match(src, i)
        if m:
            close = ']' + m.group(1) + ']'
            j = src.find(close, m.end())
            if j < 0:
                raise SyntaxError('line %d: unterminated long string' % line)
            line += src.count('\n', i, j)
            i = j + len(close)
            yield ('string', None, line)
            continue
        if c in '"\'':
            j = i + 1
            while True:
                if j >= n or src[j] == '\n':
                    raise SyntaxError('line %d: unterminated string' % line)
                if src[j] == '\\':
                    j += 2
                    continue
                if src[j] == c:
                    break
                j += 1
            i = j + 1
            yield ('string', None, line)
            continue
        m = re.compile(r'[A-Za-z_][A-Za-z0-9_]*').match(src, i)
        if m:
            word = m.group(0)
            i = m.end()
            yield ('kw' if word in KEYWORDS else 'name', word, line)
            continue
        m = re.compile(r'0[xX][0-9a-fA-F]+|(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?').match(src, i)
        if m and m.group(0):
            i = m.end()
            yield ('number', None, line)
            continue
        for op in ('...', '..', '==', '~=', '<=', '>=', '+', '-', '*', '/', '%', '^', '#', '<', '>', '=',
                   '(', ')', '{', '}', '[', ']', ';', ':', ',', '.'):
            if src.startswith(op, i):
                i += len(op)
                yield ('op', op, line)
                break
        else:
            raise SyntaxError('line %d: character %r is not Lua 5.1' % (line, c))


def check_lua(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    stack, brackets = [], []
    pairs = {')': '(', ']': '[', '}': '{'}
    pending_do = 0
    for kind, value, line in tokenize(src):
        if kind == 'op':
            if value in '([{':
                brackets.append((value, line))
            elif value in ')]}':
                if not brackets or brackets[-1][0] != pairs[value]:
                    raise SyntaxError('line %d: unbalanced %s' % (line, value))
                brackets.pop()
            continue
        if kind != 'kw':
            continue
        if value == 'function':
            stack.append(('function', line))
        elif value == 'if':
            stack.append(('if-needs-then', line))
        elif value == 'elseif':
            if not stack or stack[-1][0] != 'if':
                raise SyntaxError('line %d: elseif outside an if block' % line)
            stack[-1] = ('if-needs-then', line)
        elif value == 'then':
            if not stack or stack[-1][0] != 'if-needs-then':
                raise SyntaxError('line %d: then without if/elseif' % line)
            stack[-1] = ('if', stack[-1][1])
        elif value == 'else':
            if not stack or stack[-1][0] != 'if':
                raise SyntaxError('line %d: else outside an if block' % line)
        elif value in ('for', 'while'):
            stack.append(('loop-needs-do', line))
        elif value == 'do':
            if stack and stack[-1][0] == 'loop-needs-do':
                stack[-1] = ('loop', stack[-1][1])
            else:
                stack.append(('do', line))
        elif value == 'repeat':
            stack.append(('repeat', line))
        elif value == 'end':
            if not stack or stack[-1][0] not in ('function', 'if', 'loop', 'do'):
                raise SyntaxError('line %d: end closes %s' % (line, stack[-1] if stack else 'nothing'))
            stack.pop()
        elif value == 'until':
            if not stack or stack[-1][0] != 'repeat':
                raise SyntaxError('line %d: until without repeat' % line)
            stack.pop()
    if stack:
        raise SyntaxError('unclosed %s opened at line %d' % stack[-1])
    if brackets:
        raise SyntaxError('unclosed %s opened at line %d' % brackets[-1])


ok = True
for path in sys.argv[1:]:
    try:
        if path.lower().endswith('.xml'):
            xml.dom.minidom.parse(path)
        else:
            check_lua(path)
        print('OK    %s' % path)
    except Exception as ex:  # noqa: BLE001
        ok = False
        print('FAIL  %s: %s' % (path, ex))
sys.exit(0 if ok else 1)
