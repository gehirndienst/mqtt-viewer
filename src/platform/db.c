// SPDX-FileCopyrightText: 2026 Nikita Smirnov <nktsmirnov@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <cjson/cJSON.h>
#include <sqlite3.h>

#include "platform/db.h"
#include "platform/log.h"

struct Db {
    sqlite3* db;
    char setting_value[1024]; // scratch buffer for db_get_setting return value
};

static const char* SCHEMA_SQL = "PRAGMA journal_mode=WAL;"
                                "CREATE TABLE IF NOT EXISTS profiles ("
                                "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "    name TEXT NOT NULL,"
                                "    host TEXT NOT NULL,"
                                "    port INTEGER DEFAULT 1883,"
                                "    protocol_version INTEGER DEFAULT 311,"
                                "    client_id TEXT DEFAULT '',"
                                "    clean_session INTEGER DEFAULT 1,"
                                "    keepalive_secs INTEGER DEFAULT 60,"
                                "    username TEXT DEFAULT '',"
                                "    password TEXT DEFAULT '',"
                                "    tls_ca_cert TEXT DEFAULT '',"
                                "    tls_client_cert TEXT DEFAULT '',"
                                "    tls_client_key TEXT DEFAULT '',"
                                "    tls_version INTEGER DEFAULT 13,"
                                "    tls_verify INTEGER DEFAULT 1,"
                                "    subscriptions TEXT DEFAULT '[{\"topic\":\"#\",\"qos\":1}]'"
                                ");"
                                "CREATE TABLE IF NOT EXISTS messages ("
                                "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "    topic TEXT NOT NULL,"
                                "    payload BLOB,"
                                "    qos INTEGER,"
                                "    retained INTEGER,"
                                "    timestamp_us INTEGER,"
                                "    broker_id INTEGER"
                                ");"
                                "CREATE TABLE IF NOT EXISTS settings ("
                                "    key TEXT PRIMARY KEY,"
                                "    value TEXT"
                                ");";


Db* db_open(const char* db_path) {
    Db* p = calloc(1, sizeof(Db));
    if (!p) {
        LOG_ERROR("db_open: out of memory");
        return NULL;
    }

    int rc = sqlite3_open(db_path, &p->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_open: sqlite3_open('%s') failed: %s", db_path, sqlite3_errmsg(p->db));
        sqlite3_close(p->db);
        free(p);
        return NULL;
    }

    char* errmsg = NULL;
    rc = sqlite3_exec(p->db, SCHEMA_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_open: schema setup failed: %s", errmsg ? errmsg : "unknown");
        sqlite3_free(errmsg);
        sqlite3_close(p->db);
        free(p);
        return NULL;
    }

    sqlite3_exec(p->db, "ALTER TABLE profiles ADD COLUMN transport INTEGER DEFAULT 0;", NULL, NULL, NULL);

    LOG_INFO("db_open: opened '%s'", db_path);
    return p;
}

void db_close(Db* db) {
    if (!db) return;
    sqlite3_close(db->db);
    free(db);
}

static char* serialize_subscriptions(const BrokerProfile* profile) {
    cJSON* arr = cJSON_CreateArray();
    if (!arr) return NULL;

    for (int i = 0; i < profile->subscription_count; i++) {
        cJSON* obj = cJSON_CreateObject();
        if (!obj) continue;
        cJSON_AddStringToObject(obj, "topic", profile->subscriptions[i].topic);
        cJSON_AddNumberToObject(obj, "qos", profile->subscriptions[i].qos);
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

static void deserialize_subscriptions(BrokerProfile* profile, const char* subs_text) {
    if (!subs_text) return;

    cJSON* arr = cJSON_Parse(subs_text);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return;
    }

    int n = cJSON_GetArraySize(arr);
    profile->subscription_count = 0;
    for (int i = 0; i < n && profile->subscription_count < MAX_PROFILE_SUBS; i++) {
        cJSON* item = cJSON_GetArrayItem(arr, i);
        const char* topic = cJSON_GetStringValue(cJSON_GetObjectItem(item, "topic"));
        cJSON* qos_obj = cJSON_GetObjectItem(item, "qos");
        if (topic) {
            ProfileSubscription* sub = &profile->subscriptions[profile->subscription_count];
            strncpy(sub->topic, topic, sizeof(sub->topic) - 1);
            sub->topic[sizeof(sub->topic) - 1] = '\0';
            sub->qos = qos_obj ? (uint8_t)qos_obj->valueint : 0;
            profile->subscription_count++;
        }
    }

    cJSON_Delete(arr);
}

bool db_save_profile(Db* db, BrokerProfile* profile) {
    // TODO: horrible but it works
    if (!db || !profile) return false;

    char* subs_json = serialize_subscriptions(profile);

    const char* sql = "INSERT OR REPLACE INTO profiles "
                      "(id, name, host, port, protocol_version, client_id, clean_session, keepalive_secs,"
                      " username, password, tls_ca_cert, tls_client_cert, tls_client_key,"
                      " tls_version, tls_verify, subscriptions, transport)"
                      " VALUES (NULLIF(?1,0), ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17);";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_save_profile: prepare failed: %s", sqlite3_errmsg(db->db));
        cJSON_free(subs_json);
        return false;
    }

    sqlite3_bind_int(stmt, 1, profile->id);
    sqlite3_bind_text(stmt, 2, profile->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, profile->host, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, profile->port);
    sqlite3_bind_int(stmt, 5, profile->protocol_version);
    sqlite3_bind_text(stmt, 6, profile->client_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, profile->clean_session ? 1 : 0);
    sqlite3_bind_int(stmt, 8, profile->keepalive_secs);
    sqlite3_bind_text(stmt, 9, profile->username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, profile->password, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, profile->tls_ca_cert, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, profile->tls_client_cert, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, profile->tls_client_key, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 14, profile->tls_version);
    sqlite3_bind_int(stmt, 15, profile->tls_verify ? 1 : 0);
    sqlite3_bind_text(stmt, 16, subs_json ? subs_json : "[]", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 17, profile->transport);

    bool was_new = (profile->id == 0);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE && was_new) {
        profile->id = (int)sqlite3_last_insert_rowid(db->db);
    }
    sqlite3_finalize(stmt);
    cJSON_free(subs_json);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("db_save_profile: step failed: %s", sqlite3_errmsg(db->db));
        return false;
    }
    return true;
}

