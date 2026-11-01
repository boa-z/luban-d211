#ifndef __GATT_H__
#define __GATT_H__

#include <stdint.h>
#include <gio/gio.h>

#ifdef __cplusplus
extern "C" {
#endif

int gatt_hid_server_start(GDBusConnection *conn);
void send_shutter(void);

#ifdef __cplusplus
}
#endif

#endif
