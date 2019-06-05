// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_

#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"

namespace blink {

class Isolate {
 public:
  using CreateFunc = void* (*)();
  using DestroyFunc = void (*)();

  static Isolate* Current();
  static void SetCurrentFromMainThread(Isolate*);
  static void SetCurrentFromWorker(Isolate*);

  static size_t RegisterGlobal(CreateFunc, DestroyFunc);

  ALWAYS_INLINE void* GetOrCreateGlobal(size_t index) {
    return (globals_initialized_[index]) ? globals_[index]
                                         : CreateGlobal(index);
  }

 private:
  static constexpr size_t kMaxGlobals = 512;

  void* CreateGlobal(size_t);

  static std::atomic<size_t> global_count_;
  static CreateFunc create_funcs_[kMaxGlobals];
  static DestroyFunc destroy_funcs_[kMaxGlobals];

  void* globals_[kMaxGlobals] = {0};
  bool globals_initialized_[kMaxGlobals] = {false};
};

#define DEFINE_ISOLATE_BOUND(Type, Name, Arguments)                           \
  struct Name##Helper {                                                       \
    static void* Create() { return new Type Arguments; }                      \
    static void Destroy(void* ptr) { delete static_cast<Type*>(ptr); }        \
  };                                                                          \
  static size_t Name##_offset =                                               \
      Isolate::RegisterGlobal(&Name##Helper::Create, &Name##Helper::Destroy); \
  Type& Name = *static_cast<Type*>(                                           \
      Isolate::Current()->GetOrCreateGlobal(Name##_offset));

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_

