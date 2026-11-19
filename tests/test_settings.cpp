// test_settings.cpp  (パソコン上のテスト用。スケッチには入れない)
// settings_nvs.cpp のコンパイルとつなぎ込みの確認は、stubs/Preferences.h を使う。
#include "test_cfg.h"
#include "settings.h"
#include "settings_nvs.h"
#include "Preferences.h"
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>

std::map<std::string, std::map<std::string, uint8_t> > Preferences::store;
std::map<std::string, std::map<std::string, std::vector<uint8_t> > > Preferences::blobs;
bool Preferences::failBegin = false, Preferences::failPut = false;
int Preferences::putCalls = 0;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// ---- 試験用の保存先 (メモリ) ----
struct Mock {
  std::map<std::string, uint8_t> kv;
  int puts = 0; bool failPut = false;
};
static bool m_get(void* c, const char* k, uint8_t* v) { Mock* m = (Mock*)c; auto it = m->kv.find(k); if (it == m->kv.end()) return false; *v = it->second; return true; }
static bool m_put(void* c, const char* k, uint8_t v) { Mock* m = (Mock*)c; m->puts++; if (m->failPut) return false; m->kv[k] = v; return true; }
static SettingsBackend be(Mock* m) { SettingsBackend b = { m, m_get, m_put }; return b; }

