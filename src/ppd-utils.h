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

gboolean write_sysfs (GUdevDevice  *device,
                      const char   *attribute,
                      const char   *value,
                      GError      **error);
