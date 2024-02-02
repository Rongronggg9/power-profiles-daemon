/*
 * Copyright (c) 2023 Mario Limonciello <superm1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-driver.h"

#define PPD_TYPE_DRIVER_CPU (ppd_driver_cpu_get_type ())
G_DECLARE_DERIVABLE_TYPE (PpdDriverCpu, ppd_driver_cpu, PPD, DRIVER_CPU, PpdDriver)

/**
 * PpdDriverCpuClass:
 * @parent_class: The parent class.
 *
 * New CPU drivers should derive from #PpdDriverCpu and implement
 * both @probe () and @activate_profile.
 */
struct _PpdDriverCpuClass
{
  PpdDriverClass   parent_class;
};
