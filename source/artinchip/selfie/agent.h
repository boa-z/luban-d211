#ifndef __AGENT_H__
#define __AGENT_H__

#include <gio/gio.h>

int agent_init(GDBusConnection *conn);
int agent_deinit(GDBusConnection *conn);

#endif // __AGENT_H__
