// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_

#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"

namespace blink {

class Isolate {
 public:
  static Isolate* Current();
  static void SetCurrentFromMainThread(Isolate*);
  static void SetCurrentFromWorker(Isolate*);

 private:
};

#define DEFINE_ISOLATE_BOUND(Type, Name, Arguments) \
    DEFINE_STATIC_LOCAL(Type, Name, Arguments)

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_ISOLATE_H_

