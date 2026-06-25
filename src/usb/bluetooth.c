/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/bluetooth.h"

#include <gio/gio.h>
#include <string.h>

static void copy_s(char *dst, size_t sz, const char *src)
{
    g_strlcpy(dst, src ? src : "", sz);
}

/* Read a string property from an a{sv} dict (returns "" if absent). */
static void prop_str(GVariant *props, const char *key, char *out, size_t sz)
{
    const char *v = NULL;
    GVariant *val = g_variant_lookup_value(props, key, G_VARIANT_TYPE_STRING);
    if (val) {
        v = g_variant_get_string(val, NULL);
        copy_s(out, sz, v);
        g_variant_unref(val);
    } else {
        out[0] = '\0';
    }
}

static bool prop_bool(GVariant *props, const char *key)
{
    gboolean b = FALSE;
    g_variant_lookup(props, key, "b", &b);
    return b;
}

static uint32_t prop_u32(GVariant *props, const char *key)
{
    guint32 u = 0;
    g_variant_lookup(props, key, "u", &u);
    return u;
}

BtList *bt_enumerate(void)
{
    BtList *l = g_new0(BtList, 1);

    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) {
        g_snprintf(l->error, sizeof l->error, "system bus: %s", err->message);
        g_clear_error(&err);
        return l;
    }

    GVariant *reply = g_dbus_connection_call_sync(
        bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 4000, NULL, &err);
    if (!reply) {
        g_snprintf(l->error, sizeof l->error,
                   "BlueZ unavailable: %s", err->message);
        g_clear_error(&err);
        g_object_unref(bus);
        return l;
    }

    /* Count first, then fill (two passes keep arrays exact-sized). */
    GVariant *objects = g_variant_get_child_value(reply, 0);
    GVariantIter it;
    const char *path;
    GVariant *ifaces;

    size_t na = 0, nd = 0;
    g_variant_iter_init(&it, objects);
    while (g_variant_iter_loop(&it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant *a = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", NULL);
        GVariant *d = g_variant_lookup_value(ifaces, "org.bluez.Device1", NULL);
        if (a) { na++; g_variant_unref(a); }
        if (d) { nd++; g_variant_unref(d); }
    }
    l->adapters = g_new0(BtAdapter, na ? na : 1);
    l->devices = g_new0(BtDevice, nd ? nd : 1);

    g_variant_iter_init(&it, objects);
    while (g_variant_iter_loop(&it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant *ad = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", NULL);
        if (ad) {
            BtAdapter *a = &l->adapters[l->n_adapters++];
            copy_s(a->path, sizeof a->path, path);
            prop_str(ad, "Address", a->address, sizeof a->address);
            prop_str(ad, "Alias", a->name, sizeof a->name);
            if (!a->name[0])
                prop_str(ad, "Name", a->name, sizeof a->name);
            a->powered = prop_bool(ad, "Powered");
            g_variant_unref(ad);
        }
        GVariant *dv = g_variant_lookup_value(ifaces, "org.bluez.Device1", NULL);
        if (dv) {
            BtDevice *d = &l->devices[l->n_devices++];
            copy_s(d->path, sizeof d->path, path);
            prop_str(dv, "Address", d->address, sizeof d->address);
            prop_str(dv, "Alias", d->name, sizeof d->name);
            if (!d->name[0])
                prop_str(dv, "Name", d->name, sizeof d->name);
            prop_str(dv, "Adapter", d->adapter, sizeof d->adapter);
            d->paired = prop_bool(dv, "Paired");
            d->connected = prop_bool(dv, "Connected");
            d->trusted = prop_bool(dv, "Trusted");
            d->class = prop_u32(dv, "Class");
            g_variant_unref(dv);
        }
    }

    g_variant_unref(objects);
    g_variant_unref(reply);
    g_object_unref(bus);
    return l;
}

void bt_list_free(BtList *l)
{
    if (!l)
        return;
    g_free(l->adapters);
    g_free(l->devices);
    g_free(l);
}

int bt_remove_device(const char *adapter_path, const char *device_path,
                     char *errbuf, size_t errsz)
{
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) {
        g_snprintf(errbuf, errsz, "system bus: %s", err->message);
        g_clear_error(&err);
        return -1;
    }
    GVariant *r = g_dbus_connection_call_sync(
        bus, "org.bluez", adapter_path, "org.bluez.Adapter1", "RemoveDevice",
        g_variant_new("(o)", device_path), NULL, G_DBUS_CALL_FLAGS_NONE,
        5000, NULL, &err);
    int rc = 0;
    if (!r) {
        g_snprintf(errbuf, errsz, "RemoveDevice: %s", err->message);
        g_clear_error(&err);
        rc = -1;
    } else {
        g_variant_unref(r);
    }
    g_object_unref(bus);
    return rc;
}
