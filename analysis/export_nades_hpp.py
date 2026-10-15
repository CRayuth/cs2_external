import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "resources" / "nades" / "nade_data.hpp"
TEMPLATE_HEAD = ROOT / "analysis" / "nade_header_template.hpp"
TEMPLATE_FOOT = ROOT / "analysis" / "nade_header_footer.hpp"
DATASET = ROOT / "analysis" / "nades_by_270.decrypted.json"

KIND_NAMES = ("smoke", "flash", "molotov", "he", "decoy")
FLAG_ORDER = (
    "action_crouch",
    "action_run",
    "action_jump",
    "action_walk",
    "action_release_movement_after_throw",
    "action_always_run",
)
FLAG_BITS = {
    "action_none": 0,
    "action_crouch": 1 << 0,
    "action_run": 1 << 1,
    "action_jump": 1 << 2,
    "action_walk": 1 << 3,
    "action_release_movement_after_throw": 1 << 4,
    "action_always_run": 1 << 9,
}

ARRAY_RE = re.compile(r"^\tinline constexpr entry k_([A-Za-z0-9_]+)\[\]$")
KIND_ARRAY_RE = re.compile(r"^\tinline constexpr entry k_([A-Za-z0-9_]+)\[\] \{$")
KMAPS_RE = re.compile(r"^\tinline constexpr map_entries k_maps\[\]$")
KMAPS_INLINE_RE = re.compile(r"^\tinline constexpr map_entries k_maps\[\] \{$")
ENTRY_RE = re.compile(
    r'^\t\t\{ "(?P<name>(?:[^"\\]|\\.)*)", "(?P<action>(?:[^"\\]|\\.)*)", '
    r"(?P<x>-?\d+\.\d+)f, (?P<y>-?\d+\.\d+)f, (?P<z>-?\d+\.\d+)f, "
    r"(?P<pitch>-?\d+\.\d+)f, (?P<yaw>-?\d+\.\d+)f, kind::(?P<kind>\w+), "
    r"(?P<actions>action_[A-Za-z0-9_]+(?: \| action_[A-Za-z0-9_]+)*), "
    r"(?P<run_ticks>\d+)u, (?P<after_jump_ticks>\d+)u, "
    r"(?P<strength>-?\d+\.\d+)f, (true|false) \},$"
)
COUNT_RE = re.compile(r'^\t\t\{ "(?P<map>[^"]+)", k_(?P<sym>[A-Za-z0-9_]+), (?P<count>\d+)u \},$')
TICK_RE = re.compile(r"k_authored_tick_rate\{ (\d+) \}")


def read_lines(path):
    raw = path.read_bytes().decode("utf-8")
    if "\r\n" in raw:
        return raw.split("\r\n"), "\r\n"
    return raw.split("\n"), "\n"


def fmt4(value):
    return "%.4ff" % float(value)