bool db_delete_profile(Db* db, int profile_id) {
    if (!db) return false;

    const char* sql = "DELETE FROM profiles WHERE id = ?1;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_delete_profile: prepare failed: %s", sqlite3_errmsg(db->db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, profile_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("db_delete_profile: step failed: %s", sqlite3_errmsg(db->db));
        return false;
    }
    return true;
}

int db_load_profiles(Db* db, BrokerProfile* profiles, int max_count) {
    if (!db || !profiles || max_count <= 0) return 0;

    const char* sql = "SELECT id, name, host, port, protocol_version, client_id, clean_session,"
                      "       keepalive_secs, username, password, tls_ca_cert, tls_client_cert,"
                      "       tls_client_key, tls_version, tls_verify, subscriptions, transport"
                      " FROM profiles ORDER BY id;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_load_profiles: prepare failed: %s", sqlite3_errmsg(db->db));
        return 0;
    }

    int count = 0;
    while (count < max_count && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BrokerProfile* p = &profiles[count];
        memset(p, 0, sizeof(*p));

        p->id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        const char* host = (const char*)sqlite3_column_text(stmt, 2);
        p->port = (uint16_t)sqlite3_column_int(stmt, 3);
        p->protocol_version = sqlite3_column_int(stmt, 4);
        const char* cid = (const char*)sqlite3_column_text(stmt, 5);
        p->clean_session = sqlite3_column_int(stmt, 6) != 0;
        p->keepalive_secs = (uint16_t)sqlite3_column_int(stmt, 7);
        const char* user = (const char*)sqlite3_column_text(stmt, 8);
        const char* pass = (const char*)sqlite3_column_text(stmt, 9);
        const char* ca = (const char*)sqlite3_column_text(stmt, 10);
        const char* ccert = (const char*)sqlite3_column_text(stmt, 11);
        const char* ckey = (const char*)sqlite3_column_text(stmt, 12);
        p->tls_version = sqlite3_column_int(stmt, 13);
        p->tls_verify = sqlite3_column_int(stmt, 14) != 0;
        const char* subs = (const char*)sqlite3_column_text(stmt, 15);
        p->transport = sqlite3_column_int(stmt, 16);

        if (name) strncpy(p->name, name, sizeof(p->name) - 1);
        if (host) strncpy(p->host, host, sizeof(p->host) - 1);
        if (cid) strncpy(p->client_id, cid, sizeof(p->client_id) - 1);
        if (user) strncpy(p->username, user, sizeof(p->username) - 1);
        if (pass) strncpy(p->password, pass, sizeof(p->password) - 1);
        if (ca) strncpy(p->tls_ca_cert, ca, sizeof(p->tls_ca_cert) - 1);
        if (ccert) strncpy(p->tls_client_cert, ccert, sizeof(p->tls_client_cert) - 1);
        if (ckey) strncpy(p->tls_client_key, ckey, sizeof(p->tls_client_key) - 1);

        deserialize_subscriptions(p, subs);
        count++;
    }

    sqlite3_finalize(stmt);
    return count;
}

bool db_set_setting(Db* db, const char* key, const char* value) {
    if (!db || !key || !value) return false;

    const char* sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?1, ?2);";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_set_setting: prepare failed: %s", sqlite3_errmsg(db->db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("db_set_setting: step failed: %s", sqlite3_errmsg(db->db));
        return false;
    }
    return true;
}

