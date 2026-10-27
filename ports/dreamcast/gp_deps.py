"""Compile-order dependencies of jak1 GOAL files from goal_src/jak1/game.gp (AI-assisted).

The desktop build compiles each file after its game.gp dependency closure, which
can include files the DGO loads later (compile order is not load order). Only
(goal-src "path" deps...) and (goal-src-sequence "prefix" :deps (...) files...)
are read; deps name object stems ("gkernel-h") or "$OUT/obj/<stem>.o" paths.
"""
import re
from pathlib import Path


def _tokens(text):
    text = re.sub(r';[^\n]*', '', text)
    return re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)


def _parse(tokens):
    stack = [[]]
    for t in tokens:
        if t == '(':
            stack.append([])
        elif t == ')':
            if len(stack) > 1:
                done = stack.pop()
                stack[-1].append(done)
        else:
            stack[-1].append(t[1:-1] if t.startswith('"') else t)
    return stack[0]


def _stem(dep):
    return Path(dep).stem if dep.endswith(('.o', '.gc')) else dep


def load(root):
    """{stem: (path relative to goal_src/jak1, [dependency stems])}."""
    forms = _parse(_tokens((Path(root) / 'goal_src/jak1/game.gp').read_text()))
    graph = {}

    def walk(form):
        if not isinstance(form, list) or not form:
            return
        head = form[0]
        if head == 'goal-src' and len(form) >= 2 and isinstance(form[1], str) and form[1].endswith('.gc'):
            graph[_stem(form[1])] = (form[1], [_stem(d) for d in form[2:] if isinstance(d, str)])
        elif head == 'goal-src-sequence' and len(form) >= 2 and isinstance(form[1], str):
            prefix, rest, deps = form[1], form[2:], []
            if len(rest) >= 2 and rest[0] == ':deps' and isinstance(rest[1], list):
                deps = [_stem(d) for d in rest[1] if isinstance(d, str)]
                rest = rest[2:]
            previous = None
            for item in rest:
                if not isinstance(item, str) or not item.endswith('.gc'):
                    continue
                graph[_stem(item)] = (prefix + item, list(deps) if previous is None else [previous])
                previous = _stem(item)
        else:
            for child in form:
                walk(child)
    walk(forms)
    return graph


def closure(graph, stem):
    """Dependencies of stem (not stem itself), dependencies first."""
    order, seen = [], set()

    def visit(name, stack):
        if name in seen or name not in graph:
            return
        if name in stack:
            raise ValueError(f'dependency cycle through {name}')
        stack.add(name)
        for dep in graph[name][1]:
            visit(dep, stack)
        stack.discard(name)
        seen.add(name)
        order.append(name)
    for dep in graph[stem][1]:
        visit(dep, set())
    return order
