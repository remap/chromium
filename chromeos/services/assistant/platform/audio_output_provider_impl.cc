// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/assistant/platform/audio_output_provider_impl.h"

#include "ash/public/interfaces/constants.mojom.h"
#include "libassistant/shared/public/platform_audio_buffer.h"
#include "media/audio/audio_device_description.h"
#include "services/service_manager/public/cpp/connector.h"

namespace chromeos {
namespace assistant {

namespace {

constexpr int kNumberOfBuffersPerSec = 10;

int32_t GetBytesPerSample(const assistant_client::OutputStreamFormat& format) {
  switch (format.encoding) {
    case assistant_client::OutputStreamEncoding::STREAM_PCM_S16:
      return 2;
    case assistant_client::OutputStreamEncoding::STREAM_PCM_S32:
    case assistant_client::OutputStreamEncoding::STREAM_PCM_F32:
      return 4;
    default:
      break;
  }
  NOTREACHED();
  return 1;
}

int32_t GetBytesPerFrame(const assistant_client::OutputStreamFormat& format) {
  return GetBytesPerSample(format) * format.pcm_num_channels;
}

int32_t GetBufferSizeInBytesFromBufferFormat(
    const assistant_client::OutputStreamFormat& format) {
  int32_t frame_size_in_bytes = 0;

  switch (format.encoding) {
    case assistant_client::OutputStreamEncoding::STREAM_PCM_S16:
      frame_size_in_bytes = 2;
      break;
    case assistant_client::OutputStreamEncoding::STREAM_PCM_S32:
    case assistant_client::OutputStreamEncoding::STREAM_PCM_F32:
      frame_size_in_bytes = 4;
      break;
    default:
      NOTREACHED();
      break;
  }

  return GetBytesPerFrame(format) * format.pcm_sample_rate /
         kNumberOfBuffersPerSec;
}

media::AudioParameters GetAudioParametersFromBufferFormat(
    const assistant_client::OutputStreamFormat& output_format) {
  DCHECK(output_format.pcm_num_channels <= 2 &&
         output_format.pcm_num_channels > 0);

  return media::AudioParameters(
      media::AudioParameters::AUDIO_PCM_LOW_LATENCY,
      media::GuessChannelLayout(output_format.pcm_num_channels),
      output_format.pcm_sample_rate,
      output_format.pcm_sample_rate / kNumberOfBuffersPerSec);
}

void FillAudioFifoWithDataOfBufferFormat(
    media::AudioBlockFifo* fifo,
    const std::vector<uint8_t>& data,
    const assistant_client::OutputStreamFormat& output_format,
    int num_bytes) {
  int bytes_per_frame = GetBytesPerFrame(output_format);
  int bytes_per_sample = GetBytesPerSample(output_format);
  int frames = num_bytes / bytes_per_frame;
  fifo->Push(data.data(), frames, bytes_per_sample);
}

class AudioOutputImpl : public assistant_client::AudioOutput {
 public:
  AudioOutputImpl(service_manager::Connector* connector,
                  scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                  assistant_client::OutputStreamType type,
                  assistant_client::OutputStreamFormat format)
      : connector_(connector),
        main_thread_task_runner_(task_runner),
        stream_type_(type),
        format_(format),
        device_owner_(std::make_unique<AudioDeviceOwner>(task_runner)) {}

  ~AudioOutputImpl() override {
    main_thread_task_runner_->DeleteSoon(FROM_HERE, device_owner_.release());
  }

  // assistant_client::AudioOutput overrides:
  assistant_client::OutputStreamType GetType() override { return stream_type_; }

  void Start(assistant_client::AudioOutput::Delegate* delegate) override {
    main_thread_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&AudioDeviceOwner::StartOnMainThread,
                                  base::Unretained(device_owner_.get()),
                                  delegate, connector_, format_));
  }

  void Stop() override {
    main_thread_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&AudioDeviceOwner::StopOnMainThread,
                                  base::Unretained(device_owner_.get())));
  }

 private:
  service_manager::Connector* connector_;
  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;

  const assistant_client::OutputStreamType stream_type_;
  assistant_client::OutputStreamFormat format_;

  std::unique_ptr<AudioDeviceOwner> device_owner_;

  DISALLOW_COPY_AND_ASSIGN(AudioOutputImpl);
};

}  // namespace

