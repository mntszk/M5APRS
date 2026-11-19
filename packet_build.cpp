// packet_build.cpp
#include "packet_build.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------
// FCS
// ---------------------------------------------------------------
uint16_t ax25_fcs(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 1) crc = (crc >> 1) ^ 0x8408;
      else         crc >>= 1;
    }
  }
  return (uint16_t)(crc ^ 0xFFFF);
}

// ---------------------------------------------------------------
// 位置の文字列
// ---------------------------------------------------------------

// 緯度 "DDMM.hhN" (8文字+NUL) または 経度 "DDDMM.hhE" (9文字+NUL) を作る。
// 度と分と分の百分の1は、全体を百分の1分単位の整数にしてから分解するため、
// 丸めで60.00分になる場合も正しく繰り上がる。
static void format_coord(double v, bool isLat, uint8_t ambiguity, char* out) {
  long total = lround(fabs(v) * 6000.0);   // 百分の1分
  int deg = (int)(total / 6000);
  int rem = (int)(total % 6000);
  int min = rem / 100;
  int hun = rem % 100;
  int minIdx;                              // 「分」の十の位の位置
  if (isLat) {
    snprintf(out, 9, "%02d%02d.%02d%c", deg, min, hun, v >= 0 ? 'N' : 'S');
    minIdx = 2;
  } else {
    snprintf(out, 10, "%03d%02d.%02d%c", deg, min, hun, v >= 0 ? 'E' : 'W');
    minIdx = 3;
  }
  // 曖昧化: 下位の桁をスペースに置き換える(APRS101)
  //   1: 分の百分の1の位  2: 分の十分の1の位まで  3: 分の1の位まで  4: 分の十の位まで
  if (ambiguity >= 1) out[minIdx + 4] = ' ';
  if (ambiguity >= 2) out[minIdx + 3] = ' ';
  if (ambiguity >= 3) out[minIdx + 1] = ' ';
  if (ambiguity >= 4) out[minIdx + 0] = ' ';
}

// 高度 "/A=aaaaaa"(フィート)。負の値は "/A=-aaaaa"。9文字+NUL
static void format_altitude(double meters, char* out) {
  long ft = lround(meters / 0.3048);
  if (ft > 999999) ft = 999999;
  if (ft < -99999) ft = -99999;
  if (ft >= 0) snprintf(out, 10, "/A=%06ld", ft);
  else         snprintf(out, 10, "/A=-%05ld", -ft);
}

size_t aprs_build_position_info(const PositionReport& rpt, char dti,
                                uint8_t* out, size_t outMax) {
  if (!out) return 0;
  if (!(fabs(rpt.latDeg) <= 90.0) || !(fabs(rpt.lonDeg) <= 180.0)) return 0;  // NaNも弾く
  if (rpt.ambiguity > 4) return 0;

  char lat[9], lon[10];
  format_coord(rpt.latDeg, true,  rpt.ambiguity, lat);
  format_coord(rpt.lonDeg, false, rpt.ambiguity, lon);

  // コメント欄の最大長は、データ拡張なしで43文字、CSE/SPDありで36文字(APRS101)。
  // 高度(/A=aaaaaa、9文字)もコメント欄の一部として数える。
  const int commentLimit = rpt.hasCourseSpeed ? 36 : 43;
  char comment[48];
  int n = 0;
  if (rpt.hasAltitude) {
    format_altitude(rpt.altitudeM, comment);
    n = (int)strlen(comment);
  }
  if (rpt.comment) {
    for (const char* c = rpt.comment; *c && n < commentLimit; c++) {
      unsigned char ch = (unsigned char)*c;
      // 表示できるASCIIのみ。'|' と '~' はテレメトリ用に予約されているので使わない
      if (ch >= 0x20 && ch <= 0x7E && ch != '|' && ch != '~') comment[n++] = (char)ch;
    }
  }
  comment[n] = '\0';

  // 進行方向・速度 "CCC/SSS"
  char ext[8] = "";
  if (rpt.hasCourseSpeed) {
    unsigned course = rpt.courseDeg % 360;
    unsigned speed  = rpt.speedKnots > 999 ? 999 : rpt.speedKnots;
    if (course == 0 && speed > 0) course = 360;   // 動いていて真北なら360(000は「不明」と区別する)
    snprintf(ext, sizeof(ext), "%03u/%03u", course, speed);
  }

  size_t need = 1 + 8 + 1 + 9 + 1 + strlen(ext) + (size_t)n;
  if (need > outMax || need > APRS_INFO_MAX) return 0;

  size_t pos = 0;
  out[pos++] = (uint8_t)dti;
  memcpy(out + pos, lat, 8);               pos += 8;
  out[pos++] = (uint8_t)rpt.symTable;
  memcpy(out + pos, lon, 9);               pos += 9;
  out[pos++] = (uint8_t)rpt.symCode;
  memcpy(out + pos, ext, strlen(ext));     pos += strlen(ext);
  memcpy(out + pos, comment, (size_t)n);   pos += (size_t)n;
  return pos;
}

