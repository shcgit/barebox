/*
 * Copyright (c) 2016-2023, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Imported from TF-A v2.10.0
 */

#define pr_fmt(fmt) "fip: " fmt

#include <unistd.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/bug.h>
#include <linux/log2.h>
#include <malloc.h>
#include <errno.h>
#include <linux/limits.h>
#include <linux/uuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfile.h>

#include <fip.h>
#include <fiptool.h>

image_desc_t *new_image_desc(const uuid_t *uuid,
			     const char *name, const char *cmdline_name)
{
	image_desc_t *desc;

	desc = xzalloc(sizeof(*desc));
	memcpy(&desc->uuid, uuid, sizeof(uuid_t));
	desc->name = xstrdup(name);
	desc->cmdline_name = xstrdup(cmdline_name);
	desc->action = DO_UNSPEC;
	return desc;
}

void set_image_desc_action(image_desc_t *desc, int action,
    const char *arg)
{
	ASSERT(desc != NULL);

	if (desc->action_arg != (char *)DO_UNSPEC)
		free(desc->action_arg);
	desc->action = action;
	desc->action_arg = NULL;
	if (arg != NULL)
		desc->action_arg = xstrdup(arg);
}

void free_image_desc(image_desc_t *desc)
{
	free(desc->name);
	free(desc->cmdline_name);
	free(desc->action_arg);
	if (desc->image) {
		free(desc->image->buffer);
		free(desc->image);
	}
	free(desc);
}

void add_image_desc(struct fip_state *fip, image_desc_t *desc)
{
	image_desc_t **p = &fip->image_desc_head;

	while (*p)
		p = &(*p)->next;

	ASSERT(*p == NULL);
	*p = desc;
	fip->nr_image_descs++;
}

void free_image_descs(struct fip_state *fip)
{
	image_desc_t *desc = fip->image_desc_head, *tmp;

	while (desc != NULL) {
		tmp = desc->next;
		free_image_desc(desc);
		desc = tmp;
		fip->nr_image_descs--;
	}
	ASSERT(fip->nr_image_descs == 0);
}

void fill_image_descs(struct fip_state *fip)
{
	toc_entry_t *toc_entry;

	for (toc_entry = toc_entries;
	     toc_entry->cmdline_name != NULL;
	     toc_entry++) {
		image_desc_t *desc;

		desc = new_image_desc(&toc_entry->uuid,
		    toc_entry->name,
		    toc_entry->cmdline_name);
		add_image_desc(fip, desc);
	}
	for (toc_entry = plat_def_toc_entries;
	     toc_entry->cmdline_name != NULL;
	     toc_entry++) {
		image_desc_t *desc;

		desc = new_image_desc(&toc_entry->uuid,
		    toc_entry->name,
		    toc_entry->cmdline_name);
		add_image_desc(fip, desc);
	}
}

image_desc_t *lookup_image_desc_from_uuid(struct fip_state *fip,
						 const uuid_t *uuid)
{
	image_desc_t *desc;

	for (desc = fip->image_desc_head; desc != NULL; desc = desc->next)
		if (memcmp(&desc->uuid, uuid, sizeof(uuid_t)) == 0)
			return desc;
	return NULL;
}

image_desc_t *lookup_image_desc_from_opt(struct fip_state *fip, char **arg)
{
	int len = 0;
	image_desc_t *desc;
	char *eq;

	eq = strchrnul(*arg, '=');
	len = eq - *arg;

	for (desc = fip->image_desc_head; desc != NULL; desc = desc->next) {
		if (strncmp(desc->cmdline_name, *arg, len) == 0) {
			if (*eq)
				*arg = eq + 1;
			return desc;
		}
	}

	printf("unknown image type '%.*s'\n", len, *arg);
	return NULL;
}

