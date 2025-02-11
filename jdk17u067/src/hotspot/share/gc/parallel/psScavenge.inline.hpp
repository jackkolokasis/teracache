/*
 * Copyright (c) 2002, 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_PARALLEL_PSSCAVENGE_INLINE_HPP
#define SHARE_GC_PARALLEL_PSSCAVENGE_INLINE_HPP

#include "gc/parallel/psScavenge.hpp"

#include "gc/parallel/parallelScavengeHeap.hpp"
#include "logging/log.hpp"
#include "memory/iterator.hpp"
#include "memory/resourceArea.hpp"
#include "oops/access.inline.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/globalDefinitions.hpp"

inline void PSScavenge::save_to_space_top_before_gc() {
  ParallelScavengeHeap* heap = ParallelScavengeHeap::heap();
  _to_space_top_before_gc = heap->young_gen()->to_space()->top();
}

template <class T> inline bool PSScavenge::should_scavenge(T* p) {
  T heap_oop = RawAccess<>::oop_load(p);
  return PSScavenge::is_obj_in_young(heap_oop);
}

#ifdef TERA_MINOR_GC
template <class T> 
inline bool PSScavenge::h2_should_scavenge(T* p) {
  T heap_oop = RawAccess<>::oop_load(p);

  if (CompressedOops::is_null(heap_oop))
    return false;

  oop obj = CompressedOops::decode_not_null(heap_oop);

	if (Universe::teraHeap()->is_obj_in_h2(obj)) {
    // Check if we have references between two different individual regions
    Universe::teraHeap()->group_regions((HeapWord *)p, cast_from_oop<HeapWord*>(obj));
#ifdef BACK_REF_STAT
		Universe::teraHeap()->h2_update_back_ref_stats(false, true);
#endif
		return false;
	}
	else if (PSScavenge::is_obj_in_young(heap_oop)) {
#ifdef BACK_REF_STAT
		Universe::teraHeap()->h2_update_back_ref_stats(false, false);
#endif
		return true;
	}
	else {
		assert(Universe::teraHeap()->is_field_in_h2((void *)p), "Sanity check");
		PSScavenge::card_table()->inline_write_ref_field_gc(p, obj, true);
#ifdef BACK_REF_STAT
		Universe::teraHeap()->h2_update_back_ref_stats(true, false);
#endif
		return false;
	}
}

template <class T> 
inline bool PSScavenge::h2_should_trace(T* p) {
  T heap_oop = RawAccess<>::oop_load(p);

  if (CompressedOops::is_null(heap_oop))
    return false;

  oop obj = CompressedOops::decode_not_null(heap_oop);

	if (Universe::teraHeap()->is_obj_in_h2(obj)) {
		// Group regions if the references belong to two individual groups
		Universe::teraHeap()->group_regions((HeapWord *)p, cast_from_oop<HeapWord *>(obj));
		return false;
	}
		
  Universe::teraHeap()->h2_push_backward_reference((void *)p, obj);
  PSScavenge::card_table()->inline_write_ref_field_gc(p, obj, !PSScavenge::is_obj_in_young(heap_oop));
  return false;
}

#endif // TERA_MINOR_GC

template <class T>
inline bool PSScavenge::should_scavenge(T* p, MutableSpace* to_space) {
  if (should_scavenge(p)) {
    oop obj = RawAccess<IS_NOT_NULL>::oop_load(p);
    // Skip objects copied to to_space since the scavenge started.
    HeapWord* const addr = cast_from_oop<HeapWord*>(obj);
    return addr < to_space_top_before_gc() || addr >= to_space->end();
  }
  return false;
}

template <class T>
inline bool PSScavenge::should_scavenge(T* p, bool check_to_space) {
  if (check_to_space) {
    ParallelScavengeHeap* heap = ParallelScavengeHeap::heap();
    return should_scavenge(p, heap->young_gen()->to_space());
  }
  return should_scavenge(p);
}

#endif // SHARE_GC_PARALLEL_PSSCAVENGE_INLINE_HPP
