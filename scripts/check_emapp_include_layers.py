#!/usr/bin/env python3
#
# Copyright (c) 2015-2026 hkrn All rights reserved
#
# This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
#
# Guards the include layering of the emapp component.
#
# emapp is split into logical groups (top-level headers, command/, effect/,
# model/, internal/, plugin/, ...) that live in a single include tree, so the
# layering cannot be enforced by include paths alone. This script encodes the
# layering rules that hold today and fails when a change violates them, so
# dependency regressions surface at test time instead of silently coupling
# the component further.
#
# Usage: scripts/check_emapp_include_layers.py  (exit 0 = clean, 1 = violations)

import os
import posixpath
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMAPP = os.path.join(ROOT, "emapp")

INCLUDE_ROOTS = ("emapp/include/", "emapp/src/")

# Group of a canonical emapp path (relative to repository root).
TOP_LEVEL_HEADERS = re.compile(r"^emapp/[^/]+$")
GROUPS_WITH_SOURCES = (
    "command",
    "effect",
    "internal",
    "internal/imgui",
    "internal/project",
    "model",
    "plugin",
    "protoc",
    "src",
)
# The only multi-group cycle allowed at header level (measured 2026-09-30):
# top-level headers, model/ and effect/ headers include each other.
ALLOWED_HEADER_CYCLE = frozenset(("top", "model", "effect"))

# emapp/include/emapp/private/ is the bootstrap header set; CommonInclude.h may
# pull exactly one core header (Forward.h) and nothing else from emapp.
PRIVATE_ALLOWED = frozenset(("emapp/Forward.h",))
# emapp/include/emapp/sdk/ is the plugin SDK surface; it may include exactly the
# protobuf-c generated headers it exposes to plugin authors.
SDK_ALLOWED_PREFIX = "emapp/src/protoc/"
# Dear ImGui (and its satellite libraries) is contained in the UI layer; these
# are the only translation units outside emapp/src/internal/ allowed to use it.
IMGUI_ALLOWED_OUTSIDE_INTERNAL = frozenset(
    (
        "emapp/src/ModalDialogFactory.cc",
        "emapp/src/Allocator.cc",
    )
)
IMGUI_FAMILY = re.compile(r"(^|/)(imgui\.h|imgui_internal\.h|imguizmo/|imguifiledialog/)")

SOURCE_EXTENSIONS = (".cc", ".mm", ".h")


def canonical_group_file(path):
    """group name owning a file, or None if the file is not part of the scan"""
    rel = path.replace(os.sep, "/")
    if rel.startswith("emapp/include/emapp/"):
        rest = rel[len("emapp/include/emapp/") :]
    elif rel.startswith("emapp/src/"):
        rest = rel[len("emapp/src/") :]
    else:
        return None
    parts = rest.split("/")
    if len(parts) == 1:
        # top-level header vs top-level source
        if rel.startswith("emapp/src/"):
            return "src"
        return "top"
    if parts[0] == "internal":
        if len(parts) >= 3 and parts[1] == "imgui":
            return "internal/imgui"
        if len(parts) >= 3 and parts[1] == "project":
            return "internal/project"
        return "internal"
    if parts[0] == "protoc":
        return "protoc"
    return parts[0]


def canonical_group_of_include(inc):
    """group name of a canonical emapp include path (emapp/...)"""
    if inc.startswith("emapp/src/protoc/"):
        return "protoc"
    if TOP_LEVEL_HEADERS.match(inc):
        return "top"
    parts = inc[len("emapp/") :].split("/") if inc.startswith("emapp/") else []
    if not parts:
        return None
    if parts[0] == "internal":
        if len(parts) >= 3 and parts[1] == "imgui":
            return "internal/imgui"
        if len(parts) >= 3 and parts[1] == "project":
            return "internal/project"
        return "internal"
    return parts[0]


def resolve_include(from_file, raw):
    """Resolve an include to a repository-relative canonical path, or None when
    the include does not point inside emapp/."""
    dir_part = posixpath.dirname(from_file.replace(os.sep, "/"))
    candidates = []
    if raw.startswith("emapp/"):
        candidates.append(posixpath.normpath(raw))
        candidates.append(posixpath.normpath(posixpath.join("emapp/include", raw)))
    else:
        # gcc quoted-include search order: including file's directory first,
        # then the include path roots
        candidates.append(posixpath.normpath(posixpath.join(dir_part, raw)))
        for root in ("emapp/include", "emapp/src"):
            candidates.append(posixpath.normpath(posixpath.join(root, raw)))
    for candidate in candidates:
        if os.path.exists(os.path.join(ROOT, candidate)):
            if candidate.startswith("emapp/include/"):
                candidate = candidate[len("emapp/include/") :]
            if candidate.startswith("emapp/"):
                return candidate
            return None
    # nothing on disk (generated headers): only the canonical emapp/ form is
    # trusted; anything else is a third-party header resolved via include paths
    if raw.startswith("emapp/"):
        candidate = posixpath.normpath(raw)
        if candidate.startswith("emapp/include/"):
            candidate = candidate[len("emapp/include/") :]
        return candidate
    return None


