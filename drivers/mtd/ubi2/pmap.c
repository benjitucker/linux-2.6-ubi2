/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * The UBI PEB Map (PMAP) sub-system.
 *
 * This sub-system is responsible for mapping logical blocks to physical
 * block chain.
 *
 * TODO - explanation text
 * TODO - move to an RB tree for faster lookups
 *
 */

#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/err.h>
#include "ubi.h"

/**
 * ubi_pmap_init - Initialise a PEB map
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure to be allocated
 *
 * This function initialises the PEB map data structure.
 * Returns 0 on success or -ENOMEM if failed to allocate data structure.
 */

struct ubi_pmap *ubi_pmap_init(struct ubi_device *ubi)
{
	struct ubi_pmap *pmap = 
		vmalloc(ubi->peb_count * (sizeof struct ubi_pmap));
}

/**
 * ubi_pmap_lookup_pnum - Lookup a PEB number from the PEB map
 * @ubi: UBI device description object
 * @peb_map: PEB Map data structure
 * @vol: volume description object
 * @lnum: logical eraseblock number
 *
 * This function searches the PEB map data structure for the PEB number
 * mapped to the volume & LEB number within that volume.
 * Returns the PEB number if found or -1 otherwise.
 */

int ubi_pmap_lookup_pnum(struct ubi_device *ubi, struct ubi_pmap *peb_map,
			 struct ubi_volume *vol, int lnum)
{
	int pnum;

	for (pnum = 0; pnum < ubi->peb_count; ++pnum) {
		pmap = &ubi->peb_map[pnum];
		if (pmap->vol_id == vol->vol_id &&
		    pmap->lnum == lnum && 
		    pmap->inuse &&
		    !pmap->bad) {
			
			return pnum;
		}
	}

	return -1;
}

