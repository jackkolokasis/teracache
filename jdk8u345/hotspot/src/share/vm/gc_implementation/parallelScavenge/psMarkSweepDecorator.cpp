/*
 * Copyright (c) 2001, 2013, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "classfile/systemDictionary.hpp"
#include "gc_implementation/parallelScavenge/objectStartArray.hpp"
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#include "gc_implementation/parallelScavenge/psMarkSweep.hpp"
#include "gc_implementation/parallelScavenge/psMarkSweepDecorator.hpp"
#include "gc_implementation/shared/liveRange.hpp"
#include "gc_implementation/shared/markSweep.inline.hpp"
#include "gc_implementation/shared/spaceDecorator.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/prefetch.inline.hpp"

PSMarkSweepDecorator* PSMarkSweepDecorator::_destination_decorator = NULL;

extern uint64_t moved_to_h2;

void PSMarkSweepDecorator::set_destination_decorator_tenured() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  _destination_decorator = heap->old_gen()->object_mark_sweep();
}

void PSMarkSweepDecorator::advance_destination_decorator() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  assert(_destination_decorator != NULL, "Sanity");

  PSMarkSweepDecorator* first = heap->old_gen()->object_mark_sweep();
  PSMarkSweepDecorator* second = heap->young_gen()->eden_mark_sweep();
  PSMarkSweepDecorator* third = heap->young_gen()->from_mark_sweep();
  PSMarkSweepDecorator* fourth = heap->young_gen()->to_mark_sweep();

  if ( _destination_decorator == first ) {
    _destination_decorator = second;
  } else if ( _destination_decorator == second ) {
    _destination_decorator = third;
  } else if ( _destination_decorator == third ) {
    _destination_decorator = fourth;
  } else {
    fatal("PSMarkSweep attempting to advance past last compaction area");
  }
}

PSMarkSweepDecorator* PSMarkSweepDecorator::destination_decorator() {
  assert(_destination_decorator != NULL, "Sanity");

  return _destination_decorator;
}

// FIX ME FIX ME FIX ME FIX ME!!!!!!!!!
// The object forwarding code is duplicated. Factor this out!!!!!
//
// This method "precompacts" objects inside its space to dest. It places forwarding
// pointers into markOops for use by adjust_pointers. If "dest" should overflow, we
// finish by compacting into our own space.

void PSMarkSweepDecorator::precompact() {
  // Reset our own compact top.
  set_compaction_top(space()->bottom());

  /* We allow some amount of garbage towards the bottom of the space, so
   * we don't start compacting before there is a significant gain to be made.
   * Occasionally, we want to ensure a full compaction, which is determined
   * by the MarkSweepAlwaysCompactCount parameter. This is a significant
   * performance improvement!
   */
  bool skip_dead = ((PSMarkSweep::total_invocations() % MarkSweepAlwaysCompactCount) != 0);

  size_t allowed_deadspace = 0;
  if (skip_dead) {
    const size_t ratio = allowed_dead_ratio();
    allowed_deadspace = space()->capacity_in_words() * ratio / 100;
  }

  // Fetch the current destination decorator
  PSMarkSweepDecorator* dest = destination_decorator();
  ObjectStartArray* start_array = dest->start_array();

  HeapWord* compact_top = dest->compaction_top();
  HeapWord* compact_end = dest->space()->end();

  HeapWord* q = space()->bottom();
  HeapWord* t = space()->top();

  HeapWord*  end_of_live= q;    /* One byte beyond the last byte of the last
                                   live object. */
  HeapWord*  first_dead = space()->end(); /* The first dead object. */
  LiveRange* liveRange  = NULL; /* The current live range, recorded in the
                                   first header of preceding free area. */
  _first_dead = first_dead;

  const intx interval = PrefetchScanIntervalInBytes;

  while (q < t) {
    assert(oop(q)->mark()->is_marked() || oop(q)->mark()->is_unlocked() ||
           oop(q)->mark()->has_bias_pattern(),
           "these are the only valid states during a mark sweep");
    if (oop(q)->is_gc_marked()) {
      /* prefetch beyond q */
      Prefetch::write(q, interval);
      size_t size = oop(q)->size();

#ifdef TERA_MAJOR_GC
    // Check if the object needs to be moved in TeraCache based on the
    // current policy
    if (EnableTeraHeap && Universe::teraHeap()->h2_transfer_policy(oop(q))) {
#ifdef OBJ_STATS
        if (TeraHeapStatistics) {
          if (oop(q)->is_typeArray())
            Universe::teraHeap()->update_stats_h2_primitive_arrays(size);
        }
#endif

        Universe::teraHeap()->get_resizing_policy()->decrease_h2_candidate_size(oop(q)->size());
      
      // Take a pointer from the region
      HeapWord* h2_obj_addr = (HeapWord*) Universe::teraHeap()->h2_add_object(oop(q), size);
      moved_to_h2 += size * 8;
      assert(Universe::teraHeap()->is_obj_in_h2(oop(h2_obj_addr)), "Pointer from H2 is not valid");

      // Store the forwarding pointer into the mark word
      oop(q)->forward_to(oop(h2_obj_addr));

      // Encoding the pointer should preserve the mark
      assert(oop(q)->is_gc_marked(),  "encoding the pointer should preserve the mark");

      // Move to the next object
      q += size;

      // Set this object as live in the in the precompact space
      end_of_live = q;

      // Continue with the next object
      continue;
    }
#endif
      size_t compaction_max_size = pointer_delta(compact_end, compact_top);

      // This should only happen if a space in the young gen overflows the
      // old gen. If that should happen, we null out the start_array, because
      // the young spaces are not covered by one.
      while(size > compaction_max_size) {
        // First record the last compact_top
        dest->set_compaction_top(compact_top);

        // Advance to the next compaction decorator
        advance_destination_decorator();
        dest = destination_decorator();

        // Update compaction info
        start_array = dest->start_array();
        compact_top = dest->compaction_top();
        compact_end = dest->space()->end();
        assert(compact_top == dest->space()->bottom(), "Advanced to space already in use");
        assert(compact_end > compact_top, "Must always be space remaining");
        compaction_max_size =
          pointer_delta(compact_end, compact_top);
      }

      // store the forwarding pointer into the mark word
      if (q != compact_top) {
        oop(q)->forward_to(oop(compact_top));
        assert(oop(q)->is_gc_marked(), "encoding the pointer should preserve the mark");
      } else {
        // if the object isn't moving we can just set the mark to the default
        // mark and handle it specially later on.
        oop(q)->init_mark();
        assert(oop(q)->forwardee() == NULL, "should be forwarded to NULL");
      }

      // Update object start array
      if (start_array) {
        start_array->allocate_block(compact_top);
      }

      compact_top += size;
      assert(compact_top <= dest->space()->end(),
        "Exceeding space in destination");

      q += size;
      end_of_live = q;
    } else {
      /* run over all the contiguous dead objects */
      HeapWord* end = q;
      do {
        /* prefetch beyond end */
        Prefetch::write(end, interval);
        end += oop(end)->size();
      } while (end < t && (!oop(end)->is_gc_marked()));

      /* See if we might want to pretend this object is alive so that
       * we don't have to compact quite as often.

       * If the amount of garbage does not reach the allowed_deadspace
       * limit and the current object is equall to the compact_top
       * pointer then we precess the following block.
			 *
       * In this case, it is actually a garbage object, but it is
       * treated as a live object. (To be more precise, it is
       * considered that one live object occupies from the current
       * location (q) to the next live object position (end),  and the
       * forwarding pointer is embedded in it. 
			 *
       * In this case, since the block continues until the end, the
       * statements after are not executed. Then, the process proceeds
       * to the next object in the loop.
			 *
       * (whether or not the allowed_deadspace amount has been reached
       * is Determined by PSMarkSweepDecorator::insert_deadspace().
			 *
       * To be precise, PSMarkSweepDecorator::insert_deadspace () has
       * an argument passed by reference, so the value of
       * allowed_deadspace decreases each time it is called. When the
       * amount of garbage reaches the initial amount of
       * allowed_deadspace, allowed_deadspace The value will be 0)
			 */
      if (allowed_deadspace > 0 && q == compact_top) {
        size_t sz = pointer_delta(end, q);
        if (insert_deadspace(allowed_deadspace, q, sz)) {
          size_t compaction_max_size = pointer_delta(compact_end, compact_top);

          // This should only happen if a space in the young gen overflows the
          // old gen. If that should happen, we null out the start_array, because
          // the young spaces are not covered by one.
          while (sz > compaction_max_size) {
            // First record the last compact_top
            dest->set_compaction_top(compact_top);

            // Advance to the next compaction decorator
            advance_destination_decorator();
            dest = destination_decorator();

            // Update compaction info
            start_array = dest->start_array();
            compact_top = dest->compaction_top();
            compact_end = dest->space()->end();
            assert(compact_top == dest->space()->bottom(), "Advanced to space already in use");
            assert(compact_end > compact_top, "Must always be space remaining");
            compaction_max_size =
              pointer_delta(compact_end, compact_top);
          }

          // store the forwarding pointer into the mark word
          if (q != compact_top) {
            oop(q)->forward_to(oop(compact_top));
            assert(oop(q)->is_gc_marked(), "encoding the pointer should preserve the mark");
          } else {
            // if the object isn't moving we can just set the mark to the default
            // mark and handle it specially later on.
            oop(q)->init_mark();
            assert(oop(q)->forwardee() == NULL, "should be forwarded to NULL");
          }

          // Update object start array
          if (start_array) {
            start_array->allocate_block(compact_top);
          }

          compact_top += sz;
          assert(compact_top <= dest->space()->end(),
            "Exceeding space in destination");

          q = end;
          end_of_live = end;
          continue;
        }
      }

      /* for the previous LiveRange, record the end of the live objects. */
      if (liveRange) {
        liveRange->set_end(q);
      }

      /* record the current LiveRange object.
       * liveRange->start() is overlaid on the mark word.
       */
      liveRange = (LiveRange*)q;
      liveRange->set_start(end);
      liveRange->set_end(end);

      /* see if this is the first dead region. */
      if (q < first_dead) {
        first_dead = q;
      }

      /* move on to the next object */
      q = end;
    }
  }

  assert(q == t, "just checking");
  if (liveRange != NULL) {
    liveRange->set_end(q);
  }
  _end_of_live = end_of_live;
  if (end_of_live < first_dead) {
    first_dead = end_of_live;
  }
  _first_dead = first_dead;

  // Update compaction top
  dest->set_compaction_top(compact_top);
}

