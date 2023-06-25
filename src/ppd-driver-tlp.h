/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (c) 2023 Rong Zhang <i@rong.moe>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-driver.h"

#define PPD_TYPE_DRIVER_TLP (ppd_driver_tlp_get_type())
G_DECLARE_FINAL_TYPE(PpdDriverTlp, ppd_driver_tlp, PPD, DRIVER_TLP, PpdDriver)
