#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdint.h>

#include "gatt.h"
#include "log.h"

#define __DEBUG__

#define HID_OBJECT_PATH "/org/hid/server"

static const gchar object_manager_xml[] =
"<node>"
"  <interface name='org.freedesktop.DBus.ObjectManager'>"
"    <method name='GetManagedObjects'>"
"      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
"    </method>"
"    <signal name='InterfacesRemoved'>"
"      <arg name='interfaces' type='as'/>"
"    </signal>"
"  </interface>"
"</node>";


/* The xml of the service object */
static const gchar service_xml[] =
"<node>"
"  <interface name='org.bluez.GattService1'>"
"    <property name='UUID' type='s' access='read'/>"
"    <property name='Primary' type='b' access='read'/>"
"  </interface>"
"</node>";


/* The xml of the characteristic object */
static const gchar char_xml[] = 
"<node>"
"  <interface name='org.bluez.GattCharacteristic1'>"
"    <property name='UUID' type='s' access='read'/>"
"    <property name='Service' type='o' access='read'/>"
"    <property name='Value' type='ay' access='read'/>"
"    <property name='Notifying' type='b' access='read'/>"
"    <property name='Flags' type='as' access='read'/>"
"    <method name='ReadValue'>"
"      <arg name='options' type='a{sv}' direction='in'/>"
"      <arg name='value' type='ay' direction='out'/>"
"    </method>"
"    <method name='WriteValue'>"
"      <arg name='value' type='ay' direction='in'/>"
"      <arg name='options' type='a{sv}' direction='in'/>"
"    </method>"
"    <method name='StartNotify'>"
"    </method>"
"    <method name='StopNotify'/>"
"  </interface>"
"</node>";

/* the xml of the descriptor object */
static const gchar desc_xml[] = 
"<node>"
"  <interface name='org.bluez.GattDescriptor1'>"
"    <property name='UUID' type='s' access='read'/>"
"    <property name='Characteristic' type='o' access='read'/>"
"    <property name='Value' type='ay' access='read'/>"
"    <property name='Flags' type='as' access='read'/>"
"    <method name='ReadValue'>"
"      <arg name='options' type='a{sv}' direction='in'/>"
"      <arg name='value' type='ay' direction='out'/>"
"    </method>"
"    <method name='WriteValue'>"
"      <arg name='value' type='ay' direction='in'/>"
"      <arg name='options' type='a{sv}' direction='in'/>"
"    </method>"
"  </interface>"
"</node>";

#define DESC_FLAGS_SIZE 9
#define CHAR_FLAGS_SIZE 17

struct desc_t {
	char *UUID;
	char *Characteristic;
	char *ObjectPath;
	uint8_t Value[512];
	int len;
	/* @Flags:
	 *	"read"
	 *	"write"
	 *	"encrypt-read"
	 *	"encrypt-write"
	 *	"encrypt-authenticated-read"
	 *	"encrypt-authenticated-write"
	 *	"secure-read" (Server only)
	 *	"secure-write" (Server only)
	 *	"authorize"
	 */
	char *Flags[DESC_FLAGS_SIZE];
	/*
	 * True, if notifications or indications on this
	 * characteristic are currently enabled.
	 */
	int Notifying;
	
};

struct char_t {
	char *UUID;
	char *Service;
	char *ObjectPath;
	uint8_t Value[512];
	int len;
	/* @Flags:
	 *	"broadcast"
	 *	"read"
	 *	"write-without-response"
	 *	"write"
	 *	"notify"
	 *	"indicate"
	 *	"authenticated-signed-writes"
	 *	"extended-properties"
	 *	"reliable-write"
	 *	"writable-auxiliaries"
	 *	"encrypt-read"
	 *	"encrypt-write"
	 *	"encrypt-authenticated-read"
	 *	"encrypt-authenticated-write"
	 *	"secure-read" (Server only)
	 *	"secure-write" (Server only)
	 *	"authorize"
	 */
	char *Flags[CHAR_FLAGS_SIZE];
	/*
	 * True, if notifications or indications on this
	 * characteristic are currently enabled.
	 */
	int Notifying;
};


struct service_t {
	char *UUID;
	char *ObjectPath;
	/*
	 * Indicates whether or not this GATT service is a
	 * primary service. If false, the service is secondary.
	 */
	int Primary;
};

struct gatt_object_t {
	guint id;
	char *path;
	enum { SERVER, CHAR, DESC } type;
	union {
		struct service_t *service;
		struct char_t *chr;
		struct desc_t *desc;
	} obj;
};