def quote(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def unquote(text):
    return json.loads('"%s"' % text)


def actions_to_bits(expr):
    bits = 0
    for part in expr.split(" | "):
        part = part.strip()
        if part not in FLAG_BITS:
            raise SystemExit("unknown action flag: %s" % part)
        bits |= FLAG_BITS[part]
    return bits


def bits_to_actions(bits):
    if not bits:
        return "action_none"
    return " | ".join(name for name in FLAG_ORDER if bits & FLAG_BITS[name])


def parse_header(path):
    lines, eol = read_lines(path)
    head_end = next(i for i, line in enumerate(lines) if ARRAY_RE.match(line) or KIND_ARRAY_RE.match(line))
    preamble = lines[:head_end]

    meta = {}
    for line in preamble:
        if line.startswith("// source: "):
            meta["source"] = line[len("// source: "):]
        elif line.startswith("// author: "):
            meta["author"] = line[len("// author: "):]
    tick = TICK_RE.search("\n".join(preamble))
    meta["authored_tick_rate"] = int(tick.group(1)) if tick else 64

    arrays = {}
    order = []
    current = None
    for line in lines[head_end:]:
        m = ARRAY_RE.match(line) or KIND_ARRAY_RE.match(line)
        if m:
            current = m.group(1)
            arrays[current] = []
            order.append(current)
            continue
        if line in ("\t{", "\t};", ""):
            continue
        em = ENTRY_RE.match(line)
        if em and current is not None:
            g = em.groupdict()
            arrays[current].append({
                "name": unquote(g["name"]),
                "action": unquote(g["action"]),
                "x": float(g["x"]),
                "y": float(g["y"]),
                "z": float(g["z"]),
                "pitch": float(g["pitch"]),
                "yaw": float(g["yaw"]),
                "kind": g["kind"],
                "actions": actions_to_bits(g["actions"]),
                "run_ticks": int(g["run_ticks"]),
                "after_jump_ticks": int(g["after_jump_ticks"]),
                "throw_strength": float(g["strength"]),
                "manual": em.group(13) == "true",
            })
            continue
        if KMAPS_RE.match(line) or KMAPS_INLINE_RE.match(line):
            break

    counts = {}
    sym_by_map = {}
    started = False
    for line in lines:
        if KMAPS_RE.match(line) or KMAPS_INLINE_RE.match(line):
            started = True
            continue
        if not started:
            continue
        if line == "\t};":
            break
        cm = COUNT_RE.match(line)
        if cm:
            counts[cm.group("map")] = int(cm.group("count"))
            sym_by_map[cm.group("map")] = cm.group("sym")
        elif line == "\t{":
            continue

    kmaps_order = [name for name in counts if name in arrays or name in sym_by_map]
    kmaps_order = [name for name in sym_by_map]

    if not kmaps_order:
        raise SystemExit("no k_maps entries found in %s" % path)

    for name in kmaps_order:
        if name not in arrays:
            raise SystemExit("k_maps references missing array: %s" % name)
        if counts.get(name) != len(arrays[name]):
            raise SystemExit(
                "count mismatch for %s: header says %s, array holds %s"
                % (name, counts.get(name), len(arrays[name]))
            )

    for name in order:
        if name not in kmaps_order:
            raise SystemExit("array k_%s is not referenced by k_maps" % name)

    return meta, preamble, arrays, kmaps_order, eol


def build_dataset(path):
    meta, preamble, arrays, kmaps_order, _eol = parse_header(path)
    maps = []
    for map_name in kmaps_order:
        maps.append({
            "map": map_name,
            "symbol": "k_" + map_name,
            "lineups": arrays[map_name],
        })
    total = sum(len(m["lineups"]) for m in maps)
    return {
        "source": meta["source"],
        "author": meta["author"],
        "authored_tick_rate": meta["authored_tick_rate"],
        "map_count": len(maps),
        "lineup_count": total,
        "maps": maps,
    }


def build_templates(path):
    meta, preamble, arrays, kmaps_order, eol = parse_header(path)
    lines, _ = read_lines(path)

    head = list(preamble)
    for i, line in enumerate(head):
        if line.startswith("// source: "):
            head[i] = "// source: @source@"
        elif line.startswith("// author: "):
            head[i] = "// author: @author@"
        elif line.startswith("// maps: "):
            head[i] = "// maps: @map_count@, lineups: @lineup_count@"
    for i, line in enumerate(head):
        if "k_authored_tick_rate{" in line.replace(" ", "") or "k_authored_tick_rate" in line:
            head[i] = "\tinline constexpr std::uint16_t k_authored_tick_rate{ @authored_tick_rate@ };"
            break

    first_array = next(i for i, line in enumerate(lines) if ARRAY_RE.match(line) or KIND_ARRAY_RE.match(line))
    kmaps_line = next(i for i, line in enumerate(lines) if KMAPS_RE.match(line) or KMAPS_INLINE_RE.match(line))
    close = next(i for i in range(kmaps_line, len(lines)) if COUNT_RE.match(lines[i]))
    end_of_entries = close
    while end_of_entries + 1 < len(lines) and COUNT_RE.match(lines[end_of_entries + 1]):
        end_of_entries += 1
    footer = lines[kmaps_line:kmaps_line + 1] + lines[kmaps_line + 1:close] + lines[end_of_entries + 1:]
    footer = [line for line in footer if not COUNT_RE.match(line)]
    while footer and footer[-1] == "":
        footer.pop()

    TEMPLATE_HEAD.write_bytes((eol.join(head) + eol).encode("utf-8"))
    TEMPLATE_FOOT.write_bytes((eol.join(footer) + eol).encode("utf-8"))
    return len(head), len(footer)


def load_template(path):
    text = path.read_bytes().decode("utf-8")
    eol = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(eol)
    if lines and lines[-1] == "":
        lines.pop()
    return lines, eol


def render(dataset):
    head_lines, eol = load_template(TEMPLATE_HEAD)
    head = eol.join(head_lines)
    head = (head
            .replace("@source@", dataset["source"])
            .replace("@author@", dataset["author"])
            .replace("@map_count@", str(dataset["map_count"]))
            .replace("@lineup_count@", str(dataset["lineup_count"]))
            .replace("@authored_tick_rate@", str(dataset["authored_tick_rate"])))
    head_lines = head.split(eol)

    body = []
    for entry_map in dataset["maps"]:
        body.append("\tinline constexpr entry %s[]" % entry_map["symbol"])
        body.append("\t{")
        for row in entry_map["lineups"]:
            body.append(
                '\t\t{ "%s", "%s", %s, %s, %s, %s, %s, kind::%s, %s, %uu, %uu, %s, %s },'
                % (
                    quote(row["name"]), quote(row["action"]),
                    fmt4(row["x"]), fmt4(row["y"]), fmt4(row["z"]),
                    fmt4(row["pitch"]), fmt4(row["yaw"]),
                    row["kind"], bits_to_actions(row["actions"]),
                    row["run_ticks"], row["after_jump_ticks"],
                    fmt4(row["throw_strength"]),
                    "true" if row["manual"] else "false",
                )
            )
        body.append("\t};")
        body.append("")

    foot_lines, _ = load_template(TEMPLATE_FOOT)
    insert_at = next(i for i, line in enumerate(foot_lines) if line == "\t{") + 1
    map_rows = [
        '\t\t{ "%s", %s, %uu },' % (entry_map["map"], entry_map["symbol"], len(entry_map["lineups"]))
        for entry_map in dataset["maps"]
    ]
    foot_lines = foot_lines[:insert_at] + map_rows + foot_lines[insert_at:]

    out_lines = head_lines + body + foot_lines
    return eol.join(out_lines) + eol


def rel(path):
    try:
        return Path(path).relative_to(ROOT)
    except ValueError:
        return path


def cmd_extract(args):
    dataset = build_dataset(HEADER)
    DATASET.write_text(json.dumps(dataset, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    counts = build_templates(HEADER)
    print("dataset: %s maps, %s lineups -> %s" % (
        dataset["map_count"], dataset["lineup_count"], rel(DATASET)))
    print("templates: head %s lines, foot %s lines" % counts)
    return 0


def cmd_export(args):
    dataset = json.loads((args.dataset or DATASET).read_text(encoding="utf-8"))
    text = render(dataset)
    out = args.out or HEADER
    out.write_bytes(text.encode("utf-8"))
    print("wrote %s (%s maps, %s lineups)" % (rel(out), dataset["map_count"], dataset["lineup_count"]))
    return 0


def cmd_check(args):
    dataset = json.loads((args.dataset or DATASET).read_text(encoding="utf-8"))
    text = render(dataset)
    current = HEADER.read_bytes().decode("utf-8")
    if text == current:
        print("nade_data.hpp is reproducible from %s" % dataset["source"])
        return 0
    cur_lines = current.splitlines()
    new_lines = text.splitlines()
    diffs = 0
    for i in range(max(len(cur_lines), len(new_lines))):
        a = cur_lines[i] if i < len(cur_lines) else "<missing>"
        b = new_lines[i] if i < len(new_lines) else "<missing>"
        if a != b:
            diffs += 1
            if diffs <= 10:
                print("line %d:\n  committed: %s\n  generated: %s" % (i + 1, a, b))
    print("FAILED: %s differing lines" % diffs)
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(prog="export_nades_hpp.py")
    sub = parser.add_subparsers(dest="mode")

    p_extract = sub.add_parser("extract", help="recover dataset and templates from the current header")
    p_export = sub.add_parser("export", help="regenerate nade_data.hpp from the dataset")
    p_check = sub.add_parser("check", help="verify nade_data.hpp matches the dataset without writing")
    for p in (p_export, p_check):
        p.add_argument("--dataset", type=Path, default=None)
        p.add_argument("--out", type=Path, default=None)

    args = parser.parse_args(argv)
    if args.mode == "extract":
        return cmd_extract(args)
    if args.mode == "export":
        return cmd_export(args)
    if args.mode == "check":
        return cmd_check(args)
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
