/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-driver-power-saver.h"

struct _PpdDriverPowerSaver
{
  PpdDriver  parent_instance;
};

G_DEFINE_TYPE (PpdDriverPowerSaver, ppd_driver_power_saver, PPD_TYPE_DRIVER)

static GObject*
ppd_driver_power_saver_constructor (GType                  type,
                                         guint                  n_construct_params,
                                         GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_driver_power_saver_parent_class)->constructor (type,
                                                                                   n_construct_params,
                                                                                   construct_params);
  g_object_set (object,
                "driver-name", "power-saver",
                "profiles", PPD_PROFILE_POWER_SAVER,
                NULL);

  return object;
}

static void
ppd_driver_power_saver_class_init (PpdDriverPowerSaverClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_driver_power_saver_constructor;
}

static void
ppd_driver_power_saver_init (PpdDriverPowerSaver *self)
{
}
