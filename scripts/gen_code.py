#!/usr/bin/env python3
"""
gen_code.py — Unified C++ code generator from .mt definition files.

Usage:  python3 scripts/gen_code.py

Reads:
  src/common/type/*.mt       → define Name = baseType  (type aliases)
  src/common/message/*.mt    → message Name / struct Name  (CAF messages)
  src/common/context/*.mt    → context Name / struct Name  (context classes)

Writes:
  src/generated/Types.hpp                        (type aliases)
  src/generated/message/<Name>.hpp               (one per message/struct in message/)
  src/generated/message/Messages.hpp             (CAF registry)
  src/generated/context/<Name>Context.hpp        (one per context)
  src/generated/context/<Name>.hpp               (one per struct in context/)
  src/generated/TaskType.hpp                    (context enum)

.mt syntax:
  # comment
  include path/to/file.mt    ← #include the matching generated .hpp, resolve defines

  define <Name> = <type>     ← using Name = cpp_type;  (type/ dir only)

  message <Name>             ← CAF message type (message/ dir only)
      <type> <field>

  context <Name>             ← context class with alignas(64) (context/ dir only)
      <type> <field>
      cacheLinePadding       ← split fields across cache lines
      <type>[<size>] <field> ← array field → std::array<T, N>

  struct <Name>              ← plain data struct (message/ or context/ dir)
      <type> <field>

  All types support the same field syntax.
  context types additionally get to_string() and cacheLinePadding.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---- unified type mapping ----------------------------------------------
# mt_type → (cpp_type, include_header or "")

TYPE_MAP = {
    "string": ("std::string", "<string>"),
    "int":    ("int",         ""),
    "int8":   ("int8_t",      "<cstdint>"),
    "int16":  ("int16_t",     "<cstdint>"),
    "int32":  ("int32_t",     "<cstdint>"),
    "int64":  ("int64_t",     "<cstdint>"),
    "uint8":  ("uint8_t",     "<cstdint>"),
    "uint16": ("uint16_t",    "<cstdint>"),
    "uint32": ("uint32_t",    "<cstdint>"),
    "uint64": ("uint64_t",    "<cstdint>"),
    "bool":   ("bool",        ""),
    "double": ("double",      ""),
    "actor":  ("fw::ActorRef", '"fw/ActorTypes.hpp"'),
}

ARRAY_RE = re.compile(r"^(\w+)\[(\d+)\]$")


# ---- include / define helpers ------------------------------------------

def parse_defines(filepath: Path) -> dict[str, tuple[str, str]]:
    """Parse 'define Name = type' lines, return {name: (cpp_type, include)}."""
    defines: dict[str, tuple[str, str]] = {}
    if not filepath.exists():
        return defines
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"^define\s+(\w+)\s*=\s*(\w+)$", line)
            if not m:
                continue
            name = m.group(1)
            mt_type = m.group(2)
            entry = TYPE_MAP.get(mt_type)
            if entry is None:
                print(f"WARNING: {filepath}: unknown type '{mt_type}' for define '{name}'")
                continue
            defines[name] = entry
    return defines


# ---- include resolution ------------------------------------------------

def _discover_def_name(filepath: Path) -> tuple[str | None, str | None]:
    """Quick-scan a .mt file for its definition name and kind.

    Returns (name, kind) or (None, None).
    """
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("include"):
                continue
            m = re.match(r"^(define|message|context|struct)\s+(\w+)", line)
            if m:
                return m.group(2), m.group(1)
    return None, None


def build_mt_hpp_map(src_root: Path) -> dict[str, str]:
    """Build mapping: mt_rel_path (relative to src/) → #include path for generated .hpp.

    Scans type/, message/, context/ directories.
    """
    mapping: dict[str, str] = {}

    for sub in ["common/type", "common/message", "common/context"]:
        d = src_root / sub
        if not d.exists():
            continue
        for mtf in sorted(d.glob("*.mt")):
            name, kind = _discover_def_name(mtf)
            if name is None:
                continue
            mt_rel = str(mtf.relative_to(src_root))

            if kind == "define":
                hpp_rel = "generated/Types.hpp"
            elif kind == "context":
                hpp_rel = f"generated/context/{name}.hpp"
            elif kind == "message":
                hpp_rel = f"generated/message/{name}.hpp"
            else:  # struct — depends on directory
                dir_name = mtf.parent.name  # "message" or "context"
                hpp_rel = f"generated/{dir_name}/{name}.hpp"

            mapping[mt_rel] = hpp_rel
    return mapping


# ---- parser ------------------------------------------------------------

def parse_mt(filepath: Path, src_root: Path, mt_hpp_map: dict[str, str]) -> list[dict]:
    """Parse one .mt file.

    Returns list of definition dicts.  Includes resolved using mt_hpp_map.
    """
    definitions: list[dict] = []
    current: dict | None = None
    includes: list[str] = []

    with open(filepath) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip()

            if not line or line.lstrip().startswith("#"):
                continue

            # non-indented → directive or new definition
            if not line.startswith((" ", "\t")):
                # include directive
                inc_m = re.match(r"^include\s+(\S+)$", line)
                if inc_m:
                    inc_rel = inc_m.group(1)
                    inc_path = src_root / inc_rel
                    # Resolve using the mapping for correct case
                    hpp = mt_hpp_map.get(inc_rel, mt_rel_to_hpp_fallback(inc_rel))
                    includes.append(hpp)
                    # Pull in define statements from the included file
                    for name, entry in parse_defines(inc_path).items():
                        TYPE_MAP[name] = entry
                    continue

                m = re.match(r"^(message|context|struct)\s+(\w+)$", line)
                if not m:
                    print(f"WARNING: {filepath}:{lineno}: "
                          f"expected '<kind> Name' or 'include path', got '{line}'")
                    continue
                if current:
                    definitions.append(current)
                current = {
                    "name": m.group(2),
                    "kind": m.group(1),
                    "fields": [],
                    "file": str(filepath),
                    "includes": list(includes),
                }
                continue

            # indented → field or directive inside current block
            if current is None:
                print(f"WARNING: {filepath}:{lineno}: content outside definition block")
                continue

            stripped = line.strip()

            if stripped == "cacheLinePadding":
                current["fields"].append(("__padding__", None))
                continue

            m = re.match(r"^(\w+)(?:\[(\d+)\])?\s+(\w+)$", stripped)
            if not m:
                print(f"WARNING: {filepath}:{lineno}: "
                      f"expected '<type> <name>' or '<type>[<size>] <name>', got '{stripped}'")
                continue

            base_type = m.group(1)
            array_size = m.group(2)
            field_name = m.group(3)

            if array_size is not None:
                field_type = f"{base_type}[{array_size}]"
            else:
                field_type = base_type

            current["fields"].append((field_type, field_name))

    if current:
        definitions.append(current)

    return definitions


def mt_rel_to_hpp_fallback(mt_rel_path: str) -> str:
    """Fallback: simple .mt→.hpp conversion when mapping lookup fails."""
    hpp = mt_rel_path.replace(".mt", ".hpp")
    return hpp.replace("common/", "generated/", 1)


# ---- codegen: type aliases ---------------------------------------------

def generate_types_hpp(defines: dict[str, tuple[str, str]]) -> str:
    """Generate Types.hpp from define statements."""
    out: list[str] = []
    out.append("// Auto-generated by gen_code.py — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    # Collect includes from TYPE_MAP entries
    incs = sorted({inc for _, inc in defines.values() if inc})
    for inc in incs:
        out.append(f"#include {inc}")
    out.append("")
    out.append("namespace common")
    out.append("{")
    out.append("")
    for name, (cpp, _inc) in defines.items():
        out.append(f"using {name} = {cpp};")
    out.append("")
    out.append("} // namespace common")
    out.append("")
    return "\n".join(out)


# ---- codegen helpers ---------------------------------------------------

def resolve_cpp_type(mt_type: str) -> str:
    m = ARRAY_RE.match(mt_type)
    if m:
        base = m.group(1)
        size = m.group(2)
        inner = TYPE_MAP.get(base, (base, ""))[0]
        return f"std::array<{inner}, {size}>"
    return TYPE_MAP.get(mt_type, (mt_type, ""))[0]


def field_includes(fields: list) -> list[str]:
    """Collect #include lines needed by field types."""
    incs: set[str] = set()
    for ftype, _ in fields:
        if ftype == "__padding__":
            continue
        incs.add("<array>") if ARRAY_RE.match(ftype) else None
        base = ARRAY_RE.match(ftype).group(1) if ARRAY_RE.match(ftype) else ftype
        inc = TYPE_MAP.get(base, ("", ""))[1]
        if inc:
            incs.add(inc)
    return sorted(incs)