VolumeControlImpl::VolumeControlImpl(service_manager::Connector* connector)
    : binding_(this) {
  connector->BindInterface(ash::mojom::kServiceName, &volume_control_ptr_);
  ash::mojom::VolumeObserverPtr observer;
  binding_.Bind(mojo::MakeRequest(&observer));
  volume_control_ptr_->AddVolumeObserver(std::move(observer));
}

VolumeControlImpl::~VolumeControlImpl() = default;

void VolumeControlImpl::SetAudioFocus(
    assistant_client::OutputStreamType focused_stream) {}

float VolumeControlImpl::GetSystemVolume() {
  return volume_ * 1.0 / 100.0;
}

void VolumeControlImpl::SetSystemVolume(float new_volume, bool user_initiated) {
  volume_control_ptr_->SetVolume(new_volume * 100.0, user_initiated);
}

float VolumeControlImpl::GetAlarmVolume() {
  // TODO(muyuanli): implement.
  return 1.0f;
}

void VolumeControlImpl::SetAlarmVolume(float new_volume, bool user_initiated) {
  // TODO(muyuanli): implement.
}

bool VolumeControlImpl::IsSystemMuted() {
  return mute_;
}

void VolumeControlImpl::SetSystemMuted(bool muted) {
  volume_control_ptr_->SetMuted(muted);
}

void VolumeControlImpl::OnVolumeChanged(int volume) {
  volume_ = volume;
}

void VolumeControlImpl::OnMuteStateChanged(bool mute) {
  mute_ = mute;
}

AudioOutputProviderImpl::AudioOutputProviderImpl(
    service_manager::Connector* connector)
    : volume_control_impl_(connector),
      connector_(connector),
      main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

AudioOutputProviderImpl::~AudioOutputProviderImpl() = default;

assistant_client::AudioOutput* AudioOutputProviderImpl::CreateAudioOutput(
    assistant_client::OutputStreamType type,
    const assistant_client::OutputStreamFormat& stream_format) {
  // Owned by one arbitrary thread inside libassistant. It will be destroyed
  // once assistant_client::AudioOutput::Delegate::OnStopped() is called.
  // TODO(muyuanli): Handle encoded stream: OutputStreamEncoding::STREAM_MP3 /
  // OGG.
  return new AudioOutputImpl(connector_, main_thread_task_runner_, type,
                             stream_format);
}

std::vector<assistant_client::OutputStreamEncoding>
AudioOutputProviderImpl::GetSupportedStreamEncodings() {
  // TODO(muyuanli): implement after media decoder is ready.
  return std::vector<assistant_client::OutputStreamEncoding>{
      assistant_client::OutputStreamEncoding::STREAM_PCM_S16,
      assistant_client::OutputStreamEncoding::STREAM_PCM_S32,
      assistant_client::OutputStreamEncoding::STREAM_PCM_F32,
      assistant_client::OutputStreamEncoding::STREAM_MP3,
  };
}

assistant_client::AudioInput* AudioOutputProviderImpl::GetReferenceInput() {
  // TODO(muyuanli): implement.
  return nullptr;
}

bool AudioOutputProviderImpl::SupportsPlaybackTimestamp() const {
  // TODO(muyuanli): implement.
  return false;
}

assistant_client::VolumeControl& AudioOutputProviderImpl::GetVolumeControl() {
  return volume_control_impl_;
}

void AudioOutputProviderImpl::RegisterAudioEmittingStateCallback(
    AudioEmittingStateCallback callback) {
  // TODO(muyuanli): implement.
}