struct server_t {
	struct {
		struct service_t service;
		struct char_t protocolModel;
		struct char_t hidInfo;
		struct char_t controlPoint;
		struct char_t reportMap;
		struct char_t report1;
		struct desc_t report1Descriptor;
	} gatt;

	GDBusConnection *conn;
	GDBusNodeInfo *object_manager_node_info;
	GDBusNodeInfo *service_node_info;
	GDBusNodeInfo *char_node_info;
	GDBusNodeInfo *desc_node_info;
	guint object_manager_reg_id;
	guint service_reg_id;
	guint protocolModel_reg_id;
	guint hidInfo_reg_id;
	guint controlPoint_reg_id;
	guint reportMap_reg_id;
	guint report1_reg_id;
	guint report1Descriptor_reg_id;
};

/*
-> /org/hid/server
  |   - org.freedesktop.DBus.ObjectManager
  |
  -> /org/hid/server/service00
  | |   - org.freedesktop.DBus.Properties
  | |   - org.bluez.GattService1
  | |
  | -> /org/hid/server/service00/char0000
  | |     - org.freedesktop.DBus.Properties
  | |     - org.bluez.GattCharacteristic1
  | |
  | -> /org/hid/server/service00/char0001
  | |     - org.freedesktop.DBus.Properties
  | |     - org.bluez.GattCharacteristic1
  | |
  | -> /org/hid/server/service00/char0002
  | |     - org.freedesktop.DBus.Properties
  | |     - org.bluez.GattCharacteristic1
  | |
  | -> /org/hid/server/service00/char0003
  | |     - org.freedesktop.DBus.Properties
  | |     - org.bluez.GattCharacteristic1
  | |
  | -> /org/hid/server/service00/char0004
  |   |   - org.freedesktop.DBus.Properties
  |   |   - org.bluez.GattCharacteristic1
  |   |
  |   -> /org/hid/server/service00/char0004/desc000
  |       - org.freedesktop.DBus.Properties
  |       - org.bluez.GattDescriptor1
  |
  -> /org/hid/server/serviceXX
    |   - org.freedesktop.DBus.Properties
    |   - org.bluez.GattService1
    |
    -> /org/hid/server/serviceXX/char0000
        - org.freedesktop.DBus.Properties
        - org.bluez.GattCharacteristic1
*/

static struct server_t server_ctx = {
	.gatt = {
		/* "/service00" */
		.service = {
			.UUID = "00001812-0000-1000-8000-00805f9b34fb",
			.Primary = 1,
			.ObjectPath = HID_OBJECT_PATH"/service00",
		},
		/* "/service00/char0000" */
		.protocolModel = {
			.UUID = "00002A4E-0000-1000-8000-00805f9b34fb",
			.Service = HID_OBJECT_PATH"/service00",
			.ObjectPath = HID_OBJECT_PATH"/service00/char0000",
			.Flags = {
				[0] = "read",
				[1] = "write-without-response",
				[2] = NULL,
			},
			.Value[0] = 0x1,
			.len = 1,
		},
		/* "/service00/char0001" */
		.hidInfo = {
			.UUID = "00002A4A-0000-1000-8000-00805f9b34fb",
			.Service = HID_OBJECT_PATH"/service00",
			.ObjectPath = HID_OBJECT_PATH"/service00/char0001",
			.Flags = {
				[0] = "read",
				[1] = NULL,
			},
			.Value = {0x01, 0x11, 0x00, 0x03},
			.len = 4,
		},
		/* "/service00/char0002" */
		.controlPoint = {
			.UUID = "00002A4C-0000-1000-8000-00805f9b34fb",
			.Service = HID_OBJECT_PATH"/service00",
			.ObjectPath = HID_OBJECT_PATH"/service00/char0002",
			.Flags = {
				[0] = "write-without-response",
				[1] = NULL,
			},
			.Value = {0x00},
			.len = 1,
		},
		/* "/service00/char0003" */
		.reportMap = {
			.UUID = "00002A4B-0000-1000-8000-00805f9b34fb",
			.Service = HID_OBJECT_PATH"/service00",
			.ObjectPath = HID_OBJECT_PATH"/service00/char0003",
			.Flags = {
				[0] = "read",
				[1] = NULL,
			},
			.Value = {
				0x05, 0x0C,        // USAGE_PAGE (Consumer Devices)
    				0x09, 0x01,        // USAGE (Consumer Control)
    				0xA1, 0x01,        // COLLECTION (Application)
    				0x85, 0x01,        //   REPORT_ID (1)
    				0x75, 0x10,        //   REPORT_SIZE (16)
    				0x95, 0x01,        //   REPORT_COUNT (1)
    				0x15, 0x01,        //   LOGICAL_MINIMUM (1)
    				0x26, 0xFF, 0x07,  //   LOGICAL_MAXIMUM (2047)
    				0x19, 0x01,        //   USAGE_MINIMUM (1)
    				0x2A, 0xFF, 0x07,  //   USAGE_MAXIMUM (2047)
    				0x81, 0x00,        //   INPUT (Data, Ary, Abs)
    				0xC0,              // END_COLLECTION
			 },
			.len = 25,
		},
		/* "/service00/char0004" */
		.report1 = {
			.UUID = "00002A4D-0000-1000-8000-00805f9b34fb",
			.Service = HID_OBJECT_PATH"/service00",
			.ObjectPath = HID_OBJECT_PATH"/service00/char0004",
			.Flags = {
				[0] = "secure-read",
				[1] = "notify",
				[2] = NULL,
			},
			.Value = {0x00, 0x00},
			.len = 2,
		},
		/* "/service00/char0004/desc0000" */
		.report1Descriptor = {
			.UUID = "00002908-0000-1000-8000-00805f9b34fb",
			.Characteristic = HID_OBJECT_PATH"/service00/char0004",
			.ObjectPath = HID_OBJECT_PATH"/service00/char0004/desc0000",
			.Flags = {
				[0] = "read",
				[1] = NULL,
			},
			.Value = {0x01, 0x01},
			.len = 2,
		},
	},
};

