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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oconst.h"

typedef struct oconst_point_s
{
  uint32_t hash_value;
  void *entry;
} oconst_point_t;

struct oconst_s
{
  uint32_t point_count;
  oconst_point_t points[];
};

oconst_t *oconst_create(void **entries, size_t entry_count, size_t points_per_entry,
                        oconst_hash_uint32_func *hash_func, void *hash_context)
{
  size_t cidx = 0, total_points = entry_count * points_per_entry;
  oconst_t *oc = (oconst_t *) malloc(sizeof(oconst_t) + total_points * sizeof(oconst_point_t));

  oc->point_count = total_points;
  for (size_t i=0; i<entry_count; i++)
    {
      for (size_t p=0; p<points_per_entry; p++)
        {
          oc->points[cidx++] = (oconst_point_t) {
            .entry = entries[i],
            .hash_value = hash_func(entries[i], p, hash_context),
            };
        }
    }

  int oc_point_cmp(const void *v1, const void *v2)
    {
      const oconst_point_t *p1 = v1, *p2 = v2;
      if (p1->hash_value == p2->hash_value)
        return 0;
      return p1->hash_value > p2->hash_value ? 1 : -1;
    }
  qsort(oc->points, total_points, sizeof(oconst_point_t), oc_point_cmp);

  return oc;
}

void oconst_free(oconst_t *oc)
{
  if (oc != NULL)
    free(oc);
}

void *oconst_lookup(const oconst_t *oc, uint32_t hash_value)
{
  const oconst_point_t *first = oc->points, *last = oc->points + oc->point_count;
  const oconst_point_t *left = first, *right = last;

  while (left < right)
    {
      const oconst_point_t *middle = left + (right - left) / 2;
      if (middle->hash_value < hash_value)
        left = middle + 1;
      else
        right = middle;
    }
  if (right == last)
    return first->entry;
  return right->entry;
}
