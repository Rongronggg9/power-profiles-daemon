/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-action.h"

#define PPD_TYPE_ACTION_TRICKLE_CHARGE (ppd_action_trickle_charge_get_type ())
G_DECLARE_FINAL_TYPE (PpdActionTrickleCharge, ppd_action_trickle_charge, PPD, ACTION_TRICKLE_CHARGE, PpdAction)
