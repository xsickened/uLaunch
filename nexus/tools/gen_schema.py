#!/usr/bin/env python3
"""Erzeugt projects/nexus/source/core/schema.gen.hpp aus nexus/docs/settings-schema.json.
Die JSON-Datei ist die einzige Quelle aller Einstellungen (Prototyp und Switch-App)."""
import json, pathlib, sys

root = pathlib.Path(__file__).resolve().parents[2]
spec = json.loads((root / "nexus/docs/settings-schema.json").read_text(encoding="utf-8"))
T = {"bool": "SType::Bool", "enum": "SType::Enum", "num": "SType::Num", "color": "SType::Color", "image": "SType::Image", "str": "SType::Str"}

def cstr(s):
    return json.dumps(s if s is not None else "", ensure_ascii=False)

def cjson(v):
    if v is None: return "json(nullptr)"
    if isinstance(v, bool): return "json(true)" if v else "json(false)"
    if isinstance(v, (int, float)): return f"json({float(v)!r})"
    return f"json(std::string({cstr(v)}))"

def unit(e):
    p, lab = e["path"], e["label"]
    if e["type"] != "num": return ""
    if p.endswith(("Ms", "speed", "stagger", "debounce")): return "ms"
    if e.get("max", 0) <= 1.0001 and e.get("min", 0) >= 0 and e.get("step", 1) < 1 and not p.endswith("waveFreq"): return "%"
    if p.endswith(("Spread", "tilt", "Angle")): return "deg"
    if p.endswith("Cycle"): return "s"
    if p.endswith("Repeat"): return "x"
    if p.endswith(("height", "gap")): return "unit"
    if p.endswith(("Radius", "radius", "Width", "offsetX", "offsetY", "Amp", "Offset", "blur", "clockSize")): return "px"
    if p.endswith(("scale", "size", "Scale")): return "%"
    if p.endswith("focusZoom"): return "x"
    if p.endswith(("posX", "posY")): return "%raw"
    return ""

lines = ["// AUTOMATISCH ERZEUGT von nexus/tools/gen_schema.py – nicht von Hand bearbeiten.",
         "#pragma once", "#include \"schema.hpp\"", "", "namespace nexus {", "",
         "inline const std::vector<SchemaEntry> &Schema() {",
         "    static const std::vector<SchemaEntry> s = {"]
for e in spec["settings"]:
    opts = "{" + ", ".join(cstr(o) for o in (e.get("options") or [])) + "}"
    ols = "{" + ", ".join(cstr(o) for o in (e.get("optionLabels") or [])) + "}"
    lines.append(f"        {{{cstr(e['group'])}, {cstr(e['path'])}, {cstr(e['label'])}, {T[e['type']]}, {opts}, {ols}, "
                 f"{float(e.get('min') or 0)!r}, {float(e.get('max') or 0)!r}, {float(e.get('step') or 0)!r}, {cjson(e.get('default'))}, "
                 f"{'true' if e.get('hidden') else 'false'}, {cstr(unit(e))}}},")
lines += ["    };", "    return s;", "}", "", "} // namespace nexus", ""]
out = root / "projects/nexus/source/core/schema.gen.hpp"
out.write_text("\n".join(lines), encoding="utf-8")
print(f"{len(spec['settings'])} Einträge -> {out.relative_to(root)}")
