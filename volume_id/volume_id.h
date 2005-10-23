/*
 * volume_id - reads partition label and uuid
 *
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 */

#ifndef _VOLUME_ID_H_
#define _VOLUME_ID_H_

#include <stdint.h>

#define VOLUME_ID_VERSION		52

#define VOLUME_ID_LABEL_SIZE		64
#define VOLUME_ID_UUID_SIZE		36
#define VOLUME_ID_FORMAT_SIZE		32
#define VOLUME_ID_PATH_MAX		256
#define VOLUME_ID_PARTITIONS_MAX	256

enum volume_id_usage {
	VOLUME_ID_UNUSED,
	VOLUME_ID_UNPROBED,
	VOLUME_ID_OTHER,
	VOLUME_ID_FILESYSTEM,
	VOLUME_ID_PARTITIONTABLE,
	VOLUME_ID_RAID,
	VOLUME_ID_DISKLABEL,
	VOLUME_ID_CRYPTO,
};

struct volume_id_partition {
	enum		volume_id_usage usage_id;
	char		*usage;
	char		*type;
	uint64_t	off;
	uint64_t	len;
	uint8_t		partition_type_raw;
};

struct volume_id {
	uint8_t		label_raw[VOLUME_ID_LABEL_SIZE];
	size_t		label_raw_len;
	char		label[VOLUME_ID_LABEL_SIZE+1];
	uint8_t		uuid_raw[VOLUME_ID_UUID_SIZE];
	size_t		uuid_raw_len;
	char		uuid[VOLUME_ID_UUID_SIZE+1];
	enum		volume_id_usage usage_id;
	char		*usage;
	char		*type;
	char		type_version[VOLUME_ID_FORMAT_SIZE];

	struct volume_id_partition *partitions;
	size_t		partition_count;

	int		fd;
	uint8_t		*sbbuf;
	size_t		sbbuf_len;
	uint8_t		*seekbuf;
	uint64_t	seekbuf_off;
	size_t		seekbuf_len;
	int		fd_close:1;
};

extern struct volume_id *volume_id_open_fd(int fd);
extern struct volume_id *volume_id_open_node(const char *path);
extern struct volume_id *volume_id_open_dev_t(dev_t devt);
extern int volume_id_probe_all(struct volume_id *id, uint64_t off, uint64_t size);
extern void volume_id_close(struct volume_id *id);

#endif
