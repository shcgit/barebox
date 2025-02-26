/*
 * Copyright (c) 2014-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef FIPTOOL_H
#define FIPTOOL_H

#include <linux/uuid.h>
#include <fip.h>

enum {
	DO_UNSPEC = 0,
	DO_PACK   = 1,
	DO_UNPACK = 2,
	DO_REMOVE = 3
};

typedef struct image_desc {
	uuid_t             uuid;
	char              *name;
	char              *cmdline_name;
	int                action;
	char              *action_arg;
	struct image      *image;
	struct image_desc *next;
} image_desc_t;

typedef struct image {
	struct fip_toc_entry toc_e;
	void                *buffer;
} image_t;

struct fip_state {
	image_desc_t *image_desc_head;
	size_t nr_image_descs;
	int verbose;
};

#define pr_verbose(...) do { \
	if (fip->verbose) { \
		pr_info(__VA_ARGS__); \
	} else { \
		pr_debug(__VA_ARGS__); \
	} \
} while (0)

image_desc_t *new_image_desc(const uuid_t *uuid,
			     const char *name, const char *cmdline_name);

void set_image_desc_action(image_desc_t *desc, int action,
    const char *arg);

void free_image_desc(image_desc_t *desc);

void add_image_desc(struct fip_state *fip, image_desc_t *desc);

void free_image_descs(struct fip_state *fip);

void fill_image_descs(struct fip_state *fip);

image_desc_t *lookup_image_desc_from_uuid(struct fip_state *fip,
						 const uuid_t *uuid);

image_desc_t *lookup_image_desc_from_opt(struct fip_state *fip, char **arg);

int parse_fip(struct fip_state *fip,
		     const char *filename, fip_toc_header_t *toc_header_out);

int pack_images(struct fip_state *fip,
		const char *filename,
		uint64_t toc_flags, unsigned long align);

int update_fip(struct fip_state *fip);

#define TOC_HEADER_SERIAL_NUMBER 0x12345678

typedef struct toc_entry {
	char         *name;
	uuid_t        uuid;
	char         *cmdline_name;
} toc_entry_t;

extern toc_entry_t toc_entries[];
extern toc_entry_t plat_def_toc_entries[];

#endif /* FIPTOOL_H */
