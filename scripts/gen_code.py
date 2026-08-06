#!/usr/bin/env python3
"""
gen_code.py — Unified C++ code generator from .mt definition files.

Usage:  python3 scripts/gen_code.py

Reads:
  src/common/type/*.mt       → define Name = baseType  (type aliases)
  src/common/message/*.mt    → message Name / struct Name  (CAF messages)
  src/common/context/*.mt    → shared struct definitions (no category)
  src/common/context/systemContext/*.mt   → System-category contexts
  src/common/context/sessionContext/*.mt  → Session-category contexts
  src/common/context/busContext/*.mt      → Bus-category contexts

Writes:
  src/generated/Types.hpp                        (type aliases)
  src/generated/message/<Name>.hpp               (one per message/struct in message/)
  src/generated/message/Messages.hpp             (CAF registry)
  src/generated/context/<Name>Context.hpp        (one per context)
  src/generated/context/<Name>.hpp               (one per struct in context/)
  src/generated/TaskType.hpp                    (context enum, category-encoded)

.mt syntax:
  # comment
  include path/to/file.mt    ← same-directory include (relative to current .mt)
  include context/File.mt    ← shared struct under common/context/ (cross-category)

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

  Special built-in types:
    TaskType               ← context 枚举类型, 自动 #include "generated/TaskType.hpp"
                             (值为 common::TaskType, 可直接在 message/context/struct 中使用)
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
    "actor":  ("fw::EoAddress", '"fw/EoTypes.hpp"'),
    "TaskType": ("common::TaskType", '"generated/TaskType.hpp"'),
    "vector": ("std::vector", "<vector>"),
}

# Types that need an extra CAF inspect header (in fw/) for serialization
CAF_INSPECT_INCLUDES: dict[str, str] = {}

# Matches: TypeName, TypeName<Args>, or TypeName[N]
TYPE_NAME_RE = re.compile(r"^(\w+)(?:<([^>]+)>)?(?:\[(\d+)\])?$")

ARRAY_RE = re.compile(r"^(\w+)(?:<[^>]+>)?\[(\d+)\]$")

# Set of enum type names (populated during parsing)
ENUM_TYPES: set[str] = set()

# Set of define names (populated during type/ parsing)
DEFINE_NAMES: set[str] = set()

# Constant values extracted from Constants.hpp (name → integer value)
CONSTANT_VALUES: dict[str, int] = {}


def load_constants(constants_path: str) -> None:
    """Parse Constants.hpp and populate CONSTANT_VALUES."""
    path = ROOT / constants_path
    if not path.exists():
        return
    with open(path) as f:
        for line in f:
            m = re.match(
                r"^constexpr\s+\w+\s+(\w+)\s*=\s*(-?\d+)\s*;", line.strip()
            )
            if m:
                CONSTANT_VALUES[m.group(1)] = int(m.group(2))

# ---- category detection ------------------------------------------------
# Maps context subdirectory name → GTID Category field value (ADR-0008)
CATEGORY_DIRS = {
    "systemContext":  0x0,
    "sessionContext": 0x7,
    "busContext":     0x9,
}


def get_category_for_mt(filepath: Path, src_root: Path) -> int | None:
    """Return GTID Category value if this .mt lives in a category subdirectory
    under common/context/, otherwise None (shared struct / other dir)."""
    try:
        rel = filepath.relative_to(src_root)
    except ValueError:
        return None
    parts = rel.parts
    # common/context/<categoryDir>/<Name>.mt
    if len(parts) >= 4 and parts[0] == "common" and parts[1] == "context":
        cat_dir = parts[2]
        return CATEGORY_DIRS.get(cat_dir)
    return None


# ---- include / define helpers ------------------------------------------

def infer_namespace(include_path: str) -> str:
    parts = include_path.rstrip("/").split("/")
    if len(parts) > 1:
        return parts[0]
    return ""


def parse_type_file(filepath: Path) -> tuple[dict[str, tuple[str, str]], list[dict]]:
    """Parse a type/ .mt file.

    Handles 'define Name = Type' and 'enum Name : Type' blocks.

    Returns:
      defines: {name: (cpp_type, include)}
      enums:   list of enum dicts {name, base_type, values: [(name, val), ...], file}
    """
    defines: dict[str, tuple[str, str]] = {}
    enums: list[dict] = []

    if not filepath.exists():
        return defines, enums

    with open(filepath) as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        raw = lines[i].rstrip()
        stripped = raw.strip()
        i += 1

        if not stripped or stripped.startswith("#"):
            continue

        # define Name = Type
        dm = re.match(r"^define\s+(\w+)\s*=\s*(\w+(?:<[^>]+>)?)$", stripped)
        if dm:
            name = dm.group(1)
            mt_type_raw = dm.group(2)
            entry = resolve_type_entry(mt_type_raw)
            if entry is None:
                print(f"WARNING: {filepath}: unknown type '{mt_type_raw}' for define '{name}'")
                continue
            defines[name] = entry
            continue

        # enum Name : Type
        em = re.match(r"^enum\s+(\w+)\s*:\s*(\w+)$", stripped)
        if em:
            enum_name = em.group(1)
            base_type_raw = em.group(2)
            base_entry = resolve_type_entry(base_type_raw)
            cpp_base = base_entry[0] if base_entry else base_type_raw

            values: list[tuple[str, int]] = []
            next_val = 0
            while i < len(lines):
                vraw = lines[i].rstrip()
                vstripped = vraw.strip()
                i += 1
                if not vstripped or vstripped.startswith("#"):
                    continue
                if not vraw.startswith((" ", "\t")):
                    i -= 1
                    break
                vm = re.match(r"^(\w+)(?:\s*=\s*(-?\d+))?$", vstripped)
                if vm:
                    val_name = vm.group(1)
                    explicit = vm.group(2)
                    if explicit is not None:
                        next_val = int(explicit)
                    values.append((val_name, next_val))
                    next_val += 1

            enums.append({
                "name": enum_name,
                "base_type": cpp_base,
                "values": values,
                "file": str(filepath),
            })
            continue

    return defines, enums


# ---- structs.mt parsing -----------------------------------------------

STRUCT_RE = re.compile(
    r"^struct\s+(\w+)<([^>]+)>\s+from\s+\"([^\"]+)\"$"
)

TPARAM_RE = re.compile(r"^(?:(\w+)\s+)?(\w+)$")


def parse_structs_file(filepath: Path) -> list[dict]:
    """Parse structs.mt and return list of registered types.

    Each entry: {name, tparams (list of (decl, is_type)), header}
    """
    structs: list[dict] = []
    if not filepath.exists():
        return structs
    with open(filepath) as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            m = STRUCT_RE.match(stripped)
            if m:
                name = m.group(1)
                tparams_raw = m.group(2)
                header = m.group(3)
                tparams: list[tuple[str, str, bool]] = []
                for part in tparams_raw.split(","):
                    part = part.strip()
                    pm = TPARAM_RE.match(part)
                    if pm:
                        kind = pm.group(1) or "typename"
                        pname = pm.group(2)
                        is_type = (kind == "typename")
                        tparams.append((kind, pname, is_type))
                structs.append({
                    "name": name,
                    "tparams": tparams,
                    "header": header,
                })
    return structs


def generate_struct_caf_inspect(struct_def: dict, fw_dir: Path) -> None:
    """Generate fw/<Name>Caf.hpp with CAF inspect for a registered struct."""
    name = struct_def["name"]
    tparams = struct_def["tparams"]  # list of (kind, name, is_type)

    tparams_decl = ", ".join(f"{k} {n}" for k, n, _ in tparams)
    tparams_use = ", ".join(n for _, n, _ in tparams)

    caf_file = fw_dir / f"{name}Caf.hpp"
    lines: list[str] = []
    lines.append("// Auto-generated from structs.mt — DO NOT EDIT")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "caf/all.hpp"')
    lines.append("")
    lines.append("namespace utils")
    lines.append("{")
    lines.append(f"template <{tparams_decl}>")
    lines.append(f"class {name};")
    lines.append("}")
    lines.append("")
    lines.append("namespace utils")
    lines.append("{")
    lines.append("")
    lines.append(f"template <{tparams_decl}, class Inspector>")
    lines.append(f"bool inspect(Inspector &f, {name}<{tparams_use}> &x)")
    lines.append("{")
    lines.append("    auto sz = x.size();")
    lines.append("    if (not f.begin_sequence(sz))")
    lines.append("    {")
    lines.append("        return false;")
    lines.append("    }")
    lines.append("    for (size_t i = 0; i < sz; ++i)")
    lines.append("    {")
    lines.append("        if (not f.apply(x[i]))")
    lines.append("        {")
    lines.append("            return false;")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return f.end_sequence();")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace utils")
    lines.append("")

    caf_file.write_text("\n".join(lines))
    print(f"  generated {caf_file}")

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
        # context/ uses rglob to pick up files in category subdirectories
        glob_fn = d.rglob if sub == "common/context" else d.glob
        for mtf in sorted(glob_fn("*.mt")):
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
            else:  # struct — generated output goes under message/ or context/
                # Determine the top-level category: "message" or "context"
                # (structs in context subdirectories still output to generated/context/)
                rel_parts = mtf.relative_to(src_root).parts
                # rel_parts like ("common", "message", "Foo.mt") or ("common", "context", ..., "Bar.mt")
                out_dir = rel_parts[1] if len(rel_parts) >= 2 else mtf.parent.name
                hpp_rel = f"generated/{out_dir}/{name}.hpp"

            mapping[mt_rel] = hpp_rel
    return mapping


# ---- parser ------------------------------------------------------------

def parse_mt(filepath: Path, src_root: Path, mt_hpp_map: dict[str, str]) -> list[dict]:
    """Parse one .mt file.

    Returns list of definition dicts.  Includes resolved using mt_hpp_map.

    include directive adds #include to the generated header and pulls in
    define statements.  Fields from included structs are NOT auto-expanded;
    each message must explicitly declare its header field (e.g. MsgHead head).
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
                # include directive:  "include path/to/file.mt"  or  include "file.mt"
                inc_m = re.match(r'^include\s+(.+)$', line)
                if inc_m:
                    inc_raw = inc_m.group(1).strip()
                    inc_rel = inc_raw.strip('"')

                    if not inc_rel.endswith(".mt"):
                        type_name = Path(inc_rel).stem
                        ns = infer_namespace(inc_rel)
                        cpp_type = f"{ns}::{type_name}" if ns else type_name
                        TYPE_MAP[type_name] = (cpp_type, f'"{inc_rel}"')
                        continue

                    # 路径以 context/ message/ type/ 开头 → 从 src/common/ 解析（共享定义）
                    # 否则 → 从当前 .mt 文件所在目录解析（同目录引用）
                    if inc_rel.startswith(("context/", "message/", "type/")):
                        inc_path = (src_root / "common" / inc_rel).resolve()
                    else:
                        inc_path = (filepath.parent / inc_rel).resolve()
                    try:
                        inc_key = str(inc_path.relative_to(src_root))
                    except ValueError:
                        print(f"WARNING: {filepath}:{lineno}: "
                              f"include '{inc_rel}' not under src_root")
                        continue
                    hpp = mt_hpp_map.get(inc_key, mt_rel_to_hpp_fallback(inc_key))
                    includes.append(hpp)
                    # Pull in define statements from the included file
                    for name, entry in parse_type_file(inc_path)[0].items():
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
                # 仅 context 类型检测所属 Category
                if current["kind"] == "context":
                    current["category"] = get_category_for_mt(filepath, src_root)
                continue

            # indented → field or directive inside current block
            if current is None:
                print(f"WARNING: {filepath}:{lineno}: content outside definition block")
                continue

            stripped = line.strip()

            if stripped == "cacheLinePadding":
                current["fields"].append(("__padding__", None, None))
                continue

            m = re.match(r"^(\w+(?:<[^>]+>)?)(?:\[(\d+)\])?\s+(\w+)(?:\s*=\s*(\S+))?$", stripped)
            if not m:
                print(f"WARNING: {filepath}:{lineno}: "
                      f"expected '<type> <name>[ = <value>]', got '{stripped}'")
                continue

            base_type = m.group(1)
            array_size = m.group(2)
            field_name = m.group(3)
            default_value = m.group(4)

            if array_size is not None:
                field_type = f"{base_type}[{array_size}]"
            else:
                field_type = base_type

            current["fields"].append((field_type, field_name, default_value))

    if current:
        definitions.append(current)

    return definitions


