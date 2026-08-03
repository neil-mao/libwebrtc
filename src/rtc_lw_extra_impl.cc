#include "rtc_lw_extra_impl.h"

#include <cstdio>
#include <cstring>

#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "rtc_rtp_sender_impl.h"

// ── 导航所需内部头文件 ──────────────────────────────────
// 从 RtpSenderBase 的 GetRtpVideoSender() 获取 RtpVideoSenderInterface*
// 然后直接调用 OnEncodedImage() 发送编码帧。
#include "call/rtp_video_sender_interface.h"
#include "pc/rtp_sender.h"

namespace libwebrtc {

// ==================== 静态工厂方法 ====================

lw_extra_EncodedSender* lw_extra_EncodedSender::Create(
    scoped_refptr<RTCRtpSender> rtp_sender) {
  if (!rtp_sender) {
    RTC_LOG(LS_ERROR) << "EncodedSender::Create: null rtp_sender";
    return nullptr;
  }
  return new lw_extra_EncodedSenderImpl(std::move(rtp_sender));
}

// ==================== EncodedSenderImpl ====================

lw_extra_EncodedSenderImpl::lw_extra_EncodedSenderImpl(
    scoped_refptr<RTCRtpSender> rtp_sender)
    : rtp_sender_(std::move(rtp_sender)),
      video_enabled_(false),
      initialized_(false),
      ssrc_(0),
      rtp_video_sender_(nullptr) {
  ssrc_ = rtp_sender_->ssrc();
  LW_LOG("EncodedSenderImpl: created ssrc=%u\n", ssrc_);
}

lw_extra_EncodedSenderImpl::~lw_extra_EncodedSenderImpl() {}

void* lw_extra_EncodedSenderImpl::GetRtpVideoSender() {
  if (rtp_video_sender_) {
    return rtp_video_sender_;
  }

  if (ssrc_ == 0) {
    LW_LOG("EncodedSenderImpl::GetRtpVideoSender: ssrc=0, not yet negotiated\n");
    return nullptr;
  }

  // ── 导航：RTCRtpSender → RtpSenderBase → GetRtpVideoSender() ──
  // RTCRtpSenderImpl 持有 webrtc::RtpSenderInterface（即 RtpSenderBase）
  // RtpSenderBase::GetRtpVideoSender() 是我们新增的方法，
  // 内部通过 media_channel_ → VideoMediaSendChannelInterface → RtpVideoSenderInterface

  auto* sender_impl = static_cast<RTCRtpSenderImpl*>(rtp_sender_.get());
  auto rtc_sender = sender_impl->rtc_rtp_sender();
  if (!rtc_sender) {
    LW_LOG("EncodedSenderImpl::GetRtpVideoSender: no native rtp_sender\n");
    return nullptr;
  }

  // RtpSenderInterface → RtpSenderBase (has GetRtpVideoSender)
  auto* sender_base = static_cast<webrtc::RtpSenderBase*>(rtc_sender.get());

  auto* video_sender = sender_base->GetRtpVideoSender();
  if (!video_sender) {
    LW_LOG("EncodedSenderImpl::GetRtpVideoSender: GetRtpVideoSender returned null "
           "(media_channel not set or not video)\n");
    return nullptr;
  }

  rtp_video_sender_ = static_cast<void*>(video_sender);
  LW_LOG("EncodedSenderImpl::GetRtpVideoSender: SUCCESS sender=%p ssrc=%u\n",
         rtp_video_sender_, ssrc_);
  return rtp_video_sender_;
}

void lw_extra_EncodedSenderImpl::SetVideoEncodedSend(bool enabled) {
  video_enabled_ = enabled;

  if (enabled && !initialized_) {
    // 延迟获取 RtpVideoSender — 在 AddTrack + create_answer 之后才可用
    void* sender = GetRtpVideoSender();
    if (sender) {
      initialized_ = true;
      LW_LOG("EncodedSenderImpl::SetVideoEncodedSend: initialized ssrc=%u\n", ssrc_);
    } else {
      LW_LOG("EncodedSenderImpl::SetVideoEncodedSend: RtpVideoSender not yet available, "
             "will retry on first SendEncodedVideoFrame\n");
    }
  }
}

bool lw_extra_EncodedSenderImpl::SendEncodedVideoFrame(
    const lw_extra_EncodedVideoFrame& frame) {
  if (!video_enabled_) {
    LW_LOG("EncodedSenderImpl::SendEncodedVideoFrame: not enabled\n");
    return false;
  }

  if (!frame.data || frame.size == 0) {
    LW_LOG("EncodedSenderImpl::SendEncodedVideoFrame: invalid frame data\n");
    return false;
  }

  // 延迟初始化 — 如果 SetVideoEncodedSend 时尚未拿到 RtpVideoSender
  void* sender = rtp_video_sender_;
  if (!sender) {
    sender = GetRtpVideoSender();
    if (!sender) {
      LW_LOG("EncodedSenderImpl::SendEncodedVideoFrame: RtpVideoSender not available\n");
      return false;
    }
    initialized_ = true;
  }

  auto* video_sender = static_cast<webrtc::RtpVideoSenderInterface*>(sender);

  // ── 构建 EncodedImage ──────────────────────────────────
  webrtc::EncodedImage encoded_image;
  auto buffer = webrtc::EncodedImageBuffer::Create(frame.data, frame.size);
  encoded_image.SetEncodedData(buffer);
  encoded_image.set_size(frame.size);
  encoded_image.SetRtpTimestamp(frame.timestamp);
  encoded_image._encodedWidth = frame.width;
  encoded_image._encodedHeight = frame.height;
  encoded_image.SetFrameType(
      frame.is_key_frame ? webrtc::VideoFrameType::kVideoFrameKey
                         : webrtc::VideoFrameType::kVideoFrameDelta);

  // ── 构建 CodecSpecificInfo ─────────────────────────────
  webrtc::CodecSpecificInfo codec_specific_info;
  codec_specific_info.codecType =
      (frame.codec == lw_extra_VideoCodec::kH264) ? webrtc::kVideoCodecH264
                                                   : webrtc::kVideoCodecAV1;

  if (frame.codec == lw_extra_VideoCodec::kH264) {
    codec_specific_info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;  // FU-A 分片，支持大 NAL
    codec_specific_info.codecSpecific.H264.temporal_idx =
        webrtc::kNoTemporalIdx;
    codec_specific_info.codecSpecific.H264.idr_frame = frame.is_key_frame;
    codec_specific_info.codecSpecific.H264.base_layer_sync = false;
  }

  // ── 直发！不经过 VSE、不经过 SPS VUI 重写 ────────────
  webrtc::EncodedImageCallback::Result result =
      video_sender->OnEncodedImage(encoded_image, &codec_specific_info);

  if (result.error != webrtc::EncodedImageCallback::Result::OK) {
    LW_LOG("EncodedSenderImpl::SendEncodedVideoFrame: FAILED error=%d\n",
           static_cast<int>(result.error));
    return false;
  }

  static int send_count = 0;
  send_count++;
  if (send_count <= 5 || send_count % 150 == 0) {
    LW_LOG("EncodedSenderImpl::SendEncodedVideoFrame: OK #%d size=%zu "
           "key=%d codec=%d ts=%u\n",
           send_count, frame.size,
           static_cast<int>(frame.is_key_frame),
           static_cast<int>(frame.codec), frame.timestamp);
  }

  return true;
}

bool lw_extra_EncodedSenderImpl::SendEncodedAudioFrame(
    const lw_extra_EncodedAudioFrame& frame) {
  (void)frame;
  LW_LOG("EncodedSenderImpl::SendEncodedAudioFrame: TODO — audio not yet implemented\n");
  return false;
}

void lw_extra_EncodedSenderImpl::SetAudioEncodedSend(bool enabled) {
  (void)enabled;
  LW_LOG("EncodedSenderImpl::SetAudioEncodedSend: TODO — audio not yet implemented\n");
}

// ==================== EncodedReceiver stub ====================

lw_extra_EncodedReceiver* lw_extra_EncodedReceiver::Create(
    scoped_refptr<RTCRtpReceiver> rtp_receiver) {
  (void)rtp_receiver;
  LW_LOG("EncodedReceiver::Create: TODO — receiver not yet ported to scheme D\n");
  return nullptr;
}

}  // namespace libwebrtc