def _to_string_scalar(mt_type: str, access_expr: str) -> str:
    if mt_type == "string":
        return access_expr
    if mt_type == "bool":
        return f'({access_expr} ? "true" : "false")'
    return f"std::to_string({access_expr})"


def gen_to_string_body(fields: list) -> list[str]:
    lines = ['        std::string result;']
    real = [(t, n) for t, n in fields if t != "__padding__"]
    if not real:
        lines.append('        return result;')
        return lines
    for idx, (ftype, fname) in enumerate(real):
        comma = "," if idx < len(real) - 1 else ""
        m = ARRAY_RE.match(ftype)
        if m:
            base = m.group(1)
            size = int(m.group(2))
            lines.append(f'        result += "{fname}=[";')
            lines.append(f'        for (size_t i = 0; i < {size}; ++i) {{')
            lines.append(f'            if (i > 0) result += ", ";')
            lines.append(f'            result += {_to_string_scalar(base, fname + "[i]")};')
            lines.append(f'        }}')
            lines.append(f'        result += "]{comma} ";')
        else:
            lines.append(
                f'        result += "{fname}=" + '
                f'{_to_string_scalar(ftype, fname)} + "{comma} ";'
            )
    lines.append('        return result;')
    return lines


# ---- codegen: message --------------------------------------------------