bool PSMarkSweepDecorator::insert_deadspace(size_t& allowed_deadspace_words,
                                            HeapWord* q, size_t deadlength) {
  if (allowed_deadspace_words >= deadlength) {
    allowed_deadspace_words -= deadlength;
    CollectedHeap::fill_with_object(q, deadlength);
    oop(q)->set_mark(oop(q)->mark()->set_marked());
#ifdef TERA_FLAG
	DEBUG_ONLY(if (EnableTeraHeap) { assert(oop(q)->get_obj_state() == INIT_TF, err_msg("Object state is wrong %lu", oop(q)->get_obj_state())); });
#endif
    assert((int) deadlength == oop(q)->size(), "bad filler object size");
    // Recall that we required "q == compaction_top".
    return true;
  } else {
    allowed_deadspace_words = 0;
    return false;
  }
}

void PSMarkSweepDecorator::adjust_pointers() {
  // adjust all the interior pointers to point at the new locations of objects
  // Used by MarkSweep::mark_sweep_phase3()

  HeapWord* q = space()->bottom();
  HeapWord* t = _end_of_live;  // Established by "prepare_for_compaction".

  assert(_first_dead <= _end_of_live, "Stands to reason, no?");

  if (q < t && _first_dead > q &&
      !oop(q)->is_gc_marked()) {
    // we have a chunk of the space which hasn't moved and we've
    // reinitialized the mark word during the previous pass, so we can't
    // use is_gc_marked for the traversal.
    HeapWord* end = _first_dead;

    while (q < end) {
      // point all the oops to the new location
      size_t size = oop(q)->adjust_pointers();
      q += size;
    }

    if (_first_dead == t) {
      q = t;
    } else {
      // $$$ This is funky.  Using this to read the previously written
      // LiveRange.  See also use below.
      q = (HeapWord*)oop(_first_dead)->mark()->decode_pointer();
    }
  }
  const intx interval = PrefetchScanIntervalInBytes;

  debug_only(HeapWord* prev_q = NULL);
  while (q < t) {
    // prefetch beyond q
    Prefetch::write(q, interval);
    if (oop(q)->is_gc_marked()) {
      // q is alive
      // point all the oops to the new location
      size_t size = oop(q)->adjust_pointers();
      debug_only(prev_q = q);
      q += size;
    } else {
      // q is not a live object, so its mark should point at the next
      // live object
      debug_only(prev_q = q);
      q = (HeapWord*) oop(q)->mark()->decode_pointer();
      assert(q > prev_q, "we should be moving forward through memory");
    }
  }

  assert(q == t, "just checking");
}

