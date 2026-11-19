# check_i18n.py  (パソコン上のテスト用)
# 画面の文字の日英対応表 (ui_text.cpp) を確かめる:
#   1. 画面に出す日本語 (ui_core.cpp / ui_m5.cpp / M5APRS.ino / symbols.cpp の文字列で、かな・漢字を含むもの) が、すべて表にある
#   2. 書式 (%s %u %.1f など) の並びが、日本語と英語で同じ
#   3. ボタン表示の英語が短い (短押し 11文字、長押しは "Hold:" を付けて 11文字まで)
import re, sys, os
root = sys.argv[1] if len(sys.argv) > 1 else '.'
CJK = re.compile(r'[぀-ヿ一-鿿]')
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
FMT = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXfFeEgGcsp%]')

def code_literals(path):
    out = []
    for line in open(path, encoding='utf-8').read().split('\n'):
        i, inq, code = 0, False, line
        while i < len(line):
            c = line[i]
            if c == '\\' and inq: i += 2; continue
            if c == '"': inq = not inq
            elif not inq and line.startswith('//', i): code = line[:i]; break
            i += 1
        out += [m.group(1) for m in LIT.finditer(code)]
    return out

def table(src, name):
    m = re.search(r'static const UiTextPair ' + name + r'\[\] = \{(.*?)\n\};', src, re.S)
    return [(a.encode().decode('unicode_escape').encode('latin1').decode('utf-8'), b.encode().decode('unicode_escape').encode('latin1').decode('utf-8'))
            for a, b in re.findall(r'\{ "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)" \}', m.group(1))]

src = open(os.path.join(root, 'ui_text.cpp'), encoding='utf-8').read()
text, keys = table(src, 'TEXT'), table(src, 'KEYS')
known = set(a for a, b in text) | set(a for a, b in keys)
errs = []
for f in ('ui_core.cpp', 'ui_m5.cpp', 'M5APRS.ino', 'symbols.cpp', 'app_config.cpp'):
    for s in code_literals(os.path.join(root, f)):
        s2 = s.encode().decode('unicode_escape').encode('latin1').decode('utf-8')
        if s2.lstrip('\n').startswith('#'): continue          # config.txt の見本のコメント (画面には出さない)
        if CJK.search(s2) and s2 not in known: errs.append('表にない: %s (%s)' % (s2, f))
for a, b in text + keys:
    if FMT.findall(a) != FMT.findall(b): errs.append('書式が違う: "%s" / "%s"' % (a, b))
    if not b: errs.append('英語が空: %s' % a)
hold = dict(keys).get('長:', 'Hold:')
for a, b in keys:
    if a == '長:': continue
    if len(b) > 11 or len(hold) + len(b) > 11: errs.append('ボタンの英語が長い: %s' % b)
dup = [a for a, b in text if [x for x, y in text].count(a) > 1]
if dup: errs.append('重複: %s' % dup)
for e in errs: print(e)
print('%d texts, %d keys, %d errors' % (len(text), len(keys), len(errs)))
sys.exit(1 if errs else 0)
