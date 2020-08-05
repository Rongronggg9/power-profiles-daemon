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

#define PPD_TYPE_DRIVER_LENOVO_DYTC (ppd_driver_lenovo_dytc_get_type())
G_DECLARE_FINAL_TYPE(PpdDriverLenovoDytc, ppd_driver_lenovo_dytc, PPD, DRIVER_LENOVO_DYTC, PpdDriver)