#ifdef TERA_MAJOR_GC
void PSMarkSweepDecorator::moveObjToH2(HeapWord *q, HeapWord *compaction_top, size_t size) {
  // Size is in words. Each word is 8 bytes. I use memcpy instead of
  // memmove to avoid the extra copy of the data in the buffer.
#if defined(SYNC)
  Universe::teraHeap()->h2_write((char *)q, (char *)compaction_top, size);
  // Change the value of teraflag in the new location of the object
  oop(compaction_top)->set_in_h2();
  /* Initialize mark word of the destination */
  oop(compaction_top)->init_mark();

#elif defined(FMAP)
  // Change the value of teraflag in the new location of the object
  oop(q)->set_in_h2();
  /* Initialize mark word of the destination */
  oop(q)->init_mark();
  Universe::teraHeap()->h2_write((char *)q, (char *)compaction_top, size);

#elif defined(ASYNC)
  // Change the value of teraflag in the new location of the object
  oop(q)->set_in_h2();
  /* Initialize mark word of the destination */
  oop(q)->init_mark();

#if defined(PR_BUFFER)
  Universe::teraHeap()->h2_promotion_buffer_insert((char *)q, (char *)compaction_top, size);
#else
  Universe::teraHeap()->h2_awrite((char *)q, (char *)compaction_top, size);
#endif

#else
  memcpy(compaction_top, q, size * 8);
  if (!H2LivenessAnalysis)
    // Change the value of teraflag in the new location of the object
    oop(compaction_top)->set_in_h2();
  else {
      oop(compaction_top)->set_live();
    }

  /* Initialize mark word of the destination */
  oop(compaction_top)->init_mark();
#endif // SYNC
}
#endif // TERA_MAJOR_GC