int parse_fip(struct fip_state *fip,
		     const char *filename, fip_toc_header_t *toc_header_out)
{
	struct stat st;
	int fd;
	char *buf, *bufend;
	fip_toc_header_t *toc_header;
	fip_toc_entry_t *toc_entry;
	int terminated = 0;
	size_t st_size;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		pr_err("open %s: %m\n", filename);
		return -errno;
	}

	if (fstat(fd, &st) == -1) {
		pr_err("fstat %s: %m\n", filename);
		return -errno;
	}

	st_size = st.st_size;

	buf = xmalloc(st_size);
	if (read_full(fd, buf, st_size) != st_size) {
		pr_err("Failed to read %s: %m\n", filename);
		return -errno;
	}

	bufend = buf + st_size;
	close(fd);

	if (st_size < sizeof(fip_toc_header_t)) {
		pr_err("FIP %s is truncated\n", filename);
		return -ENODATA;
	}

	toc_header = (fip_toc_header_t *)buf;
	toc_entry = (fip_toc_entry_t *)(toc_header + 1);

	if (toc_header->name != TOC_HEADER_NAME) {
		pr_err("%s is not a FIP file: unknown magic = 0x%08x\n",
		       filename, toc_header->name);
		return -EINVAL;
	}

	/* Return the ToC header if the caller wants it. */
	if (toc_header_out != NULL)
		*toc_header_out = *toc_header;

	/* Walk through each ToC entry in the file. */
	while ((char *)toc_entry + sizeof(*toc_entry) - 1 < bufend) {
		image_t *image;
		image_desc_t *desc;

		/* Found the ToC terminator, we are done. */
		if (memcmp(&toc_entry->uuid, &uuid_null, sizeof(uuid_t)) == 0) {
			terminated = 1;
			break;
		}

		/*
		 * Build a new image out of the ToC entry and add it to the
		 * table of images.
		 */
		image = xzalloc(sizeof(*image));
		image->toc_e = *toc_entry;
		image->buffer = xmalloc(toc_entry->size);
		/* Overflow checks before memory copy. */
		if (toc_entry->size > (uint64_t)-1 - toc_entry->offset_address) {
			pr_err("FIP %s is corrupted: entry size exceeds 64 bit address space\n",
			       filename);
			return -EINVAL;
		}
		if (toc_entry->size + toc_entry->offset_address > st_size) {
			pr_err("FIP %s is corrupted: entry size (0x%llx) exceeds FIP file size (0x%zx)\n",
				filename, toc_entry->size + toc_entry->offset_address, st_size);
			return -EINVAL;
		}

		memcpy(image->buffer, buf + toc_entry->offset_address,
		    toc_entry->size);

		/* If this is an unknown image, create a descriptor for it. */
		desc = lookup_image_desc_from_uuid(fip, &toc_entry->uuid);
		if (desc == NULL) {
			char name[UUID_STRING_LEN + 1], filename[PATH_MAX];

			snprintf(name, sizeof(name), "%pU", &toc_entry->uuid);
			snprintf(filename, sizeof(filename), "%s%s",
			    name, ".bin");
			desc = new_image_desc(&toc_entry->uuid, name, "blob");
			desc->action = DO_UNPACK;
			desc->action_arg = xstrdup(filename);
			add_image_desc(fip, desc);
		}

		ASSERT(desc->image == NULL);
		desc->image = image;

		toc_entry++;
	}

	if (terminated == 0) {
		pr_err("FIP %s does not have a ToC terminator entry\n",
		    filename);
		return -EINVAL;
	}
	free(buf);
	return 0;
}

static image_t *read_image_from_file(const uuid_t *uuid, const char *filename)
{
	struct stat st;
	image_t *image;
	int fd;

	ASSERT(uuid != NULL);
	ASSERT(filename != NULL);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		pr_err("open %s: %m\n", filename);
		return NULL;
	}

	if (fstat(fd, &st) == -1) {
		pr_err("fstat %s: %m\n", filename);
		return NULL;
	}

	image = xzalloc(sizeof(*image));
	image->toc_e.uuid = *uuid;
	image->buffer = xmalloc(st.st_size);
	if (read_full(fd, image->buffer, st.st_size) != st.st_size) {
		pr_err("Failed to read %s: %m\n", filename);
		return NULL;
	}
	image->toc_e.size = st.st_size;

	close(fd);
	return image;
}

