/*
 * OConst - consistent distribution
 *
 * Copyright (c) 2013, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the 2-clause BSD license.
 * See the file `LICENSE` for details.
 *
 */

#ifndef _OSCON_H
#define _OSCON_H 1

typedef struct oconst_s oconst_t;
typedef uint32_t (oconst_hash_uint32_func)(const void *entry, uint32_t point, void *context);

// create a new continuum for the given entries using the provided parameters
oconst_t *oconst_create(void **entries, size_t entry_count, size_t points_per_entry,
                        oconst_hash_uint32_func *hash_func, void *hash_context);
void oconst_free(oconst_t *oc);

// find the entry just after the value given as a parameter
void *oconst_lookup(const oconst_t *oc, uint32_t hash_value);

#endif // !_OSCON_H
