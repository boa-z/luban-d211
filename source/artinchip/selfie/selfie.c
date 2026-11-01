#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "log.h"
#include "adapter.h"
#include "agent.h"
#include "gatt.h"
#include "advertise.h"
#include "key.h"

#define BLUEZ_BUS_NAME "org.bluez"
#define BLUEZ_DEVICE1 "org.bluez.Device1"

GDBusConnection *conn = NULL;
GMainLoop *loop = NULL;

//static void
//on_properties_changed(GDBusConnection *conn, const gchar *sender_name,
//		      const gchar *object_path, const gchar *interface_name,
//		      const gchar *signal_name, GVariant *parameters,
//		      gpointer user_data)
//{
//	GVariantIter *properties_iter;
//	GVariant *property_value;
//	GVariant *value;
//	gchar *interface;
//	gchar *property_name;
//	gboolean connected;
//	gboolean paired;
//
//	if (g_str_has_prefix(object_path, "/org/bluez/hci0/dev_") &&
//	    g_strcmp0(interface_name, "org.freedesktop.DBus.Properties") == 0) {
//		g_variant_get(parameters, "(&sa{sv}as)", &interface,
//			      &properties_iter, NULL);
//
//		if (g_strcmp0(interface, "org.bluez.Device1") == 0) {
//			while (g_variant_iter_next(properties_iter, "{&sv}", &property_name, &property_value)) {
//				if (g_strcmp0(property_name, "Connected") == 0) {
//					connected = g_variant_get_boolean(property_value);
//					if (connected) {
//						g_print("设备已成功连接: %s\n", object_path);
//					} else {
//						g_print("设备已断开连接: %s\n", object_path);
//					}
//				} else if (g_strcmp0(property_name, "Paired") == 0) {
//					paired = g_variant_get_boolean(property_value);
//					if (paired) {
//						g_print("设备已成功配对: %s\n", object_path);
//					}
//				}
//				g_variant_unref(property_value);
//			}
//		}
//		g_variant_iter_free(properties_iter);
//	}
//}
//static void *
//on_properties_changed(GDBusConnection *conn, const gchar *sender,
//		      const gchar *object_path, const gchar *interface_name,
//		      const gchar *signal_name, GVariant *parameters,
//		      gpointer user_data)
//{
//	GVariantIter *properties_iter;
//	GVariant *property_value;
//	gchar *interface;
//	gchar *property_name;
//	gboolean connected;
//	gboolean paired;
//
////#ifdef __DEBUG__
//	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
//	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
//	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
//	u_tm_log("[%s:%d] signal_name :%s\n", __FUNCTION__, __LINE__, signal_name);
////#endif
//
//	if (g_str_has_prefix(object_path, "/org/bluez/hci0/dev_") &&
//	    g_strcmp0(interface_name, "org.freedesktop.DBus.Properties") == 0) {
//		g_variant_get(parameters, "(&sa{sv}as)", &interface, &properties_iter, NULL);
//
//		if (g_strcmp0(interface, "org.bluez.Device1") == 0) {
//			while (g_variant_iter_next(properties_iter, "{&sv}", &property_name, &property_value)) {
//				if (g_strcmp0(property_name, "Connected") == 0) {
//					connected = g_variant_get_boolean(property_value);
//					if (connected) {
//						g_print("The device is successfully connected: %s\n", object_path);
//					} else {
//						g_print("The device is disconnected: %s\n", object_path);
//						//advertise_restart(conn);
//						//gatt_hid_server_start(conn);
//						//g_dbus_connection_call_sync(conn,
//						//			    "org.bluez",
//						//			    object_path,
//						//			    "org.bluez.Device1",
//						//			    "RemoveBonding",
//						//			    g_variant_new("(s)", "all"),
//						//			    NULL,
//						//			    G_DBUS_CALL_FLAGS_NONE,
//						//			    -1,
//						//			    NULL,
//						//			    NULL);
//					}
//				} else if (g_strcmp0(property_name, "Paired") == 0) {
//					paired = g_variant_get_boolean(property_value);
//					if (paired) {
//						g_print("The device successfully paired: %s\n", object_path);
//					} else {
//						g_print("The device cannel paired: %s\n", object_path);
//					}
//				}
//				g_variant_unref(property_value);
//			}
//		}
//		g_variant_iter_free(properties_iter);
//	}
//}

