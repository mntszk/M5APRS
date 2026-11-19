// afsk_rx_core.cpp
#include "afsk_rx_core.h"

#include <math.h>
#include <string.h>

#define RX_MIN_FRAME_BYTES  17     // アドレス14 + 制御1 + FCS2 (PIDなしも許す)

// FCS (CRC-16/X.25) を1バイトずつ更新
static uint16_t crc_update(uint16_t crc, uint8_t b) {
  crc ^= b;
  for (int i = 0; i < 8; i++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
  return crc;
}

static uint32_t phase_inc(uint32_t hz, uint32_t sr) {
  return (uint32_t)((((uint64_t)hz) << 32) / sr);
}

void afsk_rx_reset(AfskRx* r) {
  memset(r->mI, 0, sizeof(r->mI)); memset(r->mQ, 0, sizeof(r->mQ));
  memset(r->sI, 0, sizeof(r->sI)); memset(r->sQ, 0, sizeof(r->sQ));
  r->sumMI = r->sumMQ = r->sumSI = r->sumSQ = 0;
  r->phMark = r->phSpace = 0;
  r->wpos = 0;
  r->dcAcc = 0; r->dcInit = false;
  r->levelMark = r->levelSpace = r->cfg.toneLevelInit ? r->cfg.toneLevelInit : 1;
  r->pll = 0;
  r->curTone = true; r->prevBitTone = true;
  r->ones = 0; r->inFrame = false; r->nbits = 0;
  r->outLen = 0;
  r->hang = 0;
  r->peak = 0; r->clipHang = 0; r->peakMarkE = r->peakSpaceE = 0; r->syncFlags = 0;
}

bool afsk_rx_init(AfskRx* r, const AfskRxConfig* cfg) {
  if (!r || !cfg) return false;
  if (cfg->sampleRateHz == 0 || cfg->baud == 0 || cfg->markHz == 0 || cfg->spaceHz == 0) return false;
  if (cfg->markHz * 2u >= cfg->sampleRateHz || cfg->spaceHz * 2u >= cfg->sampleRateHz) return false;
  if (cfg->sampleRateHz < (uint32_t)cfg->baud * 4u) return false;     // 1ビットあたり最低4サンプル
  uint32_t win = (cfg->sampleRateHz + cfg->baud / 2u) / cfg->baud;      // 四捨五入
  if (win < 2 || win > AFSK_RX_MAX_WINDOW) return false;

  memset(r, 0, sizeof(*r));
  r->cfg = *cfg;
  r->win = (uint16_t)win;
  for (int i = 0; i < 256; i++) r->sinTab[i] = (int16_t)lround(1024.0 * sin(2.0 * 3.14159265358979323846 * i / 256.0));
  r->carrierMin = cfg->carrierMinEnergy ? cfg->carrierMinEnergy : 50000u;
  uint32_t hangMs = cfg->carrierHangMs ? cfg->carrierHangMs : 50u;
  r->hangSamples = (uint32_t)(((uint64_t)cfg->sampleRateHz * hangMs) / 1000u);
  if (r->hangSamples < 1) r->hangSamples = 1;
  r->incMark  = phase_inc(cfg->markHz,  cfg->sampleRateHz);
  r->incSpace = phase_inc(cfg->spaceHz, cfg->sampleRateHz);
  afsk_rx_reset(r);
  return true;
}

// ---------------------------------------------------------------
#define FLOOR_E 64u                 // これより弱いトーンは比の平均に入れない

// HDLC: 1ビット(NRZI復号後)を処理。FCSが正しいフレームが完成したら true
// ---------------------------------------------------------------
static bool hdlc_bit(AfskRx* r, int b) {
  if (b) {
    if (r->ones < 255) r->ones++;
    if (r->ones == 7) {                      // 1が7個以上: アボート
      if (r->inFrame) r->aborts++;
      r->inFrame = false;
    }
    if (r->inFrame) {
      if (r->nbits >= (AFSK_RX_FRAME_MAX * 8 - 1)) { r->inFrame = false; r->overflows++; return false; }
      if ((r->nbits & 7) == 0) r->work[r->nbits >> 3] = 0;
      r->work[r->nbits >> 3] |= (uint8_t)(1u << (r->nbits & 7));
      r->nbits++;
    }
    return false;
  }

  // b == 0
  uint8_t ones = r->ones;
  r->ones = 0;
  if (ones >= 7) return false;               // アボート中: 次のフラグまで待つ
  if (ones == 6) {                           // フラグ
    bool done = false;
    r->flagsSeen++;
    // 連続フラグの数: フレームの中身を挟まずに続いたフラグ (nbits==7 は、フラグ自身の7ビットだけ)
    if (r->inFrame && r->nbits == 7) { if (r->syncFlags < 255) r->syncFlags++; }
    else r->syncFlags = 1;
    if (r->inFrame && r->nbits >= 7) {
      // フラグの先頭の0と6個の1を、データとして取り込んでしまっているので取り除く
      uint16_t n = (uint16_t)(r->nbits - 7);
      if ((n & 7) == 0 && (n >> 3) >= RX_MIN_FRAME_BYTES) {
        size_t bytes = n >> 3;
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < bytes; i++) crc = crc_update(crc, r->work[i]);
        // 最終XORをかける前の内部値で比較する (正しいフレームなら 0xF0B8。最終XOR後の 0x0F47 と同じ意味)
        if (crc == 0xF0B8) {
          memcpy(r->out, r->work, bytes - 2);
          r->outLen = bytes - 2;
          r->framesOk++;
          r->lastOkMarkE = r->avgMarkE; r->lastOkSpaceE = r->avgSpaceE;
          done = true;
        } else {
          r->fcsErrors++;
        }
      } else if (n >= 8) {
        r->fragments++;                      // 長さが不正(バイト単位でない、短すぎる): 雑音のかけら
      }
    }
    r->inFrame = true;
    r->nbits = 0;
    return done;
  }
  if (ones == 5) return false;               // 挿入された0。捨てる
  if (r->inFrame) {
    if (r->nbits >= (AFSK_RX_FRAME_MAX * 8 - 1)) { r->inFrame = false; r->overflows++; return false; }
    if ((r->nbits & 7) == 0) r->work[r->nbits >> 3] = 0;
    r->nbits++;                              // 0を記録 (ビットは0のまま)
  }
  return false;
}

