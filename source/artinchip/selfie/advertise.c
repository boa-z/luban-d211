// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 ArtInChip Technology Co., Ltd.
 * Author: hanlei.liu <hanelei.liu@artinchip.com>
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdint.h>

#include"advertise.h"
#include "log.h"

#define BLUEZ_SERVICE "org.bluez"
#define ADVERT_OBJ_PATH "/org/hid/advertise"

struct advertise_t {
	GDBusNodeInfo *node_info;
	GDBusInterfaceInfo *if_info;
	guint id;
};

struct advertise_data_t {
	char *LocalName;
	/* "broadcast" or "peripheral" */
	char *Type;
	/*
	 * If set to 0, the client cannot connect to the device.
	 * If the type is boardcast, the property cannot be set.
	 *
	 */
	uint8_t Discoverable;
	uint16_t DiscoverableTimeout;
};

static struct advertise_data_t advertise_data = {
	.LocalName = "ArtinChip Camera",
	.Type = "peripheral",
	.Discoverable = 1,
	.DiscoverableTimeout = 0x7FFF,
};

static struct advertise_t advt_ctx;

static const gchar advertise_xml[] =
"<node>"
"  <interface name='org.bluez.LEAdvertisement1'>"
"    <method name='Release'/>"
"    <property name='LocalName' type='s' access='read'/>"
"    <property name='Type' type='s' access='read'/>"
"	 <property name='ServiceUUIDs' type='as' access='read'/>"
"    <property name='Discoverable' type='b' access='read'/>"
"    <property name='DiscoverableTimeout' type='q' access='read'/>"
"  </interface>"
"</node>";

/*
 * 1. Create a node info from the xml data
 * 2. Get the interface info via node info, interface info is used to register DBUS object.
 */
static int advertise_get_interface_info(const gchar *xml_data)
{
	GDBusNodeInfo *node_info = NULL;
	GError *error = NULL;

	node_info = g_dbus_node_info_new_for_xml(xml_data, &error);

	if (error) {
		u_tm_log("%s\n", error->message);
		g_error_free(error);
		return -1;
	}

	if (!node_info) {
		u_tm_log("node info NULL\n");
		return -1;
	}

	advt_ctx.node_info = node_info;

	return 0;
}

static void on_method_call(GDBusConnection *con,
			   const gchar *sender,
			   const gchar *obj_path,
			   const gchar *iface_name,
			   const gchar *method_name,
			   GVariant *params,
			   GDBusMethodInvocation *invoc,
			   gpointer udata)
{
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__,
		 method_name);

	if (!strcmp(method_name, "Release")) {
	}
}

/*
 * If interface info has readable properties, then a non-empty get_property
 * must be provided, or the Get and GetAll functions must be implemented in
 * the method_call method on the org.freedesktop.DBus.Properties interface.
 */
static GVariant *get_property(GDBusConnection *connection,
			      const gchar *sender,
			      const gchar *object_path,
			      const gchar *interface_name,
			      const gchar *property_name,
			      GError **error,
			      gpointer user_data)
{
	GVariantBuilder *builder = NULL;
	GVariant *v = NULL;

	//u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	//u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	//u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] property_name :%s\n", __FUNCTION__, __LINE__, property_name);

	if (!strcmp(property_name, "LocalName")) {
		v = g_variant_new("s", advertise_data.LocalName);
	} else if (!strcmp(property_name, "Type")) {
		v = g_variant_new("s", "peripheral"); /* "broadcast" or "peripheral" */
	} else if (!strcmp(property_name, "ServiceUUIDs")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("as"));

		g_variant_builder_add(builder, "s", "00001812-0000-1000-8000-00805f9b34fb");

		v = g_variant_builder_end(builder);
		g_variant_builder_unref(builder);
	} else if (!strcmp(property_name, "Discoverable")) {
		v = g_variant_new("b", advertise_data.Discoverable);
	} else if (!strcmp(property_name, "DiscoverableTimeout")) {
		v = g_variant_new("q", advertise_data.DiscoverableTimeout);
	}

	return v;
}