static struct gatt_object_t gatt_objects[] = {
	{ 0, HID_OBJECT_PATH"/service00", SERVER, .obj.service = &server_ctx.gatt.service },
	{ 0, HID_OBJECT_PATH"/service00/char0000", CHAR, .obj.chr = &server_ctx.gatt.protocolModel },
	{ 0, HID_OBJECT_PATH"/service00/char0001", CHAR, .obj.chr = &server_ctx.gatt.hidInfo },
	{ 0, HID_OBJECT_PATH"/service00/char0002", CHAR, .obj.chr = &server_ctx.gatt.controlPoint },
	{ 0, HID_OBJECT_PATH"/service00/char0003", CHAR, .obj.chr = &server_ctx.gatt.reportMap },
	{ 0, HID_OBJECT_PATH"/service00/char0004", CHAR, .obj.chr = &server_ctx.gatt.report1 },
	{ 0, HID_OBJECT_PATH"/service00/char0004/desc0000", DESC, .obj.desc = &server_ctx.gatt.report1Descriptor },
};

static gboolean send_shutter_event(gpointer user_data)
{
	GVariantBuilder *inv_prop_builder = NULL;
	GVariantBuilder *prop_builder = NULL;
	GVariantBuilder *builder = NULL;
	GVariant *parameters[3];
	GVariant *value = NULL;
	GError *error = NULL;

	u_tm_log("[%s:%d]\n", __FUNCTION__, __LINE__);
	if (!server_ctx.conn || !server_ctx.gatt.report1.Notifying) {
		return G_SOURCE_CONTINUE;
	}

	u_tm_log("[%s:%d]\n", __FUNCTION__, __LINE__);
	/*
	 * Notifications are implemented via the PropertiesChanged signal。
	 * When bluez receives a signal from the value property PropertiesChanged,
	 * bluez will send notification or indication to the client.
	 *
	 * Reference: doc/gatt-api.txt
	 * The cached value of the characteristic. This property
	 * gets updated only after a successful read request and
	 * when a notification or indication is received, upon
	 * which a PropertiesChanged signal will be emitted.
	 */
	
	/* interface_name */
	parameters[0] = g_variant_new_string("org.bluez.GattCharacteristic1");

	/* changed_properties */
	builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
	g_variant_builder_add(builder, "y", 0xe9);
	g_variant_builder_add(builder, "y", 0x00);

	value = g_variant_builder_end(builder);
	g_variant_builder_unref(builder);

	prop_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(prop_builder, "{sv}", "Value", value);
	parameters[1] = g_variant_builder_end(prop_builder);
	g_variant_builder_unref(prop_builder);

	/* invalidated_properties */
	inv_prop_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
	parameters[2] = g_variant_builder_end(inv_prop_builder);
	g_variant_builder_unref(inv_prop_builder);

	g_dbus_connection_emit_signal(server_ctx.conn,
                               "org.bluez",
                               HID_OBJECT_PATH"/service00/char0004",
                               "org.freedesktop.DBus.Properties",
                               "PropertiesChanged" ,
                               g_variant_new_tuple(parameters, 3), /* (sa{sv}as) */
                               &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
	}						   

	/* interface_name */
	parameters[0] = g_variant_new_string("org.bluez.GattCharacteristic1");

	/* changed_properties */
	builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
	g_variant_builder_add(builder, "y", 0x00);
	g_variant_builder_add(builder, "y", 0x00);
	
	value = g_variant_builder_end(builder);
	g_variant_builder_unref(builder);

	prop_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(prop_builder, "{sv}", "Value", value);
	parameters[1] = g_variant_builder_end(prop_builder);
	g_variant_builder_unref(prop_builder);

	/* invalidated_properties */
	inv_prop_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
	parameters[2] = g_variant_builder_end(inv_prop_builder);
	g_variant_builder_unref(inv_prop_builder);

	error = NULL;
	g_dbus_connection_emit_signal(server_ctx.conn,
                               "org.bluez",
                               HID_OBJECT_PATH"/service00/char0004",
                               "org.freedesktop.DBus.Properties",
                               "PropertiesChanged" ,
                               g_variant_new_tuple(parameters, 3), /* (sa{sv}as) */
                               &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
	}						   

	return G_SOURCE_CONTINUE;
}

