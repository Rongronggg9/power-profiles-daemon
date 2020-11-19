/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include <gudev/gudev.h>
#include <gio/gio.h>

char * ppd_utils_get_sysfs_path (const char *filename);
gboolean ppd_utils_write (const char  *filename,
                          const char  *value,
                          GError     **error);
gboolean ppd_utils_write_sysfs (GUdevDevice  *device,
                                const char   *attribute,
                                const char   *value,
                                GError      **error);
GFileMonitor *ppd_utils_monitor_sysfs_attr (GUdevDevice  *device,
                                            const char   *attribute,
                                            GError      **error);
GUdevDevice *ppd_utils_find_device (const char   *subsystem,
                                    GCompareFunc  func,
                                    gpointer      user_data);