static int advertise_object_register(GDBusConnection *conn)
{
	GDBusInterfaceVTable interface_vtable;
	GError *error = NULL;
	guint id;

	/* The execution of these callback functions is dependent on g_main_loop */
	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;

	id = g_dbus_connection_register_object(conn,
                                   	       ADVERT_OBJ_PATH,
                                   	       advt_ctx.node_info->interfaces[0],/*org.bluez.LEAdvertisement1*/
                                   	       &interface_vtable,
                                   	       NULL,
                                   	       NULL,
                                   	       &error);
	if(error) {
		u_tm_log("<org.bluez.LEAdvertisement1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}

	advt_ctx.id = id;

	return 0;
}

static int advertise_object_unregister(GDBusConnection *conn)
{
	gboolean result = g_dbus_connection_unregister_object(conn, advt_ctx.id);
	if (!result) {
		u_tm_log("[%s:%d] failed to unregister advertisement\n", __FUNCTION__, __LINE__);
	}

	return 0;
}

static void async_ready_callback(GObject *source_object,
				 GAsyncResult *res,
				 gpointer user_data)
{
	GDBusConnection *conn = (GDBusConnection *)user_data;
	GError *error = NULL;

	u_tm_log("async_ready_callback\n");
	g_dbus_connection_call_finish(conn, res, &error);

	if (error) {
		u_tm_log("RegisterAdvertisement Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free(error);
	}
}

static void async_stop_callback(GObject *source_object,
				 GAsyncResult *res,
				 gpointer user_data)
{
	GDBusConnection *conn = (GDBusConnection *)user_data;
	GVariant *value = NULL;
	GError *error = NULL;

	u_tm_log("async_stop_callback\n");
	value = g_dbus_connection_call_finish(conn, res, &error);
	if (value) {
		g_variant_unref(value);
	}

	if (error) {
		u_tm_log("UnregisterAdvertisement Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free(error);
	} else {
		advertise_object_unregister(conn);
		u_tm_log("Stop advertising\n");
	}
}

static int advertise_register_to_bluez_async(GDBusConnection *conn)
{
	GVariantBuilder *dict_builder = NULL;
	GVariant *vobject_path = NULL;
	GVariant *parameters = NULL;

	vobject_path = g_variant_new("o", ADVERT_OBJ_PATH);

	dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(dict_builder, "{sv}", "param", g_variant_new_string("value"));

	GVariant *dict_v = g_variant_builder_end(dict_builder);
	g_variant_builder_unref(dict_builder);

	GVariant *children[] = { vobject_path, dict_v };
	parameters = g_variant_new_tuple(children, 2);

	g_dbus_connection_call(conn,
			       BLUEZ_SERVICE,
			       "/org/bluez/hci0",
			       "org.bluez.LEAdvertisingManager1",
			       "RegisterAdvertisement",
			       parameters,
			       NULL,
			       G_DBUS_CALL_FLAGS_NONE,
			       -1,
			       NULL,
			       async_ready_callback,
			       conn);

	return 0;
}

static int advertise_unregister_to_bluez_async(GDBusConnection *conn)
{
	GVariant *vobject_path = NULL;

	vobject_path = g_variant_new("(o)", ADVERT_OBJ_PATH);

	g_dbus_connection_call(conn,
			       BLUEZ_SERVICE,
			       "/org/bluez/hci0",
			       "org.bluez.LEAdvertisingManager1",
			       "UnregisterAdvertisement",
			       vobject_path,
			       NULL,
			       G_DBUS_CALL_FLAGS_NONE,
			       -1,
			       NULL,
			       async_stop_callback,
			       conn);

	return 0;
}

int advertise_start(GDBusConnection *conn)
{
	advertise_get_interface_info(advertise_xml);
	advertise_object_register(conn);
	advertise_register_to_bluez_async(conn);

	return 0;
}

int advertise_stop(GDBusConnection *conn)
{
	//advertise_object_unregister(conn);
	advertise_unregister_to_bluez_async(conn);

	return 0;
}

int advertise_restart(GDBusConnection *conn)
{
	advertise_stop(conn);
	advertise_start(conn);

	return 0;
}