static void on_device_disconnect_changed(GDBusConnection *conn,
				       const gchar *sender,
				       const gchar *object_path,
				       const gchar *interface_name,
				       const gchar *signal_name,
				       GVariant *parameters,
				       gpointer user_data)
{
	//GVariantIter *properties_iter;
	//GVariant *property_value;
	//gchar *interface;
	//gchar *property_name;
	//gboolean connected;
	//gboolean paired;
	const gchar *reason;
	const gchar *message;

	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] signal_name :%s\n", __FUNCTION__, __LINE__, signal_name);

	g_variant_get(parameters, "(&s&s)", &reason, &message);
	g_print("Device disconnect! reason:%s, message:%s\n", reason, message);
	adapter_remove_device(conn, object_path);

	//if (g_str_has_prefix(object_path, "/org/bluez/hci0/dev_") &&
	//    g_strcmp0(interface_name, "org.freedesktop.DBus.Properties") == 0) {
	//	g_variant_get(parameters, "(&sa{sv}as)", &interface, &properties_iter, NULL);

	//	if (g_strcmp0(interface, "org.bluez.Device1") == 0) {
	//		while (g_variant_iter_next(properties_iter, "{&sv}", &property_name, &property_value)) {
	//			g_print("property_name: %s\n", property_name);
	//			if (g_strcmp0(property_name, "Connected") == 0) {
	//				connected = g_variant_get_boolean(property_value);
	//				if (connected) {
	//					g_print("The device is successfully connected: %s\n", object_path);
	//					advertise_stop(conn);
	//				} else {
	//					g_print("The device is disconnected: %s\n", object_path);
	//					adapter_remove_device(conn, object_path);
	//					//advertise_restart(conn);
	//					//gatt_hid_server_start(conn);
	//					//g_dbus_connection_call_sync(conn,
	//					//			    "org.bluez",
	//					//			    object_path,
	//					//			    "org.bluez.Device1",
	//					//			    "RemoveBonding",
	//					//			    g_variant_new("(s)", "all"),
	//					//			    NULL,
	//					//			    G_DBUS_CALL_FLAGS_NONE,
	//					//			    -1,
	//					//			    NULL,
	//					//			    NULL);
	//				}
	//			} else if (g_strcmp0(property_name, "Paired") == 0) {
	//				paired = g_variant_get_boolean(property_value);
	//				if (paired) {
	//					g_print("The device successfully paired: %s\n", object_path);
	//				} else {
	//					g_print("The device cannel paired: %s\n", object_path);
	//				}
	//			}
	//			g_variant_unref(property_value);
	//		}
	//	}
	//	g_variant_iter_free(properties_iter);
	//}

	//if (g_strcmp0(signal_name, "PropertiesChanged") != 0)
	//	return;

	//g_variant_get(parameters, "(sa{sv}as)", &interfaces_iter, &changed_properties, NULL);

	//while (g_variant_iter_loop(interfaces_iter, "s", &interface)) {
	//	if (g_strcmp0(interface, "org.bluez.Device1") == 0) {
	//		//GVariantIter *props_iter;
	//		//GVariant *value;
	//		//const gchar *prop_name;
	//		//g_variant_iter_init(&props_iter, changed_properties);
	//		//while (g_variant_iter_next(&props_iter, "{&sv}", &prop_name, &value)) {
	//		//	if (g_strcmp0(prop_name, "Connected") == 0) {
	//		//		gboolean connected;
	//		//		g_variant_get(value, "b", &connected);
	//		//		if (!connected) {
	//		//			// 断开时处理逻辑
	//		//			//gatt_uart_unregister_service(conn); // 注销服务
	//		//			advertise_stop(conn); // 停止广告
	//		//			advertise_start(conn); // 重新启动广告
	//		//		}
	//		//	}
	//		//}
	//	}
	//}
}

