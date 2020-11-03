/*
 * Copyright (c) 2011, 2014, 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <termios.h>

#include "input-event-codes.h"
#include <glib.h>
#include <linux/uinput.h>

typedef struct {
  GMainLoop *loop;
  gboolean inited_kbd;

  int uinput;
  gboolean in_proximity;
  struct termios old_tio;
} OrientationData;

static gboolean
send_uinput_event (OrientationData *data)
{
  struct input_event ev;

  memset(&ev, 0, sizeof(ev));

  ev.type = EV_SW;
  ev.code = SW_LAP_PROXIMITY;
  ev.value = data->in_proximity;
  (void) !write (data->uinput, &ev, sizeof(ev));

  memset(&ev, 0, sizeof(ev));
  gettimeofday(&ev.time, NULL);
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  (void) !write (data->uinput, &ev, sizeof(ev));

  return TRUE;
}

static gboolean
setup_uinput (OrientationData *data)
{
  struct uinput_user_dev dev;
  int fd;

  fd = open("/dev/uinput", O_RDWR);
  if (fd < 0) {
    g_warning ("Could not open uinput");
    return FALSE;
  }

  memset (&dev, 0, sizeof(dev));
  snprintf (dev.name, sizeof (dev.name), "%s", "Thinkpad proximity switches");
  dev.id.bustype = BUS_VIRTUAL;
  dev.id.vendor = 0x00;
  dev.id.product = 0x00;

  if (write (fd, &dev, sizeof(dev)) != sizeof(dev)) {
    g_warning ("Error creating uinput device");
    goto bail;
  }

  /* enabling switch events */
  if (ioctl (fd, UI_SET_EVBIT, EV_SW) < 0) {
    g_warning ("Error enabling uinput switch events");
    goto bail;
  }

  /* enabling switch */
  if (ioctl (fd, UI_SET_SWBIT, SW_LAP_PROXIMITY) < 0) {
    g_warning ("Couldn't enable lap proximity switch");
    goto bail;
  }

  /* creating the device */
  if (ioctl (fd, UI_DEV_CREATE) < 0) {
    g_warning ("Error creating uinput device");
    goto bail;
  }

  data->uinput = fd;

  return TRUE;

bail:
  close (fd);
  return FALSE;
}

static void
keyboard_usage (void)
{
  g_print ("Valid keys are: c (close, in proximity), f (far), q/x (quit)\n");
}

static gboolean
check_keyboard (GIOChannel      *source,
                GIOCondition     condition,
                OrientationData *data)
{
  GIOStatus status;
  char buf[1];
  gboolean close = FALSE;

  status = g_io_channel_read_chars (source, buf, 1, NULL, NULL);
  if (status == G_IO_STATUS_ERROR ||
      status == G_IO_STATUS_EOF) {
    g_main_loop_quit (data->loop);
    return FALSE;
  }

  if (status == G_IO_STATUS_AGAIN)
    return TRUE;

  switch (buf[0]) {
  case 'c':
    close = TRUE;
    break;
  case 'f':
    close = FALSE;
    break;
  case 'q':
  case 'x':
    g_main_loop_quit (data->loop);
    return FALSE;
  default:
    keyboard_usage ();
    return TRUE;
  }

  data->in_proximity = close;
  send_uinput_event (data);

  return TRUE;
}

static gboolean
setup_keyboard (OrientationData *data)
{
  GIOChannel *channel;
  struct termios new_tio;

  tcgetattr(STDIN_FILENO, &data->old_tio);
  new_tio = data->old_tio;
  new_tio.c_lflag &=(~ICANON & ~ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  data->inited_kbd = TRUE;

  channel = g_io_channel_unix_new (STDIN_FILENO);
  if (!channel) {
    g_warning ("Failed to open stdin");
    return FALSE;
  }

  if (g_io_channel_set_encoding (channel, NULL, NULL) != G_IO_STATUS_NORMAL) {
    g_warning ("Failed to set stdin encoding to NULL");
    return FALSE;
  }

  g_io_add_watch (channel, G_IO_IN, (GIOFunc) check_keyboard, data);
  return TRUE;
}

static void
free_orientation_data (OrientationData *data)
{
  if (data == NULL)
    return;

  if (data->inited_kbd)
    tcsetattr(STDIN_FILENO, TCSANOW, &data->old_tio);

  if (data->uinput > 0)
    close (data->uinput);
  g_clear_pointer (&data->loop, g_main_loop_unref);
  g_free (data);
}

int main (int argc, char **argv)
{
  OrientationData *data;
  int ret = 0;

  data = g_new0 (OrientationData, 1);

  /* Set up uinput */
  if (!setup_uinput (data)) {
    ret = 1;
    goto out;
  }

  if (!setup_keyboard (data)) {
    g_warning ("Failed to setup keyboard capture");
    ret = 1;
    goto out;
  }

  /* Start with the laptop away from the lap */
  data->in_proximity = FALSE;

  send_uinput_event (data);
  keyboard_usage ();

  data->loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (data->loop);

out:
  free_orientation_data (data);

  return ret;
}
