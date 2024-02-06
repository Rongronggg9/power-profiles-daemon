/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "PlatformDriver"

#include "ppd-driver-placeholder.h"

struct _PpdDriverPlaceholder
{
  PpdDriverPlatform  parent_instance;
};

G_DEFINE_TYPE (PpdDriverPlaceholder, ppd_driver_placeholder, PPD_TYPE_DRIVER_PLATFORM)

static GObject*
ppd_driver_placeholder_constructor (GType                  type,
                                    guint                  n_construct_params,
                                    GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_driver_placeholder_parent_class)->constructor (type,
                                                                              n_construct_params,
                                                                              construct_params);
  g_object_set (object,
                "driver-name", "placeholder",
                "profiles", PPD_PROFILE_POWER_SAVER | PPD_PROFILE_BALANCED,
                NULL);

  return object;
}

static void
ppd_driver_placeholder_class_init (PpdDriverPlaceholderClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructor = ppd_driver_placeholder_constructor;
}

static void
ppd_driver_placeholder_init (PpdDriverPlaceholder *self)
{
}
