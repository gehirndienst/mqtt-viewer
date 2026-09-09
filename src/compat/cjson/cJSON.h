// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// This project does not link dynamically against cJSON anymore but libmosquitto >= 2.1
// includes <cjson/cJSON.h> from mosquitto.h (mosquitto/libcommon_cjson.h) to declare
// mosquitto_properties_to_json(), which only needs the type name
// Providing it here to shift serving this dependency to the packaging layer rather than the build one
#ifndef MV_COMPAT_CJSON_H
#define MV_COMPAT_CJSON_H
typedef struct cJSON cJSON;
#endif
