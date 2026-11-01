#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdint.h>
 
#include "log.h"

#define BLUEZ_SERVICE "org.bluez"
#define ADAPTER_INTERFACE "org.bluez.Adapter1"

static void adapter_properties_set(GDBusConnection *conn, char *interface, char *name, GVariant *value)
{
	GVariant *parameters = NULL;
	GError *error = NULL;

	u_tm_log("adapter_properties_set %s:%s\n", interface, name);

	parameters = g_variant_new("(ssv)", interface, name, value);

	g_dbus_connection_call_sync(conn,
				    BLUEZ_SERVICE,
				    "/org/bluez/hci0",
				    "org.freedesktop.DBus.Properties",
				    "Set",
				    parameters,
				    NULL,
				    G_DBUS_CALL_FLAGS_NONE,
				    -1,
				    NULL,
				    &error);

	if (error) {
		u_tm_log("Error: adapter_properties_set %s\n", error->message);
		g_error_free(error);
	}
}

static GVariant *adapter_properties_get(GDBusConnection *conn, char *interface, char *name)
{
	GVariant *parameters = NULL;
	GError *error = NULL;
	GVariant *ret = NULL;
	GVariant *v = NULL;
	
	u_tm_log("adapter_properties_get %s:%s\n", interface, name);

	parameters = g_variant_new("(ss)", interface, name);

	v = g_dbus_connection_call_sync(conn,
					BLUEZ_SERVICE,
					"/org/bluez/hci0",
					"org.freedesktop.DBus.Properties",
					"Get",
					parameters,
					NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);

	if (error) {
		u_tm_log("Error: adapter_properties_get %s\n", error->message);
		g_error_free(error);
		return ret;
	}

	g_variant_get(v, "(v)", &ret);

	return ret;
}

static void async_remove_callback(GObject *source_object, GAsyncResult *res,
				gpointer user_data)
{
	GDBusConnection *conn = (GDBusConnection *)user_data;
	GVariant *value = NULL;
	GError *error = NULL;

	u_tm_log("async_remove_callback\n");
	value = g_dbus_connection_call_finish(conn, res, &error);
	if (value) {
		g_variant_unref(value);
	}

	if (error) {
		u_tm_log("RemoveDevice Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free(error);
	} else {
		u_tm_log("RemoveDevice Successfly\n");
	}
}

static int adapter_remove(GDBusConnection *conn, char *interface, const gchar *object_path)
{
	GVariant *parameters = NULL;

	u_tm_log("adapter_remove_device %s:%s\n", interface, object_path);

	parameters = g_variant_new("(o)", object_path);

	g_dbus_connection_call(conn,
			       BLUEZ_SERVICE,
			       "/org/bluez/hci0",
			       interface,
			       "RemoveDevice",
			       parameters,
			       NULL,
			       G_DBUS_CALL_FLAGS_NONE,
			       -1,
			       NULL,
			       async_remove_callback,
			       conn);

	return 0;
}

void adapter_discoverable_enable(GDBusConnection *conn)
{
	adapter_properties_set(conn, ADAPTER_INTERFACE, "Discoverable", g_variant_new("b", 1));
}

void adapter_discoverable_disable(GDBusConnection *conn)
{
	adapter_properties_set(conn, ADAPTER_INTERFACE, "Discoverable", g_variant_new("b", 0));
}

void adapter_power_on(GDBusConnection *conn)
{
	adapter_properties_set(conn, ADAPTER_INTERFACE, "Powered", g_variant_new("b", 1));
}

void adapter_power_off(GDBusConnection *conn)
{
	adapter_properties_set(conn, ADAPTER_INTERFACE, "Powered", g_variant_new("b", 0));
}

int adapter_power_state(GDBusConnection *conn)
{
	GVariant *v = NULL;
	int ret;

	v = adapter_properties_get(conn, ADAPTER_INTERFACE, "Powered");

	g_variant_get(v, "b", &ret);

	return ret;
}

int adapter_discoverable_state(GDBusConnection *conn)
{
	GVariant *v = NULL;
	int ret;

	v = adapter_properties_get(conn, ADAPTER_INTERFACE, "Discoverable");

	g_variant_get(v, "b", &ret);

	return ret;
}

int adapter_remove_device(GDBusConnection *conn, const gchar *object_path)
{
	int ret;

	ret = adapter_remove(conn, ADAPTER_INTERFACE, object_path);

	return ret;
}