AudioDeviceOwner::AudioDeviceOwner(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : main_thread_task_runner_(task_runner) {}

AudioDeviceOwner::~AudioDeviceOwner() = default;

void AudioDeviceOwner::StartOnMainThread(
    assistant_client::AudioOutput::Delegate* delegate,
    service_manager::Connector* connector,
    const assistant_client::OutputStreamFormat& format) {
  DCHECK(!output_device_);
  DCHECK(main_thread_task_runner_->RunsTasksInCurrentSequence());

  delegate_ = delegate;
  format_ = format;
  // TODO(wutao): Remove this after supporting mp3 encoding.
  if (format_.encoding == assistant_client::OutputStreamEncoding::STREAM_MP3) {
    delegate_->OnEndOfStream();
    return;
  }

  audio_param_ = GetAudioParametersFromBufferFormat(format_);

  // |audio_fifo_| contains 3x the number of frames to render.
  audio_fifo_ = std::make_unique<media::AudioBlockFifo>(
      format.pcm_num_channels, audio_param_.frames_per_buffer(), 3);
  audio_data_.resize(GetBufferSizeInBytesFromBufferFormat(format_));

  {
    base::AutoLock lock(lock_);
    ScheduleFillLocked(base::TimeTicks::Now());
  }

  // |connector| is null in unittest.
  if (connector) {
    output_device_ = std::make_unique<audio::OutputDevice>(
        connector->Clone(), audio_param_, this,
        media::AudioDeviceDescription::kDefaultDeviceId);
    output_device_->Play();
  }
}

void AudioDeviceOwner::StopOnMainThread() {
  DCHECK(main_thread_task_runner_->RunsTasksInCurrentSequence());

  output_device_.reset();
  delegate_->OnStopped();
}

int AudioDeviceOwner::Render(base::TimeDelta delay,
                             base::TimeTicks delay_timestamp,
                             int prior_frames_skipped,
                             media::AudioBus* dest) {
  base::AutoLock lock(lock_);

  if (!is_filling_ && audio_fifo_->GetAvailableFrames() <= 0) {
    delegate_->OnEndOfStream();
    return 0;
  }
  if (audio_fifo_->GetAvailableFrames() <= 0) {
    // Wait for the next round of filling. This should only happen at the
    // very beginning.
    return 0;
  }

  int available_frames = audio_fifo_->GetAvailableFrames();
  if (available_frames < dest->frames()) {
    // In our setting, dest->frames() == frames per block in |audio_fifo_|.
    DCHECK(audio_fifo_->available_blocks() == 0);

    int frames_to_fill = audio_param_.frames_per_buffer() - available_frames;

    DCHECK(frames_to_fill >= 0);

    // Fill up to one block with zero data so that |audio_fifo_| has 1 block
    // to consume. This avoids DCHECK in audio_fifo_->Consume() and also
    // prevents garbage data being copied to |dest| in production.
    audio_fifo_->PushSilence(frames_to_fill);
  }

  audio_fifo_->Consume()->CopyTo(dest);

  ScheduleFillLocked(base::TimeTicks::Now());
  return dest->frames();
}

void AudioDeviceOwner::OnRenderError() {
  DVLOG(1) << "OnRenderError()";
  delegate_->OnError(assistant_client::AudioOutput::Error::FATAL_ERROR);
}

void AudioDeviceOwner::ScheduleFillLocked(const base::TimeTicks& time) {
  lock_.AssertAcquired();
  if (is_filling_)
    return;
  is_filling_ = true;
  // FillBuffer will not be called after delegate_->OnEndOfStream, after which
  // AudioDeviceOwner will be destroyed. Thus |this| is valid for capture
  // here.
  delegate_->FillBuffer(
      audio_data_.data(),
      std::min(static_cast<int>(audio_data_.size()),
               GetBytesPerFrame(format_) * audio_fifo_->GetUnfilledFrames()),
      time.since_origin().InMicroseconds(),
      [this](int num) { this->BufferFillDone(num); });
}

void AudioDeviceOwner::BufferFillDone(int num_bytes) {
  base::AutoLock lock(lock_);
  is_filling_ = false;
  if (num_bytes == 0)
    return;
  FillAudioFifoWithDataOfBufferFormat(audio_fifo_.get(), audio_data_, format_,
                                      num_bytes);
  if (audio_fifo_->GetUnfilledFrames() > 0)
    ScheduleFillLocked(base::TimeTicks::Now());
}

}  // namespace assistant
}  // namespace chromeos
