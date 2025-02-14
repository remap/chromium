// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_ASSISTANT_AUDIO_DECODER_ASSISTANT_AUDIO_DECODER_H_
#define CHROMEOS_SERVICES_ASSISTANT_AUDIO_DECODER_ASSISTANT_AUDIO_DECODER_H_

#include <memory>

#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "chromeos/services/assistant/public/mojom/assistant_audio_decoder.mojom.h"

namespace media {
class AudioFileReader;
class AudioBus;
class DataSource;
}  // namespace media

namespace service_manager {
class ServiceContextRef;
}  // namespace service_manager

namespace chromeos {
namespace assistant {

class AssistantAudioDecoder : public mojom::AssistantAudioDecoder {
 public:
  AssistantAudioDecoder(
      std::unique_ptr<service_manager::ServiceContextRef> service_ref,
      mojom::AssistantAudioDecoderClientPtr client,
      mojom::AssistantMediaDataSourcePtr data_source);
  ~AssistantAudioDecoder() override;

  // Called by |client_| on main thread.
  void OpenDecoder(OpenDecoderCallback callback) override;
  void Decode() override;

 private:
  // Calls |decoder_| to decode on media thread.
  void OpenDecoderOnMediaThread(OpenDecoderCallback callback);
  void DecodeOnMediaThread();

  // Calls |client_| methods on main thread.
  void OnDecoderInitializedOnThread(OpenDecoderCallback callback,
                                    int sample_rate,
                                    int channels);
  void OnDecoderErrorOnThread(OpenDecoderCallback callback);
  void OnBufferDecodedOnThread(
      const std::vector<std::unique_ptr<media::AudioBus>>&
          decoded_audio_buffers);

  const std::unique_ptr<service_manager::ServiceContextRef> service_ref_;
  mojom::AssistantAudioDecoderClientPtr client_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  std::unique_ptr<media::DataSource> data_source_;
  std::unique_ptr<media::BlockingUrlProtocol> protocol_;
  std::unique_ptr<media::AudioFileReader> decoder_;
  std::unique_ptr<base::Thread> media_thread_;

  DISALLOW_COPY_AND_ASSIGN(AssistantAudioDecoder);
};

}  // namespace assistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_ASSISTANT_AUDIO_DECODER_ASSISTANT_AUDIO_DECODER_H_
