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

/**
 * SECTION:ppd-action
 * @Short_description: Profile Actions
 * @Title: Profile Actions
 *
 * Profile actions are actions to run on profile change that do not affect
 * the overall power usage, or performance level of the system, but instead
 * of individual components.
 *
 * For example, an action might want to save energy when in the `power-saver`
 * profile, and thus reduce the charging speed of a particular device. Or it
 * could automatically reduce the speed of animations, or luminosity of an
 * RGB keyboard.
 *
 * The list of actions that are currently running is available through the
 * D-Bus API.
 *
 * Note that `power-profiles-daemon` can only accept #PpdAction<!-- -->s that
 * will not make devices appear “broken” to users not in the know, so actions
 * will never disable Wi-Fi or Bluetooth, or make some buttons stop working
 * until power saving is turned off.
 */

typedef struct
{
  char          *action_name;
  PpdProfile     profile;
} PpdActionPrivate;

enum {
  PROP_0,
  PROP_ACTION_NAME
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

  /**
   * PpdAction::action-name:
   *
   * A unique action name, only used for debugging.
   */
  g_object_class_install_property (object_class, PROP_ACTION_NAME,
                                   g_param_spec_string("action-name",
                                                       "Action name",
                                                       "Action name",
                                                       NULL,
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
ppd_action_activate_profile (PpdAction  *action,
                             PpdProfile         profile,
                             GError           **error)
{
  g_return_val_if_fail (PPD_IS_ACTION (action), FALSE);

  if (!PPD_ACTION_GET_CLASS (action)->activate_profile)
    return TRUE;

  return PPD_ACTION_GET_CLASS (action)->activate_profile (action, profile, error);
}

const char *
ppd_action_get_action_name (PpdAction *action)
{
  PpdActionPrivate *priv;

  g_return_val_if_fail (PPD_IS_ACTION (action), NULL);

  priv = PPD_ACTION_GET_PRIVATE (action);
  return priv->action_name;
}
