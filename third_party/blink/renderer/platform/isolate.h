// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_

#include <atomic>

#include "base/logging.h"
#include "base/compiler_specific.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

class PLATFORM_EXPORT Isolate {
 public:
  using CreateFunc = void* (*)();

  explicit Isolate(Isolate*);

  ALWAYS_INLINE static Isolate* MainThreadCurrent() {
    DCHECK(IsMainThread());
    return g_current_main_thread_isolate_;
  }

  static Isolate* Current();

  // TODO: Make this private, and only accessible from
  // ScopedSetMainThreadIsolate.
  static void SetCurrentFromMainThread(Isolate*);

  static void SetCurrentFromWorker(Isolate*);

  static size_t RegisterGlobal(CreateFunc);

  ALWAYS_INLINE void* GetOrCreateGlobal(size_t index) {
    return (globals_initialized_[index]) ? globals_[index]
                                         : CreateGlobal(index);
  }

  ALWAYS_INLINE void** GetGlobalSlot(size_t index) { return &globals_[index]; }

  Isolate* ParentIsolate() const;

 private:
  static Isolate* g_current_main_thread_isolate_;
  static constexpr size_t kMaxGlobals = 512;

  static bool IsMainThread();

  void* CreateGlobal(size_t);

  static std::atomic<size_t> global_count_;
  static CreateFunc create_funcs_[kMaxGlobals];

  Isolate* parent_ = nullptr;
  void* globals_[kMaxGlobals] = {0};
  bool globals_initialized_[kMaxGlobals] = {false};
};

// Helper class for setting the current main thread isolate.
class ScopedSetMainThreadIsolate {
 public:
  ScopedSetMainThreadIsolate(Isolate* isolate);
  ~ScopedSetMainThreadIsolate();
};

// Helper template for wrapping simple global scope static locals.
// These are typically used as fast-path lookups, so will likely
// need to be "painted" on isolate context switch, but for now we
// simply make them lookup on access.
template <typename T>
class IsolateBoundGlobalStaticPtr {
 public:
  constexpr IsolateBoundGlobalStaticPtr() = default;
  constexpr IsolateBoundGlobalStaticPtr(std::nullptr_t){}

  // Emulate being a simple T* from the outside.
  ALWAYS_INLINE T*& operator=(T* value) { return (*GetImpl() = value); }
  ALWAYS_INLINE T& operator*() { return **GetImpl(); }
  ALWAYS_INLINE T* operator->() { return *GetImpl(); }
  ALWAYS_INLINE operator T*() { return *GetImpl(); }

 private:
  static constexpr size_t kInvalidOffset = static_cast<size_t>(-1);

  ALWAYS_INLINE T** GetImpl() {
    if (offset_ == kInvalidOffset)
      offset_ = blink::Isolate::RegisterGlobal(&Create);
    return reinterpret_cast<T**>(
        blink::Isolate::MainThreadCurrent()->GetGlobalSlot(offset_));
  }

  // A dummy Create function to satisfy the Isolate contract.
  // This should never actually run, as we directly access the underlying
  // slot.
  static void* Create() {
    NOTREACHED();
    return nullptr;
  }

  size_t offset_ = kInvalidOffset;
};

// These macros can only be used from function scope, and not from global
// scope.

// This is spiritually equivalen to DEFINE_STATIC_LOCAL or
// DEFINE_THREAD_SAFE_STATIC_LOCAL.
#define DEFINE_ISOLATE_BOUND(Type, Name, Arguments)          \
  struct Name##Helper {                                      \
    static void* Create() { return new Type Arguments; }     \
  };                                                         \
  static size_t Name##_offset =                              \
      blink::Isolate::RegisterGlobal(&Name##Helper::Create); \
  Type& Name = *static_cast<Type*>(                          \
      blink::Isolate::Current()->GetOrCreateGlobal(Name##_offset));

// This is spiritually equivalent to DEFINE_STATIC_REF.
#define DEFINE_ISOLATE_BOUND_REF(Type, Name, Arguments)      \
  struct Name##Helper {                                      \
    static void* Create() {                                  \
      scoped_refptr<Type> o = Arguments;                     \
      if (o)                                                 \
        o->AddRef();                                         \
      return o.get();                                        \
    }                                                        \
  };                                                         \
  static size_t Name##_offset =                              \
      blink::Isolate::RegisterGlobal(&Name##Helper::Create); \
  Type* Name = static_cast<Type*>(                           \
      blink::Isolate::Current()->GetOrCreateGlobal(Name##_offset));

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_
