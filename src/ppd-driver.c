/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-driver.h"
#include "ppd-enums.h"

typedef struct
{
  char          *driver_name;
  PpdProfile     profiles;
  gboolean       selected;
  char          *inhibited;
} PpdDriverPrivate;

enum {
  PROP_0,
  PROP_DRIVER_NAME,
  PROP_PROFILES,
  PROP_INHIBITED
};

#define PPD_DRIVER_GET_PRIVATE(o) (ppd_driver_get_instance_private (o))
G_DEFINE_TYPE_WITH_PRIVATE (PpdDriver, ppd_driver, G_TYPE_OBJECT)

static void
ppd_driver_set_property (GObject        *object,
                         guint           property_id,
                         const GValue   *value,
                         GParamSpec     *pspec)
{
  PpdDriver *driver = PPD_DRIVER (object);
  PpdDriverPrivate *priv = PPD_DRIVER_GET_PRIVATE (driver);

  switch (property_id) {
  case PROP_DRIVER_NAME:
    g_assert (priv->driver_name == NULL);
    priv->driver_name = g_value_dup_string (value);
    break;
  case PROP_PROFILES:
    priv->profiles = g_value_get_flags (value);
    break;
  case PROP_INHIBITED:
    g_clear_pointer (&priv->inhibited, g_free);
    priv->inhibited = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
  }
}

static void
ppd_driver_get_property (GObject        *object,
                         guint           property_id,
                         GValue         *value,
                         GParamSpec     *pspec)
{
  PpdDriver *driver = PPD_DRIVER (object);
  PpdDriverPrivate *priv = PPD_DRIVER_GET_PRIVATE (driver);

  switch (property_id) {
  case PROP_DRIVER_NAME:
    g_value_set_string (value, priv->driver_name);
    break;
  case PROP_PROFILES:
    g_value_set_flags (value, priv->profiles);
    break;
  case PROP_INHIBITED:
    g_value_set_string (value, priv->inhibited);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
  }
}

static void
ppd_driver_finalize (GObject *object)
{
  PpdDriverPrivate *priv;

  priv = PPD_DRIVER_GET_PRIVATE (PPD_DRIVER (object));
  g_clear_pointer (&priv->driver_name, g_free);
  g_clear_pointer (&priv->inhibited, g_free);

  G_OBJECT_CLASS (ppd_driver_parent_class)->finalize (object);
}

static void
ppd_driver_class_init (PpdDriverClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = ppd_driver_finalize;
  object_class->get_property = ppd_driver_get_property;
  object_class->set_property = ppd_driver_set_property;

  g_object_class_install_property (object_class, PROP_DRIVER_NAME,
                                   g_param_spec_string("driver-name",
                                                       "Driver name",
                                                       "Profile driver name",
                                                       NULL,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class, PROP_PROFILES,
                                   g_param_spec_flags("profiles",
                                                      "Profiles",
                                                      "Profiles implemented by this driver",
                                                      PPD_TYPE_PROFILE,
                                                      0,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class, PROP_INHIBITED,
                                   g_param_spec_string("inhibited",
                                                       "Inhibited",
                                                       "Why this profile is inhibited, if set",
                                                       NULL,
                                                       G_PARAM_READWRITE));
}

static void
ppd_driver_init (PpdDriver *self)
{
}

gboolean
ppd_driver_probe (PpdDriver *driver)
{
  g_return_val_if_fail (PPD_IS_DRIVER (driver), FALSE);

  if (!PPD_DRIVER_GET_CLASS (driver)->probe)
    return TRUE;

  return PPD_DRIVER_GET_CLASS (driver)->probe (driver);
}

gboolean
ppd_driver_activate_profile (PpdDriver  *driver,
                             PpdProfile         profile,
                             GError           **error)
{
  g_return_val_if_fail (PPD_IS_DRIVER (driver), FALSE);
  g_return_val_if_fail (ppd_profile_has_single_flag (profile), FALSE);

  if (!PPD_DRIVER_GET_CLASS (driver)->activate_profile)
    return TRUE;

  return PPD_DRIVER_GET_CLASS (driver)->activate_profile (driver, profile, error);
}

const char *
ppd_driver_get_driver_name (PpdDriver *driver)
{
  PpdDriverPrivate *priv;

  g_return_val_if_fail (PPD_IS_DRIVER (driver), NULL);

  priv = PPD_DRIVER_GET_PRIVATE (driver);
  return priv->driver_name;
}

PpdProfile
ppd_driver_get_profiles (PpdDriver *driver)
{
  PpdDriverPrivate *priv;

  g_return_val_if_fail (PPD_IS_DRIVER (driver), PPD_PROFILE_BALANCED);

  priv = PPD_DRIVER_GET_PRIVATE (driver);
  return priv->profiles;
}

gboolean
ppd_driver_get_selected (PpdDriver *driver)
{
  PpdDriverPrivate *priv;

  g_return_val_if_fail (PPD_IS_DRIVER (driver), FALSE);

  priv = PPD_DRIVER_GET_PRIVATE (driver);
  return priv->selected;
}

const char *
ppd_driver_get_inhibited (PpdDriver *driver)
{
  PpdDriverPrivate *priv;

  g_return_val_if_fail (PPD_IS_DRIVER (driver), NULL);

  priv = PPD_DRIVER_GET_PRIVATE (driver);
  return priv->inhibited ? priv->inhibited : "";
}

gboolean
ppd_driver_is_inhibited (PpdDriver *driver)
{
  PpdDriverPrivate *priv;

  g_return_val_if_fail (PPD_IS_DRIVER (driver), FALSE);

  priv = PPD_DRIVER_GET_PRIVATE (driver);

  return (priv->inhibited != NULL);
}
