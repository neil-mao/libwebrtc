#ifndef LIB_WEBRTC_RTC_LW_EXTRA_HXX
#define LIB_WEBRTC_RTC_LW_EXTRA_HXX

#include "base/refcount.h"
#include "rtc_rtp_sender.h"
#include "rtc_types.h"

namespace libwebrtc {

// ==================== 编码格式枚举 ====================

enum class lw_extra_VideoCodec {
  kH264 = 0,
  kAV1 = 1,
};

// ==================== 编码后数据帧结构 ====================

/**
 * @brief 编码后的视频帧
 *
 * 用于发送预编码的 H264/AV1 视频数据
 */
struct lw_extra_EncodedVideoFrame {
  lw_extra_VideoCodec codec;
  uint8_t* data;
  size_t size;
  uint32_t timestamp;       // 采样时间戳（90kHz for video）
  bool is_key_frame;
  int width;
  int height;
};

// ==================== 编码数据发送接口 ====================

/**
 * @brief 编码数据发送接口
 *
 * Scheme D: 直接从 RtpSender 拿到 RtpVideoSender，构建 EncodedImage
 * 并调用 OnEncodedImage()，完全绕过编码器框架。
 *
 * 使用方式:
 *   1. AddTrack / create_answer → WebRTC 建立 VideoSendStream
 *   2. sender = lw_extra_EncodedSender::Create(rtp_sender)
 *   3. sender->SetVideoEncodedSend(true)
 *   4. sender->SendEncodedVideoFrame(frame)
 */
class LIB_WEBRTC_API lw_extra_EncodedSender {
 public:
  virtual ~lw_extra_EncodedSender() {}

  /**
   * @brief 发送编码后的视频帧
   *
   * 直接在内部构建 EncodedImage + CodecSpecificInfo，
   * 通过 RtpVideoSender::OnEncodedImage() 发送，跳过编码器管线。
   *
   * @param frame 编码后的视频帧数据
   * @return true 发送成功，false 发送失败
   */
  virtual bool SendEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) = 0;

  /**
   * @brief 设置发送编码视频数据
   *
   * @param enabled true 启用编码数据发送
   */
  virtual void SetVideoEncodedSend(bool enabled) = 0;

  /**
   * @brief 创建 EncodedSender 实例
   *
   * 导航路径: RTCRtpSender → RtpSenderInternal → media_channel
   *           → VideoMediaSendChannelInterface → RtpVideoSenderInterface
   *
   * @param rtp_sender 对应的 RTP sender
   * @return EncodedSender 实例，失败返回 nullptr
   */
  static lw_extra_EncodedSender* Create(
      scoped_refptr<RTCRtpSender> rtp_sender);
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_LW_EXTRA_HXX
