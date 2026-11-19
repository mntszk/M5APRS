// settings_nvs.h
// settings.h の保存先 (SettingsBackend) を、ESP32のPreferences(不揮発領域)で実現する。
// ESP32のArduinoコア用。パソコンのテストには入れない。
//
// 使い方 (setup内):
//   SettingsMgr settings;
//   settings_init(&settings, settings_nvs_backend());   // 保存値を読む
// loop内 (送信中・復調中でないとき):
//   settings_commit(&settings, busy);

#ifndef SETTINGS_NVS_H
#define SETTINGS_NVS_H

#include "settings.h"

// Preferences を NVS_NAMESPACE で開き、口を返す。開けなくても口は返る(読み書きは失敗扱い=保存しない)。
const SettingsBackend* settings_nvs_backend();

#endif // SETTINGS_NVS_H
