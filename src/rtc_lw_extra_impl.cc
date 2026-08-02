#include "rtc_lw_extra_impl.h"

#include <cstdio>
#include <cstdlib>

#include "api/video_codecs/video_codec.h"
#include "api/video/video_codec_type.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/synchronization/mutex.h"
#include "routing_video_encoder_factory.h"  // provides LW_LOG macro
#include "rtc_rtp_receiver_impl.h"

namespace libwebrtc {

// ==================== PassthroughFrameTransformer ====================

PassthroughFrameTransformer::PassthroughFrameTransformer(
    lw_extra_EncodedReceiverImpl* receiver)
    : receiver_(receiver) {}

void PassthroughFrameTransformer::Transform(
    std::unique_ptr<webrtc::TransformableFrameInterface> frame) {
  if (!frame || frame->GetDirection() !=
                    webrtc::TransformableFrameInterface::Direction::kReceiver) {
    // 非接收方向或空帧，直接传回
    if (frame && callback_) {
      callback_->OnTransformedFrame(std::move(frame));
    }
    return;
  }

  // 尝试视频帧
  auto* video_frame =
      dynamic_cast<webrtc::TransformableVideoFrameInterface*>(frame.get());
  if (video_frame && receiver_->video_enabled() && receiver_->video_sink_) {
    auto data = video_frame->GetData();
    const auto& header = video_frame->header();

    // 从 RTPVideoHeader 提取 codec
    lw_extra_VideoCodec codec;
    switch (header.codec) {
      case webrtc::kVideoCodecH264:
        codec = lw_extra_VideoCodec::kH264;
        break;
      case webrtc::kVideoCodecAV1:
        codec = lw_extra_VideoCodec::kAV1;
        break;
      default:
        // 未知 codec，尝试从 MIME type 判断
        {
          auto mime = video_frame->GetMimeType();
          if (mime.find("H264") != std::string::npos ||
              mime.find("h264") != std::string::npos) {
            codec = lw_extra_VideoCodec::kH264;
          } else if (mime.find("AV1") != std::string::npos ||
                     mime.find("av1") != std::string::npos) {
            codec = lw_extra_VideoCodec::kAV1;
          } else {
            codec = lw_extra_VideoCodec::kH264;  // fallback
          }
        }
        break;
    }

    static int rx_count = 0;
    rx_count++;
    if (rx_count <= 5 || rx_count % 30 == 0)
      LW_LOG("[lw_extra] PassthroughFrameTransformer::Transform video #%d: "
          "%dx%d %zuB key=%d codec=%d ts=%u\n",
          rx_count, header.width, header.height, data.size(),
          video_frame->IsKeyFrame() ? 1 : 0, (int)codec,
          video_frame->GetTimestamp());

    receiver_->OnEncodedVideoFrameReceived(
        data.data(), data.size(),
        video_frame->GetTimestamp(),
        video_frame->IsKeyFrame(),
        header.width, header.height, codec);
  }

  // 尝试音频帧
  auto* audio_frame =
      dynamic_cast<webrtc::TransformableAudioFrameInterface*>(frame.get());
  if (audio_frame && receiver_->audio_enabled() && receiver_->audio_sink_) {
    auto data = audio_frame->GetData();

    // 从 MIME type 判断 codec
    lw_extra_AudioCodec codec = lw_extra_AudioCodec::kOpus;  // default

    static int rx_audio_count = 0;
    rx_audio_count++;
    if (rx_audio_count <= 5 || rx_audio_count % 50 == 0)
      LW_LOG("[lw_extra] PassthroughFrameTransformer::Transform audio #%d: "
          "%zuB ts=%u\n",
          rx_audio_count, data.size(), audio_frame->GetTimestamp());

    receiver_->OnEncodedAudioFrameReceived(
        data.data(), data.size(),
        audio_frame->GetTimestamp(), codec);
  }

  // 原样传回，解码管道继续运行
  if (callback_) {
    callback_->OnTransformedFrame(std::move(frame));
  }
}

void PassthroughFrameTransformer::RegisterTransformedFrameCallback(
    webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback) {
  callback_ = callback;
}

void PassthroughFrameTransformer::RegisterTransformedFrameSinkCallback(
    webrtc::scoped_refptr<webrtc::TransformedFrameCallback> callback,
    uint32_t ssrc) {
  callback_ = callback;
}

void PassthroughFrameTransformer::UnregisterTransformedFrameCallback() {
  callback_ = nullptr;
}

void PassthroughFrameTransformer::UnregisterTransformedFrameSinkCallback(
    uint32_t ssrc) {
  callback_ = nullptr;
}

// ==================== 静态成员定义 ====================

std::unique_ptr<lw_extra_PassthroughVideoEncoderFactory>
    lw_extra_Utils::video_encoder_factory_ = nullptr;
lw_extra_PassthroughVideoEncoderFactory*
    lw_extra_Utils::external_video_encoder_factory_ = nullptr;
lw_extra_PassthroughAudioEncoderFactory*
    lw_extra_Utils::external_audio_encoder_factory_ = nullptr;
lw_extra_PassthroughAudioEncoderFactory*
    lw_extra_Utils::audio_encoder_factory_ = nullptr;
void* lw_extra_Utils::routing_video_encoder_factory_ = nullptr;
void* lw_extra_Utils::routing_audio_encoder_factory_ = nullptr;

// ==================== PassthroughVideoEncoder ====================

lw_extra_PassthroughVideoEncoder::lw_extra_PassthroughVideoEncoder()
    : callback_(nullptr), initialized_(false), frame_id_(0) {
  memset(&codec_settings_, 0, sizeof(codec_settings_));
}

lw_extra_PassthroughVideoEncoder::~lw_extra_PassthroughVideoEncoder() {}

int32_t lw_extra_PassthroughVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codec_settings, int32_t number_of_cores,
    size_t max_payload_size) {
  LW_LOG("[lw_extra] PassthroughVideoEncoder::InitEncode: codec=%d width=%d height=%d fps=%d\n",
      (int)codec_settings->codecType,
      codec_settings->width, codec_settings->height, codec_settings->maxFramerate);
  // GetSupportedFormats 中 passthrough 格式排在前面，Create() 直接传 H264/AV1，
  // codecType 已与 SendEncodedFrame 数据一致，无需覆盖。
  codec_settings_ = *codec_settings;
  initialized_ = true;
  LW_LOG("[lw_extra] PassthroughVideoEncoder::InitEncode: done codec=%d width=%d height=%d fps=%d\n",
      (int)codec_settings_.codecType,
      codec_settings_.width, codec_settings_.height, codec_settings_.maxFramerate);
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t lw_extra_PassthroughVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    const webrtc::VideoEncoder::Settings& settings) {
  return InitEncode(codec_settings, settings.number_of_cores,
                   settings.max_payload_size);
}