void send_shutter(void)
{
	send_shutter_event(NULL);
}

static int gatt_create_node_info(void)
{
	GError *error = NULL;

	server_ctx.object_manager_node_info = g_dbus_node_info_new_for_xml(object_manager_xml, &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		return -1;
	}

	server_ctx.service_node_info = g_dbus_node_info_new_for_xml(service_xml, &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		goto ERROR_1;
	}

	server_ctx.char_node_info = g_dbus_node_info_new_for_xml(char_xml, &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		goto ERROR_2;
	}

	server_ctx.desc_node_info = g_dbus_node_info_new_for_xml(desc_xml, &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		goto ERROR_3;
	}
	
	u_tm_log("[%s:%d] %s\n", __FUNCTION__, __LINE__, "gatt_create node info ok");
	return 0;

ERROR_3:
	g_dbus_node_info_unref(server_ctx.char_node_info);

ERROR_2:
	g_dbus_node_info_unref(server_ctx.service_node_info);

ERROR_1:
	g_dbus_node_info_unref(server_ctx.object_manager_node_info);
	return -1;
	
}



static GVariant *get_service_property(struct service_t *service, const gchar *property_name)
{
	GVariant *v = NULL;

	if(!strcmp(property_name, "UUID")) {
		v = g_variant_new("s", service->UUID);
	} else if(!strcmp(property_name, "Primary")) {
		v = g_variant_new("b", service->Primary);
	}

	return v;
}

static GVariant *get_char_property(struct char_t *chr, const gchar *property_name)
{
	GVariantBuilder *builder = NULL;
	GVariant *v = NULL;
	int i;

	if (!strcmp(property_name, "UUID")) {
		v = g_variant_new("s", chr->UUID);
	} else if (!strcmp(property_name, "Flags")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
		for (i = 0; i < CHAR_FLAGS_SIZE; i++) {
			if (chr->Flags[i]) {
				g_variant_builder_add(builder, "s", chr->Flags[i]);
			}
		}

		v= g_variant_builder_end(builder);
		g_variant_builder_unref(builder);
	} else if (!strcmp(property_name, "Value")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
		for(i = 0; i < chr->len; i++) {
			g_variant_builder_add(builder, "y", chr->Value[i]);
		}

		v= g_variant_builder_end(builder);
		g_variant_builder_unref(builder);
	} else if (!strcmp(property_name, "Service")) {
		v = g_variant_new("o", chr->Service);
	}  else if (!strcmp(property_name, "Notifying")) {
		v = g_variant_new("b", chr->Notifying);
	}

	return v;
}

static GVariant *get_desc_property(struct desc_t *desc, const gchar *property_name)
{
	GVariantBuilder *builder = NULL;
	GVariant *v = NULL;
	int i;

	if (!strcmp(property_name, "UUID")) {
		v = g_variant_new("s", desc->UUID);
	} else if (!strcmp(property_name, "Flags")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
		for(i = 0; i < DESC_FLAGS_SIZE; i++) {
			if(desc->Flags[i])
				g_variant_builder_add(builder, "s", desc->Flags[i]);
		}

		v= g_variant_builder_end(builder);
		g_variant_builder_unref(builder);
	} else if (!strcmp(property_name, "Value")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
		for (i = 0; i < desc->len; i++) {
			g_variant_builder_add(builder, "y", desc->Value[i]);
		}

		v= g_variant_builder_end(builder);
		g_variant_builder_unref(builder);
	} else if(!strcmp(property_name, "Characteristic")) {
		v = g_variant_new("o", desc->Characteristic);
	} else if(!strcmp(property_name, "Notifying")) {
		v = g_variant_new("b", desc->Notifying);
	}
	
	return v;
}