def mt_rel_to_hpp_fallback(mt_rel_path: str) -> str:
    """Fallback: simple .mt→.hpp conversion when mapping lookup fails."""
    hpp = mt_rel_path.replace(".mt", ".hpp")
    return hpp.replace("common/", "generated/", 1)


# ---- codegen: enum ----------------------------------------------------

def generate_enum_hpp(enum_def: dict) -> str:
    """Generate an enum class header with CAF inspect + to_string overload."""
    name = enum_def["name"]
    base = enum_def["base_type"]
    values = enum_def["values"]

    out: list[str] = []
    out.append(f"// Auto-generated from {Path(enum_def['file']).name} — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append('#include "caf/all.hpp"')
    out.append("")
    out.append("namespace common")
    out.append("{")
    out.append("")
    out.append(f"enum class {name} : {base}")
    out.append("{")
    for i, (vname, vval) in enumerate(values):
        comma = "," if i < len(values) - 1 else ""
        out.append(f"    {vname} = {vval}{comma}")
    out.append("};")
    out.append("")

    # CAF serialization
    out.append(f"template <class Inspector>")
    out.append(f"bool inspect(Inspector &f, {name} &x)")
    out.append("{")
    out.append(f"    auto tmp = static_cast<std::underlying_type_t<{name}>>(x);")
    out.append(f"    if (f.value(tmp))")
    out.append(f"    {{")
    out.append(f"        x = static_cast<{name}>(tmp);")
    out.append(f"        return true;")
    out.append(f"    }}")
    out.append(f"    return false;")
    out.append(f"}}")
    out.append("")

    # to_string overload
    out.append(f"inline const char* to_string({name} v)")
    out.append("{")
    out.append(f"    switch (v)")
    out.append(f"    {{")
    for vname, _ in values:
        out.append(f'        case {name}::{vname}: return "{vname}";')
    out.append(f'        default: return "UNKNOWN";')
    out.append(f"    }}")
    out.append(f"}}")
    out.append("")
    out.append("} // namespace common")
    out.append("")
    return "\n".join(out)

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

def resolve_type_entry(mt_type_raw: str) -> tuple[str, str] | None:
    """Resolve a .mt type expression to (cpp_type_str, include_header_or_empty).

    Handles:
      - simple:  uint16  → ("uint16_t", "<cstdint>")
      - template: vector<uint16_t> → ("std::vector<uint16_t>", "<vector>")
      - array:    uint8[16] → handled by resolve_cpp_type (caller)
      - unknown:  returns (mt_type_raw, "")
    """
    m = TYPE_NAME_RE.match(mt_type_raw)
    if not m:
        return None
    base = m.group(1)
    targs = m.group(2)  # e.g. "uint16_t" for vector<uint16_t>, or None

    entry = TYPE_MAP.get(base)
    if entry is None:
        # Enum type — include its generated header
        if mt_type_raw in ENUM_TYPES:
            return (f"common::{mt_type_raw}", f'"generated/type/{mt_type_raw}.hpp"')
        # Unknown type — return as-is (may be a struct/context defined elsewhere)
        return (mt_type_raw, "")

    cpp_base, inc = entry
    if targs is not None:
        arg_parts = [a.strip() for a in targs.split(",")]
        resolved_args = []
        for a in arg_parts:
            if a.isdigit() or (a.startswith("-") and a[1:].isdigit()):
                resolved_args.append(a)
            elif a in DEFINE_NAMES or a in CONSTANT_VALUES:
                resolved_args.append(a)
            else:
                targ_entry = resolve_type_entry(a)
                resolved_args.append(targ_entry[0] if targ_entry else a)
        cpp = f"{cpp_base}<{', '.join(resolved_args)}>"
    else:
        cpp = cpp_base
    return (cpp, inc)


def resolve_cpp_type(mt_type: str) -> str:
    m = ARRAY_RE.match(mt_type)
    if m:
        base_raw = m.group(1)
        size = m.group(2)
        entry = resolve_type_entry(base_raw)
        inner = entry[0] if entry else base_raw
        return f"std::array<{inner}, {size}>"
    entry = resolve_type_entry(mt_type)
    if entry:
        return entry[0]
    return mt_type


def field_includes(fields: list, known_types: dict[str, str] | None = None) -> list[str]:
    """Collect #include lines needed by field types (standard types only)."""
    incs: set[str] = set()
    caf_incs: set[str] = set()
    needs_constants = False
    for ftype_raw, _, _ in fields:
        if ftype_raw == "__padding__":
            continue
        if ARRAY_RE.match(ftype_raw):
            incs.add("<array>")
        entry = resolve_type_entry(ftype_raw)
        if entry:
            inc = entry[1]
            if inc:
                incs.add(inc)
        m = TYPE_NAME_RE.match(ftype_raw)
        if m and m.group(1) in CAF_INSPECT_INCLUDES:
            caf_incs.add(CAF_INSPECT_INCLUDES[m.group(1)])
        if m and m.group(2):
            for a in m.group(2).split(","):
                a = a.strip()
                if a in CONSTANT_VALUES:
                    needs_constants = True
                elif a not in DEFINE_NAMES:
                    targ_entry = resolve_type_entry(a)
                    if targ_entry and targ_entry[1]:
                        incs.add(targ_entry[1])
    result = sorted(incs) + sorted(caf_incs)
    if needs_constants:
        result.append('"common/Constants.hpp"')
    return result


def _to_string_scalar(mt_type: str, access_expr: str,
                     known_types: dict[str, str] | None = None) -> str:
    if mt_type == "string":
        return access_expr
    if mt_type == "bool":
        return f'({access_expr} ? std::string{{"true"}} : std::string{{"false"}})'
    # 自定义类型（.mt 定义的 struct/context），调其 to_string()
    if known_types and mt_type in known_types:
        return f"{access_expr}.to_string()"
    if mt_type in ENUM_TYPES:
        return f"to_string({access_expr})"
    # .mt define 别名（如 EoAddress = actor）→ 查 TYPE_MAP 看 C++ 类型是否需要特殊 to_string
    entry = TYPE_MAP.get(mt_type)
    if entry is not None:
        cpp_type = entry[0]
        if cpp_type == "fw::EoAddress":
            return f"std::to_string({access_expr}.id())"
    return f"std::to_string({access_expr})"


def gen_to_string_body(fields: list,
                       known_types: dict[str, str] | None = None) -> list[str]:
    lines = ['        std::string result;']
    real = [(t, n, _d) for t, n, _d in fields if t != "__padding__"]
    if not real:
        lines.append('        return result;')
        return lines
    for idx, (ftype, fname, _d) in enumerate(real):
        comma = "," if idx < len(real) - 1 else ""
        m = ARRAY_RE.match(ftype)
        if m:
            base = m.group(1)
            size = int(m.group(2))
            lines.append(f'        result += "{fname}=[";')
            lines.append(f'        for (size_t i = 0; i < {size}; ++i) {{')
            lines.append(f'            if (i > 0) result += ", ";')
            lines.append(
                f'            result += '
                f'{_to_string_scalar(base, fname + "[i]", known_types)};'
            )
            lines.append(f'        }}')
            lines.append(f'        result += "]{comma} ";')
        else:
            lines.append(
                f'        result += "{fname}=" + '
                f'{_to_string_scalar(ftype, fname, known_types)} + "{comma} ";'
            )
    lines.append('        return result;')
    return lines


# ---- codegen: message --------------------------------------------------

def generate_message_hpp(defn: dict,
                         known_types: dict[str, str] | None = None) -> str:
    name = defn["name"]
    fields = defn["fields"]
    field_names = [fn for _, fn, _ in fields]

    out: list[str] = []
    out.append(f"// Auto-generated from {Path(defn['file']).name} — DO NOT EDIT")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append('#include "caf/all.hpp"')
    for inc in defn.get("includes", []):
        out.append(f'#include "{inc}"')
    for inc in field_includes(fields, known_types):
        out.append(f"#include {inc}")
    out.append("")
    out.append("namespace common::message")
    out.append("{")
    out.append("")
    out.append(f"struct {name}")
    out.append("{")
    for ftype, fname, _d in fields:
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

def generate_context_hpp(defn: dict,
                         known_types: dict[str, str] | None = None) -> str:
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
    for inc in field_includes(fields, known_types):
        incs.add(inc)
    incs.add("<string>")  # for to_string()
    for inc in sorted(incs):
        if inc.startswith("<"):
            out.append(f"#include {inc}")
        else:
            stripped = inc.strip('"')
            out.append(f'#include "{stripped}"')
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
    for ftype, fname, _d in fields:
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
    for line in gen_to_string_body(fields, known_types):
        out.append(line)
    out.append(f"    }}")
    out.append("")

    # ---- clear(): reset all fields — nested structs call their own clear()
    out.append(f"    void clear()")
    out.append(f"    {{")
    for ftype, fname, default in fields:
        if ftype == "__padding__":
            continue
        if default is not None:
            out.append(f"        {fname} = {default};")
        # 自定义类型（.mt 定义的 struct/context），调其 clear() 实现级联
        elif known_types and ftype in known_types:
            out.append(f"        {fname}.clear();")
        else:
            out.append(f"        {fname} = {{}};")
    out.append(f"    }}")

    out.append("};")
    out.append("")
    out.append("} // namespace common::context")
    out.append("")
    return "\n".join(out)


def generate_business_type_hpp(contexts: list[dict]) -> str:
    """Generate TaskType.hpp with category-encoded values.

    Each context dict has 'name' (str) and 'category' (int, GTID Category field).
    TaskType value = (Category << 6) | subType, so that:
      GTID = (TaskType << 6) | index
      extractTaskType(gtid) = gtid >> 6
    This layout makes TaskType bits directly mirror GTID upper 10 bits.
    subType is assigned sequentially (0, 1, 2...) within each category (sorted by name).
    """
    from collections import defaultdict

    out: list[str] = []
    out.append("// Auto-generated by gen_code.py — DO NOT EDIT")
    out.append("//")
    out.append("// TaskType enum values encode both Category and subType:")
    out.append("//   value = (Category << 6) | subType")
    out.append("// So GTID = (TaskType << 6) | index, extractTaskType = gtid >> 6.")
    out.append("// Category (4 bits) at [9:6], subType (6 bits) at [5:0].")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("#include <type_traits>")
    out.append("")
    out.append('#include "caf/all.hpp"')
    out.append("")
    out.append("namespace common")
    out.append("{")
    out.append("")

    if not contexts:
        out.append("enum class TaskType : uint16_t")
        out.append("{")
        out.append("    // (no context types defined)")
        out.append("};")
    else:
        # Group by category, sort within each group for deterministic subType assignment
        by_cat: dict[int, list[str]] = defaultdict(list)
        for ctx in contexts:
            cat = ctx.get("category")
            if cat is None:
                print(f"WARNING: context '{ctx['name']}' has no category — skipping")
                continue
            by_cat[cat].append(ctx["name"])

        # Build ordered list of (short_name, enum_value, category, subType)
        task_types: list[tuple[str, int, int, int]] = []
        for cat in sorted(by_cat.keys()):
            names = sorted(by_cat[cat])
            for sub_idx, name in enumerate(names):
                short = name.removesuffix("Context") if name.endswith("Context") else name
                value = (cat << 6) | sub_idx
                task_types.append((short, value, cat, sub_idx))

        out.append("enum class TaskType : uint16_t")
        out.append("{")
        for i, (short, val, cat, sub) in enumerate(task_types):
            comma = "," if i < len(task_types) - 1 else ""
            out.append(f"    {short} = 0x{val:04X}{comma}  // cat=0x{cat:X} sub={sub}")
        out.append("};")

    out.append("")
    out.append("// CAF 序列化支持 — 自动生成，将 enum 映射为其底层整数类型")
    out.append("template <class Inspector>")
    out.append("bool inspect(Inspector &f, TaskType &x)")
    out.append("{")
    out.append("    auto tmp = static_cast<std::underlying_type_t<TaskType>>(x);")
    out.append("    if (f.value(tmp))")
    out.append("    {")
    out.append("        x = static_cast<TaskType>(tmp);")
    out.append("        return true;")
    out.append("    }")
    out.append("    return false;")
    out.append("}")
    out.append("")
    out.append("} // namespace common")
    out.append("")
    return "\n".join(out)


def generate_task_type_traits_hpp(context_names: list[str]) -> str:
    """Generate TaskTypeTraits.hpp — compile-time TaskType → ContextType mapping.

    Produces:
      template <TaskType T> struct TaskTypeTraits;
      template <> struct TaskTypeTraits<TaskType::X> { using ContextType = context::XContext; };
      template <TaskType T> using ContextTypeOf = typename TaskTypeTraits<T>::ContextType;
    """
    out: list[str] = []
    out.append("// Auto-generated by gen_code.py — DO NOT EDIT")
    out.append("//")
    out.append("// Compile-time mapping: TaskType enum → Context class.")
    out.append("// EO 使用 TaskType 作为模板参数，通过 ContextTypeOf<T> 推导 Context 类型。")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append('#include "generated/TaskType.hpp"')
    for n in context_names:
        out.append(f'#include "generated/context/{n}.hpp"')
    out.append("")
    out.append("namespace common")
    out.append("{")
    out.append("")
    out.append("// Primary template — no default, missing specialization = compile error")
    out.append("template <TaskType T>")
    out.append("struct TaskTypeTraits;")
    out.append("")
    for n in context_names:
        short = n.removesuffix("Context") if n.endswith("Context") else n
        out.append(f"template <>")
        out.append(f"struct TaskTypeTraits<TaskType::{short}>")
        out.append(f"{{")
        out.append(f"    using ContextType = context::{n};")
        out.append(f"}};")
        out.append("")
    out.append("// Convenience alias: ContextTypeOf<TaskType::AiChat> → context::AiChatContext")
    out.append("template <TaskType T>")
    out.append("using ContextTypeOf = typename TaskTypeTraits<T>::ContextType;")
    out.append("")
    out.append("} // namespace common")
    out.append("")
    return "\n".join(out)


def generate_task_pool_types_hpp(context_names: list[str]) -> str:
    """Generate TaskPoolTypes.hpp — ContextManager tuple + reverse mapping + dispatch helpers.

    Produces:
      ContextToTaskType<Ctx>           — reverse: context class → TaskType enum
      kTaskTypeCount                   — number of task types
      ContextManagerTuple              — std::tuple<ContextManager<Ctx, 64>...>
      detail::allocateInTuple()        — runtime dispatch helper for allocate
      detail::deallocateInTuple()      — runtime dispatch helper for deallocate
      detail::availableInTuple()       — runtime dispatch helper for available
    """
    out: list[str] = []
    out.append("// Auto-generated by gen_code.py — DO NOT EDIT")
    out.append("//")
    out.append("// TaskPool support types: ContextManager tuple + runtime dispatch helpers.")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append('#include "generated/TaskType.hpp"')
    out.append('#include "generated/TaskTypeTraits.hpp"')
    out.append('#include "common/ContextManager.hpp"')
    out.append("")
    out.append("#include <cstddef>")
    out.append("#include <tuple>")
    out.append("")
    out.append("namespace common")
    out.append("{")
    out.append("")

    # --- Reverse mapping: ContextType → TaskType ---
    out.append("// Reverse mapping: context class → TaskType enum value")
    out.append("template <typename Ctx>")
    out.append("struct ContextToTaskType;")
    out.append("")
    for n in context_names:
        short = n.removesuffix("Context") if n.endswith("Context") else n
        out.append(f"template <>")
        out.append(f"struct ContextToTaskType<context::{n}>")
        out.append(f"{{")
        out.append(f"    static constexpr TaskType kValue = TaskType::{short};")
        out.append(f"}};")
        out.append("")

    # --- kTaskTypeCount ---
    out.append(f"constexpr size_t kTaskTypeCount = {len(context_names)};")
    out.append("")

    # --- ContextManagerTuple ---
    out.append("// Tuple of ContextManagers, one per TaskType (accessed via std::get<ContextManager<Ctx, 64>>)")
    out.append("using ContextManagerTuple = std::tuple<")
    for i, n in enumerate(context_names):
        comma = "," if i < len(context_names) - 1 else ""
        out.append(f"    ContextManager<context::{n}, 64>{comma}")
    out.append(">;")
    out.append("")

    # --- Runtime dispatch helpers ---
    out.append("namespace detail")
    out.append("{")
    out.append("")

    # allocateInTuple
    out.append("template <typename Tuple>")
    out.append("inline int allocateInTuple(TaskType type, Tuple& managers)")
    out.append("{")
    out.append("    switch (type)")
    out.append("    {")
    for i, n in enumerate(context_names):
        short = n.removesuffix("Context") if n.endswith("Context") else n
        out.append(f"        case TaskType::{short}: return std::get<{i}>(managers).allocate();")
    out.append("        default: return -1;")
    out.append("    }")
    out.append("}")
    out.append("")

    # deallocateInTuple
    out.append("template <typename Tuple>")
    out.append("inline void deallocateInTuple(TaskType type, Tuple& managers, int idx)")
    out.append("{")
    out.append("    switch (type)")
    out.append("    {")
    for i, n in enumerate(context_names):
        short = n.removesuffix("Context") if n.endswith("Context") else n
        out.append(f"        case TaskType::{short}: std::get<{i}>(managers).deallocate(idx); break;")
    out.append("        default: break;")
    out.append("    }")
    out.append("}")
    out.append("")

    # availableInTuple
    out.append("template <typename Tuple>")
    out.append("inline size_t availableInTuple(TaskType type, const Tuple& managers)")
    out.append("{")
    out.append("    switch (type)")
    out.append("    {")
    for i, n in enumerate(context_names):
        short = n.removesuffix("Context") if n.endswith("Context") else n
        out.append(f"        case TaskType::{short}: return static_cast<size_t>(std::get<{i}>(managers).countFree());")
    out.append("        default: return 0;")
    out.append("    }")
    out.append("}")
    out.append("")

    out.append("} // namespace detail")
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

    load_constants("src/common/Constants.hpp")

    # ---- Pass 0: parse structs.mt, register types, generate CAF inspect ---
    structs_file = src_root / "common" / "structs.mt"
    struct_defs = parse_structs_file(structs_file)
    fw_dir = src_root / "fw"
    for sdef in struct_defs:
        generate_struct_caf_inspect(sdef, fw_dir)
        name = sdef["name"]
        header = sdef["header"]
        TYPE_MAP[name] = (f"utils::{name}", f'"{header}"')
        CAF_INSPECT_INCLUDES[name] = f'"fw/{name}Caf.hpp"'
        print(f"  registered struct: {name} from {header}")

    # ---- Pass 1: build mt → hpp mapping (correct filenames) -----------
    mt_hpp_map = build_mt_hpp_map(src_root)

    # ---- Pass 2: parse all files -------------------------------------
    # 2a. Collect define + enum statements from type/
    all_defines: dict[str, tuple[str, str]] = {}
    all_enums: list[dict] = []
    if type_dir.exists():
        for mtf in sorted(type_dir.glob("*.mt")):
            defines, enums = parse_type_file(mtf)
            for name, entry in defines.items():
                all_defines[name] = entry
                TYPE_MAP[name] = entry
                DEFINE_NAMES.add(name)
            for e in enums:
                all_enums.append(e)
                ENUM_TYPES.add(e["name"])
            print(f"  type/{mtf.name}: {len(defines)} define(s), {len(enums)} enum(s)")

    # 2b. Parse message/
    all_messages: list[dict] = []
    if msg_dir.exists():
        for mtf in sorted(msg_dir.glob("*.mt")):
            defs = parse_mt(mtf, src_root, mt_hpp_map)
            all_messages.extend(defs)
            print(f"  message/{mtf.name}: {len(defs)} definition(s)")

    # 2c. Parse context/ (including category subdirectories)
    all_contexts: list[dict] = []
    if ctx_dir.exists():
        for mtf in sorted(ctx_dir.rglob("*.mt")):
            defs = parse_mt(mtf, src_root, mt_hpp_map)
            all_contexts.extend(defs)
            rel = mtf.relative_to(ctx_dir)
            print(f"  context/{rel}: {len(defs)} definition(s)")

    # ---- Pass 3: build known-types map for cross-references ----------
    # Maps type-name → #include-path (no quotes, e.g. generated/context/DeviceInfo.hpp)
    known_types: dict[str, str] = {}
    for ctx in all_contexts:
        mt_rel = str(Path(ctx["file"]).relative_to(src_root))
        hpp_rel = mt_hpp_map.get(mt_rel, mt_rel_to_hpp_fallback(mt_rel))
        known_types[ctx["name"]] = hpp_rel
    for msg in all_messages:
        mt_rel = str(Path(msg["file"]).relative_to(src_root))
        hpp_rel = mt_hpp_map.get(mt_rel, mt_rel_to_hpp_fallback(mt_rel))
        known_types[msg["name"]] = hpp_rel

    # ---- Generate ----------------------------------------------------

    # 3. Types.hpp
    if all_defines:
        gen_root.mkdir(parents=True, exist_ok=True)
        path = gen_root / "Types.hpp"
        path.write_text(generate_types_hpp(all_defines))
        print(f"  wrote {path}")

    # 3b. Enum headers (from type/ directory)
    gen_type_dir = gen_root / "type"
    for enum_def in all_enums:
        gen_type_dir.mkdir(parents=True, exist_ok=True)
        path = gen_type_dir / f"{enum_def['name']}.hpp"
        path.write_text(generate_enum_hpp(enum_def))
        print(f"  wrote {path}")

    # 4. Message headers
    if all_messages:
        gen_msg_dir.mkdir(parents=True, exist_ok=True)
        for msg in all_messages:
            path = gen_msg_dir / f"{msg['name']}.hpp"
            path.write_text(generate_message_hpp(msg, known_types))
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
            path.write_text(generate_context_hpp(ctx, known_types))
            print(f"  wrote {path}")

        context_defs = [c for c in all_contexts if c["kind"] == "context"]
        context_names = [c["name"] for c in context_defs]
        path = gen_root / "TaskType.hpp"
        path.write_text(generate_business_type_hpp(context_defs))
        print(f"  wrote {path}")

        # 6. TaskTypeTraits.hpp — compile-time TaskType → ContextType mapping
        path = gen_root / "TaskTypeTraits.hpp"
        path.write_text(generate_task_type_traits_hpp(context_names))
        print(f"  wrote {path}")

        # 7. TaskPoolTypes.hpp — ContextManager tuple + dispatch helpers
        path = gen_root / "TaskPoolTypes.hpp"
        path.write_text(generate_task_pool_types_hpp(context_names))
        print(f"  wrote {path}")

    total = len(all_defines) + len(all_enums) + len(all_messages) + len(all_contexts)
    print(f"Done: {len(all_defines)} define(s), {len(all_enums)} enum(s), "
          f"{len(all_messages)} message(s), {len(all_contexts)} context(s) generated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
