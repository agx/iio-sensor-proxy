/*
 * Copyright (c) 2019 Purism SPC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 */

#include "drivers.h"
#include "iio-buffer-utils.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct DrvData {
	guint               timeout_id;
	ReadingsUpdateFunc  callback_func;
	gpointer            user_data;
	GUdevDevice        *dev;
	const char         *name;
} DrvData;

static DrvData *drv_data = NULL;

static int
sysfs_get_int (GUdevDevice *dev,
	       const char  *attribute)
{
	int result;
	char *contents;
	char *filename;

	result = 0;
	filename = g_build_filename (g_udev_device_get_sysfs_path (dev), attribute, NULL);
	if (g_file_get_contents (filename, &contents, NULL, NULL)) {
		result = atoi (contents);
		g_free (contents);
	}
	g_free (filename);

	return result;
}

static gboolean
poll_proximity (gpointer user_data)
{
	DrvData *data = user_data;
	ProximityReadings readings;

	readings.prox = sysfs_get_int (data->dev, "in_proximity_raw");

	g_debug ("Proximity read from IIO on '%s': %d", data->name, readings.prox);

	drv_data->callback_func (&iio_poll_proximity, (gpointer) &readings, drv_data->user_data);

	return G_SOURCE_CONTINUE;
}

static gboolean
iio_poll_proximity_discover (GUdevDevice *device)
{
	/* We also handle devices with trigger buffers, but there's no trigger available on the system */
	if (g_strcmp0 (g_udev_device_get_property (device, "IIO_SENSOR_PROXY_TYPE"), "iio-poll-proximity") != 0)
		return FALSE;

	g_debug ("Found IIO poll proximity sensor at %s", g_udev_device_get_sysfs_path (device));
	return TRUE;
}

static void
iio_poll_proximity_set_polling (gboolean state)
{
	if (drv_data->timeout_id > 0 && state)
		return;
	if (drv_data->timeout_id == 0 && !state)
		return;

	if (drv_data->timeout_id) {
		g_source_remove (drv_data->timeout_id);
		drv_data->timeout_id = 0;
	}

	if (state) {
		drv_data->timeout_id = g_timeout_add (700, poll_proximity, drv_data);
		g_source_set_name_by_id (drv_data->timeout_id, "[iio_poll_proximity_set_polling] poll_proximity");
	}
}

static gboolean
iio_poll_proximity_open (GUdevDevice        *device,
		     ReadingsUpdateFunc  callback_func,
		     gpointer            user_data)
{
	iio_fixup_sampling_frequency (device);

	drv_data = g_new0 (DrvData, 1);
	drv_data->dev = g_object_ref (device);
	drv_data->name = g_udev_device_get_sysfs_attr (device, "name");
	drv_data->callback_func = callback_func;
	drv_data->user_data = user_data;

	return TRUE;
}

static void
iio_poll_proximity_close (void)
{
	iio_poll_proximity_set_polling (FALSE);
	g_clear_object (&drv_data->dev);
	g_clear_pointer (&drv_data, g_free);
}

SensorDriver iio_poll_proximity = {
	.name = "IIO Poll proximity sensor",
	.type = DRIVER_TYPE_PROXIMITY,
	.specific_type = DRIVER_TYPE_PROXIMITY_IIO,

	.discover = iio_poll_proximity_discover,
	.open = iio_poll_proximity_open,
	.set_polling = iio_poll_proximity_set_polling,
	.close = iio_poll_proximity_close,
};
