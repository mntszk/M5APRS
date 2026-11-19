#!/usr/bin/env python3
# aprs-deviceid の tocalls.yaml から、宛先 (TOCALL) で機種を見分ける表 tocalls_table.h を作る。
#   python3 tools/gen_tocalls.py <aprs-deviceid の tocalls.yaml> > tocalls_table.h
# 表の並び: 文字の決まった部分が多いもの (完全一致を含む) を先にして、最初に合ったものを使う
# (aprs-deviceid の README の「完全一致、次に一番長く合う形」と同じ結果になる)。
# 形の記号: ? = 任意の1文字、n = 数字1文字、* = 以降なんでも
import sys, yaml, datetime

MODEL_MAX = 19                       # packet_parse.h の MICE_MODEL_MAX - 1 (バイト)
SKIP = {"APRS"}                      # 汎用 (機種不明) は出さない

def literal_count(p):
    return sum(1 for c in p if c not in "?n*")

def cut(s, n):
    b = s.encode("utf-8")
    if len(b) <= n: return s
    b = b[:n]
    while True:
        try: return b.decode("utf-8").rstrip()
        except UnicodeDecodeError: b = b[:-1]

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

src = sys.argv[1]
d = yaml.safe_load(open(src, encoding="utf-8"))
rows = []
for t in d["tocalls"]:
    p = t["tocall"]
    if p in SKIP: continue
    name = t.get("model") or t.get("vendor") or ""
    if not name: continue
    rows.append((p, cut(name, MODEL_MAX)))
rows.sort(key=lambda r: (-literal_count(r[0]), -len(r[0].rstrip("*")), r[0]))

print("// tocalls_table.h -- tools/gen_tocalls.py で作成 (手で直さない)")
print("// 宛先 (TOCALL) から機種を見分ける表。APRS Foundation の aprs-deviceid (https://github.com/aprsorg/aprs-deviceid)")
print("// の tocalls.yaml にもとづくデータで、CC BY-SA 2.0 (https://creativecommons.org/licenses/by-sa/2.0/) で提供される")
print("// (ほかの部分は MIT)。THIRD_PARTY_LICENSES.md 参照。")
print("// 作成日: %s  件数: %d" % (datetime.date.today().isoformat(), len(rows)))
print("// 形の記号: ? = 任意の1文字、n = 数字1文字、* = 以降なんでも。上から順に調べ、最初に合ったものを使う")
print("#ifndef TOCALLS_TABLE_H")
print("#define TOCALLS_TABLE_H")
print("struct TocallDev { const char* pattern; const char* model; };")
print("static const TocallDev TOCALL_DEV[] = {")
for p, m in rows:
    print("  { %s, %s }," % (cstr(p), cstr(m)))
print("};")
print("#endif")
