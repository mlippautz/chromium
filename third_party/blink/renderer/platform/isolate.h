// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_

#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"

namespace blink {

class PLATFORM_EXPORT Isolate {
 public:
  using CreateFunc = void* (*)();

  explicit Isolate(Isolate*);

  static Isolate* Current();
  static void SetCurrentFromMainThread(Isolate*);
  static void SetCurrentFromWorker(Isolate*);

  static size_t RegisterGlobal(CreateFunc);

  ALWAYS_INLINE void* GetOrCreateGlobal(size_t index) {
    return (globals_initialized_[index]) ? globals_[index]
                                         : CreateGlobal(index);
  }

  Isolate* ParentIsolate() const;

 private:
  static constexpr size_t kMaxGlobals = 512;

  void* CreateGlobal(size_t);

  static std::atomic<size_t> global_count_;
  static CreateFunc create_funcs_[kMaxGlobals];

  Isolate* parent_ = nullptr;
  void* globals_[kMaxGlobals] = {0};
  bool globals_initialized_[kMaxGlobals] = {false};
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

