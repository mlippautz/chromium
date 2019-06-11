// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/wtf/type_traits.h"

#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/thread_specific.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace WTF {
namespace internal {

namespace {

// Like DEFINE_THREAD_SAFE_STATIC_LOCAL but without
// ScopedBanGarbageCollectedAlloc.
#define DEFINE_REENTRANT_THREAD_SAFE_STATIC_LOCAL(Type, Name, Arguments)       \
  static WTF::StaticSingleton<Type> s_##Name(                                  \
      [&]() { return new WTF::StaticSingleton<Type>::WrapperType Arguments; }, \
      [&](void* leaked_ptr) {                                                  \
        new (leaked_ptr) WTF::StaticSingleton<Type>::WrapperType Arguments;    \
      });                                                                      \
  Type& Name = s_##Name.Get(true)

ThreadSpecific<ScopedBanGarbageCollectedAlloc*>&
GetBanGarbageCollectedAllocTLS() {
  DEFINE_REENTRANT_THREAD_SAFE_STATIC_LOCAL(
      ThreadSpecific<ScopedBanGarbageCollectedAlloc*>,
      ban_garbage_collected_alloc_tls, ());
  return ban_garbage_collected_alloc_tls;
}

}  // namespace

#if DCHECK_IS_ON()
ScopedBanGarbageCollectedAlloc::ScopedBanGarbageCollectedAlloc() {
  auto& ban_garbage_collected_alloc_tls = GetBanGarbageCollectedAllocTLS();
  // Allow nesting, only the last one going out-of-scope will undo the ban.
  if (*ban_garbage_collected_alloc_tls == nullptr)
    *ban_garbage_collected_alloc_tls = this;
}

ScopedBanGarbageCollectedAlloc::~ScopedBanGarbageCollectedAlloc() {
  auto& ban_garbage_collected_alloc_tls = GetBanGarbageCollectedAllocTLS();
  if (*ban_garbage_collected_alloc_tls == this)
    *ban_garbage_collected_alloc_tls = nullptr;
}
#endif  // DCHECK_IS_ON()

}  // namespace internal

bool IsGarbageCollectedAllocAllowed() {
  return *internal::GetBanGarbageCollectedAllocTLS() == nullptr;
}

}  // namespace WTF