// ---------------------------------------------------------------
// AX.25
// ---------------------------------------------------------------

// "CALL" または "CALL-N" を、コールサイン(大文字、最大6文字)とSSIDに分ける。
static bool parse_call(const char* s, size_t len, char call[7], int* ssid) {
  size_t dash = len;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '-') { dash = i; break; }
  }
  if (dash < 1 || dash > 6) return false;
  for (size_t i = 0; i < dash; i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    call[i] = c;
  }
  call[dash] = '\0';
  int v = 0;
  if (dash < len) {
    size_t digits = len - dash - 1;
    if (digits < 1 || digits > 2) return false;
    for (size_t i = dash + 1; i < len; i++) {
      if (s[i] < '0' || s[i] > '9') return false;
      v = v * 10 + (s[i] - '0');
    }
    if (v > 15) return false;
  }
  *ssid = v;
  return true;
}

// アドレス1つ(7バイト)を書く。コールサインは1ビット左へずらし、6文字に満たない分はスペース。
// SSIDバイト: 0b011SSSS0 (予約ビット=1、C/Hビット=0)。最後のアドレスだけ最下位ビットを1にする。
static void put_address(uint8_t* out, const char* call, int ssid, bool last) {
  size_t n = strlen(call);
  for (size_t i = 0; i < 6; i++) {
    char c = (i < n) ? call[i] : ' ';
    out[i] = (uint8_t)((uint8_t)c << 1);
  }
  out[6] = (uint8_t)(0x60 | ((ssid & 0x0F) << 1) | (last ? 1 : 0));
}

size_t ax25_build_ui_frame(const Ax25Params& p, const uint8_t* info, size_t infoLen,
                           uint8_t* out, size_t outMax) {
  if (!out || !p.srcCall || !p.destCall || (!info && infoLen > 0)) return 0;
  if (p.srcSsid > 15 || p.destSsid > 15) return 0;

  char srcCall[7], destCall[7];
  int  dummy = 0;
  size_t sl = strlen(p.srcCall), dl = strlen(p.destCall);
  // コールサイン部分にSSIDは含めない(含まれていたら不正として扱う)
  if (memchr(p.srcCall, '-', sl) || memchr(p.destCall, '-', dl)) return 0;
  if (!parse_call(p.srcCall, sl, srcCall, &dummy)) return 0;
  if (!parse_call(p.destCall, dl, destCall, &dummy)) return 0;

  // デジパスを分解する
  char digiCall[AX25_MAX_DIGIS][7];
  int  digiSsid[AX25_MAX_DIGIS];
  int  digiCount = 0;
  if (p.digipath && p.digipath[0] != '\0') {
    const char* s = p.digipath;
    while (*s) {
      const char* e = s;
      while (*e && *e != ',') e++;
      const char* b = s;                         // 前後のスペースを除く
      size_t len = (size_t)(e - s);
      while (len > 0 && *b == ' ') { b++; len--; }
      while (len > 0 && b[len - 1] == ' ') len--;
      if (digiCount >= AX25_MAX_DIGIS) return 0;
      if (!parse_call(b, len, digiCall[digiCount], &digiSsid[digiCount])) return 0;
      digiCount++;
      s = (*e == ',') ? e + 1 : e;
    }
  }

  size_t need = 7 * (2 + (size_t)digiCount) + 2 + infoLen + 2;
  if (need > outMax || need > AX25_FRAME_MAX) return 0;

  size_t pos = 0;
  put_address(out + pos, destCall, p.destSsid, false);  pos += 7;
  put_address(out + pos, srcCall,  p.srcSsid,  digiCount == 0);  pos += 7;
  for (int i = 0; i < digiCount; i++) {
    put_address(out + pos, digiCall[i], digiSsid[i], i == digiCount - 1);
    pos += 7;
  }
  out[pos++] = 0x03;   // 制御: UIフレーム
  out[pos++] = 0xF0;   // PID: レイヤ3なし
  if (infoLen > 0) { memcpy(out + pos, info, infoLen); pos += infoLen; }

  uint16_t fcs = ax25_fcs(out, pos);
  out[pos++] = (uint8_t)(fcs & 0xFF);
  out[pos++] = (uint8_t)(fcs >> 8);
  return pos;
}
