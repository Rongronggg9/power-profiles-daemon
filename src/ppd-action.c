/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-action.h"
#include "ppd-enums.h"

typedef struct
{
  char          *action_name;
  PpdProfile     profile;
} PpdActionPrivate;

enum {
  PROP_0,
  PROP_ACTION_NAME,
  PROP_PROFILE,
};

#define PPD_ACTION_GET_PRIVATE(o) (ppd_action_get_instance_private (o))
G_DEFINE_TYPE_WITH_PRIVATE (PpdAction, ppd_action, G_TYPE_OBJECT)

static void
ppd_action_set_property (GObject        *object,
                         guint           property_id,
                         const GValue   *value,
                         GParamSpec     *pspec)
{
  PpdAction *action = PPD_ACTION (object);
  PpdActionPrivate *priv = PPD_ACTION_GET_PRIVATE (action);

  switch (property_id) {
  case PROP_ACTION_NAME:
    g_assert (priv->action_name == NULL);
    priv->action_name = g_value_dup_string (value);
    break;
  case PROP_PROFILE:
    priv->profile = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
  }
}

static void
ppd_action_get_property (GObject        *object,
                         guint           property_id,
                         GValue         *value,
                         GParamSpec     *pspec)
{
  PpdAction *action = PPD_ACTION (object);
  PpdActionPrivate *priv = PPD_ACTION_GET_PRIVATE (action);

  switch (property_id) {
  case PROP_ACTION_NAME:
    g_value_set_string (value, priv->action_name);
    break;
  case PROP_PROFILE:
    g_value_set_enum (value, priv->profile);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
  }
}

static void
ppd_action_finalize (GObject *object)
{
  PpdActionPrivate *priv;

  priv = PPD_ACTION_GET_PRIVATE (PPD_ACTION (object));
  g_clear_pointer (&priv->action_name, g_free);

  G_OBJECT_CLASS (ppd_action_parent_class)->finalize (object);
}

static void
ppd_action_class_init (PpdActionClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = ppd_action_finalize;
  object_class->get_property = ppd_action_get_property;
  object_class->set_property = ppd_action_set_property;

  g_object_class_install_property (object_class, PROP_ACTION_NAME,
                                   g_param_spec_string("action-name",
                                                       "Action name",
                                                       "Action name",
                                                       NULL,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class, PROP_PROFILE,
                                   g_param_spec_enum("profile",
                                                     "Profile",
                                                     "Profile attached to this action",
                                                     PPD_TYPE_PROFILE,
                                                     PPD_PROFILE_UNSET,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
ppd_action_init (PpdAction *self)
{
}

gboolean
ppd_action_probe (PpdAction *action)
{
  g_return_val_if_fail (PPD_IS_ACTION (action), FALSE);

  if (!PPD_ACTION_GET_CLASS (action)->probe)
    return TRUE;

  return PPD_ACTION_GET_CLASS (action)->probe (action);
}

gboolean
ppd_action_activate (PpdAction  *action,
                             GError           **error)
{
  g_return_val_if_fail (PPD_IS_ACTION (action), FALSE);

  if (!PPD_ACTION_GET_CLASS (action)->activate)
    return TRUE;

  return PPD_ACTION_GET_CLASS (action)->activate (action, error);
}

gboolean
ppd_action_deactivate (PpdAction  *action,
                               GError           **error)
{
  g_return_val_if_fail (PPD_IS_ACTION (action), FALSE);

  if (!PPD_ACTION_GET_CLASS (action)->deactivate)
    return TRUE;

  return PPD_ACTION_GET_CLASS (action)->deactivate (action, error);
}

const char *
ppd_action_get_action_name (PpdAction *action)
{
  PpdActionPrivate *priv;

  g_return_val_if_fail (PPD_IS_ACTION (action), NULL);

  priv = PPD_ACTION_GET_PRIVATE (action);
  return priv->action_name;
}

PpdProfile
ppd_action_get_profile (PpdAction *action)
{
  PpdActionPrivate *priv;

  g_return_val_if_fail (PPD_IS_ACTION (action), PPD_PROFILE_BALANCED);

  priv = PPD_ACTION_GET_PRIVATE (action);
  return priv->profile;
}