def generate_message_hpp(defn: dict) -> str:
    name = defn["name"]
    fields = defn["fields"]
    field_names = [fn for _, fn in fields]

    out: list[str] = []
    out.append(f"// Auto-generated from {Path(defn['file']).name} — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append('#include "caf/all.hpp"')
    for inc in defn.get("includes", []):
        out.append(f'#include "{inc}"')
    for inc in field_includes(fields):
        out.append(f"#include {inc}")
    out.append("")
    out.append("namespace common::message")
    out.append("{")
    out.append("")
    out.append(f"struct {name}")
    out.append("{")
    for ftype, fname in fields:
        out.append(f"    {resolve_cpp_type(ftype)} {fname};")
    out.append("};")
    out.append("")
    out.append(f"template <class Inspector>")
    out.append(f"bool inspect(Inspector& f, {name}& x) {{")
    if field_names:
        exprs = ", ".join(f'f.field("{fn}", x.{fn})' for fn in field_names)
        out.append(f"    return f.object(x).fields({exprs});")
    else:
        out.append(f"    return f.object(x).fields();")
    out.append("}")
    out.append("")
    out.append("} // namespace common::message")
    out.append("")
    return "\n".join(out)


def generate_messages_hpp(message_names: list[str]) -> str:
    out: list[str] = []
    out.append("// Auto-generated by gen_code.py — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append('#include "caf/type_id.hpp"')
    out.append("")
    for n in message_names:
        out.append(f'#include "generated/message/{n}.hpp"')
    out.append("")
    out.append("CAF_BEGIN_TYPE_ID_BLOCK(flowhub, first_custom_type_id)")
    for n in message_names:
        out.append(f"CAF_ADD_TYPE_ID(flowhub, (common::message::{n}))")
    out.append("CAF_END_TYPE_ID_BLOCK(flowhub)")
    out.append("")
    return "\n".join(out)


# ---- codegen: context / struct -----------------------------------------

def generate_context_hpp(defn: dict) -> str:
    name = defn["name"]
    kind = defn["kind"]  # "context" | "struct"
    fields = defn["fields"]

    out: list[str] = []
    out.append(f"// Auto-generated from {Path(defn['file']).name} — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    # Collect includes, deduplicate
    incs: set[str] = set()
    for inc in defn.get("includes", []):
        incs.add(inc)
    for inc in field_includes(fields):
        incs.add(inc)
    incs.add("<string>")  # for to_string()
    for inc in sorted(incs):
        if inc.startswith("<"):
            out.append(f"#include {inc}")
        else:
            out.append(f'#include "{inc}"')
    out.append("")
    out.append("namespace common::context")
    out.append("{")
    out.append("")

    if kind == "context":
        out.append(f"class alignas(64) {name}")
        out.append("{")
        out.append("public:")
    else:
        out.append(f"struct {name}")
        out.append("{")

    next_needs_align = False
    for ftype, fname in fields:
        if ftype == "__padding__":
            next_needs_align = True
            out.append("")
            out.append("    // --- cache line boundary ---")
            continue
        cpp = resolve_cpp_type(ftype)
        if next_needs_align:
            out.append(f"    alignas(64) {cpp} {fname};")
            next_needs_align = False
        else:
            out.append(f"    {cpp} {fname};")

    out.append("")
    out.append(f"    std::string to_string() const")
    out.append(f"    {{")
    for line in gen_to_string_body(fields):
        out.append(line)
    out.append(f"    }}")

    out.append("};")
    out.append("")
    out.append("} // namespace common::context")
    out.append("")
    return "\n".join(out)


def generate_business_type_hpp(context_names: list[str]) -> str:
    out: list[str] = []
    out.append("// Auto-generated by gen_code.py — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace common")
    out.append("{")
    out.append("")
    out.append("enum class TaskType : uint8_t")
    out.append("{")
    if context_names:
        for i, n in enumerate(context_names):
            short = n.removesuffix("Context") if n.endswith("Context") else n
            comma = "," if i < len(context_names) - 1 else ""
            out.append(f"    {short}{comma}")
    else:
        out.append("    // (no context types defined)")
    out.append("};")
    out.append("")
    out.append("} // namespace common")
    out.append("")
    return "\n".join(out)


# ---- main -------------------------------------------------------------

def main() -> int:
    src_root = ROOT / "src"
    type_dir = src_root / "common" / "type"
    msg_dir = src_root / "common" / "message"
    ctx_dir = src_root / "common" / "context"

    gen_root = src_root / "generated"
    gen_msg_dir = gen_root / "message"
    gen_ctx_dir = gen_root / "context"

    # ---- Pass 1: build mt → hpp mapping (correct filenames) -----------
    mt_hpp_map = build_mt_hpp_map(src_root)

    # ---- Pass 2: parse all files -------------------------------------
    # 2a. Collect define statements from type/
    all_defines: dict[str, tuple[str, str]] = {}
    if type_dir.exists():
        for mtf in sorted(type_dir.glob("*.mt")):
            for name, entry in parse_defines(mtf).items():
                all_defines[name] = entry
            print(f"  type/{mtf.name}: {len(parse_defines(mtf))} define(s)")

    # 2b. Parse message/
    all_messages: list[dict] = []
    if msg_dir.exists():
        for mtf in sorted(msg_dir.glob("*.mt")):
            defs = parse_mt(mtf, src_root, mt_hpp_map)
            all_messages.extend(defs)
            print(f"  message/{mtf.name}: {len(defs)} definition(s)")

    # 2c. Parse context/
    all_contexts: list[dict] = []
    if ctx_dir.exists():
        for mtf in sorted(ctx_dir.glob("*.mt")):
            defs = parse_mt(mtf, src_root, mt_hpp_map)
            all_contexts.extend(defs)
            print(f"  context/{mtf.name}: {len(defs)} definition(s)")

    # ---- Generate ----------------------------------------------------

    # 3. Types.hpp
    if all_defines:
        gen_root.mkdir(parents=True, exist_ok=True)
        path = gen_root / "Types.hpp"
        path.write_text(generate_types_hpp(all_defines))
        print(f"  wrote {path}")

    # 4. Message headers
    if all_messages:
        gen_msg_dir.mkdir(parents=True, exist_ok=True)
        for msg in all_messages:
            path = gen_msg_dir / f"{msg['name']}.hpp"
            path.write_text(generate_message_hpp(msg))
            print(f"  wrote {path}")

        msg_names = [m["name"] for m in all_messages if m["kind"] == "message"]
        path = gen_msg_dir / "Messages.hpp"
        path.write_text(generate_messages_hpp(msg_names))
        print(f"  wrote {path}")

    # 5. Context / struct headers
    if all_contexts:
        gen_ctx_dir.mkdir(parents=True, exist_ok=True)
        for ctx in all_contexts:
            out_name = ctx["name"]
            path = gen_ctx_dir / f"{out_name}.hpp"
            path.write_text(generate_context_hpp(ctx))
            print(f"  wrote {path}")

        context_names = [c["name"] for c in all_contexts if c["kind"] == "context"]
        path = gen_root / "TaskType.hpp"
        path.write_text(generate_business_type_hpp(context_names))
        print(f"  wrote {path}")

    total = len(all_defines) + len(all_messages) + len(all_contexts)
    print(f"Done: {len(all_defines)} define(s), {len(all_messages)} message(s), "
          f"{len(all_contexts)} context(s) generated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
