/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-driver-fake.h"

#include <unistd.h>
#include <stdio.h>
#include <termios.h>

extern void main_loop_quit (void);
void restart_profile_drivers (void);

struct _PpdDriverFake
{
  PpdDriver  parent_instance;

  gboolean tio_set;
  struct termios old_tio;
  gboolean inhibited;
};

G_DEFINE_TYPE (PpdDriverFake, ppd_driver_fake, PPD_TYPE_DRIVER)

static GObject*
ppd_driver_fake_constructor (GType                  type,
                             guint                  n_construct_params,
                             GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_driver_fake_parent_class)->constructor (type,
                                                                       n_construct_params,
                                                                       construct_params);
  g_object_set (object,
                "driver-name", "fake",
                "profiles", PPD_PROFILE_ALL,
                NULL);

  return object;
}

static void
toggle_inhibition (PpdDriverFake *fake)
{
  fake->inhibited = !fake->inhibited;

  g_object_set (G_OBJECT (fake),
                "performance-inhibited", fake->inhibited ? "lap-detected" : NULL,
                NULL);
}

static void
keyboard_usage (void)
{
  g_print ("Valid keys are: i (toggle inhibition), r (restart drivers), q/x (quit)\n");
}

static gboolean
check_keyboard (GIOChannel    *source,
                GIOCondition   condition,
                PpdDriverFake *fake)
{
  GIOStatus status;
  char buf[1];

  status = g_io_channel_read_chars (source, buf, 1, NULL, NULL);
  if (status == G_IO_STATUS_ERROR ||
      status == G_IO_STATUS_EOF) {
    g_warning ("Error checking keyboard");
    return FALSE;
  }

  if (status == G_IO_STATUS_AGAIN)
    return TRUE;

  switch (buf[0]) {
  case 'i':
    g_print ("Toggling inhibition\n");
    toggle_inhibition (fake);
    break;
  case 'r':
    g_print ("Restarting profile drivers\n");
    restart_profile_drivers ();
    break;
  case 'q':
  case 'x':
    main_loop_quit ();
    break;
  default:
    keyboard_usage ();
    return TRUE;
  }

  return TRUE;
}

static gboolean
setup_keyboard (PpdDriverFake *fake)
{
  GIOChannel *channel;
  struct termios new_tio;

  tcgetattr(STDIN_FILENO, &fake->old_tio);
  new_tio = fake->old_tio;
  new_tio.c_lflag &=(~ICANON & ~ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  channel = g_io_channel_unix_new (STDIN_FILENO);
  if (!channel) {
    g_warning ("Failed to open stdin");
    return FALSE;
  }

  if (g_io_channel_set_encoding (channel, NULL, NULL) != G_IO_STATUS_NORMAL) {
    g_warning ("Failed to set stdin encoding to NULL");
    return FALSE;
  }

  g_io_add_watch (channel, G_IO_IN, (GIOFunc) check_keyboard, fake);
  fake->tio_set = TRUE;
  return TRUE;
}

static gboolean
envvar_set (const char *key)
{
  const char *value;

  value = g_getenv (key);
  if (value == NULL ||
      *value == '0' ||
      *value == 'f')
    return FALSE;

  return TRUE;
}

static PpdProbeResult
ppd_driver_fake_probe (PpdDriver  *driver,
                       PpdProfile *prev_profile)
{
  PpdDriverFake *fake;

  if (!envvar_set ("POWER_PROFILE_DAEMON_FAKE_DRIVER"))
    return PPD_PROBE_RESULT_FAIL;

  fake = PPD_DRIVER_FAKE (driver);
  if (!setup_keyboard (fake))
    return PPD_PROBE_RESULT_FAIL;
  keyboard_usage ();

  return PPD_PROBE_RESULT_SUCCESS;
}

static gboolean
ppd_driver_fake_activate_profile (PpdDriver                   *driver,
                                  PpdProfile                   profile,
                                  PpdProfileActivationReason   reason,
                                  GError                     **error)
{
  g_print ("Receive '%s' profile activation for reason '%s'\n",
           ppd_profile_to_str (profile),
           ppd_profile_activation_reason_to_str (reason));

  return TRUE;
}

static void
ppd_driver_fake_finalize (GObject *object)
{
  PpdDriverFake *fake;

  fake = PPD_DRIVER_FAKE (object);
  if (fake->tio_set)
    tcsetattr(STDIN_FILENO, TCSANOW, &fake->old_tio);
  G_OBJECT_CLASS (ppd_driver_fake_parent_class)->finalize (object);
}

static void
ppd_driver_fake_class_init (PpdDriverFakeClass *klass)
{
  GObjectClass *object_class;
  PpdDriverClass *driver_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_driver_fake_constructor;
  object_class->finalize = ppd_driver_fake_finalize;

  driver_class = PPD_DRIVER_CLASS(klass);
  driver_class->probe = ppd_driver_fake_probe;
  driver_class->activate_profile = ppd_driver_fake_activate_profile;
}

static void
ppd_driver_fake_init (PpdDriverFake *self)
{
}