static GVariant *get_property_variant(const gchar *object_path, const gchar *interface_name, const gchar *property_name)
{
	GVariant *v = NULL;
	int i;

	for (i = 0; i < G_N_ELEMENTS(gatt_objects); i++) {
		switch (gatt_objects[i].type) {
		case SERVER:
			if (!strcmp(object_path, gatt_objects[i].obj.service->ObjectPath)) {
				v = get_service_property(gatt_objects[i].obj.service, property_name);
				return v;
			}
			break;
		case CHAR:
			if (!strcmp(object_path, gatt_objects[i].obj.chr->ObjectPath)) {
				v = get_char_property(gatt_objects[i].obj.chr, property_name);
				return v;
			}
			break;
		case DESC:
			if (!strcmp(object_path, gatt_objects[i].obj.desc->ObjectPath)) {
				v = get_desc_property(gatt_objects[i].obj.desc, property_name);
				return v;
			}
			break;
		}
	}

	return v;
}

static GVariant *gatt_create_service_objects(struct service_t *service)
{
	GVariant *v_property, *v;
	GVariantBuilder *builder_if;
	GVariantBuilder *builder_obj;

	builder_if = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	
	v_property = get_property_variant(service->ObjectPath, "org.bluez.GattService1", "UUID");
	g_variant_builder_add(builder_if, "{&sv}", "UUID", v_property);

	v_property = get_property_variant(service->ObjectPath, "org.bluez.GattService1", "Primary");
	g_variant_builder_add(builder_if, "{&sv}", "Primary", v_property);
	
	v = g_variant_builder_end(builder_if);
	g_variant_builder_unref(builder_if);

	builder_obj = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
	
	g_variant_builder_add(builder_obj, "{&s@a{sv}}", "org.bluez.GattService1", v);
	
	v = g_variant_builder_end(builder_obj);
	g_variant_builder_unref(builder_obj);

	return v;
}

static GVariant *gatt_create_char_objects(struct char_t *chr)
{
	GVariant *v_property = NULL, *v = NULL;
	GVariantBuilder *builder_if = NULL;
	GVariantBuilder *builder_obj = NULL;
	int i;

	const char *char_list[] = {"UUID", "Service", "Value", "Notifying", "Flags"};

	builder_if = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	for (i = 0; i < sizeof(char_list) / sizeof(const char *); i++) {
		v_property = get_property_variant(chr->ObjectPath, "org.bluez.GattCharacteristic1", char_list[i]);
		g_variant_builder_add(builder_if, "{&sv}", char_list[i], v_property);
	}
	
	v = g_variant_builder_end(builder_if);
	g_variant_builder_unref(builder_if);
	
	builder_obj = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
	
	g_variant_builder_add(builder_obj, "{&s@a{sv}}", "org.bluez.GattCharacteristic1", v);
	
	v = g_variant_builder_end(builder_obj);
	g_variant_builder_unref(builder_obj);
	
	return v;
}

static GVariant *gatt_create_desc_objects(struct desc_t *desc)
{
	GVariant *v_property, *v;
	GVariantBuilder *builder_if;
	GVariantBuilder *builder_obj;
	int i;

	const char *desc_list[] = {"UUID", "Characteristic", "Value", "Notifying", "Flags"};

	builder_if = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	for (i = 0; i < sizeof(desc_list) / sizeof(const char *); i++) {
		v_property = get_property_variant(desc->ObjectPath, "org.bluez.GattDescriptor1", desc_list[i]);
		g_variant_builder_add(builder_if, "{&sv}", desc_list[i], v_property);
	}

	v = g_variant_builder_end(builder_if);
	g_variant_builder_unref(builder_if);

	builder_obj = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));

	g_variant_builder_add(builder_obj, "{&s@a{sv}}", "org.bluez.GattDescriptor1", v);

	v = g_variant_builder_end(builder_obj);
	g_variant_builder_unref(builder_obj);

	return v;
}

/*
 * type='a{oa{sa{sv}}}'
 */