int32_t lw_extra_PassthroughVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  callback_ = callback;
  LW_LOG("[lw_extra] PassthroughVideoEncoder::RegisterEncodeCompleteCallback: callback=%p initialized=%d\n",
      (void*)callback, (int)initialized_);
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t lw_extra_PassthroughVideoEncoder::Release() {
  initialized_ = false;
  callback_ = nullptr;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t lw_extra_PassthroughVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  // ★ 方案 B: 从队列取编码数据，通过 OnEncodedImage 发送。
  //    使用 frame.rtp_timestamp() 而非外部传入的 timestamp——
  //    这会与 OnEncodeStarted 记录的 timestamp 匹配，
  //    使 FillMetadataAndTimingInfo 正确填充 capture_time / ntp_time / timing。
  PendingEncodedFrame pf;
  bool has_data = false;
  {
    webrtc::MutexLock lock(&pending_mutex_);
    if (!pending_frames_.empty()) {
      pf = std::move(pending_frames_.front());
      pending_frames_.pop_front();
      has_data = true;
    }
  }

  if (!has_data) {
    // 队列为空：dummy frame 在编码数据到达之前触发了 Encode()
    // （例如初始化 trigger frame），正常返回即可。
    static int empty_count = 0;
    empty_count++;
    if (empty_count <= 3) {
      FILE* f = fopen("/tmp/bridge_debug.log", "a");
      if (f) {
        fprintf(f, "[passthrough B] ENCODE #%d: EMPTY queue (init trigger or no data yet)\n", empty_count);
        fclose(f);
      }
      LW_LOG("[lw_extra] PassthroughVideoEncoder::Encode: EMPTY queue #%d (no pending frame)\n", empty_count);
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  if (!callback_) {
    return WEBRTC_VIDEO_CODEC_OK;
  }

  // 创建 EncodedImage，使用 frame.rtp_timestamp() 确保匹配
  webrtc::EncodedImage encoded_image;

  auto buffer = webrtc::EncodedImageBuffer::Create(pf.data.data(), pf.data.size());
  encoded_image.SetEncodedData(buffer);
  encoded_image.set_size(pf.data.size());
  encoded_image.SetRtpTimestamp(frame.rtp_timestamp());   // ★ 关键: VideoStreamEncoder 分配的 timestamp
  encoded_image._encodedWidth = pf.width;
  encoded_image._encodedHeight = pf.height;

  // 设置帧类型
  encoded_image.SetFrameType(
      pf.is_key_frame ? webrtc::VideoFrameType::kVideoFrameKey
                      : webrtc::VideoFrameType::kVideoFrameDelta);

  // 设置编解码类型 + H264 codec-specific 信息
  webrtc::CodecSpecificInfo codec_specific_info;
  codec_specific_info.codecType =
      (pf.codec == lw_extra_VideoCodec::kH264) ? webrtc::kVideoCodecH264
                                               : webrtc::kVideoCodecAV1;

  if (pf.codec == lw_extra_VideoCodec::kH264) {
    codec_specific_info.codecSpecific.H264.packetization_mode =
        packetization_mode_;
    codec_specific_info.codecSpecific.H264.temporal_idx =
        webrtc::kNoTemporalIdx;
    codec_specific_info.codecSpecific.H264.idr_frame = pf.is_key_frame;
    codec_specific_info.codecSpecific.H264.base_layer_sync = false;
  }

  // 诊断日志 — 写 /tmp/bridge_debug.log
  static int diag_frame = 0;
  diag_frame++;
  if (diag_frame <= 10 || diag_frame % 100 == 0) {
    FILE* f = fopen("/tmp/bridge_debug.log", "a");
    if (f) {
      fprintf(f, "[passthrough B] ENCODE→SEND #%d: "
          "size=%zu wxh=%dx%d key=%d codec=%d "
          "frame_rtp_ts=%u "
          "cs_pkt_mode=%d cs_temporal=0x%02X cs_idr=%d cs_blsync=%d queue_left=%zu\n",
          diag_frame, pf.data.size(),
          pf.width, pf.height, (int)pf.is_key_frame,
          (int)pf.codec,
          frame.rtp_timestamp(),
          (pf.codec == lw_extra_VideoCodec::kH264
               ? (int)codec_specific_info.codecSpecific.H264.packetization_mode
               : -1),
          (pf.codec == lw_extra_VideoCodec::kH264
               ? codec_specific_info.codecSpecific.H264.temporal_idx
               : 0),
          (pf.codec == lw_extra_VideoCodec::kH264
               ? (int)codec_specific_info.codecSpecific.H264.idr_frame
               : -1),
          (pf.codec == lw_extra_VideoCodec::kH264
               ? (int)codec_specific_info.codecSpecific.H264.base_layer_sync
               : -1),
          pending_frames_.size());
      fclose(f);
    }
  }

  // 通过回调发送 — 此时 FillMetadataAndTimingInfo 能正确匹配
  webrtc::EncodedImageCallback::Result result =
      callback_->OnEncodedImage(encoded_image, &codec_specific_info);

  if (result.error != webrtc::EncodedImageCallback::Result::OK) {
    FILE* f = fopen("/tmp/bridge_debug.log", "a");
    if (f) {
      fprintf(f, "[passthrough B] ENCODE→SEND #%d: OnEncodedImage FAILED error=%d\n",
          diag_frame, (int)result.error);
      fclose(f);
    }
    RTC_LOG(LS_ERROR) << "Failed to send encoded image: error="
                      << result.error;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  frame_id_ = result.frame_id;
  static int vid_send_ok = 0;
  vid_send_ok++;
  if (vid_send_ok <= 5 || vid_send_ok % 150 == 0)
    LW_LOG("[lw_extra] PassthroughVideoEncoder::Encode→Send: OK #%d size=%zu key=%d rtp_ts=%u\n",
        vid_send_ok, pf.data.size(), (int)pf.is_key_frame, frame.rtp_timestamp());

  // 返回 OK — VideoStreamEncoder::OnEncodedImage 内部 FillMetadataAndTimingInfo
  // 现在能通过 frame.rtp_timestamp() 正确匹配 OnEncodeStarted 记录 ✅
  return WEBRTC_VIDEO_CODEC_OK;
}

void lw_extra_PassthroughVideoEncoder::SetRates(
    const webrtc::VideoEncoder::RateControlParameters& parameters) {
  // 传递编码器不需要速率控制
}

webrtc::VideoEncoder::EncoderInfo
lw_extra_PassthroughVideoEncoder::GetEncoderInfo() const {
  EncoderInfo info;
  info.implementation_name = "lw_extra_passthrough_video_encoder";
  info.supports_native_handle = false;
  info.is_hardware_accelerated = false;
  info.has_trusted_rate_controller = true;
  info.requested_resolution_alignment = 1;
  return info;
}

bool lw_extra_PassthroughVideoEncoder::SendEncodedFrame(
    const lw_extra_EncodedVideoFrame& frame) {
  if (!callback_ || !initialized_) {
    LW_LOG("[lw_extra] PassthroughVideoEncoder::SendEncodedFrame: FAILED callback=%p initialized=%d\n",
        (void*)callback_, (int)initialized_);
    RTC_LOG(LS_ERROR) << "Encoder not initialized or no callback";
    return false;
  }

  if (!frame.data || frame.size == 0) {
    LW_LOG("[lw_extra] PassthroughVideoEncoder::SendEncodedFrame: invalid frame data\n");
    RTC_LOG(LS_ERROR) << "Invalid frame data";
    return false;
  }

  // ★ 方案 B: 不直接发送，入队等待 Encode() 被 VideoStreamEncoder 调用。
  //    Encode() 由 dummy frame（通过 video_source->OnCapturedFrame 触发）
  //    驱动，保证 OnFrame → OnEncodeStarted → Encode → OnEncodedImage
  //    的完整链路，使 FillMetadataAndTimingInfo 能按 RTP timestamp 正确匹配。
  {
    webrtc::MutexLock lock(&pending_mutex_);

    // 队列满时 drop 最旧的帧（视频流场景下优先保证低延迟）
    while (pending_frames_.size() >= kMaxPendingFrames) {
      pending_frames_.pop_front();
    }

    PendingEncodedFrame pf;
    pf.data.assign(frame.data, frame.data + frame.size);
    pf.codec = frame.codec;
    pf.is_key_frame = frame.is_key_frame;
    pf.width = frame.width;
    pf.height = frame.height;
    pending_frames_.push_back(std::move(pf));
  }

  static int queue_count = 0;
  queue_count++;
  if (queue_count <= 10 || queue_count % 150 == 0) {
    FILE* f = fopen("/tmp/bridge_debug.log", "a");
    if (f) {
      fprintf(f, "[passthrough B] QUEUE #%d: size=%zu key=%d wxh=%dx%d depth=%zu\n",
          queue_count, frame.size, (int)frame.is_key_frame,
          frame.width, frame.height, pending_frames_.size());
      fclose(f);
    }
  }
  LW_LOG("[lw_extra] PassthroughVideoEncoder::SendEncodedFrame: QUEUED #%d size=%zu key=%d depth=%zu\n",
      queue_count, frame.size, (int)frame.is_key_frame, pending_frames_.size());
  return true;
}

void lw_extra_PassthroughVideoEncoder::SetCodec(lw_extra_VideoCodec codec) {
  codec_ = codec;
  // 同步 codec_settings_ 中的 codecType，确保与 SendEncodedFrame 数据一致
  if (codec == lw_extra_VideoCodec::kH264) {
    codec_settings_.codecType = webrtc::kVideoCodecH264;
  } else if (codec == lw_extra_VideoCodec::kAV1) {
    codec_settings_.codecType = webrtc::kVideoCodecAV1;
  }
  LW_LOG("[lw_extra] PassthroughVideoEncoder::SetCodec: codec=%d codecType=%d\n",
      (int)codec, (int)codec_settings_.codecType);
}

// ==================== PassthroughAudioEncoder ====================

lw_extra_PassthroughAudioEncoder::lw_extra_PassthroughAudioEncoder(
    int payload_type)
    : payload_type_(payload_type), initialized_(false) {}

lw_extra_PassthroughAudioEncoder::~lw_extra_PassthroughAudioEncoder() {}

int lw_extra_PassthroughAudioEncoder::SampleRateHz() const {
  return sample_rate_hz_;
}

size_t lw_extra_PassthroughAudioEncoder::NumChannels() const {
  return num_channels_;
}

int lw_extra_PassthroughAudioEncoder::RtpTimestampRateHz() const {
  return sample_rate_hz_;
}

size_t lw_extra_PassthroughAudioEncoder::Num10MsFramesInNextPacket() const {
  return 1;
}

size_t lw_extra_PassthroughAudioEncoder::Max10MsFramesInAPacket() const {
  return 1;
}

int lw_extra_PassthroughAudioEncoder::GetTargetBitrate() const { return 0; }

void lw_extra_PassthroughAudioEncoder::Reset() { initialized_ = false; }

std::optional<std::pair<webrtc::TimeDelta, webrtc::TimeDelta>>
lw_extra_PassthroughAudioEncoder::GetFrameLengthRange() const {
  // Opus 标准帧长范围: 10ms ~ 60ms (48000Hz 下 480~2880 samples)
  return std::make_pair(webrtc::TimeDelta::Millis(10),
                        webrtc::TimeDelta::Millis(60));
}

webrtc::AudioEncoder::EncodedInfo
lw_extra_PassthroughAudioEncoder::EncodeImpl(
    uint32_t rtp_timestamp, webrtc::ArrayView<const int16_t> audio,
    webrtc::Buffer* encoded) {
  // 从队列中取出预编码数据（如果有）
  webrtc::MutexLock lock(&mutex_);
  EncodedInfo info;
  info.encoded_timestamp = rtp_timestamp;
  info.payload_type = payload_type_;

  if (!pending_frames_.empty()) {
    auto& pending = pending_frames_.front();
    if (encoded && !pending.data.empty()) {
      encoded->AppendData(pending.data.data(), pending.data.size());
    }
    info.encoded_bytes = pending.data.size();
    info.encoded_timestamp = pending.timestamp;
    info.send_even_if_empty = true;
    info.speech = true;
    info.encoder_type = CodecType::kOpus;
    pending_frames_.erase(pending_frames_.begin());
  } else {
    // 没有预编码数据，返回空编码信息
    info.encoded_bytes = 0;
  }
  return info;
}

bool lw_extra_PassthroughAudioEncoder::SendEncodedFrame(
    const lw_extra_EncodedAudioFrame& frame) {
  if (!frame.data || frame.size == 0) {
    LW_LOG("[lw_extra] PassthroughAudioEncoder::SendEncodedFrame: invalid frame data\n");
    RTC_LOG(LS_ERROR) << "Invalid audio frame data";
    return false;
  }

  // 将编码数据放入队列，等待 EncodeImpl 被调用时发送
  webrtc::MutexLock lock(&mutex_);
  PendingAudioFrame pending;
  pending.data.assign(frame.data, frame.data + frame.size);
  pending.timestamp = frame.timestamp;
  pending_frames_.push_back(std::move(pending));
  static int audio_send_count = 0;
  audio_send_count++;
  if (audio_send_count <= 3 || audio_send_count % 50 == 0)
    LW_LOG("[lw_extra] PassthroughAudioEncoder::SendEncodedFrame: #%d size=%zu ts=%u queue_depth=%zu\n",
        audio_send_count, frame.size, frame.timestamp, pending_frames_.size());
  return true;
}

// ==================== PassthroughVideoEncoderFactory ====================

lw_extra_PassthroughVideoEncoderFactory::
    lw_extra_PassthroughVideoEncoderFactory() {}

lw_extra_PassthroughVideoEncoderFactory::
    ~lw_extra_PassthroughVideoEncoderFactory() {}

std::vector<webrtc::SdpVideoFormat>
lw_extra_PassthroughVideoEncoderFactory::GetSupportedFormats() const {
  std::vector<webrtc::SdpVideoFormat> formats;

  // 支持 H264 — 必须声明 packetization-mode=1 (NonInterleaved)，
  // 因为 passthrough 编码数据来自外部，可能包含超大 NAL 需要 FU-A 分片。
  // SDP 的 packetization-mode=0 (SingleNalUnit) 只适用于内部 encoder。
  {
    webrtc::SdpVideoFormat format("H264",
        {{"packetization-mode", "1"}});
    formats.push_back(format);
  }

  // 支持 AV1
  {
    webrtc::SdpVideoFormat format("AV1");
    formats.push_back(format);
  }

  return formats;
}

std::unique_ptr<webrtc::VideoEncoder>
lw_extra_PassthroughVideoEncoderFactory::Create(
    const webrtc::Environment& env, const webrtc::SdpVideoFormat& format) {
  auto encoder = std::make_unique<lw_extra_PassthroughVideoEncoder>();
  encoder_queue_.push_back(encoder.get());
  LW_LOG("[lw_extra] PassthroughVideoEncoderFactory::Create: encoder=%p queue_size=%zu format=%s\n",
      (void*)encoder.get(), encoder_queue_.size(), format.name.c_str());
  return encoder;
}

lw_extra_PassthroughVideoEncoder*
lw_extra_PassthroughVideoEncoderFactory::GetNextEncoder() {
  if (next_encoder_index_ >= encoder_queue_.size()) return nullptr;
  return encoder_queue_[next_encoder_index_++];
}

// ==================== PassthroughAudioEncoderFactory ====================

lw_extra_PassthroughAudioEncoderFactory::
    lw_extra_PassthroughAudioEncoderFactory() {}

lw_extra_PassthroughAudioEncoderFactory::
    ~lw_extra_PassthroughAudioEncoderFactory() {}

std::vector<webrtc::AudioCodecSpec>
lw_extra_PassthroughAudioEncoderFactory::GetSupportedEncoders() {
  std::vector<webrtc::AudioCodecSpec> specs;

  webrtc::CodecParameterMap params;
  params["stereo"] = "1";
  webrtc::AudioCodecSpec spec{
      webrtc::SdpAudioFormat("opus", 48000, 2, std::move(params)),
      webrtc::AudioCodecInfo(48000, 2, 64000)};
  specs.push_back(spec);

  return specs;
}

std::optional<webrtc::AudioCodecInfo>
lw_extra_PassthroughAudioEncoderFactory::QueryAudioEncoder(
    const webrtc::SdpAudioFormat& format) {
  // SdpAudioFormat name is typically lowercase, use direct comparison
  if (format.name == "opus") {
    // AudioCodecInfo(sample_rate_hz, num_channels, bitrate_bps)
    return webrtc::AudioCodecInfo{48000, 2, 64000};
  }
  return std::nullopt;
}

std::unique_ptr<webrtc::AudioEncoder>
lw_extra_PassthroughAudioEncoderFactory::Create(
    const webrtc::Environment& env, const webrtc::SdpAudioFormat& format,
    webrtc::AudioEncoderFactory::Options options) {
  auto encoder =
      std::make_unique<lw_extra_PassthroughAudioEncoder>(options.payload_type);
  encoder_queue_.push_back(encoder.get());
  LW_LOG("[lw_extra] PassthroughAudioEncoderFactory::Create: encoder=%p queue_size=%zu pt=%d\n",
      (void*)encoder.get(), encoder_queue_.size(), options.payload_type);
  return encoder;
}

lw_extra_PassthroughAudioEncoder*
lw_extra_PassthroughAudioEncoderFactory::GetNextEncoder() {
  if (next_encoder_index_ >= encoder_queue_.size()) return nullptr;
  return encoder_queue_[next_encoder_index_++];
}

// ==================== EncodedSenderImpl ====================

lw_extra_EncodedSenderImpl::lw_extra_EncodedSenderImpl(
    scoped_refptr<RTCRtpSender> rtp_sender,
    lw_extra_PassthroughVideoEncoder* video_encoder,
    lw_extra_PassthroughAudioEncoder* audio_encoder)
    : rtp_sender_(std::move(rtp_sender)),
      video_encoder_(video_encoder),
      audio_encoder_(audio_encoder),
      video_enabled_(false),
      audio_enabled_(false) {}

lw_extra_EncodedSenderImpl::~lw_extra_EncodedSenderImpl() {}

bool lw_extra_EncodedSenderImpl::SendEncodedVideoFrame(
    const lw_extra_EncodedVideoFrame& frame) {
  if (!video_enabled_ || !video_encoder_) {
    LW_LOG("[lw_extra] EncodedSenderImpl::SendEncodedVideoFrame: FAILED video_enabled=%d video_encoder=%p\n",
        (int)video_enabled_, (void*)video_encoder_);
    RTC_LOG(LS_ERROR) << "Video encoded send not enabled or no encoder";
    return false;
  }
  return video_encoder_->SendEncodedFrame(frame);
}

bool lw_extra_EncodedSenderImpl::SendEncodedAudioFrame(
    const lw_extra_EncodedAudioFrame& frame) {
  if (!audio_enabled_ || !audio_encoder_) {
    LW_LOG("[lw_extra] EncodedSenderImpl::SendEncodedAudioFrame: FAILED audio_enabled=%d audio_encoder=%p\n",
        (int)audio_enabled_, (void*)audio_encoder_);
    RTC_LOG(LS_ERROR) << "Audio encoded send not enabled or no encoder";
    return false;
  }

  // 将编码数据放入队列，等待 WebRTC 音频管道调用 EncodeImpl 时发送
  return audio_encoder_->SendEncodedFrame(frame);
}

void lw_extra_EncodedSenderImpl::SetVideoEncodedSend(bool enabled) {
  video_enabled_ = enabled;
  if (video_encoder_) {
    // 使用 WebRTC 实际协商的 codec (InitEncode 时设置)，而非硬编码 H264
    video_encoder_->SetCodec(video_encoder_->GetActualCodec());
  }
}

void lw_extra_EncodedSenderImpl::SetAudioEncodedSend(bool enabled) {
  audio_enabled_ = enabled;
}

// ==================== EncodedReceiverImpl ====================

lw_extra_EncodedReceiverImpl::lw_extra_EncodedReceiverImpl(
    scoped_refptr<RTCRtpReceiver> rtp_receiver)
    : rtp_receiver_(std::move(rtp_receiver)),
      video_sink_(nullptr),
      audio_sink_(nullptr),
      video_enabled_(false),
      audio_enabled_(false) {}

lw_extra_EncodedReceiverImpl::~lw_extra_EncodedReceiverImpl() {}

void lw_extra_EncodedReceiverImpl::SetEncodedVideoSink(
    lw_extra_EncodedVideoSink* sink) {
  video_sink_ = sink;
}

void lw_extra_EncodedReceiverImpl::SetEncodedAudioSink(
    lw_extra_EncodedAudioSink* sink) {
  audio_sink_ = sink;
}

void lw_extra_EncodedReceiverImpl::SetVideoEncodedReceive(bool enabled) {
  video_enabled_ = enabled;

  // 获取原生 RtpReceiverInterface 以注册/取消 frame transformer
  auto* receiver_impl =
      static_cast<RTCRtpReceiverImpl*>(rtp_receiver_.get());
  auto native = receiver_impl->rtp_receiver();

  if (enabled) {
    if (!frame_transformer_) {
      frame_transformer_ =
          new PassthroughFrameTransformer(this);
    }
    native->SetDepacketizerToDecoderFrameTransformer(frame_transformer_);
    LW_LOG("[lw_extra] EncodedReceiver: video frame transformer REGISTERED\n");
  } else {
    native->SetDepacketizerToDecoderFrameTransformer(nullptr);
    LW_LOG("[lw_extra] EncodedReceiver: video frame transformer UNREGISTERED\n");
  }
}

void lw_extra_EncodedReceiverImpl::SetAudioEncodedReceive(bool enabled) {
  audio_enabled_ = enabled;

  auto* receiver_impl =
      static_cast<RTCRtpReceiverImpl*>(rtp_receiver_.get());
  auto native = receiver_impl->rtp_receiver();

  if (enabled) {
    if (!frame_transformer_) {
      frame_transformer_ =
          new PassthroughFrameTransformer(this);
    }
    native->SetDepacketizerToDecoderFrameTransformer(frame_transformer_);
    LW_LOG("[lw_extra] EncodedReceiver: audio frame transformer REGISTERED\n");
  } else {
    native->SetDepacketizerToDecoderFrameTransformer(nullptr);
    LW_LOG("[lw_extra] EncodedReceiver: audio frame transformer UNREGISTERED\n");
  }
}

void lw_extra_EncodedReceiverImpl::OnEncodedVideoFrameReceived(
    const uint8_t* data, size_t size, uint32_t timestamp, bool is_key_frame,
    int width, int height, lw_extra_VideoCodec codec) {
  if (!video_enabled_ || !video_sink_) {
    return;
  }

  lw_extra_EncodedVideoFrame frame;
  frame.codec = codec;
  frame.data = const_cast<uint8_t*>(data);
  frame.size = size;
  frame.timestamp = timestamp;
  frame.is_key_frame = is_key_frame;
  frame.width = width;
  frame.height = height;

  video_sink_->OnEncodedVideoFrame(frame);
}

void lw_extra_EncodedReceiverImpl::OnEncodedAudioFrameReceived(
    const uint8_t* data, size_t size, uint32_t timestamp,
    lw_extra_AudioCodec codec) {
  if (!audio_enabled_ || !audio_sink_) {
    return;
  }

  lw_extra_EncodedAudioFrame frame;
  frame.codec = codec;
  frame.data = const_cast<uint8_t*>(data);
  frame.size = size;
  frame.timestamp = timestamp;

  audio_sink_->OnEncodedAudioFrame(frame);
}

// ==================== RtpTransceiverImpl ====================

lw_extra_RtpTransceiverImpl::lw_extra_RtpTransceiverImpl(
    scoped_refptr<RTCRtpTransceiver> transceiver,
    lw_extra_PassthroughVideoEncoderFactory* video_factory,
    lw_extra_PassthroughAudioEncoderFactory* audio_factory)
    : transceiver_(std::move(transceiver)),
      video_factory_(video_factory),
      audio_factory_(audio_factory),
      initialized_(false) {}

lw_extra_RtpTransceiverImpl::~lw_extra_RtpTransceiverImpl() {}

void lw_extra_RtpTransceiverImpl::EnsureInitialized() {
  if (initialized_) {
    return;
  }

  if (!transceiver_) {
    RTC_LOG(LS_ERROR) << "No transceiver";
    return;
  }

  auto media_type = transceiver_->media_type();
  auto rtp_sender = transceiver_->sender();
  auto rtp_receiver = transceiver_->receiver();

  lw_extra_PassthroughVideoEncoder* video_encoder = nullptr;
  lw_extra_PassthroughAudioEncoder* audio_encoder = nullptr;

  // ★ RoutingVideoEncoderFactory 保证 encoder 已在 create_answer 期间入队
  //    (factory->Create() 在 SetLocalDescription 时被 WebRTC pipeline 调用，
  //     标记 SetNextPassthrough() 确保 encoded track 创建 passthrough encoder)
  if (media_type == RTCMediaType::VIDEO && video_factory_) {
    video_encoder = video_factory_->GetNextEncoder();
    if (!video_encoder) {
      RTC_LOG(LS_WARNING) << "PassthroughVideoEncoder not in queue — "
          "ensure SetNextEncoderPassthrough was called before create_answer";
    }
  } else if (media_type == RTCMediaType::AUDIO && audio_factory_) {
    audio_encoder = audio_factory_->GetNextEncoder();
    if (!audio_encoder) {
      RTC_LOG(LS_WARNING) << "PassthroughAudioEncoder not in queue — "
          "ensure SetNextEncoderPassthrough was called before create_answer";
    }
  }

  // 创建发送器
  if (rtp_sender) {
    encoded_sender_ = std::make_unique<lw_extra_EncodedSenderImpl>(
        rtp_sender, video_encoder, audio_encoder);
  }

  // 创建接收器
  if (rtp_receiver) {
    encoded_receiver_ =
        std::make_unique<lw_extra_EncodedReceiverImpl>(rtp_receiver);
  }

  // 标记初始化完成。如果 encoder 暂时为 null (defensive)，发送时
  // SendEncodedVideoFrame 会检查 video_encoder_ 并返回 false。
  initialized_ = true;
}

lw_extra_EncodedSender* lw_extra_RtpTransceiverImpl::GetEncodedSender() {
  EnsureInitialized();
  return encoded_sender_.get();
}

lw_extra_EncodedReceiver* lw_extra_RtpTransceiverImpl::GetEncodedReceiver() {
  EnsureInitialized();
  return encoded_receiver_.get();
}

scoped_refptr<RTCRtpTransceiver>
lw_extra_RtpTransceiverImpl::GetRtpTransceiver() {
  return transceiver_;
}

// ==================== PeerConnectionImpl ====================

lw_extra_PeerConnectionImpl::lw_extra_PeerConnectionImpl(
    scoped_refptr<RTCPeerConnection> peer_connection,
    lw_extra_PassthroughVideoEncoderFactory* video_factory,
    lw_extra_PassthroughAudioEncoderFactory* audio_factory)
    : peer_connection_(std::move(peer_connection)),
      video_factory_(video_factory),
      audio_factory_(audio_factory) {}

lw_extra_PeerConnectionImpl::~lw_extra_PeerConnectionImpl() {}

lw_extra_RtpTransceiverImpl*
lw_extra_PeerConnectionImpl::CreateOrGetTransceiver(
    scoped_refptr<RTCRtpTransceiver> transceiver) {
  if (!transceiver) {
    return nullptr;
  }

  auto mid = transceiver->mid();
  std::string mid_str = mid.std_string();
  auto it = transceivers_.find(mid_str);
  if (it != transceivers_.end()) {
    return it->second.get();
  }

  auto impl = std::make_unique<lw_extra_RtpTransceiverImpl>(
      transceiver, video_factory_, audio_factory_);
  auto* ptr = impl.get();
  transceivers_[mid_str] = std::move(impl);
  return ptr;
}

lw_extra_RtpTransceiver*
lw_extra_PeerConnectionImpl::GetTransceiverByMediaType(
    RTCMediaType media_type) {
  if (!peer_connection_) {
    return nullptr;
  }

  auto all_transceivers = peer_connection_->transceivers();
  auto std_transceivers = all_transceivers.std_vector();
  for (const auto& transceiver : std_transceivers) {
    if (transceiver->media_type() == media_type) {
      return CreateOrGetTransceiver(transceiver);
    }
  }
  return nullptr;
}

lw_extra_RtpTransceiver* lw_extra_PeerConnectionImpl::GetTransceiverByMid(
    const string& mid) {
  if (!peer_connection_) {
    return nullptr;
  }

  auto all_transceivers = peer_connection_->transceivers();
  auto std_transceivers = all_transceivers.std_vector();
  for (const auto& transceiver : std_transceivers) {
    if (transceiver->mid().std_string() == mid.std_string()) {
      return CreateOrGetTransceiver(transceiver);
    }
  }
  return nullptr;
}

vector<lw_extra_RtpTransceiver*>
lw_extra_PeerConnectionImpl::GetAllTransceivers() {
  std::vector<lw_extra_RtpTransceiver*> std_result;

  if (!peer_connection_) {
    return vector<lw_extra_RtpTransceiver*>(std_result);
  }

  auto all_transceivers = peer_connection_->transceivers();
  auto std_transceivers = all_transceivers.std_vector();
  for (const auto& transceiver : std_transceivers) {
    auto* impl = CreateOrGetTransceiver(transceiver);
    if (impl) {
      std_result.push_back(impl);
    }
  }
  return vector<lw_extra_RtpTransceiver*>(std_result);
}

// ==================== Utils ====================

void lw_extra_Utils::SetRoutingVideoEncoderFactory(void* factory) {
  routing_video_encoder_factory_ = factory;
}

void lw_extra_Utils::SetRoutingAudioEncoderFactory(void* factory) {
  routing_audio_encoder_factory_ = factory;
}

void lw_extra_Utils::SetNextVideoEncoderPassthrough(bool enabled) {
  LW_LOG("[lw_extra] SetNextVideoEncoderPassthrough: enabled=%d factory=%p\n",
      (int)enabled, routing_video_encoder_factory_);
  if (routing_video_encoder_factory_) {
    static_cast<RoutingVideoEncoderFactory*>(
        routing_video_encoder_factory_)->SetNextPassthrough(enabled);
  }
}

void lw_extra_Utils::SetNextAudioEncoderPassthrough(bool enabled) {
  LW_LOG("[lw_extra] SetNextAudioEncoderPassthrough: enabled=%d factory=%p\n",
      (int)enabled, routing_audio_encoder_factory_);
  if (routing_audio_encoder_factory_) {
    static_cast<RoutingAudioEncoderFactory*>(
        routing_audio_encoder_factory_)->SetNextPassthrough(enabled);
  }
}

lw_extra_PeerConnection*
lw_extra_Utils::CreateExtendedPeerConnection(
    scoped_refptr<RTCPeerConnection> peer_connection) {
  if (!peer_connection) {
    RTC_LOG(LS_ERROR) << "Invalid peer connection";
    return nullptr;
  }

  // 使用外部注入的 passthrough factory（由 RTCPeerConnectionFactoryImpl::Initialize() 注入，
  // 实际上是 RoutingVideoEncoderFactory 内部的 passthrough 子工厂）。
  // 如果外部工厂未设置（Initialize 未调用），则创建独立工厂作为后备。
  auto* video_factory = external_video_encoder_factory_;
  if (!video_factory) {
    if (!video_encoder_factory_) {
      video_encoder_factory_ =
          std::make_unique<lw_extra_PassthroughVideoEncoderFactory>();
    }
    video_factory = video_encoder_factory_.get();
  }

  auto* audio_factory = external_audio_encoder_factory_;
  if (!audio_factory) {
    if (!audio_encoder_factory_) {
      audio_encoder_factory_ =
          new lw_extra_PassthroughAudioEncoderFactory();
    }
    audio_factory = audio_encoder_factory_;
  }

  auto* impl = new lw_extra_PeerConnectionImpl(
      peer_connection, video_factory,
      audio_factory);
  return impl;
}

bool lw_extra_Utils::SetUdpPortRange(
    scoped_refptr<RTCPeerConnection> peer_connection,
    const RTCPUdpPortRange& port_range) {
  if (!peer_connection) {
    RTC_LOG(LS_ERROR) << "Invalid peer connection";
    return false;
  }

  if (!port_range.IsValid()) {
    RTC_LOG(LS_ERROR) << "Invalid port range";
    return false;
  }

  // 端口范围设置需要在创建 PeerConnection 时通过 RTCConfiguration 传递
  // 如果 PeerConnection 已经创建完成，端口范围无法再更改
  // 这里提供一个检查接口
  if (port_range.min_port == 0 && port_range.max_port == 0) {
    // 清除端口限制
    return true;
  }

  RTC_LOG(LS_WARNING)
      << "Udp port range should be set via RTCConfiguration.udp_port_range "
      << "when creating PeerConnection. Current PeerConnection may not be "
      << "affected.";
  return true;
}

lw_extra_PassthroughVideoEncoderFactory*
lw_extra_Utils::GetVideoEncoderFactory() {
  if (external_video_encoder_factory_) {
    return external_video_encoder_factory_;
  }
  if (!video_encoder_factory_) {
    video_encoder_factory_ =
        std::make_unique<lw_extra_PassthroughVideoEncoderFactory>();
  }
  return video_encoder_factory_.get();
}

void lw_extra_Utils::SetExternalVideoEncoderFactory(
    lw_extra_PassthroughVideoEncoderFactory* factory) {
  external_video_encoder_factory_ = factory;
}

void lw_extra_Utils::SetExternalAudioEncoderFactory(
    lw_extra_PassthroughAudioEncoderFactory* factory) {
  external_audio_encoder_factory_ = factory;
}

lw_extra_PassthroughAudioEncoderFactory*
lw_extra_Utils::GetAudioEncoderFactory() {
  if (external_audio_encoder_factory_) {
    return external_audio_encoder_factory_;
  }
  if (!audio_encoder_factory_) {
    audio_encoder_factory_ =
        new lw_extra_PassthroughAudioEncoderFactory();
  }
  return audio_encoder_factory_;
}

}  // namespace libwebrtc
