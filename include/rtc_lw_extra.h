#ifndef LIB_WEBRTC_RTC_LW_EXTRA_HXX
#define LIB_WEBRTC_RTC_LW_EXTRA_HXX

#include "base/refcount.h"
#include "rtc_rtp_sender.h"
#include "rtc_rtp_receiver.h"
#include "rtc_types.h"

namespace libwebrtc {

// ==================== 编码格式枚举 ====================

enum class lw_extra_VideoCodec {
  kH264 = 0,
  kAV1 = 1,
};

enum class lw_extra_AudioCodec {
  kOpus = 0,
};

// ==================== 编码后数据帧结构 ====================

struct lw_extra_EncodedVideoFrame {
  lw_extra_VideoCodec codec;
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
  bool is_key_frame;
  int width;
  int height;
};

struct lw_extra_EncodedAudioFrame {
  lw_extra_AudioCodec codec;
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
};

// ==================== 编码数据接收回调 ====================

class LIB_WEBRTC_API lw_extra_EncodedVideoSink {
 public:
  virtual ~lw_extra_EncodedVideoSink() {}
  virtual void OnEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) = 0;
};

class LIB_WEBRTC_API lw_extra_EncodedAudioSink {
 public:
  virtual ~lw_extra_EncodedAudioSink() {}
  virtual void OnEncodedAudioFrame(const lw_extra_EncodedAudioFrame& frame) = 0;
};

// ==================== 编码数据发送接口 ====================

/**
 * @brief 编码数据发送接口 (Scheme D)
 *
 * 直接从 RTCRtpSender 导航到 RtpVideoSender，构建 EncodedImage
 * 并调用 OnEncodedImage()，完全绕过编码器框架。
 */
class LIB_WEBRTC_API lw_extra_EncodedSender {
 public:
  virtual ~lw_extra_EncodedSender() {}

  virtual bool SendEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) = 0;
  virtual bool SendEncodedAudioFrame(const lw_extra_EncodedAudioFrame& frame) = 0;
  virtual void SetVideoEncodedSend(bool enabled) = 0;
  virtual void SetAudioEncodedSend(bool enabled) = 0;

  static lw_extra_EncodedSender* Create(
      scoped_refptr<RTCRtpSender> rtp_sender);
};

// ==================== 编码数据接收接口 ====================

/**
 * @brief 编码数据接收接口
 *
 * TODO: Scheme D receiver — port PassthroughFrameTransformer from jteam.
 * Currently returns nullptr for Create().
 */
class LIB_WEBRTC_API lw_extra_EncodedReceiver {
 public:
  virtual ~lw_extra_EncodedReceiver() {}

  virtual void SetEncodedVideoSink(lw_extra_EncodedVideoSink* sink) = 0;
  virtual void SetEncodedAudioSink(lw_extra_EncodedAudioSink* sink) = 0;
  virtual void SetVideoEncodedReceive(bool enabled) = 0;
  virtual void SetAudioEncodedReceive(bool enabled) = 0;

  static lw_extra_EncodedReceiver* Create(
      scoped_refptr<RTCRtpReceiver> rtp_receiver);
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_LW_EXTRA_HXX
