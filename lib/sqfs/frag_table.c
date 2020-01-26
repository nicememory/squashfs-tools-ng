/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * frag_table.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#define SQFS_BUILDING_DLL
#include "config.h"

#include "sqfs/frag_table.h"
#include "sqfs/super.h"
#include "sqfs/table.h"
#include "sqfs/error.h"
#include "sqfs/block.h"
#include "compat.h"

#include <stdlib.h>
#include <string.h>


typedef struct {
	sqfs_u32 index;
	sqfs_u32 offset;
	sqfs_u32 size;
	sqfs_u32 hash;
} chunk_info_t;


struct sqfs_frag_table_t {
	size_t capacity;
	size_t used;
	sqfs_fragment_t *table;

	/* TODO: more efficient data structure */
	size_t chunk_num;
	size_t chunk_max;
	chunk_info_t *chunk_list;
};

sqfs_frag_table_t *sqfs_frag_table_create(sqfs_u32 flags)
{
	sqfs_frag_table_t *tbl;

	if (flags != 0)
		return NULL;

	tbl = calloc(1, sizeof(*tbl));
	if (tbl == NULL)
		return NULL;

	return tbl;
}

void sqfs_frag_table_destroy(sqfs_frag_table_t *tbl)
{
	free(tbl->chunk_list);
	free(tbl->table);
	free(tbl);
}

int sqfs_frag_table_read(sqfs_frag_table_t *tbl, sqfs_file_t *file,
			 const sqfs_super_t *super, sqfs_compressor_t *cmp)
{
	sqfs_u64 location, lower, upper;
	void *raw = NULL;
	size_t size;
	int err;

	free(tbl->table);
	tbl->table = NULL;
	tbl->capacity = 0;
	tbl->used = 0;

	if (super->flags & SQFS_FLAG_NO_FRAGMENTS)
		return 0;

	if (super->fragment_table_start == 0xFFFFFFFFFFFFFFFFUL)
		return 0;

	if (super->fragment_entry_count == 0)
		return 0;

	if (super->fragment_table_start >= super->bytes_used)
		return SQFS_ERROR_OUT_OF_BOUNDS;

	/* location must be after inode & directory table,
	   but before the ID table */
	if (super->fragment_table_start < super->directory_table_start)
		return SQFS_ERROR_CORRUPTED;

	if (super->fragment_table_start >= super->id_table_start)
		return SQFS_ERROR_CORRUPTED;

	location = super->fragment_table_start;
	lower = super->directory_table_start;
	upper = super->id_table_start;

	if (super->export_table_start < super->id_table_start)
		upper = super->export_table_start;

	if (SZ_MUL_OV(super->fragment_entry_count, sizeof(sqfs_fragment_t),
		      &size)) {
		return SQFS_ERROR_OVERFLOW;
	}

	err = sqfs_read_table(file, cmp, size, location, lower, upper, &raw);
	if (err) {
		free(raw);
		return err;
	}

	tbl->table = raw;
	tbl->capacity = super->fragment_entry_count;
	tbl->used = super->fragment_entry_count;
	return 0;
}

int sqfs_frag_table_write(sqfs_frag_table_t *tbl, sqfs_file_t *file,
			  sqfs_super_t *super, sqfs_compressor_t *cmp)
{
	size_t i;
	int err;

	if (tbl->used == 0) {
		super->fragment_table_start = 0xFFFFFFFFFFFFFFFF;
		super->flags |= SQFS_FLAG_NO_FRAGMENTS;
		super->flags &= ~SQFS_FLAG_ALWAYS_FRAGMENTS;
		super->flags &= ~SQFS_FLAG_UNCOMPRESSED_FRAGMENTS;
		return 0;
	}

	err = sqfs_write_table(file, cmp, tbl->table,
			       sizeof(tbl->table[0]) * tbl->used,
			       &super->fragment_table_start);
	if (err)
		return err;

	super->fragment_entry_count = tbl->used;
	super->flags &= ~SQFS_FLAG_NO_FRAGMENTS;
	super->flags |= SQFS_FLAG_ALWAYS_FRAGMENTS;
	super->flags |= SQFS_FLAG_UNCOMPRESSED_FRAGMENTS;

	for (i = 0; i < tbl->used; ++i) {
		if (SQFS_IS_BLOCK_COMPRESSED(le32toh(tbl->table[i].size))) {
			super->flags &= ~SQFS_FLAG_UNCOMPRESSED_FRAGMENTS;
			break;
		}
	}

	return 0;
}

