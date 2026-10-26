"""Inventory target-configuration dependencies in original GOAL source (AI-assisted).

Reads unmodified GOAL source text only. It does not compile, link, execute or
change any file except --output. The report lists reader-time configuration
branches (PC_PORT, BIG_MEMORY, profiler, VM, INSTRUCTION_SET and other global
constants), the macros/constants/globals whose meaning they change, every form
that references those names, PS2 hardware/scratchpad address literals and
inline-assembly functions. It is an audit aid for Dreamcast target setup, not a
proof that unlisted forms are portable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCES = sorted((ROOT / "goal_src/jak1/kernel").glob("*.gc"))
UPSTREAM_COMMIT = "6076aab5a95955443a9e1ed8f72a25bc0e2fcf9d"

# Reader-time directives evaluated by GOOS before compilation.
DIRECTIVES = ("#cond", "#when", "#unless", "#if")
# INSTRUCTION_SET is supplied by the compiler rather than defined in source.
BUILTIN_ROOTS = ("INSTRUCTION_SET",)
# Definers whose second element is the defined name.
PROPAGATING = {"defmacro": "macro", "defconstant": "constant",
               "defglobalconstant": "constant", "define": "global",
               "define-perm": "global"}
TERMINAL = {"defun": "function", "defun-debug": "function",
            "defbehavior": "function", "defmethod": "method",
            "deftype": "type", "defstate": "state", "define-extern": "extern"}
# Inclusive PS2 EE address ranges; the scratchpad end is a valid top pointer.
HARDWARE_REGIONS = (
    ("ee-registers", 0x10000000, 0x1000FFFF),
    ("vu-memory", 0x11000000, 0x1100FFFF),
    ("gs-privileged", 0x12000000, 0x12001FFF),
    ("scratchpad", 0x70000000, 0x70004000),
)
DELIMITERS = set("()'`,\";") | set(" \t\r\n\f\v")
PREFIXES = {"'": "quote", "`": "quasiquote", ",": "unquote", ",@": "unquote-splicing"}


class SourceError(ValueError):
    pass


class Atom:
    __slots__ = ("text", "line", "kind")

    def __init__(self, text, line, kind):
        self.text, self.line, self.kind = text, line, kind


class Node:
    __slots__ = ("items", "line")

    def __init__(self, items, line):
        self.items, self.line = items, line

    def head(self):
        if self.items and isinstance(self.items[0], Atom) and self.items[0].kind == "symbol":
            return self.items[0].text
        return None


def read_forms(text, name="<source>"):
    """Parse GOAL/GOOS text into nodes, rejecting malformed input."""
    pos, line, length = 0, 1, len(text)

    def fail(message, where):
        raise SourceError(f"{name}:{where}: {message}")

    def skip_space():
        nonlocal pos, line
        while pos < length:
            ch = text[pos]
            if ch == "\n":
                line += 1
                pos += 1
            elif ch in " \t\r\f\v":
                pos += 1
            elif ch == ";":
                while pos < length and text[pos] != "\n":
                    pos += 1
            elif text.startswith("#|", pos):
                start, depth = line, 0
                while True:
                    if pos >= length:
                        fail("unterminated block comment", start)
                    if text.startswith("#|", pos):
                        depth += 1
                        pos += 2
                    elif text.startswith("|#", pos):
                        depth -= 1
                        pos += 2
                        if depth == 0:
                            break
                    else:
                        if text[pos] == "\n":
                            line += 1
                        pos += 1
            else:
                return

    def read():
        nonlocal pos, line
        skip_space()
        if pos >= length:
            fail("unexpected end of input", line)
        ch, start = text[pos], line
        if ch == "(" or text.startswith("#(", pos):
            pos += 1 if ch == "(" else 2
            items = []
            while True:
                skip_space()
                if pos >= length:
                    fail("unclosed list", start)
                if text[pos] == ")":
                    pos += 1
                    return Node(items, start)
                items.append(read())
        if ch == ")":
            fail("unexpected )", start)
        if ch in "'`,":
            prefix = ",@" if text.startswith(",@", pos) else ch
            pos += len(prefix)
            return Node([Atom(PREFIXES[prefix], start, "symbol"), read()], start)
        if ch == '"':
            pos += 1
            chars = []
            while True:
                if pos >= length:
                    fail("unterminated string", start)
                c = text[pos]
                if c == "\\" and pos + 1 < length:
                    chars.append(text[pos:pos + 2])
                    if text[pos + 1] == "\n":
                        line += 1
                    pos += 2
                    continue
                pos += 1
                if c == '"':
                    return Atom("".join(chars), start, "string")
                if c == "\n":
                    line += 1
                chars.append(c)
        begin = pos
        if text.startswith("#\\", pos):
            if pos + 2 >= length:
                fail("unterminated character", start)
            pos += 3
            while pos < length and text[pos] not in DELIMITERS:
                pos += 1
            return Atom(text[begin:pos], start, "char")
        while pos < length and text[pos] not in DELIMITERS:
            pos += 1
        token = text[begin:pos]
        return Atom(token, start, "number" if parse_integer(token) is not None else "symbol")

    forms = []
    while True:
        skip_space()
        if pos >= length:
            return forms
        forms.append(read())


def parse_integer(token):
    body = token[1:] if token[:1] in "+-" else token
    sign = -1 if token.startswith("-") else 1
    for prefix, base in (("#x", 16), ("#b", 2)):
        if body.lower().startswith(prefix):
            digits = body[2:].replace("_", "")
            try:
                return sign * int(digits, base) if digits else None
            except ValueError:
                return None
    if body.isdigit():
        return sign * int(body)
    return None


def hardware_region(value):
    for region, low, high in HARDWARE_REGIONS:
        if low <= value <= high:
            return region
    return None


def symbols(node):
    """Yield every symbol atom below node, including quoted templates."""
    if isinstance(node, Atom):
        if node.kind == "symbol":
            yield node
        return
    for item in node.items:
        yield from symbols(item)


def definition_name(node):
    head = node.head()
    if head in PROPAGATING or head in TERMINAL:
        if len(node.items) > 1 and isinstance(node.items[1], Atom):
            return head, node.items[1].text
    return None


def form_label(node):
    named = definition_name(node)
    if named and named[0] == "defmethod" and len(node.items) > 2:
        # Older form: (defmethod name type (args)); newer: (defmethod name ((this type)) ...).
        owner = node.items[2]
        if isinstance(owner, Node) and owner.items and isinstance(owner.items[0], Node) \
                and len(owner.items[0].items) == 2:
            owner = owner.items[0].items[1]
        if isinstance(owner, Atom):
            return f"defmethod {named[1]} {owner.text}"
    if named:
        return f"{named[0]} {named[1]}"
    return node.head() or "<list>"


def conditions(node):
    """Return the condition expressions of a reader directive."""
    head = node.head()
    if head == "#cond":
        return [clause.items[0] for clause in node.items[1:]
                if isinstance(clause, Node) and clause.items]
    return node.items[1:2]


class Audit:
    def __init__(self, sources, extra_roots=()):
        self.files = []
        for path in sources:
            data = Path(path).read_bytes()
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError as error:
                raise SourceError(f"{path}: not UTF-8: {error}") from None
            self.files.append((Path(path), data, read_forms(text, str(path))))
        self.extra_roots = tuple(extra_roots)

    def label(self, path):
        try:
            return Path(path).resolve().relative_to(ROOT).as_posix()
        except ValueError:
            return Path(path).as_posix()

    def walk(self, node, top, cond_stack, quasi, visit):
        """Depth-first walk tracking enclosing top form, directives and templates."""
        if isinstance(node, Atom):
            return
        visit(node, top, cond_stack, quasi)
        head = node.head()
        depth = quasi + (head == "quasiquote") - (head in ("unquote", "unquote-splicing"))
        stack = cond_stack + [node] if head in DIRECTIVES else cond_stack
        for item in node.items:
            self.walk(item, top, stack, max(depth, 0), visit)

    def run(self):
        roots = []
        for path, _, forms in self.files:
            for form in forms:
                def collect(node, top, conds, quasi):
                    if node.head() == "defglobalconstant" and not quasi and len(node.items) >= 3 \
                            and isinstance(node.items[1], Atom):
                        roots.append(dict(name=node.items[1].text, file=self.label(path),
                                          line=node.line, value=render(node.items[2])))
                self.walk(form, form, [], 0, collect)
        root_names = set(BUILTIN_ROOTS) | set(self.extra_roots) | {r["name"] for r in roots}

        # Fixed point: propagating definitions become dependent when they sit in
        # a configuration branch, reference a dependent name or embed hardware.
        definitions = []
        for path, _, forms in self.files:
            for form in forms:
                def record(node, top, conds, quasi):
                    named = definition_name(node)
                    if named and not quasi:
                        definitions.append((path, node, named[0], named[1], list(conds)))
                self.walk(form, form, [], 0, record)
        dependent = {name: dict(reason="root") for name in root_names}
        changed = True
        while changed:
            changed = False
            for path, node, head, name, conds in definitions:
                if head not in PROPAGATING or name in dependent and dependent[name]["reason"] == "root":
                    continue
                reason = via = None
                branch = [c for c in conds if self.config_names(c, dependent)]
                if branch:
                    reason, via = "inside-config-branch", sorted(self.config_names(branch[-1], dependent))
                else:
                    refs = sorted({s.text for s in symbols(Node(node.items[2:], node.line))
                                   if s.text in dependent and s.text != name})
                    hardware = [a for a in numbers(node) if hardware_region(parse_integer(a.text))]
                    if refs:
                        reason, via = "references", refs
                    elif hardware:
                        reason, via = "hardware-address", sorted({a.text for a in hardware})
                if reason and name not in dependent:
                    dependent[name] = dict(reason=reason, via=via)
                    changed = True

        conditionals, use_sites, hardware, asm = [], [], [], []
        dependent_definitions = []
        seen_defs = set()
        for path, _, forms in self.files:
            label = self.label(path)
            for form in forms:
                # A definition's own name atom is not a reference to itself.
                defining = set()

                def mark(node, top, conds, quasi):
                    if definition_name(node) and not quasi:
                        defining.add(id(node.items[1]))
                self.walk(form, form, [], 0, mark)
                refs = {}
                for s in symbols(form):
                    if s.text in dependent and id(s) not in defining:
                        refs.setdefault(s.text, set()).add(s.line)
                if refs:
                    use_sites.append(dict(file=label, line=form.line, form=form_label(form),
                                          names={k: sorted(v) for k, v in sorted(refs.items())}))

                def inspect(node, top, conds, quasi):
                    head = node.head()
                    if head in DIRECTIVES:
                        names = self.config_names(node, dependent)
                        conditionals.append(dict(
                            file=label, line=node.line, directive=head,
                            conditions=[render(c) for c in conditions(node)],
                            config_names=sorted(names), config=bool(names),
                            form=form_label(top), in_template=bool(quasi)))
                    named = definition_name(node)
                    if named and not quasi and named[1] in dependent \
                            and dependent[named[1]]["reason"] != "root" and named[0] in PROPAGATING:
                        key = (label, node.line, named[1])
                        if key not in seen_defs:
                            seen_defs.add(key)
                            entry = dict(name=named[1], kind=PROPAGATING[named[0]], file=label,
                                         line=node.line)
                            entry.update(dependent[named[1]])
                            dependent_definitions.append(entry)
                    if named and named[0] in ("defun", "defmethod", "defbehavior", "defun-debug") \
                            and is_asm_func(node):
                        isa = any(d.head() in DIRECTIVES and "INSTRUCTION_SET" in
                                  {s.text for c in conditions(d) for s in symbols(c)}
                                  for d in subnodes(node))
                        asm.append(dict(file=label, line=node.line, form=form_label(node),
                                        instruction_set_conditional=isa))
                    for item in node.items:
                        if isinstance(item, Atom) and item.kind == "number":
                            region = hardware_region(parse_integer(item.text))
                            if region:
                                hardware.append(dict(file=label, line=item.line, value=item.text,
                                                     region=region, form=form_label(top),
                                                     in_template=bool(quasi)))
                self.walk(form, form, [], 0, inspect)
                if isinstance(form, Atom) and form.kind == "number" \
                        and hardware_region(parse_integer(form.text)):
                    hardware.append(dict(file=label, line=form.line, value=form.text,
                                         region=hardware_region(parse_integer(form.text)),
                                         form="<atom>", in_template=False))

        config_conds = [c for c in conditionals if c["config"]]
        return dict(
            ai_assisted=True,
            scope="Unmodified GOAL source text only; no compilation, linking or execution",
            warning="An unlisted form is not proven portable; hardware literals are candidates",
            upstream_commit=UPSTREAM_COMMIT,
            # LF-normalized so Windows autocrlf and Linux checkouts pin the same text.
            sources=[dict(path=self.label(p),
                          sha256_lf=hashlib.sha256(d.replace(b"\r\n", b"\n")).hexdigest(),
                          top_level_forms=len(f)) for p, d, f in self.files],
            roots=roots, builtin_roots=list(BUILTIN_ROOTS) + list(self.extra_roots),
            conditionals=conditionals,
            dependent_definitions=dependent_definitions,
            use_sites=use_sites,
            hardware_addresses=hardware,
            asm_functions=asm,
            summary=dict(
                roots=len(roots), conditionals=len(conditionals),
                config_conditionals=len(config_conds),
                config_conditionals_in_templates=sum(c["in_template"] for c in config_conds),
                config_names_used={n: sum(n in c["config_names"] for c in config_conds)
                                   for n in sorted({n for c in config_conds for n in c["config_names"]})},
                dependent_definitions=len(dependent_definitions),
                use_sites=len(use_sites),
                hardware_addresses=len(hardware),
                asm_functions=len(asm),
                asm_functions_without_instruction_set_branch=sum(
                    not a["instruction_set_conditional"] for a in asm)))

    @staticmethod
    def config_names(directive, dependent):
        return {s.text for c in conditions(directive) for s in symbols(c) if s.text in dependent}


def subnodes(node):
    for item in node.items:
        if isinstance(item, Node):
            yield item
            yield from subnodes(item)


def numbers(node):
    if isinstance(node, Atom):
        if node.kind == "number":
            yield node
        return
    for item in node.items:
        yield from numbers(item)


def is_asm_func(node):
    for item in node.items:
        if isinstance(item, Node) and item.head() == "declare":
            for decl in item.items[1:]:
                if isinstance(decl, Node) and decl.head() == "asm-func":
                    return True
    return False


def render(node, limit=160):
    def text(n):
        if isinstance(n, Atom):
            return f'"{n.text}"' if n.kind == "string" else n.text
        head = n.head()
        for mark, name in (("'", "quote"), ("`", "quasiquote"), (",@", "unquote-splicing"),
                           (",", "unquote")):
            if head == name and len(n.items) == 2:
                return mark + text(n.items[1])
        return "(" + " ".join(text(i) for i in n.items) + ")"
    out = text(node)
    return out if len(out) <= limit else out[:limit - 3] + "..."


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, action="append",
                        help="GOAL source to scan (repeatable); default: goal_src/jak1/kernel/*.gc")
    parser.add_argument("--root", action="append", default=[],
                        help="extra configuration symbol to treat as a root")
    parser.add_argument("--output", type=Path, help="write the JSON report here")
    parser.add_argument("--check", type=Path,
                        help="compare against a committed report; exit 1 when it differs")
    args = parser.parse_args(argv)
    sources = args.source or DEFAULT_SOURCES
    for path in sources:
        if not path.is_file():
            parser.error(f"source does not exist: {path}")
    if not sources:
        parser.error("no GOAL sources to scan")
    try:
        report = Audit(sources, args.root).run()
    except SourceError as error:
        parser.error(str(error))
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8", newline="\n")
    if args.check:
        expected = json.loads(args.check.read_text(encoding="utf-8"))
        if expected != report:
            print(f"target configuration audit differs from {args.check}; "
                  "review the source change and regenerate with --output", file=sys.stderr)
            return 1
    print(json.dumps(dict(result="PASS" if args.check else "REPORT", **report["summary"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
