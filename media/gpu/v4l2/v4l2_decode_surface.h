// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_V4L2_V4L2_DECODE_SURFACE_H_
#define MEDIA_GPU_V4L2_V4L2_DECODE_SURFACE_H_

#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "ui/gfx/geometry/rect.h"

struct v4l2_ext_controls;
struct v4l2_buffer;

namespace media {

// A V4L2-specific decode surface generated by V4L2DecodeSurfaceHandler.
// It is used to store common picture metadata (e.g. visible_rect) and
// platform-specific metadata (e.g. {input,output}_record).
class V4L2DecodeSurface : public base::RefCounted<V4L2DecodeSurface> {
 public:
  // Callback function that releases the according output record.
  // |output_record_| will be passed to the callback function as argument.
  using ReleaseCB = base::OnceClosure;

  // V4L2DecodeSurfaceHandler maintains a list of InputRecords, which records
  // the status and metadata of input buffers.
  // |input_record| is the index of the input record that corresponds to this
  // V4L2DecodeSurface instance.
  // |output_record|, similar to |input_record|, is the index of output record
  // that corresponds to this instance.
  // |release_cb| is the callback function that will be called when the instance
  // is destroyed.
  V4L2DecodeSurface(int input_record, int output_record, ReleaseCB release_cb);

  // Mark the surface as decoded. This will also release all surfaces used for
  // reference, as they are not needed anymore and execute the done callback,
  // if not null.
  void SetDecoded();
  void SetVisibleRect(const gfx::Rect& visible_rect);
  // Take references to each reference surface and keep them until the
  // target surface is decoded.
  void SetReferenceSurfaces(
      std::vector<scoped_refptr<V4L2DecodeSurface>> ref_surfaces);
  // If provided via this method, |done_cb| callback will be executed after
  // decoding into this surface is finished. The callback is reset afterwards,
  // so it needs to be set again before each decode operation.
  void SetDecodeDoneCallback(base::OnceClosure done_cb);

  // Update the passed v4l2_ext_controls structure to add the request or
  // config store information.
  virtual void PrepareSetCtrls(struct v4l2_ext_controls* ctrls) const = 0;
  // Update the passed v4l2_buffer structure to add the request or
  // config store information.
  virtual void PrepareQueueBuffer(struct v4l2_buffer* buffer) const = 0;
  // Return the ID to use in order to reference this frame.
  virtual uint64_t GetReferenceID() const = 0;
  // Submit the request corresponding to this surface once all controls have
  // been set and all buffers queued.
  virtual bool Submit() const = 0;

  bool decoded() const { return decoded_; }
  int input_record() const { return input_record_; }
  int output_record() const { return output_record_; }
  gfx::Rect visible_rect() const { return visible_rect_; }

  std::string ToString() const;

 protected:
  virtual ~V4L2DecodeSurface();
  friend class base::RefCounted<V4L2DecodeSurface>;

  SEQUENCE_CHECKER(sequence_checker_);

 private:
  // The index of the corresponding input record.
  const int input_record_;
  // The index of the corresponding output record.
  const int output_record_;
  // The visible size of the buffer.
  gfx::Rect visible_rect_;

  // Indicate whether the surface is decoded or not.
  bool decoded_;
  // Callback function which is called when the instance is destroyed.
  ReleaseCB release_cb_;
  // Callback function which is called after the surface has been decoded.
  base::OnceClosure done_cb_;

  // The decoded surfaces of the reference frames, which is kept until the
  // surface has been decoded.
  std::vector<scoped_refptr<V4L2DecodeSurface>> reference_surfaces_;

  DISALLOW_COPY_AND_ASSIGN(V4L2DecodeSurface);
};

// An implementation of V4L2DecodeSurface that uses the config store to
// associate controls/buffers to frames.
class V4L2ConfigStoreDecodeSurface : public V4L2DecodeSurface {
 public:
  V4L2ConfigStoreDecodeSurface(int input_record,
                               int output_record,
                               ReleaseCB release_cb)
      : V4L2DecodeSurface(input_record, output_record, std::move(release_cb)),
        config_store_(input_record + 1) {}

  void PrepareSetCtrls(struct v4l2_ext_controls* ctrls) const override;
  void PrepareQueueBuffer(struct v4l2_buffer* buffer) const override;
  uint64_t GetReferenceID() const override;
  bool Submit() const override;

 private:
  ~V4L2ConfigStoreDecodeSurface() override = default;

  // The configuration store of the input buffer.
  uint32_t config_store_;
};

}  // namespace media

#endif  // MEDIA_GPU_V4L2_V4L2_DECODE_SURFACE_H_
