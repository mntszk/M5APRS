// logger_sd.cpp
#include "logger_sd.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"      // SD_CS_PIN

static bool g_mounted = false;

static void unmount() {
  if (g_mounted) SD.end();
  g_mounted = false;
}

static bool sd_append(void*, const char* dir, const char* name, const char* header, const char* data, size_t len) {
  if (!g_mounted) {
    if (!SD.begin(SD_CS_PIN, SPI, LOGGER_SD_FREQ)) return false;     // カードなし・初期化失敗
    g_mounted = true;
  }
  if (!SD.exists(dir) && !SD.mkdir(dir)) { unmount(); return false; }

  char path[96];
  if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return false;

  File f = SD.open(path, FILE_APPEND);
  if (!f) { unmount(); return false; }
  bool ok = true;
  if (header && f.size() == 0) {
    size_t hl = strlen(header);
    ok = (f.write((const uint8_t*)header, hl) == hl);
  }
  if (ok) ok = (f.write((const uint8_t*)data, len) == len);
  f.close();                                 // 毎回閉じる (電源断で失う分を最小にする)
  if (!ok) unmount();
  return ok;
}

static bool mount() {
  if (!g_mounted) {
    if (!SD.begin(SD_CS_PIN, SPI, LOGGER_SD_FREQ)) return false;
    g_mounted = true;
  }
  return true;
}

static bool paths(const char* dir, const char* name, char* path, size_t pn, char* tmp, size_t tn) {
  if (snprintf(path, pn, "%s/%s", dir, name) >= (int)pn) return false;
  return snprintf(tmp, tn, "%s/%s.tmp", dir, name) < (int)tn;
}

bool logger_sd_file_begin(const char* dir, const char* name) {
  char path[96], tmp[100];
  if (!paths(dir, name, path, sizeof(path), tmp, sizeof(tmp)) || !mount()) return false;
  if (!SD.exists(dir) && !SD.mkdir(dir)) { unmount(); return false; }
  File f = SD.open(tmp, FILE_WRITE);                 // 空にする
  if (!f) { unmount(); return false; }
  f.close();
  return true;
}

bool logger_sd_file_append(const char* dir, const char* name, const uint8_t* data, size_t len) {
  char path[96], tmp[100];
  if (!paths(dir, name, path, sizeof(path), tmp, sizeof(tmp)) || !mount()) return false;
  File f = SD.open(tmp, FILE_APPEND);
  if (!f) { unmount(); return false; }
  bool ok = (f.write(data, len) == len);
  f.close();
  if (!ok) unmount();
  return ok;
}

bool logger_sd_file_commit(const char* dir, const char* name) {
  char path[96], tmp[100];
  if (!paths(dir, name, path, sizeof(path), tmp, sizeof(tmp)) || !mount()) return false;
  if (SD.exists(path) && !SD.remove(path)) { unmount(); return false; }
  if (!SD.rename(tmp, path)) { unmount(); return false; }
  return true;
}

size_t logger_sd_read_file(const char* dir, const char* name, uint8_t* buf, size_t cap) {
  if (!mount()) return 0;
  char path[96];
  if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return 0;
  if (!SD.exists(path)) return 0;
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  size_t n = f.size();
  if (n == 0 || n > cap) { f.close(); return 0; }
  size_t got = f.read(buf, n);
  f.close();
  return got == n ? n : 0;
}

long logger_sd_file_size(const char* dir, const char* name) {
  if (!mount()) return -1;
  char path[96];
  if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return -2;
  if (!SD.exists(path)) return -2;
  File f = SD.open(path, FILE_READ);
  if (!f) return -1;
  long n = (long)f.size();
  f.close();
  return n;
}

const LogSink* logger_sd_sink() {
  static LogSink s = { 0, sd_append };
  return &s;
}