// ---------------------------------------------------------------
// 1サンプルの処理
// ---------------------------------------------------------------
bool afsk_rx_process(AfskRx* r, int16_t sample) {
  int32_t x = sample;
  if (x > 4095) x = 4095;
  if (x < -4095) x = -4095;

  // 直流分の除去 (時定数 約256サンプル。最初のサンプルで初期化して立ち上がりを速くする)
  if (!r->dcInit) { r->dcAcc = x * 256; r->dcInit = true; }
  r->dcAcc += x - (r->dcAcc >> 8);
  int32_t y = x - (r->dcAcc >> 8);

  // 相関 (sin/cos との積を窓の長さぶん足し込む)
  uint8_t im = (uint8_t)(r->phMark >> 24),  is = (uint8_t)(r->phSpace >> 24);
  r->phMark += r->incMark; r->phSpace += r->incSpace;
  int32_t pMI = y * r->sinTab[(uint8_t)(im + 64)], pMQ = y * r->sinTab[im];
  int32_t pSI = y * r->sinTab[(uint8_t)(is + 64)], pSQ = y * r->sinTab[is];
  uint16_t w = r->wpos;
  r->sumMI += pMI - r->mI[w]; r->mI[w] = pMI;
  r->sumMQ += pMQ - r->mQ[w]; r->mQ[w] = pMQ;
  r->sumSI += pSI - r->sI[w]; r->sI[w] = pSI;
  r->sumSQ += pSQ - r->sQ[w]; r->sQ[w] = pSQ;
  if (++r->wpos >= r->win) r->wpos = 0;

  // 各トーンの強さ (エネルギー)
  int32_t a = r->sumMI >> 10, b = r->sumMQ >> 10, c = r->sumSI >> 10, d = r->sumSQ >> 10;
  uint32_t em = (uint32_t)(a * a) + (uint32_t)(b * b);
  uint32_t es = (uint32_t)(c * c) + (uint32_t)(d * d);

  // 信号検出
  if (em > r->carrierMin || es > r->carrierMin) r->hang = r->hangSamples;
  else if (r->hang) { r->hang--; if (!r->hang) r->syncFlags = 0; }
  // 信号がないときは、組み立て中のフレームを数えずに捨てる (下でビットもHDLCに渡さない)
  if (!r->hang && (r->inFrame || r->ones)) { r->inFrame = false; r->nbits = 0; r->ones = 0; }

  // 監視用の値 (ピークホールド。上がるのは即時、下がるのは約100msかけて)
  {
    int32_t ay = y < 0 ? -y : y;
    if (ay > r->peak) r->peak = ay; else if (r->peak > 0) r->peak -= (r->peak >> 10) + 1;
    // 振り切れ: ADCの端に張り付いていて、しかも信号が振れているときだけ。
    // 入力が開放やバイアス不良で端に張り付いているだけ (直流分が範囲外。振れていない) なら、振り切れとはしない。
    if ((x >= 2000 || x <= -2000) && (y >= 300 || y <= -300)) r->clipHang = r->cfg.sampleRateHz / 10;
    else if (r->clipHang) r->clipHang--;
    // トーンごとの強さは、そのトーンが明らかに優勢なときだけ拾う。2200Hz は1ビットの窓 (11サンプル) に整数周期で入らないので、
    // MARK の間も SPACE の相関に MARK の約4割が漏れて見え、SPACE だけのときも ±9% ほど揺れる。
    // バー用はピークホールド、比の計算用は優勢なときの平均 (0.13.3。以前はピークで比を出していて、真の比 2 が 1.6、6 が 2.4 と小さく出ていた)
    if (em > (es << 2) && em > r->peakMarkE) r->peakMarkE = em; else r->peakMarkE -= (r->peakMarkE >> 9) + (r->peakMarkE ? 1u : 0u);
    if (es > (em << 2) && es > r->peakSpaceE) r->peakSpaceE = es; else r->peakSpaceE -= (r->peakSpaceE >> 9) + (r->peakSpaceE ? 1u : 0u);
    if (em > (es << 3) && em > FLOOR_E) r->avgMarkE += ((int32_t)(em >> 1) - (int32_t)(r->avgMarkE >> 1)) / 32;
    if (es > (em << 3) && es > FLOOR_E) r->avgSpaceE += ((int32_t)(es >> 1) - (int32_t)(r->avgSpaceE >> 1)) / 32;
  }

  // 基準値の更新: そのトーンが明らかに優勢なときだけ。上がるのは速く、下がるのは遅く(ピークホールド)
  const uint32_t FLOOR = 64;
  if (em > FLOOR && em > (es << 2)) {
    if (em > r->levelMark) r->levelMark += (em - r->levelMark) >> 2;
    else                   r->levelMark -= (r->levelMark - em) >> 10;
  } else if (es > FLOOR && es > (em << 2)) {
    if (es > r->levelSpace) r->levelSpace += (es - r->levelSpace) >> 2;
    else                    r->levelSpace -= (r->levelSpace - es) >> 10;
  }
  if (r->levelMark < 1) r->levelMark = 1;
  if (r->levelSpace < 1) r->levelSpace = 1;

  // 判定: em/levelMark > es/levelSpace ⇔ em*levelSpace > es*levelMark
  bool tone = (int64_t)em * r->levelSpace > (int64_t)es * r->levelMark;
  if (em == 0 && es == 0) tone = r->curTone;

  // ビット同期: 判定の切り替わりは、ビットの境目の半ビット後(窓が半分ずつ新旧のとき)に起きる。
  // 判定の瞬間はその半ビット後(= pll が一周する時)なので、切り替わり時の pll は一周の半分になるはず。
  const uint32_t sr = r->cfg.sampleRateHz;
  if (tone != r->curTone) {
    int32_t err = (int32_t)r->pll - (int32_t)(sr / 2);
    r->pll = (uint32_t)((int32_t)r->pll - err / 4);
    r->curTone = tone;
  }

  bool got = false;
  r->pll += r->cfg.baud;
  if (r->pll >= sr) {
    r->pll -= sr;
    int bit = (r->curTone == r->prevBitTone) ? 1 : 0;   // NRZI: 変化なし=1, 変化=0
    r->prevBitTone = r->curTone;
    if (r->hang) got = hdlc_bit(r, bit);
  }
  return got;
}

