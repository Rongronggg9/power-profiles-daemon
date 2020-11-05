#include "up-input.h"
#include "ppd-utils.h"
#include "input-event-codes.h"

static int
find_lap_prox_switch (GUdevDevice *dev,
                      gpointer     user_data)
{
  g_autoptr(GUdevDevice) parent = NULL;
  parent = g_udev_device_get_parent (dev);
  if (!parent)
    return -1;
  return g_strcmp0 (g_udev_device_get_property (parent, "NAME"), "\"Thinkpad proximity switches\"");
}

static void
usage (char **argv)
{
  g_print ("Usage: %s /dev/input/eventXX\n", argv[0]);
}

int main (int argc, char **argv)
{
  GMainLoop *loop;
  g_autoptr(UpInput) input = NULL;
  g_autoptr(GUdevDevice) device = NULL;

  if (argc != 2) {
    usage (argv);
    return 1;
  }

  device = ppd_utils_find_device ("input",
                                  (GCompareFunc) find_lap_prox_switch,
                                  NULL);

  input = up_input_new_for_switch (SW_LAP_PROXIMITY);
  if (!up_input_coldplug (input, device)) {
    g_warning ("Couldn't coldplug input device");
    return 1;
  }

  loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (loop);

  return 0;
}
