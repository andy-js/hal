/***************************************************************************
 * CVSID: $Id$
 *
 * hotplug.c : Handling of hotplug events
 *
 * Copyright (C) 2004 David Zeuthen, <david@fubar.dk>
 *
 * Licensed under the Academic Free License version 2.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "../osspec.h"
#include "../logger.h"
#include "../hald.h"
#include "../callout.h"
#include "../device_info.h"
#include "../hald_conf.h"

#include "hotplug.h"
#include "physdev.h"
#include "classdev.h"
#include "blockdev.h"


/** Queue of ordered hotplug events */
GQueue *hotplug_event_queue;

/** List of HotplugEvent objects we are currently processing */
GSList *hotplug_events_in_progress = NULL;

void 
hotplug_event_end (void *end_token)
{
	HotplugEvent *hotplug_event = (HotplugEvent *) end_token;

	hotplug_events_in_progress = g_slist_remove (hotplug_events_in_progress, hotplug_event);
	g_free (hotplug_event);
	hotplug_event_process_queue ();
}

static void
hotplug_event_begin (HotplugEvent *hotplug_event)
{
	static char sys_devices_path[HAL_PATH_MAX];
	static char sys_class_path[HAL_PATH_MAX];
	static char sys_block_path[HAL_PATH_MAX];
	static gsize sys_devices_path_len = 0;
	static gsize sys_class_path_len = 0;
	static gsize sys_block_path_len = 0;

	if (sys_block_path_len == 0) {
		sys_devices_path_len = g_snprintf (sys_devices_path, HAL_PATH_MAX, "%s/devices", hal_sysfs_path);
		sys_class_path_len   = g_snprintf (sys_class_path, HAL_PATH_MAX, "%s/class", hal_sysfs_path);
		sys_block_path_len   = g_snprintf (sys_block_path, HAL_PATH_MAX, "%s/block", hal_sysfs_path);
	}


	if (strncmp (hotplug_event->sysfs_path, sys_devices_path, sys_devices_path_len) == 0) {		
		if (hotplug_event->is_add) {
			HalDevice *parent;
			parent = hal_util_find_closest_ancestor (hotplug_event->sysfs_path);
			hotplug_event_begin_add_physdev (hotplug_event->subsystem, 
							 hotplug_event->sysfs_path, 
							 parent,
							 (void *) hotplug_event);
		} else {
			hotplug_event_begin_remove_physdev (hotplug_event->subsystem, 
							    hotplug_event->sysfs_path, 
							    (void *) hotplug_event);
		}
	} else if (strncmp (hotplug_event->sysfs_path, sys_class_path, sys_class_path_len) == 0) {
		if (hotplug_event->is_add) {
			gchar *target;
			HalDevice *physdev;
			char physdevpath[256];

			/* TODO: fixup net devices by looking at ifindex */
			
			g_snprintf (physdevpath, HAL_PATH_MAX, "%s/device", hotplug_event->sysfs_path);
			if (((target = g_file_read_link (physdevpath, NULL)) != NULL)) {
				gchar *normalized_target;

				normalized_target = hal_util_get_normalized_path (hotplug_event->sysfs_path, target);
				g_free (target);

				physdev = hal_device_store_match_key_value_string (hald_get_gdl (), 
										   "linux.sysfs_path_device", 
										   normalized_target);
				g_free (normalized_target);
			} else {
				physdev = NULL;
			}

			hotplug_event_begin_add_classdev (hotplug_event->subsystem,
							  hotplug_event->sysfs_path,
							  hotplug_event->device_file,
							  physdev,
							  (void *) hotplug_event);
		} else {
			hotplug_event_begin_remove_classdev (hotplug_event->subsystem,
							     hotplug_event->sysfs_path,
							     (void *) hotplug_event);
		}
	} else if (strncmp (hotplug_event->sysfs_path, sys_block_path, sys_block_path_len) == 0) {
		gchar *parent_path;
		gboolean is_partition;
		
		parent_path = hal_util_get_parent_sysfs_path (hotplug_event->sysfs_path);
		is_partition = (strcmp (parent_path, sys_block_path) != 0);
		
		if (hotplug_event->is_add) {
			HalDevice *parent;

			if (is_partition) {
				parent = hal_device_store_match_key_value_string (hald_get_gdl (), 
										  "linux.sysfs_path_device", 
										  parent_path);
			} else {
				gchar *target;
				char physdevpath[256];
				
				g_snprintf (physdevpath, HAL_PATH_MAX, "%s/device", hotplug_event->sysfs_path);
				if (((target = g_file_read_link (physdevpath, NULL)) != NULL)) {
					gchar *normalized_target;

					normalized_target = hal_util_get_normalized_path (hotplug_event->sysfs_path, target);
					g_free (target);
					parent = hal_device_store_match_key_value_string (hald_get_gdl (), 
											  "linux.sysfs_path_device", 
											  normalized_target);
					g_free (normalized_target);
				} else {
					parent = NULL;
				}
			}
			
			hotplug_event_begin_add_blockdev (hotplug_event->sysfs_path,
							  hotplug_event->device_file,
							  is_partition,
							  parent,
							  (void *) hotplug_event);
		} else {
			hotplug_event_begin_remove_blockdev (hotplug_event->sysfs_path,
							     is_partition,
							     (void *) hotplug_event);
		}
	} else {
		/* just ignore this hotplug event */
		hotplug_event_end ((void *) hotplug_event);
	}
}

void 
hotplug_event_enqueue (HotplugEvent *hotplug_event)
{
	if (hotplug_event_queue == NULL)
		hotplug_event_queue = g_queue_new ();

	g_queue_push_tail (hotplug_event_queue, hotplug_event);
}

void 
hotplug_event_process_queue (void)
{
	HotplugEvent *hotplug_event;


	if (hotplug_event_queue == NULL)
		goto out;

	/* do not process events if some other event is in progress 
	 *
	 * TODO: optimize so we can do add events in parallel by inspecting the
	 *       wait_for_sysfs_path parameter and hotplug_events_in_progress list
	 */
	if (hotplug_events_in_progress != NULL && g_slist_length (hotplug_events_in_progress) > 0)
		goto out;

	hotplug_event = g_queue_pop_head (hotplug_event_queue);
	if (hotplug_event == NULL)
		goto out;

	hotplug_events_in_progress = g_slist_append (hotplug_events_in_progress, hotplug_event);
	hotplug_event_begin (hotplug_event);

out:
	;	
}