#ifndef __ADVERTISE_H__
#define __ADVERTISE_H__

#include <gio/gio.h>

int advertise_start(GDBusConnection *conn);
int advertise_stop(GDBusConnection *conn);
int advertise_restart(GDBusConnection *conn);

#endif