static GVariant *gatt_create_managed_objects(void)
{
	GVariantBuilder *builder_mobjs = NULL;
	GVariant *v = NULL;

	builder_mobjs = g_variant_builder_new(G_VARIANT_TYPE("a{oa{sa{sv}}}"));

	for (int i = 0; i < G_N_ELEMENTS(gatt_objects); i++) {
		switch (gatt_objects[i].type) {
		case SERVER:
			v = gatt_create_service_objects(gatt_objects[i].obj.service);
			break;
		case CHAR:
			v = gatt_create_char_objects(gatt_objects[i].obj.chr);
			break;
		case DESC:
			v = gatt_create_desc_objects(gatt_objects[i].obj.desc);
			break;
		}
		g_variant_builder_add(builder_mobjs, "{&o@a{sa{sv}}}", gatt_objects[i].path, v);
	}
	
	/* Build the GetManagedObjects return value type */
	v = g_variant_builder_end(builder_mobjs);
	g_variant_builder_unref(builder_mobjs);

	GVariant *tuples[] = {v};
	
	return g_variant_new_tuple(tuples, 1);
}

void service_call(struct service_t *service, const gchar *method_name)
{
}

void char_call(GDBusMethodInvocation *invoc, GVariant *params, struct char_t *chr, const gchar *method_name)
{
	GVariantBuilder *builder = NULL;
	GVariantBuilder *prop_builder = NULL;
	GVariantBuilder *inv_prop_builder = NULL;
	GVariant *parameters[3];
	GVariant *value = NULL;
	GError *error = NULL;
	int i;

#ifdef __DEBUG__
	u_tm_log("params type: \"%s\"\n", g_variant_get_type_string(params));
#endif

	if(!server_ctx.conn || chr->len > 512) {
		return ;
	}

	if(!strcmp(method_name, "WriteValue")) {
		/* interface_name */
		parameters[0] = g_variant_new_string("org.bluez.GattCharacteristic1");

		/* changed_properties */
		builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
		for (i = 0; i < chr->len; i++) {
			g_variant_builder_add(builder, "y", chr->Value[i]);
		}
		
		value = g_variant_builder_end(builder);

		prop_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(prop_builder, "{sv}", "Value", value);
		parameters[1] = g_variant_builder_end(prop_builder);

		/* invalidated_properties */
		inv_prop_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
		parameters[2] = g_variant_builder_end(inv_prop_builder);

		g_dbus_connection_emit_signal(server_ctx.conn,
        	                       "org.bluez",
        	                       chr->ObjectPath,
        	                       "org.freedesktop.DBus.Properties",
        	                       "PropertiesChanged" ,
        	                       g_variant_new_tuple(parameters, 3), /* (sa{sv}as) */
        	                       &error);
		if(error) {
			u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
			g_error_free (error);
		}						   
	} else if (!strcmp(method_name, "ReadValue")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
		for (i = 0; i < chr->len; i++) {
			g_variant_builder_add(builder, "y", chr->Value[i]);
		}
		
		value = g_variant_builder_end(builder);

		g_dbus_method_invocation_return_value(invoc, g_variant_new("(@ay)", value));
	} else if(!strcmp(method_name, "StartNotify")) {
		chr->Notifying = 1;
		u_tm_log("Start chr->Notifying = %d\n", chr->Notifying);
	} else if(!strcmp(method_name, "StopNotify")) {
		chr->Notifying = 0;
		u_tm_log("Stop chr->Notifying = %d\n", chr->Notifying);
	}
}

void desc_call(GDBusMethodInvocation *invoc, GVariant *params, struct desc_t *desc, const gchar *method_name)
{
	GVariantBuilder *builder = NULL;
	GVariantBuilder *prop_builder = NULL;
	GVariantBuilder *inv_prop_builder = NULL;
	GVariant *parameters[3];
	GVariant *value = NULL;
	GError *error = NULL;
	int i;

#ifdef __DEBUG__
	u_tm_log("params type: \"%s\"\n", g_variant_get_type_string(params));
#endif

	if(!server_ctx.conn || desc->len > 512) {
		return ;
	}

	if(!strcmp(method_name, "WriteValue")) {
		/* interface_name */
		parameters[0] = g_variant_new_string("org.bluez.GattDescriptor1");

		/* changed_properties */
		builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
		for (i = 0; i < desc->len; i++) {
			g_variant_builder_add(builder, "y", desc->Value[i]);
		}
		
		value = g_variant_builder_end(builder);

		prop_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(prop_builder, "{sv}", "Value", value);
		parameters[1] = g_variant_builder_end(prop_builder);

		/* invalidated_properties */
		inv_prop_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
		parameters[2] = g_variant_builder_end(inv_prop_builder);

		g_dbus_connection_emit_signal(server_ctx.conn,
        	                       "org.bluez",
        	                       desc->ObjectPath,
        	                       "org.freedesktop.DBus.Properties",
        	                       "PropertiesChanged" ,
        	                       g_variant_new_tuple(parameters, 3), /* (sa{sv}as) */
        	                       &error);
		if(error) {
			u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
			g_error_free (error);
		}						   
	} else if (!strcmp(method_name, "ReadValue")) {
		builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
		for (i = 0; i < desc->len; i++) {
			g_variant_builder_add(builder, "y", desc->Value[i]);
		}
		
		value = g_variant_builder_end(builder);

		g_dbus_method_invocation_return_value(invoc, g_variant_new("(@ay)", value));
	}
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
#ifdef __DEBUG__
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, obj_path);
	u_tm_log("[%s:%d] iface_name :%s\n", __FUNCTION__, __LINE__, iface_name);
	u_tm_log("[%s:%d] method_name :%s\n", __FUNCTION__, __LINE__, method_name);
