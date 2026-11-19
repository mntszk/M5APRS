// test_position.cpp  (パソコン上のテスト用。スケッチには入れない)
// 位置の取得元: 固定位置の判定、最後の位置の保存、取得元の解決、設定、送信内容の組み立て
#include "test_cfg.h"
#include "position_source.h"
#include "last_pos.h"
#include "last_pos_nvs.h"
#include "tx_compose.h"
#include "settings.h"
#include "settings_nvs.h"
#include "packet_parse.h"
#include "packet_build.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <map>

std::map<std::string, std::map<std::string, uint8_t> > Preferences::store;
std::map<std::string, std::map<std::string, std::vector<uint8_t> > > Preferences::blobs;
bool Preferences::failBegin = false, Preferences::failPut = false;
int Preferences::putCalls = 0;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) do { checks++; double _a = (a), _b = (b); if (!(fabs(_a - _b) <= (tol))) { failures++; printf("FAIL line %d: %s=%.7f vs %.7f\n", __LINE__, #a, _a, _b); } } while (0)

static std::string nm(const std::string& body) {
  uint8_t x = 0; for (size_t i = 0; i < body.size(); i++) x ^= (uint8_t)body[i];
  char h[8]; snprintf(h, sizeof(h), "*%02X", x);
  return "$" + body + h + "\r\n";
}
static void feed(GpsState& g, const std::string& s, uint32_t now) { gps_feed(&g, s.data(), s.size(), now); }
static void fix(GpsState& g, uint32_t now) {
  feed(g, nm("GPRMC,123519,A,3540.0000,N,13945.0000,E,10.0,90.0,091026,,,A"), now);
  feed(g, nm("GPGGA,123519,3540.0000,N,13945.0000,E,1,08,0.9,123.4,M,46.9,M,,"), now);
}

// ---- 最後の位置の保存先 (モック) ----
struct LpMock { bool has = false; LastPos stored; int saves = 0, loads = 0; bool failSave = false; };
static bool lp_load(void* c, LastPos* o) { LpMock* m = (LpMock*)c; m->loads++; if (!m->has) return false; *o = m->stored; return true; }
static bool lp_save(void* c, const LastPos* p) { LpMock* m = (LpMock*)c; m->saves++; if (m->failSave) return false; m->stored = *p; m->has = true; return true; }
static LastPosBackend lpbe(LpMock* m) { LastPosBackend b = { m, lp_load, lp_save }; return b; }

// 設定の保存先 (モック)
struct SMock { std::map<std::string, uint8_t> kv; };
static bool s_get(void* c, const char* k, uint8_t* v) { SMock* m = (SMock*)c; auto it = m->kv.find(k); if (it == m->kv.end()) return false; *v = it->second; return true; }
static bool s_put(void* c, const char* k, uint8_t v) { ((SMock*)c)->kv[k] = v; return true; }

static int first_set() { for (int i = 0; i < FIXED_POSITION_COUNT; i++) if (fixed_position_set(i)) return i; return -1; }

int main() {
  test_cfg_setup();
  const int nSet = fixed_position_count_set();
  const int firstSet = first_set();
  printf("固定位置の設定済み: %d件\n", nSet);

  // ---- 緯度経度の妥当性 ----
  CHECK(pos_plausible(35.0, 139.0) && pos_plausible(-90, 180) && pos_plausible(90, -180) && pos_plausible(0.0, 1.0) && pos_plausible(1.0, 0.0));
  CHECK(!pos_plausible(0, 0) && !pos_plausible(90.1, 0) && !pos_plausible(0, 180.1) && !pos_plausible(-91, 10));
  CHECK(!pos_plausible(NAN, 10) && !pos_plausible(10, NAN) && !pos_plausible(INFINITY, 0));
  CHECK(!fixed_position_set(-1) && !fixed_position_set(FIXED_POSITION_COUNT));
  { int n = 0; for (int i = 0; i < FIXED_POSITION_COUNT; i++) if (fixed_position_set(i)) n++; CHECK(n == nSet); }

  // ---- 最後の位置: 読み込み ----
  { LpMock mk; LastPosMgr m; LastPosBackend b = lpbe(&mk);
    last_pos_init(&m, &b); CHECK(!m.cur.valid && mk.loads == 1);
    mk.has = true; mk.stored = { true, 35.5, 139.5, true, 12.0 }; last_pos_init(&m, &b);
    CHECK(m.cur.valid && m.cur.latDeg == 35.5 && m.cur.hasAlt && m.cur.altM == 12.0);
    mk.stored = { true, 35.5, 139.5, false, 99.0 }; last_pos_init(&m, &b); CHECK(m.cur.valid && !m.cur.hasAlt && m.cur.altM == 0.0);
    mk.stored = { true, 95.0, 139.5, false, 0 }; last_pos_init(&m, &b); CHECK(!m.cur.valid);                // 範囲外は使わない
    mk.stored = { true, 0.0, 0.0, false, 0 }; last_pos_init(&m, &b); CHECK(!m.cur.valid);
    mk.stored = { true, NAN, 1.0, false, 0 }; last_pos_init(&m, &b); CHECK(!m.cur.valid);
    mk.stored = { true, 35.0, 139.0, true, NAN }; last_pos_init(&m, &b); CHECK(m.cur.valid && !m.cur.hasAlt);   // 高度が不正でも位置は使う
    last_pos_init(&m, NULL); CHECK(!m.cur.valid);
    CHECK(!last_pos_update(&m, 1000, true, 35, 139, true, 1, false) && mk.saves == 0); }          // 保存先なしでも落ちない

  // ---- 最後の位置: 保存のタイミング ----
  { LpMock mk; LastPosMgr m; LastPosBackend b = lpbe(&mk); last_pos_init(&m, &b);
    const double lat = 35.0, lon = 139.0;
    CHECK(!last_pos_update(&m, 1000, false, lat, lon, true, 10, false) && mk.saves == 0);          // 測位なし: 書かない
    CHECK(!last_pos_update(&m, 1000, true, 0.0, 0.0, true, 10, false) && mk.saves == 0);           // 位置が不正: 書かない
    // 測位直後: 高度がまだなら最大5秒待つ
    CHECK(!last_pos_update(&m, 2000, true, lat, lon, false, 0, false) && mk.saves == 0);
    CHECK(!last_pos_update(&m, 6900, true, lat, lon, false, 0, false) && mk.saves == 0);
    CHECK(last_pos_update(&m, 7000, true, lat, lon, false, 0, false) && mk.saves == 1);           // 5秒たったら高度なしで保存
    CHECK(mk.stored.valid && mk.stored.latDeg == lat && !mk.stored.hasAlt);
    // その後: 動いていなければ書かない (何時間たっても)
    for (uint32_t t = 10000; t < 10000 + 3600000; t += 30000) last_pos_update(&m, t, true, lat, lon, true, 10, false);
    CHECK(mk.saves == 1);
    // 500m動いても、前回の保存から10分たっていなければ書かない
    double lat500 = lat + 0.0046;                                                                  // 約511m
    CHECK(geo_distance_km(lat, lon, lat500, lon) * 1000 > 500);
    uint32_t base = m.lastSaveMs;
    CHECK(!last_pos_update(&m, base + 599000, true, lat500, lon, true, 10, false) && mk.saves == 1);
    CHECK(last_pos_update(&m, base + 600000, true, lat500, lon, true, 10, false) && mk.saves == 2);   // 10分たったら書く
    CHECK(mk.stored.latDeg == lat500 && mk.stored.hasAlt && mk.stored.altM == 10);
    // 10分たっていても、500m未満なら書かない (保存位置との距離で判定)
    base = m.lastSaveMs;
    CHECK(!last_pos_update(&m, base + 700000, true, lat500 + 0.004, lon, true, 10, false) && mk.saves == 2);   // 約444m
    CHECK(last_pos_update(&m, base + 700100, true, lat500 + 0.0046, lon, true, 10, false) && mk.saves == 3);
    // 送信中・受信中は書かない。終わったら書く
    base = m.lastSaveMs; double far = lat + 1.0;
    CHECK(!last_pos_update(&m, base + 700000, true, far, lon, true, 10, true) && mk.saves == 3);
    CHECK(last_pos_update(&m, base + 700500, true, far, lon, true, 10, false) && mk.saves == 4);
    // 書けなかったら10秒おいて再試行 (その間は書こうとしない)
    mk.failSave = true; base = m.lastSaveMs; double far2 = far + 1.0;
    CHECK(!last_pos_update(&m, base + 700000, true, far2, lon, true, 10, false) && mk.saves == 5 && m.writeErrors == 1);
    CHECK(!last_pos_update(&m, base + 709999, true, far2, lon, true, 10, false) && mk.saves == 5);
    mk.failSave = false;
    CHECK(last_pos_update(&m, base + 710000, true, far2, lon, true, 10, false) && mk.saves == 6 && m.writeErrors == 1);
    CHECK(m.writeCount == 5); }

  // ---- 最後の位置: 起動直後の判断 ----
  { LpMock mk; mk.has = true; mk.stored = { true, 35.0, 139.0, true, 5.0 };
    LastPosMgr m; LastPosBackend b = lpbe(&mk); last_pos_init(&m, &b);
    // 保存済みと同じ場所 (50m以内) → 起動後の最初の保存は不要
    CHECK(!last_pos_update(&m, 3000, true, 35.0002, 139.0, true, 10, false) && mk.saves == 0 && m.savedThisBoot);
    // 以後は「動いた距離」の判定へ (保存済みの位置を基準に)
    CHECK(!last_pos_update(&m, 3000 + 600000, true, 35.003, 139.0, true, 10, false) && mk.saves == 0);      // 約333m
    CHECK(last_pos_update(&m, 3000 + 600100, true, 35.0046, 139.0, true, 10, false) && mk.saves == 1);       // 約511m
    // 保存済みと遠い場所で起動 → 最初の測位で書く
    LpMock mk2; mk2.has = true; mk2.stored = { true, 35.0, 139.0, true, 5.0 };
    LastPosMgr m2; LastPosBackend b2 = lpbe(&mk2); last_pos_init(&m2, &b2);
    CHECK(last_pos_update(&m2, 1000, true, 36.0, 140.0, true, 10, false) && mk2.saves == 1 && mk2.stored.latDeg == 36.0);
    // millis の桁あふれをまたいでも10分の判定ができる
    LpMock mk3; LastPosMgr m3; LastPosBackend b3 = lpbe(&mk3); last_pos_init(&m3, &b3);
    CHECK(last_pos_update(&m3, 0xFFFFFF00u, true, 35.0, 139.0, true, 1, false));
    CHECK(!last_pos_update(&m3, 0xFFFFFF00u + 599000u, true, 36.0, 139.0, true, 1, false));
    CHECK(last_pos_update(&m3, 0xFFFFFF00u + 600000u, true, 36.0, 139.0, true, 1, false) && mk3.saves == 2); }

  // ---- 最後の位置: NVSへの保存 ----
  { Preferences::store.clear(); Preferences::blobs.clear(); Preferences::failBegin = false; Preferences::failPut = false;
    { LastPosMgr m; last_pos_init(&m, last_pos_nvs_backend()); CHECK(!m.cur.valid);
      CHECK(last_pos_update(&m, 1000, true, 35.123456789, -139.987654321, true, 12.5, false) && m.cur.valid); }
    LastPosMgr m2; last_pos_init(&m2, last_pos_nvs_backend());
    CHECK(m2.cur.valid && m2.cur.latDeg == 35.123456789 && m2.cur.lonDeg == -139.987654321 && m2.cur.hasAlt && m2.cur.altM == 12.5);   // 精度を落とさず復元
    Preferences::blobs[NVS_NAMESPACE]["lastpos"][0] = 9;                                            // 版が違う保存値は使わない
    LastPosMgr m3; last_pos_init(&m3, last_pos_nvs_backend()); CHECK(!m3.cur.valid);
    Preferences::blobs[NVS_NAMESPACE]["lastpos"].resize(10);                                        // 長さが違う保存値は使わない
    LastPosMgr m4; last_pos_init(&m4, last_pos_nvs_backend()); CHECK(!m4.cur.valid);
    Preferences::failPut = true;
    LastPosMgr m5; last_pos_init(&m5, last_pos_nvs_backend());
    CHECK(!last_pos_update(&m5, 1000, true, 35.0, 139.0, true, 1, false) && m5.writeErrors == 1 && !m5.cur.valid);   // 書けなければ保持しない
    Preferences::failPut = false; }

  // ---- 設定: 取得元と固定位置 ----
  { SMock sk; SettingsBackend be = { &sk, s_get, s_put }; SettingsMgr s; settings_init(&s, &be);
    CHECK(s.cur.positionSource == DEFAULT_POSITION_SOURCE && s.cur.fixedIndex == DEFAULT_FIXED_INDEX && !settings_dirty(&s));
    CHECK(!settings_set_position_source(&s, POSITION_SOURCE_GPS) && !settings_set_position_source(&s, 3) && s.cur.positionSource == POSITION_SOURCE_GPS);   // 範囲外は無視
    CHECK(!settings_set_fixed_index(&s, FIXED_POSITION_COUNT) && s.cur.fixedIndex == DEFAULT_FIXED_INDEX);
    settings_next_position_source(&s); CHECK(s.cur.positionSource == POSITION_SOURCE_LAST);
    settings_next_position_source(&s);
    if (nSet > 0) { CHECK(s.cur.positionSource == POSITION_SOURCE_FIXED); settings_next_position_source(&s); }
    CHECK(s.cur.positionSource == POSITION_SOURCE_GPS);                                           // 固定位置が1件もなければ「固定」は飛ばして GPS へ
    // 固定位置の選択は、設定済みの欄だけを巡る
    if (nSet == 0) { uint8_t before = s.cur.fixedIndex; settings_next_fixed(&s); CHECK(s.cur.fixedIndex == before); }
    else {
      settings_set_fixed_index(&s, (uint8_t)firstSet); std::vector<int> seen;
      for (int k = 0; k < nSet; k++) { seen.push_back(s.cur.fixedIndex); settings_next_fixed(&s); }
      CHECK(s.cur.fixedIndex == firstSet);                                                         // 一周して元へ
      for (size_t i = 0; i < seen.size(); i++) CHECK(fixed_position_set(seen[i]));
      std::vector<int> u = seen; for (size_t i = 0; i < u.size(); i++) for (size_t j = i + 1; j < u.size(); j++) CHECK(u[i] != u[j]);
      // 今の欄が未設定でも、次の設定済みの欄へ進む
      int unset = -1; for (int i = 0; i < FIXED_POSITION_COUNT; i++) if (!fixed_position_set(i)) { unset = i; break; }
      if (unset >= 0) { settings_set_fixed_index(&s, (uint8_t)unset); settings_next_fixed(&s); CHECK(fixed_position_set(s.cur.fixedIndex)); } }
    // 保存と復元
    settings_set_position_source(&s, POSITION_SOURCE_LAST); settings_set_fixed_index(&s, (uint8_t)(nSet ? firstSet : 3));
    CHECK(settings_commit(&s, false) >= 1 && sk.kv["psrc"] == POSITION_SOURCE_LAST);
    SettingsMgr s2; settings_init(&s2, &be); CHECK(s2.cur.positionSource == POSITION_SOURCE_LAST && s2.cur.fixedIndex == (nSet ? firstSet : 3));
    sk.kv["psrc"] = 7; sk.kv["fix"] = FIXED_POSITION_COUNT;                                       // 不正な保存値は初期値
    SettingsMgr s3; settings_init(&s3, &be); CHECK(s3.cur.positionSource == DEFAULT_POSITION_SOURCE && s3.cur.fixedIndex == DEFAULT_FIXED_INDEX); }

  // ---- 取得元の解決: GPS ----
  { SettingsMgr s; settings_init(&s, NULL); GpsState g; gps_init(&g); LastPosMgr lp; last_pos_init(&lp, NULL); OwnPos o;
    position_resolve(&s, &g, &lp, 1000, &o);
    CHECK(!o.valid && o.reason == POS_GPS_NO_DATA && o.source == POSITION_SOURCE_GPS);              // NMEAが届いていない
    feed(g, nm("GPRMC,123519,V,,,,,,,091026,,,N"), 1000);
    position_resolve(&s, &g, &lp, 1500, &o); CHECK(!o.valid && o.reason == POS_GPS_NO_FIX);          // 届くが測位なし
    fix(g, 2000);
    position_resolve(&s, &g, &lp, 2500, &o);
    CHECK(o.valid && !o.stale && o.reason == POS_OK); NEAR(o.latDeg, 35.6667, 0.001); NEAR(o.lonDeg, 139.75, 0.001);
    CHECK(o.hasSpeed && o.speedKnots == 10.0 && o.hasCourse && o.courseDeg == 90.0 && o.hasAlt); NEAR(o.altM, 123.4, 0.01);
    position_resolve(&s, &g, &lp, 2000 + 60000, &o);                                                 // NMEAが途絶えて1分 → 最終測位位置を使う
    CHECK(o.valid && o.stale && o.ageSec >= 59);
    position_resolve(&s, &g, &lp, 2000 + 301000, &o); CHECK(!o.valid && o.reason == POS_GPS_NO_DATA); // 5分を超えたら使わない
    // 取得元が GPS のとき、保存位置や固定位置があっても、自動では使わない
    LpMock mk; mk.has = true; mk.stored = { true, 10.0, 20.0, false, 0 }; LastPosBackend b = lpbe(&mk); last_pos_init(&lp, &b);
    position_resolve(&s, &g, &lp, 2000 + 301000, &o); CHECK(!o.valid); }

  // ---- 取得元の解決: 最後の位置 ----
  { SettingsMgr s; settings_init(&s, NULL); settings_set_position_source(&s, POSITION_SOURCE_LAST);
    GpsState g; gps_init(&g); fix(g, 1000); LastPosMgr lp; last_pos_init(&lp, NULL); OwnPos o;
    position_resolve(&s, &g, &lp, 1500, &o);
    CHECK(!o.valid && o.reason == POS_LAST_NONE && o.source == POSITION_SOURCE_LAST);                // 保存位置なし。GPSが測位中でも使わない
    LpMock mk; mk.has = true; mk.stored = { true, 10.5, 20.5, true, 77.0 }; LastPosBackend b = lpbe(&mk); last_pos_init(&lp, &b);
    position_resolve(&s, &g, &lp, 1500, &o);
    CHECK(o.valid && o.latDeg == 10.5 && o.lonDeg == 20.5 && o.hasAlt && o.altM == 77.0 && !o.hasSpeed && !o.hasCourse && !o.stale && o.label[0] == 0);
    GpsState g0; gps_init(&g0); position_resolve(&s, &g0, &lp, 99999999, &o); CHECK(o.valid);        // GPSが無くても使える
    CHECK(o.latDeg == 10.5); }

  // ---- 取得元の解決: 固定位置 ----
  { SettingsMgr s; settings_init(&s, NULL); settings_set_position_source(&s, POSITION_SOURCE_FIXED);
    GpsState g; gps_init(&g); LastPosMgr lp; last_pos_init(&lp, NULL); OwnPos o;
    int unset = -1; for (int i = 0; i < FIXED_POSITION_COUNT; i++) if (!fixed_position_set(i)) { unset = i; break; }
    if (unset >= 0) { settings_set_fixed_index(&s, (uint8_t)unset); position_resolve(&s, &g, &lp, 1000, &o); CHECK(!o.valid && o.reason == POS_FIXED_UNSET && o.source == POSITION_SOURCE_FIXED); }
    if (firstSet >= 0) {
      settings_set_fixed_index(&s, (uint8_t)firstSet); position_resolve(&s, &g, &lp, 1000, &o);
      const FixedPosition* f = &FIXED_POSITIONS[firstSet];
      CHECK(o.valid && o.latDeg == f->latDeg && o.lonDeg == f->lonDeg && o.hasAlt == f->hasAlt && !strcmp(o.label, f->name) && !o.hasSpeed);
      if (f->hasAlt) CHECK(o.altM == f->altM); } }

  // ---- 送信内容 ----
  { SettingsMgr s; settings_init(&s, NULL); settings_set_course_speed(&s, true); settings_set_altitude(&s, true);
    GpsState g; gps_init(&g); fix(g, 1000); LpMock mk; mk.has = true; mk.stored = { true, 10.5, 20.5, true, 77.0 };
    LastPosMgr lp; LastPosBackend b = lpbe(&mk); last_pos_init(&lp, &b); OwnPos o; TxComposed* c = new TxComposed; AprsPacket p;
    position_resolve(&s, &g, &lp, 1500, &o); CHECK(tx_compose(&s, &o, c) == TXC_OK && c->source == POSITION_SOURCE_GPS);
    CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p) && p.hasSpeed && p.speedKnots == 10 && p.hasAltitude);
    settings_set_position_source(&s, POSITION_SOURCE_LAST); position_resolve(&s, &g, &lp, 1500, &o);
    CHECK(tx_compose(&s, &o, c) == TXC_OK && c->source == POSITION_SOURCE_LAST && !c->usedStaleFix);
    CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p));
    NEAR(p.latDeg, 10.5, 0.001); NEAR(p.lonDeg, 20.5, 0.001);                                       // GPSではなく保存位置で送る
    CHECK(!p.hasSpeed && !p.hasCourse && p.hasAltitude); NEAR(p.altitudeM, 77.0, 0.4);              // 進行方向・速度は付けない。高度は保存位置のもの
    settings_set_altitude(&s, false); position_resolve(&s, &g, &lp, 1500, &o);
    CHECK(tx_compose(&s, &o, c) == TXC_OK && aprs_parse_frame(c->frame, c->frameLen - 2, &p) && !p.hasAltitude);   // 高度オフなら付けない
    if (firstSet >= 0) {
      settings_set_position_source(&s, POSITION_SOURCE_FIXED); settings_set_fixed_index(&s, (uint8_t)firstSet); settings_set_altitude(&s, true);
      position_resolve(&s, &g, &lp, 1500, &o); CHECK(tx_compose(&s, &o, c) == TXC_OK && c->source == POSITION_SOURCE_FIXED);
      CHECK(aprs_parse_frame(c->frame, c->frameLen - 2, &p)); NEAR(p.latDeg, FIXED_POSITIONS[firstSet].latDeg, 0.001);
      CHECK(p.hasAltitude == FIXED_POSITIONS[firstSet].hasAlt && !p.hasSpeed); }
    // 位置がないときは作らない
    GpsState g0; gps_init(&g0); LastPosMgr lp0; last_pos_init(&lp0, NULL);
    settings_set_position_source(&s, POSITION_SOURCE_LAST); position_resolve(&s, &g0, &lp0, 1500, &o); CHECK(tx_compose(&s, &o, c) == TXC_NO_POSITION);
    settings_set_position_source(&s, POSITION_SOURCE_GPS); position_resolve(&s, &g0, &lp0, 1500, &o); CHECK(tx_compose(&s, &o, c) == TXC_NO_POSITION);
    CHECK(tx_compose(&s, NULL, c) == TXC_NO_POSITION);
    delete c; }

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
