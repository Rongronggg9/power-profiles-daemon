/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-driver.h"

#define PPD_TYPE_DRIVER_BALANCED (ppd_driver_balanced_get_type())
G_DECLARE_FINAL_TYPE(PpdDriverBalanced, ppd_driver_balanced, PPD, DRIVER_BALANCED, PpdDriver)
