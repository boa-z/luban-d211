// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 ArtInChip Technology Co., Ltd.
 * Author: hanlei.liu <hanelei.liu@artinchip.com>
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include "log.h"

#define AGENT_PATH "/org/bluez/agent"

static guint agent_reg_id = 0;

static const gchar agent_interface_xml[] =
"<node>"
"  <interface name='org.bluez.Agent1'>"
"    <method name='Release'/>"
"    <method name='RequestPinCode'>"
"      <arg type='o' name='device' direction='in'/>"
"      <arg type='s' name='pincode' direction='out'/>"
"    </method>"
"    <method name='RequestPasskey'>"
"      <arg type='o' name='device' direction='in'/>"
"      <arg type='u' name='passkey' direction='out'/>"
"    </method>"
"    <method name='DisplayPasskey'>"
"      <arg type='o' name='device' direction='in'/>"
"      <arg type='u' name='passkey' direction='in'/>"
"      <arg type='u' name='entered' direction='in'/>"
"    </method>"
"    <method name='RequestConfirmation'>"
"      <arg type='o' name='device' direction='in'/>"
"      <arg type='u' name='passkey' direction='in'/>"
"    </method>"
"    <method name='RequestAuthorization'>"
"      <arg type='o' name='device' direction='in'/>"
"    </method>"
"    <method name='AuthorizeService'>"
"      <arg type='o' name='device' direction='in'/>"
"      <arg type='s' name='uuid' direction='in'/>"
"    </method>"
"    <method name='Cancel'/>"
"  </interface>"
"</node>";

static void on_agent_method_call(GDBusConnection *conn,
				 const gchar *sender,
				 const gchar *object_path,
				 const gchar *interface_name,
				 const gchar *method_name,
				 GVariant *parameters,
				 GDBusMethodInvocation *invocation,
				 gpointer user_data)
{
	const gchar *device_path;
	guint passkey;

	u_tm_log("Agent method called: %s\n", method_name);

	if (g_strcmp0(method_name, "RequestConfirmation") == 0) {
		g_variant_get(parameters, "(ou)", &device_path, &passkey);

		u_tm_log("Auto-confirming pairing for device: %s, passkey: %06u\n", device_path, passkey);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "RequestPinCode") == 0) {
		// Automatically provide the default PIN code "0000"
		u_tm_log("RequestPinCode: %s, pincode: %s\n", device_path, "0000");
		g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "0000"));
	} else if (g_strcmp0(method_name, "RequestPasskey") == 0) {
		// Automatically provide the default Passkey 123456
		g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 123456));
		u_tm_log("RequestPasskey: %s, passkey: %s\n", device_path, "123456");
	} else if (g_strcmp0(method_name, "RequestAuthorization") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "AuthorizeService") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "DisplayPasskey") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "Release") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "Cancel") == 0) {
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else {
		u_tm_log("Unhandled agent method: %s\n", method_name);
		g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.NotImplemented", "Method not implemented");
	}
}

static GVariant *on_agent_get_property(GDBusConnection *conn,
				       const gchar *sender,
				       const gchar *object_path,
				       const gchar *interface_name,
				       const gchar *property_name,
				       GError **error,
				       gpointer user_data)
{
	u_tm_log("Get property: %s\n", property_name);
	return NULL;
}

static int register_agent(GDBusConnection *conn)
{
	GError *error = NULL;
	GDBusNodeInfo *node_info = NULL;
	GDBusInterfaceVTable interface_vtable;

	node_info = g_dbus_node_info_new_for_xml(agent_interface_xml, &error);
	if (error) {
		u_tm_log("Error creating agent interface: %s\n", error->message);
		g_error_free(error);
		return -1;
	}

	interface_vtable.method_call = on_agent_method_call;
	interface_vtable.get_property = on_agent_get_property;
	interface_vtable.set_property = NULL;

	agent_reg_id = g_dbus_connection_register_object(conn,
							 AGENT_PATH,
							 node_info->interfaces[0],
							 &interface_vtable,
							 NULL,
							 NULL,
							 &error);

	if (error) {
		u_tm_log("Error register agent object: %s\n", error->message);
		g_error_free(error);
		return -1;
	}

	// Register to the BlueZ Agent Manager
	g_dbus_connection_call_sync(conn,
				    "org.bluez",
				    "/org/bluez",
				    "org.bluez.AgentManager1",
				    "RegisterAgent",
				    g_variant_new("(os)", AGENT_PATH, "KeyboardDisplay"),
				    NULL,
				    G_DBUS_CALL_FLAGS_NONE,
				    -1,
				    NULL,
				    &error);
	if (error) {
		u_tm_log("Error register agent: %s\n", error->message);
		g_error_free(error);
		return -1;
	}

	// Set as the default agent
	g_dbus_connection_call_sync(conn,
				    "org.bluez",
				    "/org/bluez",
				    "org.bluez.AgentManager1",
				    "RequestDefaultAgent",
				    g_variant_new("(o)", AGENT_PATH),
				    NULL,
				    G_DBUS_CALL_FLAGS_NONE,
				    -1,
				    NULL,
				    &error);
	if (error) {
		u_tm_log("Error request default agent: %s\n", error->message);
		g_error_free(error);
		return -1;
	}

	u_tm_log("Bluetooth pairing agent registered successfully\n");
	return 0;
}

static int unregister_agent(GDBusConnection *conn)
{
	gboolean result;

	if (agent_reg_id > 0) {
		g_dbus_connection_call_sync(conn,
				       "org.bluez",
				       "/org/bluez",
				       "org.bluez.AgentManager1",
				       "UnregisterAgent",
				       g_variant_new("(o)", AGENT_PATH),
				       NULL,
				       G_DBUS_CALL_FLAGS_NONE,
				       -1,
				       NULL,
				       NULL);
		result = g_dbus_connection_unregister_object(conn, agent_reg_id);
		if (!result) {
			u_tm_log("Error unregistering agent.\n");
			return -1;
		}
	}
	agent_reg_id = 0;

	u_tm_log("Bluetooth pairing agent unregistered successfully\n");
	return 0;
}

int agent_init(GDBusConnection *conn)
{
	return register_agent(conn);
}

int agent_deinit(GDBusConnection *conn)
{
	return unregister_agent(conn);
}
