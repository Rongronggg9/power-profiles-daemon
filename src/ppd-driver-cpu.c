/*
 * Copyright (c) 2023 Mario Limonciello <superm1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "CpuDriver"

#include "ppd-driver-cpu.h"

/**
 * SECTION:ppd-driver-cpu
 * @Short_description: CPU Drivers
 * @Title: CPU Profile Drivers
 *
 * Profile drivers are the implementation of the different profiles for
 * the whole system. A driver will need to implement support for `power-saver`
 * and `balanced` at a minimum.
 *
 * CPU drivers are typically used to change specifically the CPU efficiency
 * to match the desired platform state.
 */

G_DEFINE_TYPE (PpdDriverCpu, ppd_driver_cpu, PPD_TYPE_DRIVER)

static void
ppd_driver_cpu_finalize (GObject *object)
{
  G_OBJECT_CLASS (ppd_driver_cpu_parent_class)->finalize (object);
}

static void
ppd_driver_cpu_class_init (PpdDriverCpuClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = ppd_driver_cpu_finalize;
}

static void
ppd_driver_cpu_init (PpdDriverCpu *self)
{
}
