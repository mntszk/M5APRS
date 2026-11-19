// last_pos_nvs.h
// last_pos.h の保存先 (LastPosBackend) を、ESP32のPreferences(不揮発領域)で実現する。ESP32用。
// 使い方 (setup内): last_pos_init(&lastPos, last_pos_nvs_backend());
#ifndef LAST_POS_NVS_H
#define LAST_POS_NVS_H

#include "last_pos.h"

const LastPosBackend* last_pos_nvs_backend();

#endif
