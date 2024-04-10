/*
 * Copyright (c) 1997, 2014, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_OOPS_OOP_HPP
#define SHARE_VM_OOPS_OOP_HPP

#include "memory/iterator.hpp"
#include "memory/memRegion.hpp"
#include "memory/specialized_oop_closures.hpp"
#include "oops/metadata.hpp"
#include "utilities/macros.hpp"
#include "utilities/top.hpp"
#include "memory/sharedDefines.h"

// oopDesc is the top baseclass for objects classes.  The {name}Desc classes describe
// the format of Java objects so the fields can be accessed from C++.
// oopDesc is abstract.
// (see oopHierarchy for complete oop class hierarchy)
//
// no virtual functions allowed

// store into oop with store check
template <class T> void oop_store(T* p, oop v);
template <class T> void oop_store(volatile T* p, oop v);

extern bool always_do_update_barrier;

// Forward declarations.
class OopClosure;
class ScanClosure;
class FastScanClosure;
class FilteringClosure;
class BarrierSet;
class CMSIsAliveClosure;

class PSPromotionManager;
class ParCompactionManager;

class oopDesc {
  friend class VMStructs;
 private:
  volatile markOop  _mark;
  union _metadata {
    Klass*      _klass;
    narrowKlass _compressed_klass;
  } _metadata;

#ifdef TERA_FLAG
  // TeraFlag word is used by the TeraCache. TeraFlag is a 64-bit word and is
  // divided in three parts:
  //
  //+-------+----------------------------------------------------------------+
  //| Bits  | Description                                                    |
  //+-------+----------------------------------------------------------------+
  //| 63-48 | Represent the RDD partition Id                                 |
  //+-------+----------------------------------------------------------------+
  //| 32-47 | Represent the RDD Id											                     |
  //+-------+----------------------------------------------------------------+
  //| 16-47 | Represent if object is leaf/primitive_array                    |
  //+-------+----------------------------------------------------------------+
  //| 15-0  | Represent the state of the object                              |
  //+-------+----------------------------------------------------------------+

  volatile int64_t _tera_flag;      //< MarkTeracache objects
#endif // TERA_FLAG

  // Fast access to barrier set.  Must be initialized.
  static BarrierSet* _bs;

 public:
  markOop  mark() const         { return _mark; }
  markOop* mark_addr() const    { return (markOop*) &_mark; }

#ifdef TERA_FLAG
  // Save the H2 destination address of the object. By saving the
  // destination address to the teraflag we avoid to overwrite the
  // mark word of the object
  void set_h2_dst_addr(uint64_t addr);
  
  // Get the H2 destination address of the candidate object.
  uint64_t get_h2_dst_addr();
  // Mark an object with 'id' to be moved in H2. H2 allocator uses the
  // 'id' to locate objects with the same 'id' by to the same region.
  // 'id' is defined by the application.
  void mark_move_h2(uint64_t rdd_id, uint64_t part_id) { 
	  _tera_flag = (part_id << 48);
	  _tera_flag |= (rdd_id << 32);
	  _tera_flag |= MOVE_TO_TERA;
  }

  // Check if an object is marked to be moved in H2
  bool is_marked_move_h2() { 
    return (_tera_flag & 0xffff) == MOVE_TO_TERA ;
  }

  // Mark this object that is located in TeraCache
  void set_in_h2() { 
	  uint64_t part_id = (_tera_flag >> 48);
	  uint64_t rdd_id  = (_tera_flag >> 32) & 0xffff;
	  uint64_t primitive = (_tera_flag >> 16) & 0xffff;

	  _tera_flag = (part_id << 48);
	  _tera_flag |= (rdd_id << 32);
	  _tera_flag |= (primitive << 16);
	  _tera_flag |= IN_TERA_HEAP;
  }

  // Get the state of the object
  uint64_t get_obj_state() { 
	  // Get the object state. The state is saved in the lowes 32bit 
	  return (_tera_flag & 0xffff);
  }
  
  // Init the object state 
  void init_obj_state() { 
	  _tera_flag = INIT_TF;
  }

  // Get the object group id
  int get_obj_group_id() {
	  return ((_tera_flag >> 32) & 0xffff) ;
  }

  // Get object partition Id
  uint64_t get_obj_part_id() {
	  return _tera_flag >> 48;
  }

  bool is_live(){
    return (get_obj_state() == LIVE_TERA_OBJ || 
            get_obj_state() == VISITED_TERA_OBJ || 
            get_obj_state() == MOVE_TO_TERA);
  }

  void reset_live(){
      set_in_h2();
  }

  void set_live(){
	  uint64_t part_id = (_tera_flag >> 48);
	  uint64_t rdd_id = (_tera_flag >> 32) & 0xffff;
	  uint64_t primitive = (_tera_flag >> 16) & 0xffff;

	  _tera_flag = (part_id << 48);
	  _tera_flag |= (rdd_id << 32);
	  _tera_flag |= (primitive << 16);
	  _tera_flag |= LIVE_TERA_OBJ;
  }

  void set_visited(){
	  uint64_t part_id = (_tera_flag >> 48);
	  uint64_t rdd_id = (_tera_flag >> 32) & 0xffff;
	  uint64_t primitive = (_tera_flag >> 16) & 0xffff;

	  _tera_flag = (part_id << 48);
	  _tera_flag |= (rdd_id << 32);
	  _tera_flag |= (primitive << 16);
	  _tera_flag |= VISITED_TERA_OBJ;
  }

  bool is_visited(){
	  return (get_obj_state() == VISITED_TERA_OBJ);
  }
  
  // Set object flag if is promitive array or leaf object. Leaf
  // objects are the objects that contain only primitive fields and no
  // references to other objects
  void set_primitive(bool is_primitive_array) { 
	  uint64_t part_id = (_tera_flag >> 48);
	  uint64_t rdd_id  = (_tera_flag >> 32) & 0xffff;
	  uint64_t state = _tera_flag & 0xffff;

	  _tera_flag = (part_id << 48);
	  _tera_flag |= (rdd_id << 32);
	  _tera_flag |= is_primitive_array ? (PRIMITIVE_ARRAY << 16) : (LEAF_OBJECT << 16);
	  _tera_flag |= state;
  }

  // Set object flag if is a non primitive object
  void set_non_primitive() {
	  uint64_t part_id = (_tera_flag >> 48);
	  uint64_t rdd_id  = (_tera_flag >> 32) & 0xffff;
	  uint64_t state = _tera_flag & 0xffff;

	  _tera_flag = (part_id << 48);
	  _tera_flag |= (rdd_id << 32);
	  _tera_flag |= (NON_PRIMITIVE << 16);
	  _tera_flag |= state;
  }
  
  // Check if the object is primitive array or leaf object
  bool is_primitive() {
    uint64_t state = ((_tera_flag >> 16) & 0xffff);
	  return (state == PRIMITIVE_ARRAY) || (state == LEAF_OBJECT);
  }
  
#endif // TERA_FLAG

  void set_mark(volatile markOop m)      { _mark = m;   }

  void    release_set_mark(markOop m);
  markOop cas_set_mark(markOop new_mark, markOop old_mark);

  // Used only to re-initialize the mark word (e.g., of promoted
  // objects during a GC) -- requires a valid klass pointer
  void init_mark();

  Klass* klass() const;
  Klass* klass_or_null() const volatile;
  Klass* klass_or_null_acquire() const volatile;
  Klass** klass_addr();
  narrowKlass* compressed_klass_addr();

  void set_klass(Klass* k);
  void release_set_klass(Klass* k);

  // For klass field compression
  int klass_gap() const;
  void set_klass_gap(int z);
  // For when the klass pointer is being used as a linked list "next" field.
  void set_klass_to_list_ptr(oop k);
  oop list_ptr_from_klass();

  // size of object header, aligned to platform wordSize
  static int header_size()          { return sizeof(oopDesc)/HeapWordSize; }

  // Returns whether this is an instance of k or an instance of a subclass of k
  bool is_a(Klass* k)  const;

  // Returns the actual oop size of the object
  int size();

  // Sometimes (for complicated concurrency-related reasons), it is useful
  // to be able to figure out the size of an object knowing its klass.
  int size_given_klass(Klass* klass);

  // type test operations (inlined in oop.inline.h)
  bool is_instance()            const;
  bool is_instanceMirror()      const;
  bool is_instanceClassLoader() const;
  bool is_instanceRef()         const;
  bool is_array()               const;
  bool is_objArray()            const;
  bool is_typeArray()           const;

 private:
  // field addresses in oop
  void*     field_base(int offset)        const;

  jbyte*    byte_field_addr(int offset)   const;
  jchar*    char_field_addr(int offset)   const;
  jboolean* bool_field_addr(int offset)   const;
  jint*     int_field_addr(int offset)    const;
  jshort*   short_field_addr(int offset)  const;
  jlong*    long_field_addr(int offset)   const;
  jfloat*   float_field_addr(int offset)  const;
  jdouble*  double_field_addr(int offset) const;
  Metadata** metadata_field_addr(int offset) const;

 public:
  // Need this as public for garbage collection.
  template <class T> T* obj_field_addr(int offset) const;

  // Needed for javaClasses
  address*  address_field_addr(int offset) const;

  static bool is_null(oop obj);
  static bool is_null(narrowOop obj);
  static bool is_null(Klass* obj);

  // Decode an oop pointer from a narrowOop if compressed.
  // These are overloaded for oop and narrowOop as are the other functions
  // below so that they can be called in template functions.
  static oop decode_heap_oop_not_null(oop v);
  static oop decode_heap_oop_not_null(narrowOop v);
  static oop decode_heap_oop(oop v);
  static oop decode_heap_oop(narrowOop v);

  // Encode an oop pointer to a narrow oop.  The or_null versions accept
  // null oop pointer, others do not in order to eliminate the
  // null checking branches.
  static narrowOop encode_heap_oop_not_null(oop v);
  static narrowOop encode_heap_oop(oop v);

  // Load an oop out of the Java heap
  static narrowOop load_heap_oop(narrowOop* p);
  static oop       load_heap_oop(oop* p);

  // Load an oop out of Java heap and decode it to an uncompressed oop.
  static oop load_decode_heap_oop_not_null(narrowOop* p);
  static oop load_decode_heap_oop_not_null(oop* p);
  static oop load_decode_heap_oop(narrowOop* p);
  static oop load_decode_heap_oop(oop* p);

  // Store an oop into the heap.
  static void store_heap_oop(narrowOop* p, narrowOop v);
  static void store_heap_oop(oop* p, oop v);

  // Encode oop if UseCompressedOops and store into the heap.
  static void encode_store_heap_oop_not_null(narrowOop* p, oop v);
  static void encode_store_heap_oop_not_null(oop* p, oop v);
  static void encode_store_heap_oop(narrowOop* p, oop v);
  static void encode_store_heap_oop(oop* p, oop v);

  static void release_store_heap_oop(volatile narrowOop* p, narrowOop v);
  static void release_store_heap_oop(volatile oop* p, oop v);

  static void release_encode_store_heap_oop_not_null(volatile narrowOop* p, oop v);
  static void release_encode_store_heap_oop_not_null(volatile oop* p, oop v);
  static void release_encode_store_heap_oop(volatile narrowOop* p, oop v);
  static void release_encode_store_heap_oop(volatile oop* p, oop v);

  static oop atomic_exchange_oop(oop exchange_value, volatile HeapWord *dest);
  static oop atomic_compare_exchange_oop(oop exchange_value,
                                         volatile HeapWord *dest,
                                         oop compare_value,
                                         bool prebarrier = false);

  // Access to fields in a instanceOop through these methods.
  oop obj_field(int offset) const;
  volatile oop obj_field_volatile(int offset) const;
  void obj_field_put(int offset, oop value);
  void obj_field_put_raw(int offset, oop value);
  void obj_field_put_volatile(int offset, oop value);

  Metadata* metadata_field(int offset) const;
  void metadata_field_put(int offset, Metadata* value);

  jbyte byte_field(int offset) const;
  void byte_field_put(int offset, jbyte contents);

  jchar char_field(int offset) const;
  void char_field_put(int offset, jchar contents);

  jboolean bool_field(int offset) const;
  void bool_field_put(int offset, jboolean contents);

  jint int_field(int offset) const;
  void int_field_put(int offset, jint contents);

  jshort short_field(int offset) const;
  void short_field_put(int offset, jshort contents);

  jlong long_field(int offset) const;
  void long_field_put(int offset, jlong contents);

  jfloat float_field(int offset) const;
  void float_field_put(int offset, jfloat contents);

  jdouble double_field(int offset) const;
  void double_field_put(int offset, jdouble contents);

  address address_field(int offset) const;
  void address_field_put(int offset, address contents);

  oop obj_field_acquire(int offset) const;
  void release_obj_field_put(int offset, oop value);

  jbyte byte_field_acquire(int offset) const;
  void release_byte_field_put(int offset, jbyte contents);

  jchar char_field_acquire(int offset) const;
  void release_char_field_put(int offset, jchar contents);

  jboolean bool_field_acquire(int offset) const;
  void release_bool_field_put(int offset, jboolean contents);

  jint int_field_acquire(int offset) const;
  void release_int_field_put(int offset, jint contents);

  jshort short_field_acquire(int offset) const;
  void release_short_field_put(int offset, jshort contents);

  jlong long_field_acquire(int offset) const;
  void release_long_field_put(int offset, jlong contents);

  jfloat float_field_acquire(int offset) const;
  void release_float_field_put(int offset, jfloat contents);

  jdouble double_field_acquire(int offset) const;
  void release_double_field_put(int offset, jdouble contents);

  address address_field_acquire(int offset) const;
  void release_address_field_put(int offset, address contents);

  // printing functions for VM debugging
  void print_on(outputStream* st) const;         // First level print
  void print_value_on(outputStream* st) const;   // Second level print.
  void print_address_on(outputStream* st) const; // Address printing

  // printing on default output stream
  void print();
  void print_value();
  void print_address();

  // return the print strings
  char* print_string();
  char* print_value_string();

  // verification operations
  void verify_on(outputStream* st);
  void verify();

  // locking operations
  bool is_locked()   const;
  bool is_unlocked() const;
  bool has_bias_pattern() const;

  // asserts
  bool is_oop(bool ignore_mark_word = false) const;
  bool is_oop_or_null(bool ignore_mark_word = false) const;
#ifndef PRODUCT
  bool is_unlocked_oop() const;
#endif

  // garbage collection
  bool is_gc_marked() const;
  // Apply "MarkSweep::mark_and_push" to (the address of) every non-NULL
  // reference field in "this".
  void follow_contents(void);
 
#ifdef TERA_MAJOR_GC
  void h2_follow_contents(void);
#endif

#if INCLUDE_ALL_GCS
  // Parallel Scavenge
  void push_contents(PSPromotionManager* pm);

#ifdef TERA_CARDS
  // Parallel Scavenge TeraCache
  void h2_push_contents(PSPromotionManager* pm);
  
  // Parallel Trace TeraCache contents
  void h2_trace_contents(PSPromotionManager* pm);
#endif

  // Parallel Old
  void update_contents(ParCompactionManager* cm);

  void follow_contents(ParCompactionManager* cm);
#endif // INCLUDE_ALL_GCS

  bool is_scavengable() const;

  // Forward pointer operations for scavenge
  bool is_forwarded() const;

  void forward_to(oop p);
  bool cas_forward_to(oop p, markOop compare);

#if INCLUDE_ALL_GCS
  // Like "forward_to", but inserts the forwarding pointer atomically.
  // Exactly one thread succeeds in inserting the forwarding pointer, and
  // this call returns "NULL" for that thread; any other thread has the
  // value of the forwarding pointer returned and does not modify "this".
  oop forward_to_atomic(oop p);
#endif // INCLUDE_ALL_GCS

  oop forwardee() const;

  // Age of object during scavenge
  uint age() const;
  void incr_age();

  // Adjust all pointers in this object to point at it's forwarded location and
  // return the size of this oop.  This is used by the MarkSweep collector.
  int adjust_pointers();

  // mark-sweep support
  void follow_body(int begin, int end);

  // Fast access to barrier set
  static BarrierSet* bs()            { return _bs; }
  static void set_bs(BarrierSet* bs) { _bs = bs; }

  // iterators, returns size of object
#define OOP_ITERATE_DECL(OopClosureType, nv_suffix)                      \
  int oop_iterate(OopClosureType* blk);                                  \
  int oop_iterate(OopClosureType* blk, MemRegion mr);  // Only in mr.

  ALL_OOP_OOP_ITERATE_CLOSURES_1(OOP_ITERATE_DECL)
  ALL_OOP_OOP_ITERATE_CLOSURES_2(OOP_ITERATE_DECL)

#if INCLUDE_ALL_GCS

#define OOP_ITERATE_BACKWARDS_DECL(OopClosureType, nv_suffix)            \
  int oop_iterate_backwards(OopClosureType* blk);

  ALL_OOP_OOP_ITERATE_CLOSURES_1(OOP_ITERATE_BACKWARDS_DECL)
  ALL_OOP_OOP_ITERATE_CLOSURES_2(OOP_ITERATE_BACKWARDS_DECL)
#endif

  int oop_iterate_no_header(OopClosure* bk);
  int oop_iterate_no_header(OopClosure* bk, MemRegion mr);

  // identity hash; returns the identity hash key (computes it if necessary)
  // NOTE with the introduction of UseBiasedLocking that identity_hash() might reach a
  // safepoint if called on a biased object. Calling code must be aware of that.
  intptr_t identity_hash();
  intptr_t slow_identity_hash();

  // Alternate hashing code if string table is rehashed
  unsigned int new_hash(juint seed);

  // marks are forwarded to stack when object is locked
  bool     has_displaced_mark() const;
  markOop  displaced_mark() const;
  void     set_displaced_mark(markOop m);

  // for code generation
  static int mark_offset_in_bytes()    { return offset_of(oopDesc, _mark); }
  static int klass_offset_in_bytes()   { return offset_of(oopDesc, _metadata._klass); }
  static int klass_gap_offset_in_bytes();
#ifdef TERA_FLAG
  // 0 +-----------+
  //   | _mark     |
  // 8 +-----------+
  //   | _klass    |
  // 16+-----------+
  //   | _tera_flag|
  //   +-----------+
  static long teraflag_offset_in_bytes() { return offset_of(oopDesc, _tera_flag); }
#endif // TERA_FLAG
};

#endif // SHARE_VM_OOPS_OOP_HPP