int main() {
  test_cfg_setup();
  // ---- 初期値 ----
  { Settings s; settings_defaults(&s);
    CHECK(s.ambiguity == DEFAULT_AMBIGUITY_LEVEL && s.digipathIndex == DEFAULT_DIGIPATH_INDEX &&
          s.symbolIndex == 0 && s.ssid == DEFAULT_SSID && s.sendCourseSpeed == DEFAULT_SEND_COURSE_SPEED &&
          s.sendAltitude == DEFAULT_SEND_ALTITUDE);
    CHECK(!settings_sanitize(&s));
    s.ambiguity = 5; s.digipathIndex = 3; s.symbolIndex = SYMBOL_CHOICE_COUNT; s.ssid = 16;
    CHECK(settings_sanitize(&s));
    CHECK(s.ambiguity == 0 && s.digipathIndex == DEFAULT_DIGIPATH_INDEX && s.symbolIndex == 0 && s.ssid == DEFAULT_SSID); }

  // ---- 保存値なしで起動: 初期値のまま、何も書かない ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.ambiguity == 0 && m.cur.digipathIndex == 1 && m.cur.symbolIndex == 0 && m.cur.ssid == 9);
    CHECK(!settings_dirty(&m)); CHECK(settings_commit(&m, false) == 0 && mk.puts == 0);
    CHECK(strcmp(settings_digipath(&m), "WIDE1-1") == 0);
    CHECK(strcmp(settings_symbol(&m)->name, "車") == 0 && settings_symbol(&m)->code == '>' && settings_ssid(&m) == 9);

    // 値を変えたら、そのキーだけ書く
    CHECK(settings_set_ambiguity(&m, 2));
    CHECK(settings_dirty(&m)); CHECK(settings_commit(&m, false) == 1 && mk.puts == 1 && mk.kv["amb"] == 2);
    CHECK(!settings_dirty(&m)); CHECK(settings_commit(&m, false) == 0 && mk.puts == 1);
    // 同じ値を設定しても変更なし・書き込みなし
    CHECK(!settings_set_ambiguity(&m, 2)); CHECK(settings_commit(&m, false) == 0 && mk.puts == 1);
    // 変えて元に戻したら、保存済みと同じなので書かない
    settings_set_ambiguity(&m, 4); settings_set_ambiguity(&m, 2);
    CHECK(!settings_dirty(&m)); CHECK(settings_commit(&m, false) == 0 && mk.puts == 1);
    // 初期値に戻すときは、保存済み(2)と違うので書く
    settings_set_ambiguity(&m, 0); CHECK(settings_commit(&m, false) == 1 && mk.kv["amb"] == 0 && mk.puts == 2);
    // 複数を一度に変えても、変わったものだけ
    settings_set_digipath(&m, 2); settings_set_symbol(&m, 3); settings_set_course_speed(&m, true);
    CHECK(settings_commit(&m, false) == 3 && mk.puts == 5);
    CHECK(mk.kv["digi"] == 2 && mk.kv["sym"] == 3 && mk.kv["cs"] == 1 && mk.kv.count("alt") == 0);
    settings_toggle_altitude(&m); CHECK(settings_commit(&m, false) == 1 && mk.kv["alt"] == 1);
    settings_toggle_altitude(&m); CHECK(settings_commit(&m, false) == 1 && mk.kv["alt"] == 0); }

  // ---- 次回起動で復元 ----
  { Mock mk; { SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
      settings_set_ambiguity(&m, 3); settings_set_digipath(&m, 0); settings_set_symbol(&m, 6); settings_set_ssid(&m, 0);
      settings_set_course_speed(&m, true); settings_set_altitude(&m, true); settings_commit(&m, false); }
    SettingsBackend b = be(&mk); SettingsMgr m2; settings_init(&m2, &b);
    CHECK(m2.cur.ambiguity == 3 && m2.cur.digipathIndex == 0 && m2.cur.symbolIndex == 6 && m2.cur.ssid == 0 &&
          m2.cur.sendCourseSpeed && m2.cur.sendAltitude);
    CHECK(strcmp(settings_digipath(&m2), "") == 0 && strcmp(settings_symbol(&m2)->name, "バス") == 0 && settings_ssid(&m2) == 0);
    CHECK(!settings_dirty(&m2)); CHECK(settings_commit(&m2, false) == 0 && mk.puts == 6); }

  // ---- 不正な保存値は使わない (キーごとに初期値へ) ----
  { Mock mk; mk.kv["amb"] = 9; mk.kv["digi"] = 7; mk.kv["sym"] = 200; mk.kv["ssid"] = 16; mk.kv["cs"] = 2; mk.kv["alt"] = 1;
    SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.ambiguity == 0 && m.cur.digipathIndex == DEFAULT_DIGIPATH_INDEX && m.cur.symbolIndex == 0 && m.cur.ssid == DEFAULT_SSID &&
          !m.cur.sendCourseSpeed && m.cur.sendAltitude);
    CHECK(!settings_dirty(&m)); }

  // ---- 送信中・復調中は書かない。終わってから書く ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    settings_set_symbol(&m, 2);
    CHECK(settings_commit(&m, true) == 0 && mk.puts == 0 && settings_dirty(&m));
    CHECK(settings_commit(&m, true) == 0 && mk.puts == 0);
    CHECK(settings_commit(&m, false) == 1 && mk.puts == 1 && mk.kv["sym"] == 2 && !settings_dirty(&m)); }

  // ---- 書き込み失敗: 次回また試す ----
  { Mock mk; mk.failPut = true; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    settings_set_digipath(&m, 2);
    CHECK(settings_commit(&m, false) == 0 && m.writeErrors == 1 && settings_dirty(&m));
    mk.failPut = false;
    CHECK(settings_commit(&m, false) == 1 && !settings_dirty(&m) && mk.kv["digi"] == 2); }

  // ---- 範囲外の設定は無視・丸め ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(!settings_set_digipath(&m, 3)); CHECK(!settings_set_symbol(&m, SYMBOL_CHOICE_COUNT)); CHECK(!settings_set_symbol(&m, 255)); CHECK(!settings_set_ssid(&m, 16));
    CHECK(settings_set_ambiguity(&m, 200) && m.cur.ambiguity == MAX_AMBIGUITY_LEVEL);
    CHECK(m.cur.digipathIndex == 1 && m.cur.symbolIndex == 0 && m.cur.ssid == 9); }

  // ---- メニューの切り替えは一周する ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    for (int i = 0; i < 5; i++) settings_next_ambiguity(&m);
    CHECK(m.cur.ambiguity == 0);
    settings_next_ambiguity(&m); CHECK(m.cur.ambiguity == 1);
    for (int i = 0; i < 3; i++) settings_next_digipath(&m);
    CHECK(m.cur.digipathIndex == 1);
    for (int i = 0; i < SYMBOL_CHOICE_COUNT; i++) settings_next_symbol(&m);
    CHECK(m.cur.symbolIndex == 0);
    settings_next_symbol(&m); CHECK(strcmp(settings_symbol(&m)->name, "トラック") == 0);
    for (int i = 0; i < 16; i++) settings_next_ssid(&m);
    CHECK(m.cur.ssid == 9);
    for (int i = 0; i < 7; i++) settings_next_ssid(&m);
    CHECK(m.cur.ssid == 0);                                                          // 15 の次は 0 (SSIDなし)
    settings_toggle_course_speed(&m); CHECK(m.cur.sendCourseSpeed);
    settings_toggle_course_speed(&m); CHECK(!m.cur.sendCourseSpeed); }

  // ---- 保存先なし ----
  { SettingsMgr m; settings_init(&m, NULL);
    settings_set_ambiguity(&m, 2); CHECK(settings_commit(&m, false) == 0 && m.cur.ambiguity == 2);
    SettingsBackend half = { 0, NULL, NULL }; settings_init(&m, &half); CHECK(settings_commit(&m, false) == 0); }

  // ---- 全シンボル・デジパスが表と一致する ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    for (int i = 0; i < SYMBOL_CHOICE_COUNT; i++) {
      settings_set_symbol(&m, (uint8_t)i); CHECK(settings_symbol(&m) == &SYMBOL_CHOICES[i]);
    }
    for (int i = 0; i < DIGIPATH_CHOICE_COUNT; i++) {
      settings_set_digipath(&m, (uint8_t)i); CHECK(strcmp(settings_digipath(&m), DIGIPATH_CHOICES[i]) == 0);
    } }

  // ---- ESP32 Preferences 経由のつなぎ込み (代用品で確認) ----
  { Preferences::store.clear(); Preferences::putCalls = 0; Preferences::failBegin = false; Preferences::failPut = false;
    SettingsMgr m; settings_init(&m, settings_nvs_backend());
    CHECK(m.cur.ambiguity == 0 && !settings_dirty(&m));
    settings_set_ambiguity(&m, 4); settings_set_symbol(&m, 5);
    CHECK(settings_commit(&m, false) == 2 && Preferences::putCalls == 2);
    CHECK(Preferences::store[NVS_NAMESPACE]["amb"] == 4 && Preferences::store[NVS_NAMESPACE]["sym"] == 5);
    SettingsMgr m2; settings_init(&m2, settings_nvs_backend());                  // 再起動のつもり
    CHECK(m2.cur.ambiguity == 4 && m2.cur.symbolIndex == 5);
    settings_set_ambiguity(&m2, 4); CHECK(settings_commit(&m2, false) == 0 && Preferences::putCalls == 2);
    Preferences::failPut = true;
    settings_set_ambiguity(&m2, 1); CHECK(settings_commit(&m2, false) == 0 && m2.writeErrors == 1 && settings_dirty(&m2));
    Preferences::failPut = false; CHECK(settings_commit(&m2, false) == 1); }

  // ---- モニター音量 ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.monitorLevel == DEFAULT_MONITOR_LEVEL && !settings_dirty(&m));
    CHECK(!settings_set_monitor(&m, DEFAULT_MONITOR_LEVEL));                       // 同じ値は変更なし
    CHECK(settings_set_monitor(&m, 0) && m.cur.monitorLevel == 0);
    CHECK(settings_set_monitor(&m, 200) && m.cur.monitorLevel == MONITOR_LEVEL_MAX);   // 範囲外は最大にそろえる
    settings_set_monitor(&m, 0);
    for (int i = 1; i <= MONITOR_LEVEL_MAX; i++) { settings_next_monitor(&m); CHECK(m.cur.monitorLevel == i); }
    settings_next_monitor(&m); CHECK(m.cur.monitorLevel == 0);                      // 最大の次はミュートへ戻る
    CHECK(settings_commit(&m, true) == 0 && mk.puts == 0);                          // 送信中・受信中は書かない
    settings_set_monitor(&m, 3);
    CHECK(settings_dirty(&m) && settings_commit(&m, false) == 1 && mk.kv["mon"] == 3 && !settings_dirty(&m));
    CHECK(settings_commit(&m, false) == 0);                                         // 同じ値は書かない
    SettingsMgr m2; settings_init(&m2, &b); CHECK(m2.cur.monitorLevel == 3);        // 次回起動で復元
    mk.kv["mon"] = MONITOR_LEVEL_MAX + 1;                                           // 不正な保存値は初期値
    SettingsMgr m3; settings_init(&m3, &b); CHECK(m3.cur.monitorLevel == DEFAULT_MONITOR_LEVEL);
    Settings s; settings_defaults(&s); s.monitorLevel = 9; CHECK(settings_sanitize(&s) && s.monitorLevel == DEFAULT_MONITOR_LEVEL); }

  // ---- 受信音の音量 (モニター音量とは別に保存) ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.chimeLevel == DEFAULT_CHIME_LEVEL && !settings_dirty(&m));
    CHECK(settings_set_chime(&m, 200) && m.cur.chimeLevel == MONITOR_LEVEL_MAX && m.cur.monitorLevel == DEFAULT_MONITOR_LEVEL);
    settings_set_chime(&m, 0);
    for (int i = 1; i <= MONITOR_LEVEL_MAX; i++) { settings_next_chime(&m); CHECK(m.cur.chimeLevel == i); }
    settings_next_chime(&m); CHECK(m.cur.chimeLevel == 0);                          // 最大の次はオフへ戻る
    CHECK(settings_commit(&m, false) == 1 && mk.kv["chime"] == 0 && mk.kv.count("mon") == 0);
    SettingsMgr m2; settings_init(&m2, &b); CHECK(m2.cur.chimeLevel == 0 && m2.cur.monitorLevel == DEFAULT_MONITOR_LEVEL);
    mk.kv["chime"] = MONITOR_LEVEL_MAX + 1;
    SettingsMgr m3; settings_init(&m3, &b); CHECK(m3.cur.chimeLevel == DEFAULT_CHIME_LEVEL); }

  // ---- 画面の明るさ ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    static const uint8_t T[BRIGHTNESS_LEVEL_COUNT] = BRIGHTNESS_TABLE;
    CHECK(m.cur.brightness == DEFAULT_BRIGHTNESS_LEVEL && settings_backlight(&m) == T[DEFAULT_BRIGHTNESS_LEVEL] && !settings_dirty(&m));
    settings_set_brightness(&m, 0);
    for (int i = 1; i < BRIGHTNESS_LEVEL_COUNT; i++) { uint8_t prev = settings_backlight(&m); settings_set_brightness(&m, (uint8_t)i); CHECK(settings_backlight(&m) > prev); }   // 段階が上がるほど明るい
    CHECK(settings_backlight(&m) == 255);                                           // いちばん明るい段階は最大
    settings_next_brightness(&m); CHECK(m.cur.brightness == 0 && settings_backlight(&m) >= 1 && settings_backlight(&m) < 20);   // 最も明るいの次は最も暗いへ
    CHECK(!settings_set_brightness(&m, BRIGHTNESS_LEVEL_COUNT) && m.cur.brightness == 0);   // 範囲外は変えない
    CHECK(settings_commit(&m, false) == 1 && mk.kv["bri"] == 0);
    SettingsMgr m2; settings_init(&m2, &b); CHECK(m2.cur.brightness == 0);          // 次回起動で復元
    mk.kv["bri"] = 77; SettingsMgr m3; settings_init(&m3, &b); CHECK(m3.cur.brightness == DEFAULT_BRIGHTNESS_LEVEL);   // 不正な保存値は初期値
    Mock old; SettingsBackend bo = be(&old); SettingsMgr m4; settings_init(&m4, &bo); CHECK(m4.cur.brightness == DEFAULT_BRIGHTNESS_LEVEL); }   // 保存値なし (0.5.0 まで)

  // ---- 表示の言語 ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.language == DEFAULT_LANGUAGE && !settings_dirty(&m));
    settings_next_language(&m); CHECK(m.cur.language == 1);
    settings_next_language(&m); CHECK(m.cur.language == 0);                          // 英語の次は日本語へ戻る
    CHECK(!settings_set_language(&m, LANGUAGE_COUNT) && m.cur.language == 0);       // 範囲外は変えない
    settings_set_language(&m, 1); CHECK(settings_commit(&m, false) == 1 && mk.kv["lang"] == 1);
    SettingsMgr m2; settings_init(&m2, &b); CHECK(m2.cur.language == 1);            // 次回起動で復元
    mk.kv["lang"] = 9; SettingsMgr m3; settings_init(&m3, &b); CHECK(m3.cur.language == DEFAULT_LANGUAGE);   // 不正な保存値は初期値
    Mock old; SettingsBackend bo = be(&old); SettingsMgr m4; settings_init(&m4, &bo); CHECK(m4.cur.language == DEFAULT_LANGUAGE); }   // 保存値なし (0.6.0 まで)

  // ---- レーダーの距離と時間 ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.radarRange == DEFAULT_RADAR_RANGE && m.cur.radarTime == DEFAULT_RADAR_TIME && settings_radar_range_km(&m) == 0 && settings_radar_time_min(&m) == 60);
    static const uint16_t KM[] = { 1, 2, 5, 10, 20, 50, 100, 0 }, MIN[] = { 10, 30, 60, 180, 360, 720, 1440 };
    for (int i = 0; i < RADAR_RANGE_COUNT; i++) { settings_set_radar_range(&m, (uint8_t)i); CHECK(settings_radar_range_km(&m) == KM[i]); }
    for (int i = 0; i < RADAR_TIME_COUNT; i++) { settings_set_radar_time(&m, (uint8_t)i); CHECK(settings_radar_time_min(&m) == MIN[i]); }
    settings_next_radar_range(&m); CHECK(m.cur.radarRange == 0);                    // 自動の次は 1km
    settings_next_radar_time(&m); CHECK(m.cur.radarTime == 0);                      // 24時間の次は 10分
    CHECK(!settings_set_radar_range(&m, RADAR_RANGE_COUNT) && !settings_set_radar_time(&m, RADAR_TIME_COUNT));
    settings_set_radar_range(&m, 3); settings_set_radar_time(&m, 1); settings_commit(&m, false);
    CHECK(mk.kv["rdrr"] == 3 && mk.kv["rdrt"] == 1);
    SettingsMgr m2; settings_init(&m2, &b); CHECK(settings_radar_range_km(&m2) == 10 && settings_radar_time_min(&m2) == 30);
    mk.kv["rdrr"] = 8; mk.kv["rdrt"] = 7; SettingsMgr m3; settings_init(&m3, &b);
    CHECK(m3.cur.radarRange == DEFAULT_RADAR_RANGE && m3.cur.radarTime == DEFAULT_RADAR_TIME); }

  // ---- 0.7.0 までのプリセットから、シンボルとSSIDへ ----
  { struct { uint8_t p; char code; uint8_t ssid; } T[] = { {0,'>',9}, {1,'[',7}, {2,'-',0}, {3,'b',7}, {4,'l',12}, {5,';',12}, {6,'s',8}, {7,'k',14} };
    for (size_t k = 0; k < sizeof(T) / sizeof(T[0]); k++) {
      Mock mk; mk.kv["preset"] = T[k].p; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
      CHECK(settings_symbol(&m)->table == '/' && settings_symbol(&m)->code == T[k].code && settings_ssid(&m) == T[k].ssid);
      bool def = (T[k].code == '>' && T[k].ssid == 9);
      CHECK(settings_dirty(&m) == !def);                                             // 初期値と違えば、次の commit で新しいキーに書く
      settings_commit(&m, false);
      if (!def) { CHECK(mk.kv.count("sym") == 1 || mk.kv.count("ssid") == 1);
        SettingsMgr m2; settings_init(&m2, &b); CHECK(settings_symbol(&m2)->code == T[k].code && settings_ssid(&m2) == T[k].ssid && !settings_dirty(&m2)); } }
    // 新しいキーがあれば、プリセットは見ない。不正なプリセット値は使わない
    { Mock mk; mk.kv["preset"] = 2; mk.kv["ssid"] = 5; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
      CHECK(settings_symbol(&m)->code == '>' && settings_ssid(&m) == 5); }
    { Mock mk; mk.kv["preset"] = 8; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
      CHECK(settings_symbol(&m)->code == '>' && settings_ssid(&m) == 9 && !settings_dirty(&m)); } }

  // ---- シンボルのアイコン ----
  { CHECK(symbol_icon('/', '>') == 1 && symbol_icon('/', '#') == SYMBOL_ICON_DIGI && symbol_icon('/', '_') == 20);
    CHECK(symbol_icon('I', '&') == SYMBOL_ICON_IGATE && symbol_icon('\\', '&') == SYMBOL_ICON_IGATE && symbol_icon('R', '&') == SYMBOL_ICON_IGATE);
    // 代替表 (オーバーレイ付き) は、意味の近いものだけプライマリ表のアイコンに当てる
    { int car = SYMBOL_CHOICES[symbol_find('/', '>')].icon, van = SYMBOL_CHOICES[symbol_find('/', 'v')].icon, jeep = SYMBOL_CHOICES[symbol_find('/', 'j')].icon,
          semi = SYMBOL_CHOICES[symbol_find('/', 'u')].icon, ship = SYMBOL_CHOICES[symbol_find('/', 's')].icon, yacht = SYMBOL_CHOICES[symbol_find('/', 'Y')].icon,
          plane = SYMBOL_CHOICES[symbol_find('/', '\'')].icon, home = SYMBOL_CHOICES[symbol_find('/', '-')].icon;
      CHECK(symbol_icon('\\', '>') == car && symbol_icon('S', '>') == car && symbol_icon('\\', 'v') == van && symbol_icon('\\', 'k') == jeep);
      CHECK(symbol_icon('T', 'u') == semi && symbol_icon('\\', 's') == ship && symbol_icon('Y', 's') == yacht && symbol_icon('\\', '^') == plane);
      CHECK(symbol_icon('\\', '-') == home && symbol_icon('S', '-') == home);
      CHECK(symbol_icon('\\', '[') == 0 && symbol_icon('\\', 'O') == 0 && symbol_icon('/', 'x') == 0 && symbol_icon('Y', 'Y') == 0 && symbol_icon('\\', '_') == 0); }
    CHECK(symbol_icon('1', '#') == SYMBOL_ICON_DIGI && symbol_icon('/', '&') == 0 && symbol_icon('a', '&') == 0 && symbol_icon(0, 0) == 0);
    for (int i = 0; i < SYMBOL_CHOICE_COUNT; i++) {
      const SymbolChoice* c = &SYMBOL_CHOICES[i];
      CHECK(symbol_find(c->table, c->code) == i && symbol_icon(c->table, c->code) == c->icon && symbol_icon_bitmap(c->icon) != NULL);
      for (int j = 0; j < i; j++) CHECK(SYMBOL_CHOICES[j].code != c->code || SYMBOL_CHOICES[j].table != c->table); }   // 重複なし
    for (int k = 1; k <= SYMBOL_ICON_COUNT; k++) { const uint8_t* bm = symbol_icon_bitmap(k); int on = 0; for (int i = 0; i < 32; i++) for (int bt = 0; bt < 8; bt++) on += (bm[i] >> bt) & 1;
      CHECK(on > 20 && on < 220 && symbol_icon_color(k) <= SYM_COL_GREEN); }                 // 空や塗りつぶしの絵がない
    CHECK(symbol_icon_bitmap(0) == NULL && symbol_icon_bitmap(SYMBOL_ICON_COUNT + 1) == NULL);
    CHECK(symbol_icon_color(16) == SYM_COL_CYAN && symbol_icon_color(20) == SYM_COL_YELLOW && symbol_icon_color(21) == SYM_COL_GREEN && symbol_icon_color(1) == SYM_COL_WHITE);
    CHECK(!strcmp(ssid_use(0), "SSIDなし (固定局)") && !strcmp(ssid_use(9), "モービル") && !strcmp(ssid_use(15), "汎用 (実験用)"));
    CHECK(!strcmp(ssid_use(1), "汎用 (低地デジ)") && !strcmp(ssid_use(2), "汎用 (9k6デジ)") && !strcmp(ssid_use(3), "汎用 (広域デジ)") && !strcmp(ssid_use(4), "汎用 (実験用)")); }

  // ---- 自動送信 (オフ / Ackのみ / 間隔) ----
  { Mock mk; SettingsBackend b = be(&mk); SettingsMgr m; settings_init(&m, &b);
    CHECK(m.cur.autoTx == AUTO_TX_OFF && !settings_dirty(&m));
    settings_next_auto_tx(&m); CHECK(m.cur.autoTx == AUTO_TX_ACK_ONLY);
    for (int i = 2; i < AUTO_TX_CHOICES; i++) { settings_next_auto_tx(&m); CHECK(m.cur.autoTx == i); }
    settings_next_auto_tx(&m); CHECK(m.cur.autoTx == AUTO_TX_OFF);                   // 最後の次はオフへ
    CHECK(!settings_set_auto_tx(&m, AUTO_TX_CHOICES) && m.cur.autoTx == AUTO_TX_OFF); // 範囲外は無視
    CHECK(settings_set_auto_tx(&m, 5));                                             // 5 = 3分
    CHECK(settings_commit(&m, false) == 1 && mk.kv["auto"] == 5);
    SettingsMgr m2; settings_init(&m2, &b); CHECK(m2.cur.autoTx == 5);               // 電源を入れ直しても引き継ぐ
    mk.kv["auto"] = AUTO_TX_CHOICES;                                                // 不正な保存値は初期値 (オフ)
    SettingsMgr m3; settings_init(&m3, &b); CHECK(m3.cur.autoTx == AUTO_TX_OFF);
    Settings st; settings_defaults(&st); st.autoTx = 200; CHECK(settings_sanitize(&st) && st.autoTx == AUTO_TX_OFF); }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
