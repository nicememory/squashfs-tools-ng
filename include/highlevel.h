/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * highlevel.h
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#ifndef HIGHLEVEL_H
#define HIGHLEVEL_H

#include "config.h"

#include "sqfs/compress.h"
#include "sqfs/id_table.h"
#include "sqfs/inode.h"
#include "sqfs/data.h"
#include "sqfs/table.h"
#include "sqfs/meta_writer.h"
#include "sqfs/xattr.h"
#include "sqfs/dir.h"
#include "sqfs/io.h"
#include "data_reader.h"
#include "data_writer.h"
#include "fstree.h"

#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>

/*
  High level helper function to serialize an entire file system tree to
  a squashfs inode table and directory table.

  The data is written to the given file descriptor and the super block is
  update accordingly (inode and directory table start and total size).

  The function internally creates two meta data writers and uses
  meta_writer_write_inode to serialize the inode table of the fstree.

  Returns 0 on success. Prints error messages to stderr on failure.
 */
int sqfs_serialize_fstree(sqfs_file_t *file, sqfs_super_t *super, fstree_t *fs,
			  sqfs_compressor_t *cmp, sqfs_id_table_t *idtbl);

/*
  Generate a squahfs xattr table from a file system tree.

  Returns 0 on success. Prints error messages to stderr on failure.
 */
int write_xattr(sqfs_file_t *file, fstree_t *fs, sqfs_super_t *super,
		sqfs_compressor_t *cmp);

/*
  Generate an NFS export table.

  Returns 0 on success. Prints error messages to stderr on failure.
 */
int write_export_table(sqfs_file_t *file, fstree_t *fs, sqfs_super_t *super,
		       sqfs_compressor_t *cmp);

/* Print out fancy statistics for squashfs packing tools */
void sqfs_print_statistics(sqfs_super_t *super, data_writer_stats_t *stats);

void compressor_print_available(void);

E_SQFS_COMPRESSOR compressor_get_default(void);

int compressor_cfg_init_options(sqfs_compressor_config_t *cfg,
				E_SQFS_COMPRESSOR id,
				size_t block_size, char *options);

void compressor_print_help(E_SQFS_COMPRESSOR id);

sqfs_inode_generic_t *tree_node_to_inode(fstree_t *fs, sqfs_id_table_t *idtbl,
					 tree_node_t *node);

int inode_stat(const sqfs_tree_node_t *node, struct stat *sb);

char *sqfs_tree_node_get_path(const sqfs_tree_node_t *node);

#endif /* HIGHLEVEL_H */