def iter_sources():
    for base, sub in (("emapp/include", None), ("emapp/src", None)):
        for dirpath, _dirnames, filenames in os.walk(os.path.join(ROOT, base)):
            for filename in filenames:
                if filename.endswith(SOURCE_EXTENSIONS):
                    full = os.path.join(dirpath, filename)
                    yield full, os.path.relpath(full, ROOT).replace(os.sep, "/")


def parse_includes(path):
    includes = []
    with open(path, errors="ignore") as fh:
        for number, line in enumerate(fh, start=1):
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                includes.append((m.group(1), True, number))
                continue
            m = re.match(r"\s*#include\s+<([^>]+)>", line)
            if m:
                includes.append((m.group(1), False, number))
    return includes


def tarjan_scc(nodes, edges):
    index = {}
    low = {}
    on_stack = {}
    stack = []
    counter = [0]
    result = []

    def strongconnect(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on_stack[v] = True
        for w in edges.get(v, ()):
            if w not in index:
                strongconnect(w)
                low[v] = min(low[v], low[w])
            elif on_stack.get(w):
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            component = []
            while True:
                w = stack.pop()
                on_stack[w] = False
                component.append(w)
                if w == v:
                    break
            result.append(component)

    for node in nodes:
        if node not in index:
            strongconnect(node)
    return result


def main():
    violations = []
    header_edges = {}
    scanned = 0

    for full, rel in iter_sources():
        scanned += 1
        group = canonical_group_file(rel)
        if group is None:
            continue
        is_header = full.endswith(".h")
        for raw, quoted, number in parse_includes(full):
            # R3: imgui family containment
            if IMGUI_FAMILY.search(raw):
                outside_internal = not rel.startswith(
                    ("emapp/src/internal/", "emapp/include/emapp/internal/")
                )
                if outside_internal and rel not in IMGUI_ALLOWED_OUTSIDE_INTERNAL:
                    violations.append(
                        f'{rel}:{number}: imgui include "{raw}" outside the UI layer '
                        "(add to IMGUI_ALLOWED_OUTSIDE_INTERNAL only if deliberate)"
                    )
            if not quoted and not raw.startswith("emapp/"):
                continue
            target = resolve_include(rel, raw)
            if target is None or target == rel:
                continue
            target_group = canonical_group_of_include(target)

            # R1: private/ headers stay leaf headers
            if rel.startswith("emapp/include/emapp/private/"):
                if not (target in PRIVATE_ALLOWED or target.startswith("emapp/private/")):
                    violations.append(
                        f'{rel}:{number}: private header includes "{target}" '
                        f"(allowed: {', '.join(sorted(PRIVATE_ALLOWED))} or private/ siblings)"
                    )
            # R2: sdk/ headers only expose protobuf-c generated headers
            elif rel.startswith("emapp/include/emapp/sdk/"):
                if not (
                    target.startswith(SDK_ALLOWED_PREFIX)
                    or target.startswith("emapp/sdk/")
                ):
                    violations.append(
                        f'{rel}:{number}: sdk header includes "{target}" '
                        f"(allowed: {SDK_ALLOWED_PREFIX}* or sdk/ siblings)"
                    )
            # R3b: protoc sources may not reach back into emapp
            elif group == "protoc":
                if target_group != "protoc":
                    violations.append(
                        f'{rel}:{number}: generated protoc source includes "{target}"'
                    )
            # R4: core sources must not reach into the imgui dialog layer
            elif rel.startswith("emapp/src/") and "/" not in rel[len("emapp/src/") :]:
                if target_group == "internal/imgui":
                    violations.append(
                        f'{rel}:{number}: core source includes "{target}" (imgui dialog layer)'
                    )

            if is_header and target_group and target_group != group:
                header_edges.setdefault(group, set()).add(target_group)

    # R5: header groups form a DAG except for the known core cycle
    nodes = sorted(
        set(header_edges)
        | {g for targets in header_edges.values() for g in targets}
        | set(GROUPS_WITH_SOURCES)
        | {"top", "private", "sdk"}
    )
    for component in tarjan_scc(nodes, header_edges):
        members = frozenset(component)
        if len(members) > 1 and members != ALLOWED_HEADER_CYCLE:
            violations.append(
                "header layering cycle between groups: " + ", ".join(sorted(members))
            )

    if violations:
        print(f"emapp include layering violations ({len(violations)}):")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print(f"emapp include layering OK ({scanned} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
