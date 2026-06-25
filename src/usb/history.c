/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/history.h"

#include <limits.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

struct UsbHistory {
    sqlite3 *db;
};

/* Build "<XDG_DATA_HOME or ~/.local/share>/usbexplorer/history.db", creating
 * the directory.  Returns 1 on success. */
static int history_path(char *out, size_t outsz)
{
    const char *base = getenv("XDG_DATA_HOME");
    char buf[PATH_MAX];
    if (base && base[0]) {
        snprintf(buf, sizeof buf, "%s/usbexplorer", base);
    } else {
        const char *home = getenv("HOME");
        if (!home)
            return 0;
        snprintf(buf, sizeof buf, "%s/.local/share/usbexplorer", home);
    }
    /* mkdir -p (best effort over the leaf component) */
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", buf);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);

    snprintf(out, outsz, "%.*s/history.db", (int)(outsz > 12 ? outsz - 12 : 0), buf);
    return 1;
}

UsbHistory *usb_history_open(void)
{
    char path[PATH_MAX];
    if (!history_path(path, sizeof path))
        return NULL;

    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }
    const char *schema =
        "CREATE TABLE IF NOT EXISTS events ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " ts INTEGER NOT NULL,"
        " action TEXT NOT NULL,"
        " vid INTEGER, pid INTEGER,"
        " serial TEXT, product TEXT, devname TEXT);";
    if (sqlite3_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }

    UsbHistory *h = calloc(1, sizeof *h);
    if (!h) {
        sqlite3_close(db);
        return NULL;
    }
    h->db = db;
    return h;
}

void usb_history_close(UsbHistory *h)
{
    if (!h)
        return;
    sqlite3_close(h->db);
    free(h);
}

void usb_history_record(UsbHistory *h, const char *action, uint16_t vid,
                        uint16_t pid, const char *serial, const char *product,
                        const char *devname)
{
    if (!h)
        return;
    sqlite3_stmt *st = NULL;
    const char *sql = "INSERT INTO events(ts,action,vid,pid,serial,product,devname)"
                      " VALUES(?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(h->db, sql, -1, &st, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_text(st, 2, action ? action : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, vid);
    sqlite3_bind_int(st, 4, pid);
    sqlite3_bind_text(st, 5, serial ? serial : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, product ? product : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, devname ? devname : "", -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

static const char *col_text(sqlite3_stmt *st, int i)
{
    const unsigned char *t = sqlite3_column_text(st, i);
    return t ? (const char *)t : "";
}

int usb_history_dump(UsbHistory *h, int limit, int csv, FILE *out)
{
    if (!h)
        return -1;
    char sql[160];
    if (limit > 0)
        snprintf(sql, sizeof sql,
                 "SELECT ts,action,vid,pid,serial,product,devname FROM events"
                 " ORDER BY id DESC LIMIT %d;", limit);
    else
        snprintf(sql, sizeof sql,
                 "SELECT ts,action,vid,pid,serial,product,devname FROM events"
                 " ORDER BY id DESC;");

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(h->db, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    if (csv)
        fprintf(out, "timestamp,datetime,action,vid,pid,serial,product,devname\n");

    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        time_t ts = (time_t)sqlite3_column_int64(st, 0);
        struct tm tm;
        char when[32];
        localtime_r(&ts, &tm);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);

        const char *action = col_text(st, 1);
        int vid = sqlite3_column_int(st, 2), pid = sqlite3_column_int(st, 3);
        const char *serial = col_text(st, 4), *product = col_text(st, 5);
        const char *devname = col_text(st, 6);

        if (csv)
            fprintf(out, "%lld,%s,%s,%04x,%04x,\"%s\",\"%s\",%s\n",
                    (long long)ts, when, action, vid, pid, serial, product, devname);
        else
            fprintf(out, "%s  %-7s %04x:%04x  %-8s %s%s%s\n", when, action, vid, pid,
                    devname, product, serial[0] ? "  serial=" : "", serial);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}