#endif

	if (!strcmp(obj_path, HID_OBJECT_PATH)) {
		if (!strcmp(method_name, "GetManagedObjects")) {
			g_dbus_method_invocation_return_value(invoc, gatt_create_managed_objects());
		}
	}

	for (int i = 0; i < G_N_ELEMENTS(gatt_objects); i++) {
		switch (gatt_objects[i].type) {
		case SERVER:
			if (!strcmp(obj_path, gatt_objects[i].obj.service->ObjectPath)) {
			}
			break;
		case CHAR:
			if (!strcmp(obj_path, gatt_objects[i].obj.chr->ObjectPath)) {
				char_call(invoc, params, gatt_objects[i].obj.chr, method_name);
			}
			break;
		case DESC:
			if (!strcmp(obj_path, gatt_objects[i].obj.desc->ObjectPath)) {
				desc_call(invoc, params, gatt_objects[i].obj.desc, method_name);
			}
			break;
		}
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
	GVariant *v = NULL;

#ifdef __DEBUG__
	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] property_name :%s\n", __FUNCTION__, __LINE__, property_name);
#endif

	v = get_property_variant(object_path, interface_name, property_name);
	return v;
}

static int gatt_service_object_register(GDBusConnection *conn, struct service_t *service)
{
	GError *error = NULL;
	GDBusInterfaceVTable interface_vtable;

	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;

	//service->id = g_dbus_connection_register_object(conn,
	g_dbus_connection_register_object(conn,
                                   	  		service->ObjectPath,
                                   	  		server_ctx.service_node_info->interfaces[0],/*org.bluez.GattService1*/
                                   	  		&interface_vtable,
                                   	  		NULL,
                                   	  		NULL,
                                   	  		&error);
	if(error) {
		u_tm_log("<org.bluez.GattService1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}
	return 0;
}

static int gatt_char_object_register(GDBusConnection *conn, struct char_t *chr)
{
	GError *error = NULL;
	GDBusInterfaceVTable interface_vtable;

	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;

	//chr->id = g_dbus_connection_register_object(conn,
	g_dbus_connection_register_object(conn,
                                   	  	    chr->ObjectPath,
                                   	  	    server_ctx.char_node_info->interfaces[0],/*org.bluez.GattCharacteristic1*/
                                   	  	    &interface_vtable,
                                   	  	    NULL,
                                   	  	    NULL,
                                   	  	    &error);
	if(error) {
		u_tm_log("<org.bluez.GattCharacteristic1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}
	return 0;
}

static int gatt_desc_object_register(GDBusConnection *conn, struct desc_t *desc)
{
	GError *error = NULL;
	GDBusInterfaceVTable interface_vtable;

	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;

	//desc->id = g_dbus_connection_register_object(conn,
	g_dbus_connection_register_object(conn,
                                   		     desc->ObjectPath,
                                   		     server_ctx.desc_node_info->interfaces[0],/*org.bluez.GattDescriptor1*/
                                   		     &interface_vtable,
                                   		     NULL,
                                   		     NULL,
                                   		     &error);
	if(error) {
		u_tm_log("<org.bluez.GattDecriptor1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}
	return 0;
}

static int gatt_object_register(GDBusConnection *conn)
{
	GError *error = NULL;
	GDBusInterfaceVTable interface_vtable;

	/* The execution of these callback functions is dependent on g_main_loop */
	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = NULL; /* ObjectManager property does not exist */
	interface_vtable.set_property = NULL;

  
	server_ctx.object_manager_reg_id = 
		g_dbus_connection_register_object(conn,
                                   		HID_OBJECT_PATH,
                                   		server_ctx.object_manager_node_info->interfaces[0],/*org.freedesktop.DBus.ObjectManager*/
                                   		&interface_vtable,
                                   		NULL,
                                   		NULL,
                                   		&error);
	if(error) {
		u_tm_log("<org.freedesktop.DBus.ObjectManager> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}

	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;

	for (int i = 0; i < G_N_ELEMENTS(gatt_objects); i++) {
		switch (gatt_objects[i].type) {
		case SERVER:
			if (gatt_service_object_register(conn, gatt_objects[i].obj.service)) {
				return -1;
			}
			break;
		case CHAR:
			if (gatt_char_object_register(conn, gatt_objects[i].obj.chr)) {
				return -1;
			}
			break;
		case DESC:
			if (gatt_desc_object_register(conn, gatt_objects[i].obj.desc)) {
				return -1;
			}
			break;
		}
	}
	
	u_tm_log("[%s:%d] %s\n", __FUNCTION__, __LINE__, "gatt object register ok");
	
	return 0;
}

static void async_ready_callback(GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
	GDBusConnection *conn = (GDBusConnection *)user_data;
	GError *error = NULL;

	g_dbus_connection_call_finish(conn, res, &error);

	if (error) {
		u_tm_log("Error: RegisterApplication %s\n", error->message);
		g_error_free(error);
		return;
	}

	u_tm_log("async_ready_callback: hid_register_application ok \n");
}

static void hid_register_application_async(GDBusConnection *conn)
{
	GVariantBuilder *dict_builder = NULL;
	GVariant *parameters = NULL;
	GVariant *vobject_path = NULL;
	GVariant *dict_v = NULL;
	
	vobject_path = g_variant_new("o", HID_OBJECT_PATH);

	dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	dict_v = g_variant_builder_end(dict_builder);
	g_variant_builder_unref(dict_builder);
	
	GVariant *children[] = {vobject_path, dict_v};
	parameters = g_variant_new_tuple(children, 2);

	g_dbus_connection_call(conn,
			       "org.bluez",
			       "/org/bluez/hci0",
			       "org.bluez.GattManager1",
			       "RegisterApplication",
			       parameters,
			       NULL,
			       G_DBUS_CALL_FLAGS_NONE,
			       -1,
			       NULL,
			       async_ready_callback,
			       conn);
}

static void
on_properties_changed(GDBusConnection *conn, const gchar *sender,
		      const gchar *object_path, const gchar *interface_name,
		      const gchar *signal_name, GVariant *parameters,
		      gpointer user_data)
{
	GVariantIter *properties_iter;
	GVariant *property_value;
	gchar *property_interface;
	gchar *property_name;
	gsize size;

//#ifdef __DEBUG__
	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] signal_name :%s\n", __FUNCTION__, __LINE__, signal_name);
//#endif

	if (g_str_has_prefix(object_path, "/org/hid/server") &&
	    g_strcmp0(interface_name, "org.bluez.GattCharacteristic1") == 0 &&
	    g_strcmp0(signal_name, "PropertiesChanged") == 0) {
		g_variant_get(parameters, "(&sa{sv}as)", &property_interface, &properties_iter, NULL);

		while (g_variant_iter_next(properties_iter, "{&sv}", &property_name, &property_value)) {
			if (g_strcmp0(property_name, "Value") == 0) {
				const guint8 *data = g_variant_get_fixed_array(property_value, &size, sizeof(guint8));
				if (data != NULL && size >= 2) {
					uint16_t cccd_value = (data[1] << 8) | data[0];
					g_print("CCD write! path:%s, ccdd value: 0x%04x\n", object_path, cccd_value);
					switch (cccd_value) {
						case 0x0000:
							g_print("Disable notify/indicate!\n");
							break;
						case 0x0001:
							g_print("Start notify!\n");
							break;
						case 0x0002:
							g_print("Start indicate!\n");
							break;
						default:
							g_print("Unknown cccd value!\n");
							break;
					}
				}
			}
			g_variant_unref(property_value);
			g_free(property_name);
		}
		g_variant_iter_free(properties_iter);
	}
}

int gatt_hid_server_start(GDBusConnection *conn)
{
	g_dbus_connection_signal_subscribe(conn,
								   "org.bluez",
								   "org.freedesktop.DBus.Properties",
								   "PropertiesChanged",
								   NULL,
								   "org.bluez.GattCharacteristic1",
								   G_DBUS_SIGNAL_FLAGS_NONE,
								   on_properties_changed,
								   NULL,
								   NULL);
	gatt_create_node_info();
	gatt_object_register(conn);
	hid_register_application_async(conn);
	server_ctx.conn = conn;
	return 0;
}