void PSMarkSweepDecorator::compact(bool mangle_free_space ) {
  // Copy all live objects to their new location
  // Used by MarkSweep::mark_sweep_phase4()

  HeapWord*       q = space()->bottom();
  HeapWord* const t = _end_of_live;

  if (q < t && _first_dead > q && !oop(q)->is_gc_marked()) {

#ifdef TERA_MAJOR_GC
    if (EnableTeraHeap) {
      HeapWord* const end = _first_dead;

      while (q < end) {
        /* Get the size of the object */
        size_t size = oop(q)->size();

        if(oop(q)->is_gc_marked() && oop(q)->forwardee() != NULL) {
          HeapWord* compaction_top = (HeapWord*)oop(q)->forwardee();

          if (Universe::teraHeap()->is_obj_in_h2(oop(compaction_top))) {
            moveObjToH2(q, compaction_top, size);
          }
          else {
            /* Copy object to the new destination */
            Copy::aligned_conjoint_words(q, compaction_top, size);
            /* Initialize mark word of the destination */
            oop(compaction_top)->init_mark();
          }
        }
        else {
          oop(q)->init_mark();
        }
        /* Move to the next object */
        q += size;
      }
    }
#endif // TERA_MAJOR_GC
    if (_first_dead == t) {
      q = t;
    } else {
      // $$$ Funky
      q = (HeapWord*) oop(_first_dead)->mark()->decode_pointer();
    }
  }

  const intx scan_interval = PrefetchScanIntervalInBytes;
  const intx copy_interval = PrefetchCopyIntervalInBytes;

  while (q < t) {
    if (!oop(q)->is_gc_marked()) {
      // mark is pointer to next marked oop
      q = (HeapWord*) oop(q)->mark()->decode_pointer();
    } else {
      // prefetch beyond q
      Prefetch::read(q, scan_interval);

      // size and destination
      size_t size = oop(q)->size();
      HeapWord* compaction_top = (HeapWord*)oop(q)->forwardee();

      // prefetch beyond compaction_top
      Prefetch::write(compaction_top, copy_interval);

      // copy object and reinit its mark
      assert(q != compaction_top, "everything in this pass should be moving");
#ifdef TERA_MAJOR_GC
		  // Change the value of teraflag in the new location of the object
		  if (EnableTeraHeap && Universe::teraHeap()->is_obj_in_h2(oop(compaction_top))) {
			  moveObjToH2(q, compaction_top, size);
		  }
		  else {
			  Copy::aligned_conjoint_words(q, compaction_top, size);
			  // Change the value of teraflag in the new location of the object
			  oop(compaction_top)->init_mark();
		  }
#else
		  Copy::aligned_conjoint_words(q, compaction_top, size);
		  oop(compaction_top)->init_mark();
#endif
      q += size;
    }
  }

  assert(compaction_top() >= space()->bottom() && compaction_top() <= space()->end(),
         "should point inside space");
  space()->set_top(compaction_top());

  if (mangle_free_space) {
    space()->mangle_unused_area();
  }
}
