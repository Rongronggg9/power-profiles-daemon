/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (c) 2022 Prajna Sariputra <putr4.s@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-driver-cpu.h"

#define PPD_TYPE_DRIVER_AMD_PSTATE (ppd_driver_amd_pstate_get_type ())
G_DECLARE_FINAL_TYPE (PpdDriverAmdPstate, ppd_driver_amd_pstate, PPD, DRIVER_AMD_PSTATE, PpdDriverCpu)
