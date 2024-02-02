/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-driver-cpu.h"

#define PPD_TYPE_DRIVER_INTEL_PSTATE (ppd_driver_intel_pstate_get_type ())
G_DECLARE_FINAL_TYPE (PpdDriverIntelPstate, ppd_driver_intel_pstate, PPD, DRIVER_INTEL_PSTATE, PpdDriverCpu)