static void on_device_property_changed(GDBusConnection *conn,
				       const gchar *sender,
				       const gchar *object_path,
				       const gchar *interface_name,
				       const gchar *signal_name,
				       GVariant *parameters,
				       gpointer user_data)
{
	//GVariantIter *interfaces_iter;
	//GVariant *changed_properties;
	//const gchar *interface;
	GVariantIter *properties_iter;
	GVariant *property_value;
	gchar *interface;
	gchar *property_name;
	gboolean connected;
	gboolean resolved;
	gboolean paired;

	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] signal_name :%s\n", __FUNCTION__, __LINE__, signal_name);

	if (g_str_has_prefix(object_path, "/org/bluez/hci0/dev_") &&
	    g_strcmp0(interface_name, "org.freedesktop.DBus.Properties") == 0) {
		g_variant_get(parameters, "(&sa{sv}as)", &interface, &properties_iter, NULL);

		if (g_strcmp0(interface, "org.bluez.Device1") == 0) {
			while (g_variant_iter_next(properties_iter, "{&sv}", &property_name, &property_value)) {
				g_print("property_name: %s\n", property_name);
				if (g_strcmp0(property_name, "Connected") == 0) {
					connected = g_variant_get_boolean(property_value);
					if (connected) {
						g_print("The device is successfully connected: %s\n", object_path);
						//advertise_stop(conn);
					} else {
						g_print("The device is disconnected: %s\n", object_path);
						connected = g_variant_get_boolean(property_value);
						if (!connected) {
							g_print("The device is Confirm disconnected: %s\n", object_path);
						}
						//adapter_remove_device(conn, object_path);
						//advertise_restart(conn);
						//gatt_hid_server_start(conn);
						//g_dbus_connection_call_sync(conn,
						//			    "org.bluez",
						//			    object_path,
						//			    "org.bluez.Device1",
						//			    "RemoveBonding",
						//			    g_variant_new("(s)", "all"),
						//			    NULL,
						//			    G_DBUS_CALL_FLAGS_NONE,
						//			    -1,
						//			    NULL,
						//			    NULL);
					}
				} else if (g_strcmp0(property_name, "Paired") == 0) {
					paired = g_variant_get_boolean(property_value);
					if (paired) {
						g_print("The device successfully paired: %s\n", object_path);
					} else {
						g_print("The device cannel paired: %s\n", object_path);
					}
				} else if (g_strcmp0(property_name, "ServicesResolved") == 0) {
					resolved = g_variant_get_boolean(property_value);
					if (resolved) {
						g_print("The service resolved: %s\n", object_path);
					} else {
						g_print("The service not resolved: %s\n", object_path);
					}
				}
				g_variant_unref(property_value);
			}
		}
		g_variant_iter_free(properties_iter);
	}

	//if (g_strcmp0(signal_name, "PropertiesChanged") != 0)
	//	return;

	//g_variant_get(parameters, "(sa{sv}as)", &interfaces_iter, &changed_properties, NULL);

	//while (g_variant_iter_loop(interfaces_iter, "s", &interface)) {
	//	if (g_strcmp0(interface, "org.bluez.Device1") == 0) {
	//		//GVariantIter *props_iter;
	//		//GVariant *value;
	//		//const gchar *prop_name;
	//		//g_variant_iter_init(&props_iter, changed_properties);
	//		//while (g_variant_iter_next(&props_iter, "{&sv}", &prop_name, &value)) {
	//		//	if (g_strcmp0(prop_name, "Connected") == 0) {
	//		//		gboolean connected;
	//		//		g_variant_get(value, "b", &connected);
	//		//		if (!connected) {
	//		//			// 断开时处理逻辑
	//		//			//gatt_uart_unregister_service(conn); // 注销服务
	//		//			advertise_stop(conn); // 停止广告
	//		//			advertise_start(conn); // 重新启动广告
	//		//		}
	//		//	}
	//		//}
	//	}
	//}
}

static void cleanup(int signo)
{
	if (signo == SIGINT) {
		u_tm_log("received SIGINT");
		// Unregister agent
		agent_deinit(conn);
		if (loop) {
			g_main_loop_unref(loop);
			loop = NULL;
		}
		if (conn) {
			g_object_unref(conn);
			conn = NULL;
		}
	}
}

int main(int argc, char *argv[])
{
	GError *error = NULL;

	if (key_init()) {
		fprintf(stderr, "key init failed.\n");
		return 0;
	}

	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (error) {
		g_error_free(error);
		return 0;
	}

	if (signal(SIGINT, cleanup) == SIG_ERR)
		u_tm_log("can't catch SIGINT");

	loop = g_main_loop_new(NULL, FALSE);

	//subscription_id = g_dbus_connection_signal_subscribe(conn,
	//						     "org.bluez",
	//						     "PropertiesChanged",
	//						     NULL,
	//						     NULL,
	//						     G_DBUS_SIGNAL_FLAGS_NONE,
	//						     on_properties_changed,
	//						     NULL,
	//						     NULL);
	//if (subscription_id == 0) {
	//	g_printerr("无法订阅PropertiesChanged信号\n");
	//	g_object_unref(conn);
	//	return -1;
	//}

	g_dbus_connection_signal_subscribe(conn,
					   "org.bluez",
					   "org.bluez.Device1",
					   "Disconnected",
					   NULL,
					   NULL,
					   G_DBUS_SIGNAL_FLAGS_NONE,
					   on_device_disconnect_changed,
					   NULL,
					   NULL);

	g_dbus_connection_signal_subscribe(conn,
					   "org.bluez",
					   "org.freedesktop.DBus.Properties",
					   "PropertiesChanged",
					   NULL,
					   "org.bluez.Device1",
					   G_DBUS_SIGNAL_FLAGS_NONE,
					   on_device_property_changed,
					   NULL,
					   NULL);

	adapter_power_on(conn);
	adapter_discoverable_enable(conn);

	u_tm_log("adapter_power_state = %d\n", adapter_power_state(conn));
	u_tm_log("adapter_discoverable_state = %d\n", adapter_discoverable_state(conn));

	/* Register agent */
	agent_init(conn);

	/* start advertising */
	advertise_start(conn);

	/* Register gatt server */
	gatt_hid_server_start(conn);

	/* Start the mainloop*/
	g_main_loop_run(loop);

	/* Clean up mainloop */
	g_main_loop_unref(loop);

	/* Disconnect from DBus */
	g_dbus_connection_close_sync(conn, NULL, NULL);
	g_object_unref(conn);

	return 0;
}