void afsk_rx_discard_frame(AfskRx* r) {
  r->inFrame = false; r->nbits = 0; r->ones = 0;
}

bool afsk_rx_carrier(const AfskRx* r) { return r->hang > 0; }

void afsk_rx_monitor(const AfskRx* r, AfskRxMonitor* o) {
  o->peak = (uint16_t)(r->peak > 4095 ? 4095 : r->peak);
  o->clipped = r->clipHang > 0;
  { int32_t dc = r->dcAcc >> 8; o->dcOffset = (int16_t)(dc > 4095 ? 4095 : (dc < -4095 ? -4095 : dc)); }
  // 振幅Aの純音の相関出力は A*win/2 なので、強さ(エネルギー)の平方根を win/2 で割ると A
  double k = 2.0 / (double)r->win;
  double m = sqrt((double)r->peakMarkE) * k, s = sqrt((double)r->peakSpaceE) * k;
  o->markAmp = (uint16_t)(m > 4095.0 ? 4095 : m);
  o->spaceAmp = (uint16_t)(s > 4095.0 ? 4095 : s);
  o->carrier = r->hang > 0;
  o->syncFlags = r->syncFlags;
  o->inFrame = r->inFrame;
  o->frameBytes = r->inFrame ? (uint16_t)(r->nbits >> 3) : 0;
  o->flagsSeen = r->flagsSeen;
  { double am = sqrt((double)r->avgMarkE) * k, as = sqrt((double)r->avgSpaceE) * k;
    o->markAvgAmp = (uint16_t)(am > 4095.0 ? 4095 : am); o->spaceAvgAmp = (uint16_t)(as > 4095.0 ? 4095 : as); }
  double lm = sqrt((double)r->lastOkMarkE) * k, ls = sqrt((double)r->lastOkSpaceE) * k;
  o->lastOkMarkAmp = (uint16_t)(lm > 4095.0 ? 4095 : lm);
  o->lastOkSpaceAmp = (uint16_t)(ls > 4095.0 ? 4095 : ls);
}

const uint8_t* afsk_rx_frame(const AfskRx* r, size_t* len) {
  if (len) *len = r->outLen;
  return r->out;
}
