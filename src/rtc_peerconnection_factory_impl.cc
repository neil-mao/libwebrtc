#include "rtc_peerconnection_factory_impl.h"

#include <cstdlib>

#include "rtc_lw_extra_impl.h"

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio/create_audio_device_module.h"
#include "api/create_peerconnection_factory.h"
#include "api/media_stream_interface.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "modules/audio_device/audio_device_impl.h"
#include "rtc_audio_source_impl.h"
#include "rtc_media_stream_impl.h"
#include "rtc_mediaconstraints_impl.h"
#include "rtc_peerconnection_impl.h"
#include "rtc_rtp_capabilities_impl.h"
#include "rtc_video_device_impl.h"
#include "rtc_video_source_impl.h"
#if defined(USE_INTEL_MEDIA_SDK)
#include "src/win/mediacapabilities.h"
#include "src/win/msdkvideodecoderfactory.h"
#include "src/win/msdkvideoencoderfactory.h"
#endif
#if defined(WEBRTC_IOS)
#include "engine/sdk/objc/Framework/Classes/videotoolboxvideocodecfactory.h"
#endif
#include <api/task_queue/default_task_queue_factory.h>

namespace libwebrtc {

// 检查是否禁用音频设备初始化
// 环境变量: LIBWEBRTC_DISABLE_AUDIO=1
static bool IsAudioDisabled() {
  const char* env = std::getenv("LIBWEBRTC_DISABLE_AUDIO");
  return env != nullptr && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y');
}

#if defined(USE_INTEL_MEDIA_SDK)
std::unique_ptr<webrtc::VideoEncoderFactory> CreateIntelVideoEncoderFactory() {
  if (!owt::base::MediaCapabilities::Get()) {
    return webrtc::CreateBuiltinVideoEncoderFactory();
  }
  return std::make_unique<owt::base::MSDKVideoEncoderFactory>();
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreateIntelVideoDecoderFactory() {
  if (!owt::base::MediaCapabilities::Get()) {
    return webrtc::CreateBuiltinVideoDecoderFactory();
  }
  return std::make_unique<owt::base::MSDKVideoDecoderFactory>();
}
#endif

RTCPeerConnectionFactoryImpl::RTCPeerConnectionFactoryImpl():
env_(webrtc::EnvironmentFactory().Create()) {}

RTCPeerConnectionFactoryImpl::~RTCPeerConnectionFactoryImpl() {}

bool RTCPeerConnectionFactoryImpl::Initialize() {
  worker_thread_ = webrtc::Thread::Create();
  worker_thread_->SetName("worker_thread", nullptr);
  RTC_CHECK(worker_thread_->Start()) << "Failed to start thread";

  signaling_thread_ = webrtc::Thread::Create();
  signaling_thread_->SetName("signaling_thread", nullptr);
  RTC_CHECK(signaling_thread_->Start()) << "Failed to start thread";

  network_thread_ = webrtc::Thread::CreateWithSocketServer();
  network_thread_->SetName("network_thread", nullptr);
  RTC_CHECK(network_thread_->Start()) << "Failed to start thread";

  // 检查是否禁用音频
  bool audio_disabled = IsAudioDisabled();

  if (!audio_device_module_) {
    task_queue_factory_ = webrtc::CreateDefaultTaskQueueFactory();
    if (audio_disabled) {
      // 使用 kDummyAudio — 桩实现，不需要硬件，但给 WebRtcVoiceEngine 有效指针
      worker_thread_->BlockingCall([&] {
        audio_device_module_ = webrtc::CreateAudioDeviceModule(
            env_, webrtc::AudioDeviceModule::kDummyAudio, false);
      });
    } else {
      worker_thread_->BlockingCall([&] { CreateAudioDeviceModule_w(); });
    }
  }

  // 音频处理 + transport 始终创建 (WebRtcVoiceEngine 不接受 nullptr)
  if (!audio_processing_impl_) {
    worker_thread_->BlockingCall([this] {
      audio_processing_impl_ = new RefCountedObject<RTCAudioProcessingImpl>();
    });
  }

  if (!audio_transport_factory_) {
    worker_thread_->BlockingCall([this] {
      audio_transport_factory_ =
          webrtc::make_ref_counted<CustomAudioTransportFactory>();
    });
  }

  if (!rtc_peerconnection_factory_) {
    // 如果启用了 passthrough 视频编码模式，创建 PassthroughVideoEncoderFactory
    if (use_passthrough_video_encoder_ && !passthrough_video_encoder_factory_) {
      passthrough_video_encoder_factory_ =
          std::make_unique<lw_extra_PassthroughVideoEncoderFactory>();
      // 注入到 lw_extra_Utils，使 CreateExtendedPeerConnection 复用同一工厂
      lw_extra_Utils::SetExternalVideoEncoderFactory(
          passthrough_video_encoder_factory_.get());
    }

    // 如果启用了 passthrough 音频编码模式，创建 PassthroughAudioEncoderFactory
    if (use_passthrough_audio_encoder_ && !passthrough_audio_encoder_factory_) {
      passthrough_audio_encoder_factory_ =
          new lw_extra_PassthroughAudioEncoderFactory();
      // 注入到 lw_extra_Utils，使 CreateExtendedPeerConnection 复用同一工厂
      lw_extra_Utils::SetExternalAudioEncoderFactory(
          passthrough_audio_encoder_factory_);
    }

    // 视频编码器工厂：passthrough 模式使用自定义工厂，否则使用内置工厂。
    // 将 unique_ptr 所有权转移给 CreatePeerConnectionFactory（WebRTC 内部管理生命周期）。
    std::unique_ptr<webrtc::VideoEncoderFactory> video_encoder_factory;
    if (use_passthrough_video_encoder_) {
      video_encoder_factory = std::move(passthrough_video_encoder_factory_);
    } else {
#if defined(USE_INTEL_MEDIA_SDK)
      builtin_video_encoder_factory_ = CreateIntelVideoEncoderFactory();
      video_encoder_factory = std::move(builtin_video_encoder_factory_);
#else
      builtin_video_encoder_factory_ = webrtc::CreateBuiltinVideoEncoderFactory();
      video_encoder_factory = std::move(builtin_video_encoder_factory_);
#endif
    }

    // 音频编码器工厂：passthrough 模式使用自定义工厂，否则使用内置工厂。
    // PassthroughAudioEncoderFactory 通过 scoped_refptr 管理生命周期（refcounted）。
    webrtc::scoped_refptr<webrtc::AudioEncoderFactory> audio_encoder_factory;
    if (use_passthrough_audio_encoder_ && passthrough_audio_encoder_factory_) {
      audio_encoder_factory = passthrough_audio_encoder_factory_;
    } else {
      audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
    }

    rtc_peerconnection_factory_ = CreatePeerConnectionFactory(
        network_thread_.get(), worker_thread_.get(), signaling_thread_.get(),
        audio_device_module_,
        audio_encoder_factory,
        webrtc::CreateBuiltinAudioDecoderFactory(),
        std::move(video_encoder_factory),
#if defined(USE_INTEL_MEDIA_SDK)
        CreateIntelVideoDecoderFactory(),
#else
        webrtc::CreateBuiltinVideoDecoderFactory(),
#endif
        nullptr,
        audio_processing_impl_->GetAudioProcessing(),
        nullptr, nullptr,
        audio_transport_factory_);
  }

  if (!rtc_peerconnection_factory_.get()) {
    Terminate();
    return false;
  }

  return true;
}

bool RTCPeerConnectionFactoryImpl::Terminate() {
  worker_thread_->BlockingCall([&] {
    audio_device_impl_ = nullptr;
    video_device_impl_ = nullptr;
    audio_processing_impl_ = nullptr;
  });
  // 清除外部工厂指针，避免 rtc_peerconnection_factory_ 销毁后悬挂
  lw_extra_Utils::SetExternalAudioEncoderFactory(nullptr);
  passthrough_audio_encoder_factory_ = nullptr;
  rtc_peerconnection_factory_ = NULL;
  if (audio_device_module_) {
    worker_thread_->BlockingCall([this] { DestroyAudioDeviceModule_w(); });
  }

  return true;
}

void RTCPeerConnectionFactoryImpl::CreateAudioDeviceModule_w() {
  if (!audio_device_module_) {
    audio_device_module_ = webrtc::CreateAudioDeviceModule(
        env_,
        webrtc::AudioDeviceModule::kPlatformDefaultAudio,
        false);
    // Initialize the ADM eagerly so device enumeration (RecordingDevices/
    // PlayoutDevices) works before a PeerConnection has been created. On
    // desktop these queries return early unless the module is initialized,
    // and the voice engine otherwise defers Init() until the audio pipeline
    // is set up. Init() is idempotent, so the later engine call is a no-op.
    if (audio_device_module_)
      audio_device_module_->Init();
  }
}

void RTCPeerConnectionFactoryImpl::DestroyAudioDeviceModule_w() {
  if (audio_device_module_) audio_device_module_ = nullptr;
}

scoped_refptr<RTCPeerConnection> RTCPeerConnectionFactoryImpl::Create(
    const RTCConfiguration& configuration,
    scoped_refptr<RTCMediaConstraints> constraints) {
  scoped_refptr<RTCPeerConnection> peerconnection =
      scoped_refptr<RTCPeerConnectionImpl>(
          new RefCountedObject<RTCPeerConnectionImpl>(
              configuration, constraints, rtc_peerconnection_factory_));
  peerconnections_.push_back(peerconnection);
  return peerconnection;
}

void RTCPeerConnectionFactoryImpl::Delete(
    scoped_refptr<RTCPeerConnection> peerconnection) {
  peerconnections_.erase(
      std::remove_if(
          peerconnections_.begin(), peerconnections_.end(),
          [peerconnection](const scoped_refptr<RTCPeerConnection> pc_) {
            return pc_ == peerconnection;
          }),
      peerconnections_.end());
}

scoped_refptr<RTCAudioDevice> RTCPeerConnectionFactoryImpl::GetAudioDevice() {
  // 如果音频被禁用，返回 nullptr
  if (IsAudioDisabled()) {
    return nullptr;
  }

  if (!audio_device_module_) {
    worker_thread_->BlockingCall([this] { CreateAudioDeviceModule_w(); });
  }

  if (!audio_device_impl_)
    audio_device_impl_ =
        scoped_refptr<AudioDeviceImpl>(new RefCountedObject<AudioDeviceImpl>(
            audio_device_module_, worker_thread_.get()));

  return audio_device_impl_;
}

scoped_refptr<RTCAudioProcessing>
RTCPeerConnectionFactoryImpl::GetAudioProcessing() {
  // 如果音频被禁用，返回 nullptr
  if (IsAudioDisabled()) {
    return nullptr;
  }

  if (!audio_processing_impl_) {
    worker_thread_->BlockingCall([this] {
      audio_processing_impl_ = new RefCountedObject<RTCAudioProcessingImpl>();
    });
  }

  return audio_processing_impl_;
}

scoped_refptr<RTCVideoDevice> RTCPeerConnectionFactoryImpl::GetVideoDevice() {
  if (!video_device_impl_)
    video_device_impl_ = scoped_refptr<RTCVideoDeviceImpl>(
        new RefCountedObject<RTCVideoDeviceImpl>(worker_thread_.get()));

  return video_device_impl_;
}

webrtc::scoped_refptr<libwebrtc::LocalAudioSource>
RTCPeerConnectionFactoryImpl::CreateAudioSourceWithOptions(
    webrtc::AudioOptions* options, bool is_custom_source) {
  RTC_DCHECK(options);
  // if is_custom_source == true, not using the default audio transport,
  // you can put costom audio frame via LocalAudioSource::CaptureFrame(...)
  // and the audio transport will be null.
  // otherwise, use the default audio transport, audio transport will
  // put audio frame from your platform adm to your
  // LocalAudioSource::SendAudioData(...).
  // 当音频被禁用时，audio_transport_factory_ 为 null，也视为 custom source
  bool use_null_transport = is_custom_source || !audio_transport_factory_;

  if (webrtc::Thread::Current() != signaling_thread_.get()) {
    return signaling_thread_->BlockingCall([this, options, use_null_transport] {
      return libwebrtc::LocalAudioSource::Create(
          options, use_null_transport
                       ? nullptr
                       : audio_transport_factory_->audio_transport_impl());
    });
  }
  return libwebrtc::LocalAudioSource::Create(
      options, use_null_transport
                   ? nullptr
                   : audio_transport_factory_->audio_transport_impl());
}

scoped_refptr<RTCAudioSource> RTCPeerConnectionFactoryImpl::CreateAudioSource(
    const string audio_source_label, RTCAudioSource::SourceType source_type,
    RTCAudioOptions options) {
  auto rtc_options = webrtc::AudioOptions();
  rtc_options.echo_cancellation = options.echo_cancellation;
  rtc_options.auto_gain_control = options.auto_gain_control;
  rtc_options.noise_suppression = options.noise_suppression;
  rtc_options.highpass_filter = options.highpass_filter;
  webrtc::scoped_refptr<libwebrtc::LocalAudioSource> rtc_source_track =
      CreateAudioSourceWithOptions(
          &rtc_options, source_type == RTCAudioSource::SourceType::kCustom);
  scoped_refptr<RTCAudioSourceImpl> source = scoped_refptr<RTCAudioSourceImpl>(
      new RefCountedObject<RTCAudioSourceImpl>(rtc_source_track, source_type));
  return source;
}

#ifdef RTC_DESKTOP_DEVICE
scoped_refptr<RTCDesktopDevice>
RTCPeerConnectionFactoryImpl::GetDesktopDevice() {
  if (!desktop_device_impl_) {
    desktop_device_impl_ = scoped_refptr<RTCDesktopDeviceImpl>(
        new RefCountedObject<RTCDesktopDeviceImpl>(signaling_thread_.get()));
  }
  return desktop_device_impl_;
}
#endif

scoped_refptr<RTCVideoSource> RTCPeerConnectionFactoryImpl::CreateVideoSource(
    scoped_refptr<RTCVideoCapturer> capturer, const string video_source_label,
    scoped_refptr<RTCMediaConstraints> constraints) {
  if (webrtc::Thread::Current() != signaling_thread_.get()) {
    scoped_refptr<RTCVideoSource> source = signaling_thread_->BlockingCall(
        [this, capturer, video_source_label, constraints] {
          return CreateVideoSource_s(
              capturer, to_std_string(video_source_label).c_str(), constraints);
        });
    return source;
  }

  return CreateVideoSource_s(
      capturer, to_std_string(video_source_label).c_str(), constraints);
}

scoped_refptr<RTCVideoSource> RTCPeerConnectionFactoryImpl::CreateVideoSource_s(
    scoped_refptr<RTCVideoCapturer> capturer, const char* video_source_label,
    scoped_refptr<RTCMediaConstraints> constraints) {
  RTCVideoCapturerImpl* capturer_impl =
      static_cast<RTCVideoCapturerImpl*>(capturer.get());
  /*RTCMediaConstraintsImpl* media_constraints =
          static_cast<RTCMediaConstraintsImpl*>(constraints.get());*/
  std::shared_ptr<webrtc::internal::VideoCapturer> internal_capturer =
      capturer_impl->video_capturer();
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> rtc_source_track =
      webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(
          new webrtc::RefCountedObject<webrtc::internal::CapturerTrackSource>(
              internal_capturer));
  // Pass the internal capturer through so RTCVideoSource::OnCapturedFrame can
  // forward user-supplied frames into the same broadcast pipeline.
  scoped_refptr<RTCVideoSourceImpl> source = scoped_refptr<RTCVideoSourceImpl>(
      new RefCountedObject<RTCVideoSourceImpl>(rtc_source_track,
                                               internal_capturer));
  return source;
}

scoped_refptr<RTCVideoSource> RTCPeerConnectionFactoryImpl::CreateCustomVideoSource(
  string video_source_label,
  scoped_refptr<RTCMediaConstraints> constraints) {
  // A vanilla internal::VideoCapturer is a complete passthrough: its default
  // StartCapture/StopCapture/CaptureStarted are no-ops, but OnFrame still
  // routes pushed frames through the video adapter and broadcaster, which is
  // all OnCapturedFrame needs.
  std::shared_ptr<webrtc::internal::VideoCapturer> internal_capturer =
      std::make_shared<webrtc::internal::VideoCapturer>();
  auto capture = scoped_refptr<RTCVideoCapturerImpl>(
      new RefCountedObject<RTCVideoCapturerImpl>(internal_capturer));
  return CreateVideoSource(capture, video_source_label, constraints);
}

#ifdef RTC_DESKTOP_DEVICE
scoped_refptr<RTCVideoSource> RTCPeerConnectionFactoryImpl::CreateDesktopSource(
    scoped_refptr<RTCDesktopCapturer> capturer, const string video_source_label,
    scoped_refptr<RTCMediaConstraints> constraints) {
  if (webrtc::Thread::Current() != signaling_thread_.get()) {
    scoped_refptr<RTCVideoSource> source = signaling_thread_->BlockingCall(
        [this, capturer, video_source_label, constraints] {
          return CreateDesktopSource_d(
              capturer, to_std_string(video_source_label).c_str(), constraints);
        });
    return source;
  }

  return CreateDesktopSource_d(
      capturer, to_std_string(video_source_label).c_str(), constraints);
}

scoped_refptr<RTCVideoSource>
RTCPeerConnectionFactoryImpl::CreateDesktopSource_d(
    scoped_refptr<RTCDesktopCapturer> capturer, const char* video_source_label,
    scoped_refptr<RTCMediaConstraints> constraints) {
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> rtc_source_track =
      webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(
          new webrtc::RefCountedObject<ScreenCapturerTrackSource>(capturer));

  scoped_refptr<RTCVideoSourceImpl> source = scoped_refptr<RTCVideoSourceImpl>(
      new RefCountedObject<RTCVideoSourceImpl>(rtc_source_track));

  return source;
}
#endif

scoped_refptr<RTCMediaStream> RTCPeerConnectionFactoryImpl::CreateStream(
    const string stream_id) {
  webrtc::scoped_refptr<webrtc::MediaStreamInterface> rtc_stream =
      rtc_peerconnection_factory_->CreateLocalMediaStream(
          to_std_string(stream_id));

  scoped_refptr<MediaStreamImpl> stream = scoped_refptr<MediaStreamImpl>(
      new RefCountedObject<MediaStreamImpl>(rtc_stream));

  return stream;
}

scoped_refptr<RTCVideoTrack> RTCPeerConnectionFactoryImpl::CreateVideoTrack(
    scoped_refptr<RTCVideoSource> source, const string track_id) {
  scoped_refptr<RTCVideoSourceImpl> source_adapter(
      static_cast<RTCVideoSourceImpl*>(source.get()));
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> rtc_video_track =
      rtc_peerconnection_factory_->CreateVideoTrack(
          source_adapter->rtc_source_track(), track_id.std_string());

  scoped_refptr<VideoTrackImpl> video_track = scoped_refptr<VideoTrackImpl>(
      new RefCountedObject<VideoTrackImpl>(rtc_video_track));

  // 	webrtc::VideoTrackProxyWithInternal<webrtc::VideoTrackInterface>
  // *track_proxy =
  // dynamic_cast<webrtc::VideoTrackProxyWithInternal<webrtc::VideoTrackInterface>
  // *>(video_track.get()); 	if (track_proxy) {
  // 		webrtc::MediaStreamTrack<VideoTrackInterface> *track =
  // dynamic_cast<webrtc::MediaStreamTrack<VideoTrackInterface>*>(track_proxy->internal());
  // 		LOG(INFO) << "VideoTrackInterface: " << track->id();
  // 	}

  return video_track;
}

scoped_refptr<RTCAudioTrack> RTCPeerConnectionFactoryImpl::CreateAudioTrack(
    scoped_refptr<RTCAudioSource> source, const string track_id) {
  RTCAudioSourceImpl* source_impl =
      static_cast<RTCAudioSourceImpl*>(source.get());

  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
      rtc_peerconnection_factory_->CreateAudioTrack(
          to_std_string(track_id), source_impl->rtc_audio_source().get()));

  scoped_refptr<AudioTrackImpl> track = scoped_refptr<AudioTrackImpl>(
      new RefCountedObject<AudioTrackImpl>(audio_track));
  return track;
}

scoped_refptr<RTCRtpCapabilities>
RTCPeerConnectionFactoryImpl::GetRtpSenderCapabilities(
    RTCMediaType media_type) {
  if (webrtc::Thread::Current() != signaling_thread_.get()) {
    scoped_refptr<RTCRtpCapabilities> capabilities =
        signaling_thread_->BlockingCall([this, media_type] {
          return GetRtpSenderCapabilities(media_type);
        });
    return capabilities;
  }

  webrtc::MediaType type = webrtc::MediaType::AUDIO;
  switch (media_type) {
    case RTCMediaType::AUDIO:
      type = webrtc::MediaType::AUDIO;
      break;
    case RTCMediaType::VIDEO:
      type = webrtc::MediaType::VIDEO;
      break;
    default:
      break;
  }
  webrtc::RtpCapabilities rtp_capabilities =
      rtc_peerconnection_factory_->GetRtpSenderCapabilities(type);
  return scoped_refptr<RTCRtpCapabilities>(
      new RefCountedObject<RTCRtpCapabilitiesImpl>(rtp_capabilities));
}

scoped_refptr<RTCRtpCapabilities>
RTCPeerConnectionFactoryImpl::GetRtpReceiverCapabilities(
    RTCMediaType media_type) {
  if (webrtc::Thread::Current() != signaling_thread_.get()) {
    scoped_refptr<RTCRtpCapabilities> capabilities =
        signaling_thread_->BlockingCall([this, media_type] {
          return GetRtpSenderCapabilities(media_type);
        });
    return capabilities;
  }
  webrtc::MediaType type = webrtc::MediaType::AUDIO;
  switch (media_type) {
    case RTCMediaType::AUDIO:
      type = webrtc::MediaType::AUDIO;
      break;
    case RTCMediaType::VIDEO:
      type = webrtc::MediaType::VIDEO;
      break;
    default:
      break;
  }
  webrtc::RtpCapabilities rtp_capabilities =
      rtc_peerconnection_factory_->GetRtpReceiverCapabilities(type);
  return scoped_refptr<RTCRtpCapabilities>(
      new RefCountedObject<RTCRtpCapabilitiesImpl>(rtp_capabilities));
}

void RTCPeerConnectionFactoryImpl::SetUsePassthroughVideoEncoder(bool enabled) {
  use_passthrough_video_encoder_ = enabled;
}

void RTCPeerConnectionFactoryImpl::SetUsePassthroughAudioEncoder(bool enabled) {
  use_passthrough_audio_encoder_ = enabled;
}

}  // namespace libwebrtc
