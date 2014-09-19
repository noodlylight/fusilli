#ifndef FUSILLI_WINDOW_MANAGER_H
#define FUSILLI_WINDOW_MANAGER_H

#include <glib-object.h>

#include "gnome-window-manager.h"

#define FUSILLI_WINDOW_MANAGER(obj) \
        G_TYPE_CHECK_INSTANCE_CAST (obj, fusilli_window_manager_get_type (), \
                                    FusilliWindowManager)

#define FUSILLI_WINDOW_MANAGER_CLASS(klass) \
        G_TYPE_CHECK_CLASS_CAST (klass, fusilli_window_manager_get_type (), \
                                 MetacityWindowManagerClass)

#define IS_FUSILLI_WINDOW_MANAGER(obj) \
        G_TYPE_CHECK_INSTANCE_TYPE (obj, fusilli_window_manager_get_type ())


typedef struct _FusilliWindowManager        FusilliWindowManager;
typedef struct _FusilliWindowManagerClass   FusilliWindowManagerClass;
typedef struct _FusilliWindowManagerPrivate FusilliWindowManagerPrivate;

struct _FusilliWindowManager {
	GnomeWindowManager         parent;
	FusilliWindowManagerPrivate *p;
};

struct _FusilliWindowManagerClass {
	GnomeWindowManagerClass klass;
};

GType
fusilli_window_manager_get_type (void);

GObject *
window_manager_new (int expected_interface_version);

#endif