int pack_images(struct fip_state *fip,
		const char *filename,
		uint64_t toc_flags, unsigned long align)
{
	int fd;
	image_desc_t *desc;
	fip_toc_header_t *toc_header;
	fip_toc_entry_t *toc_entry;
	char *buf;
	uint64_t entry_offset, buf_size, payload_size = 0, pad_size;
	size_t nr_images = 0;

	for (desc = fip->image_desc_head; desc != NULL; desc = desc->next)
		if (desc->image != NULL)
			nr_images++;

	buf_size = sizeof(fip_toc_header_t) +
	    sizeof(fip_toc_entry_t) * (nr_images + 1);
	buf = calloc(1, buf_size);
	if (buf == NULL)
		return -ENOMEM;

	/* Build up header and ToC entries from the image table. */
	toc_header = (fip_toc_header_t *)buf;
	toc_header->name = TOC_HEADER_NAME;
	toc_header->serial_number = TOC_HEADER_SERIAL_NUMBER;
	toc_header->flags = toc_flags;

	toc_entry = (fip_toc_entry_t *)(toc_header + 1);

	entry_offset = buf_size;
	for (desc = fip->image_desc_head; desc != NULL; desc = desc->next) {
		image_t *image = desc->image;

		if (image == NULL || (image->toc_e.size == 0ULL))
			continue;
		payload_size += image->toc_e.size;
		entry_offset = (entry_offset + align - 1) & ~(align - 1);
		image->toc_e.offset_address = entry_offset;
		*toc_entry++ = image->toc_e;
		entry_offset += image->toc_e.size;
	}

	/*
	 * Append a null uuid entry to mark the end of ToC entries.
	 * NOTE the offset address for the last toc_entry must match the fip
	 * size.
	 */
	memset(toc_entry, 0, sizeof(*toc_entry));
	toc_entry->offset_address = (entry_offset + align - 1) & ~(align - 1);

	/* Generate the FIP file. */
	fd = creat(filename, 0777);
	if (fd < 0) {
		pr_err("creat %s: %m\n", filename);
		return -errno;
	}

	pr_verbose("Metadata size: %llu bytes\n", buf_size);

	if (write_full(fd, buf, buf_size) < 0) {
		pr_err("Failed to write %s: %m\n", filename);
		return -errno;
	}

	pr_verbose("Payload size: %llu bytes\n", payload_size);

	for (desc = fip->image_desc_head; desc != NULL; desc = desc->next) {
		image_t *image = desc->image;

		if (image == NULL)
			continue;
		if (pwrite_full(fd, image->buffer, image->toc_e.size, image->toc_e.offset_address) < 0) {
			pr_err("Failed to write %s: %m\n", filename);
			return -errno;
		}
	}

	if (lseek(fd, entry_offset, SEEK_SET) < 0) {
		pr_err("Failed to set file position: %m\n");
		return -errno;
	}

	pad_size = toc_entry->offset_address - entry_offset;
	while (pad_size--) {
		uint8_t zero = 0x00;
		write(fd, &zero, sizeof(zero));
	}

	free(buf);
	close(fd);
	return 0;
}

/*
 * This function is shared between the create and update subcommands.
 * The difference between the two subcommands is that when the FIP file
 * is created, the parsing of an existing FIP is skipped.  This results
 * in update_fip() creating the new FIP file from scratch because the
 * internal image table is not populated.
 */
int update_fip(struct fip_state *fip)
{
	image_desc_t *desc;

	/* Add or replace images in the FIP file. */
	for (desc = fip->image_desc_head; desc != NULL; desc = desc->next) {
		image_t *image;

		if (desc->action != DO_PACK)
			continue;

		image = read_image_from_file(&desc->uuid,
		    desc->action_arg);
		if (!image)
			return -1;

		if (desc->image != NULL) {
			pr_verbose("Replacing %s with %s\n",
				   desc->cmdline_name,
				   desc->action_arg);
			free(desc->image);
			desc->image = image;
		} else {
			pr_verbose("Adding image %s\n", desc->action_arg);
			desc->image = image;
		}
	}

	return 0;
}