const char* db_get_setting(Db* db, const char* key, const char* fallback) {
    if (!db || !key) return fallback;

    const char* sql = "SELECT value FROM settings WHERE key = ?1;";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_get_setting: prepare failed: %s", sqlite3_errmsg(db->db));
        return fallback;
    }

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        const char* val = (const char*)sqlite3_column_text(stmt, 0);
        if (val) {
            strncpy(db->setting_value, val, sizeof(db->setting_value) - 1);
            db->setting_value[sizeof(db->setting_value) - 1] = '\0';
        } else {
            db->setting_value[0] = '\0';
        }
        sqlite3_finalize(stmt);
        return db->setting_value;
    }

    sqlite3_finalize(stmt);
    return fallback;
}

bool db_save_messages(Db* db, const MessageRecord* records, int count) {
    if (!db || !records || count <= 0) return false;

    const char* sql = "INSERT INTO messages (topic, payload, qos, retained, timestamp_us, broker_id)"
                      " VALUES (?1, ?2, ?3, ?4, ?5, ?6);";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_save_messages: prepare failed: %s", sqlite3_errmsg(db->db));
        return false;
    }

    sqlite3_exec(db->db, "BEGIN;", NULL, NULL, NULL);

    bool ok = true;
    for (int i = 0; i < count; i++) {
        const MessageRecord* r = &records[i];
        sqlite3_bind_text(stmt, 1, r->topic, -1, SQLITE_STATIC);
        if (r->preview[0] != '\0') {
            sqlite3_bind_text(stmt, 2, r->preview, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 2);
        }
        sqlite3_bind_int(stmt, 3, r->qos);
        sqlite3_bind_int(stmt, 4, r->retained ? 1 : 0);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)r->timestamp_us);
        sqlite3_bind_int64(stmt, 6, (sqlite3_int64)r->broker_id);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            LOG_ERROR("db_save_messages: step failed at record %d: %s", i, sqlite3_errmsg(db->db));
            ok = false;
            break;
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_exec(db->db, "COMMIT;", NULL, NULL, NULL);
    } else {
        sqlite3_exec(db->db, "ROLLBACK;", NULL, NULL, NULL);
    }
    return ok;
}

int db_load_messages(Db* db, MessageRecord* records, int max_count) {
    if (!db || !records || max_count <= 0) return 0;

    const char* sql = "SELECT topic, qos, retained, timestamp_us, broker_id, payload"
                      " FROM messages"
                      " ORDER BY timestamp_us DESC"
                      " LIMIT ?1;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_load_messages: prepare failed: %s", sqlite3_errmsg(db->db));
        return 0;
    }

    sqlite3_bind_int(stmt, 1, max_count);

    int count = 0;
    while (count < max_count && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MessageRecord* r = &records[count];
        memset(r, 0, sizeof(*r));

        const char* topic = (const char*)sqlite3_column_text(stmt, 0);
        if (topic) {
            strncpy(r->topic, topic, sizeof(r->topic) - 1);
            r->topic[sizeof(r->topic) - 1] = '\0';
        }
        r->qos = (uint8_t)sqlite3_column_int(stmt, 1);
        r->retained = sqlite3_column_int(stmt, 2) != 0;
        r->timestamp_us = (uint64_t)sqlite3_column_int64(stmt, 3);
        r->broker_id = (uint32_t)sqlite3_column_int64(stmt, 4);
        const char* preview = (const char*)sqlite3_column_text(stmt, 5);
        if (preview) {
            strncpy(r->preview, preview, sizeof(r->preview) - 1);
            r->preview[sizeof(r->preview) - 1] = '\0';
            r->payload_len = (uint32_t)strlen(r->preview);
        }
        r->payload = NULL;
        count++;
    }

    sqlite3_finalize(stmt);
    return count;
}

bool db_trim_messages(Db* db, int max_rows) {
    if (!db || max_rows < 0) return false;

    const char* sql = "DELETE FROM messages WHERE id NOT IN ("
                      "  SELECT id FROM messages ORDER BY timestamp_us DESC LIMIT ?1"
                      ");";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("db_trim_messages: prepare failed: %s", sqlite3_errmsg(db->db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, max_rows);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("db_trim_messages: step failed: %s", sqlite3_errmsg(db->db));
        return false;
    }
    return true;
}

void db_resolve_path(char* out, size_t out_size) {
    const char* override = getenv("MQTT_VIEWER_DB");
    if (override) {
        snprintf(out, out_size, "%s", override);
        return;
    }
    const char* home = getenv("HOME");
    if (!home) {
        snprintf(out, out_size, "mqtt_viewer.db");
        return;
    }
    char dir[1024];
#ifdef __APPLE__
    snprintf(dir, sizeof(dir), "%s/Library/Application Support/mqtt-viewer", home);
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg)
        snprintf(dir, sizeof(dir), "%s/mqtt-viewer", xdg);
    else
        snprintf(dir, sizeof(dir), "%s/.local/share/mqtt-viewer", home);
#endif
    mkdir(dir, 0755);
    snprintf(out, out_size, "%s/mqtt-viewer.db", dir);
}
