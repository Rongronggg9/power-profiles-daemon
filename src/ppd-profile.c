/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-profile.h"
#include "ppd-enums.h"

const char *
ppd_profile_to_str (PpdProfile profile)
{
  GFlagsClass *klass = g_type_class_ref (PPD_TYPE_PROFILE);
  GFlagsValue *value = g_flags_get_first_value (klass, profile);
  const gchar *name = value ? value->value_nick : "";
  g_type_class_unref (klass);
  return name;
}

PpdProfile
ppd_profile_from_str (const char *str)
{
  GFlagsClass *klass = g_type_class_ref (PPD_TYPE_PROFILE);
  GFlagsValue *value = g_flags_get_value_by_nick (klass, str);
  PpdProfile profile = value ? value->value : PPD_PROFILE_UNSET;
  g_type_class_unref (klass);
  return profile;
}

gboolean
ppd_profile_has_single_flag (PpdProfile profile)
{
  GFlagsClass *klass = g_type_class_ref (PPD_TYPE_PROFILE);
  GFlagsValue *value = g_flags_get_first_value (klass, profile);
  gboolean ret = FALSE;
  if (value && value->value == profile)
    ret = TRUE;
  g_type_class_unref (klass);
  return ret;
}