int sqfs_frag_table_lookup(sqfs_frag_table_t *tbl, sqfs_u32 index,
			   sqfs_fragment_t *out)
{
	if (index >= tbl->used)
		return SQFS_ERROR_OUT_OF_BOUNDS;

	out->start_offset = le64toh(tbl->table[index].start_offset);
	out->size = le32toh(tbl->table[index].size);
	out->pad0 = le32toh(tbl->table[index].pad0);
	return 0;
}

int sqfs_frag_table_append(sqfs_frag_table_t *tbl, sqfs_u64 location,
			   sqfs_u32 size, sqfs_u32 *index)
{
	size_t new_sz, total;
	void *new;

	if (tbl->used == tbl->capacity) {
		new_sz = tbl->capacity ? tbl->capacity * 2 : 128;

		if (SZ_MUL_OV(new_sz, sizeof(tbl->table[0]), &total))
			return SQFS_ERROR_OVERFLOW;

		new = realloc(tbl->table, total);
		if (new == NULL)
			return SQFS_ERROR_ALLOC;

		tbl->capacity = new_sz;
		tbl->table = new;
	}

	if (index != NULL)
		*index = tbl->used;

	memset(tbl->table + tbl->used, 0, sizeof(tbl->table[0]));
	tbl->table[tbl->used].start_offset = htole64(location);
	tbl->table[tbl->used].size = htole32(size);

	tbl->used += 1;
	return 0;
}

int sqfs_frag_table_set(sqfs_frag_table_t *tbl, sqfs_u32 index,
			sqfs_u64 location, sqfs_u32 size)
{
	if (index >= tbl->used)
		return SQFS_ERROR_OUT_OF_BOUNDS;

	tbl->table[index].start_offset = htole64(location);
	tbl->table[index].size = htole32(size);
	return 0;
}

size_t sqfs_frag_table_get_size(sqfs_frag_table_t *tbl)
{
	return tbl->used;
}

int sqfs_frag_table_add_tail_end(sqfs_frag_table_t *tbl,
				 sqfs_u32 index, sqfs_u32 offset,
				 sqfs_u32 size, sqfs_u32 hash)
{
	size_t new_sz;
	void *new;

	if (tbl->chunk_num == tbl->chunk_max) {
		new_sz = tbl->chunk_max ? tbl->chunk_max * 2 : 128;
		new = realloc(tbl->chunk_list,
			      sizeof(tbl->chunk_list[0]) * new_sz);

		if (new == NULL)
			return SQFS_ERROR_ALLOC;

		tbl->chunk_list = new;
		tbl->chunk_max = new_sz;
	}

	tbl->chunk_list[tbl->chunk_num].index = index;
	tbl->chunk_list[tbl->chunk_num].offset = offset;
	tbl->chunk_list[tbl->chunk_num].size = size;
	tbl->chunk_list[tbl->chunk_num].hash = hash;
	tbl->chunk_num += 1;
	return 0;
}

int sqfs_frag_table_find_tail_end(sqfs_frag_table_t *tbl,
				  sqfs_u32 hash, sqfs_u32 size,
				  sqfs_u32 *index, sqfs_u32 *offset)
{
	size_t i;

	for (i = 0; i < tbl->chunk_num; ++i) {
		if (tbl->chunk_list[i].hash == hash &&
		    tbl->chunk_list[i].size == size) {
			if (index != NULL)
				*index = tbl->chunk_list[i].index;
			if (offset != NULL)
				*offset = tbl->chunk_list[i].offset;
			return 0;
		}
	}

	return SQFS_ERROR_NO_ENTRY;
}
