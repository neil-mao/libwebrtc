#ifndef LIB_WEBRTC_RTC_LW_EXTRA_IMPL_HXX
#define LIB_WEBRTC_RTC_LW_EXTRA_IMPL_HXX

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_codec.h"
#include "rtc_lw_extra.h"
#include "rtc_rtp_sender.h"

namespace libwebrtc {

// ── 调试日志 ────────────────────────────────────────────
// 设置 LIBWEBRTC_LW_LOG=1 开启日志

inline bool lw_log_enabled() {
    static int enabled = -1;
    if (enabled < 0) {
        const char* v = getenv("LIBWEBRTC_LW_LOG");
        enabled = (v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y')) ? 1 : 0;
    }
    return enabled == 1;
}

#define LW_LOG(fmt, ...) do { \
    if (lw_log_enabled()) fprintf(stderr, "[lw_extra] " fmt, ##__VA_ARGS__); \
} while(0)

// ==================== 编码数据发送器实现 ====================

/**
 * @brief 编码数据发送器实现 (Scheme D)
 *
 * 导航路径:
 *   RTCRtpSender → rtc_rtp_sender() → webrtc::RtpSenderInterface
 *     → RtpSenderBase::GetRtpVideoSender() → RtpVideoSenderInterface
 *       → OnEncodedImage(encoded_image, codec_specific_info)
 *
 * 不创建 fake encoder、不走 factory、不经过 VSE。
 */
class lw_extra_EncodedSenderImpl : public lw_extra_EncodedSender {
 public:
  explicit lw_extra_EncodedSenderImpl(
      scoped_refptr<RTCRtpSender> rtp_sender);
  ~lw_extra_EncodedSenderImpl() override;

  bool SendEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) override;
  bool SendEncodedAudioFrame(const lw_extra_EncodedAudioFrame& frame) override;
  void SetVideoEncodedSend(bool enabled) override;
  void SetAudioEncodedSend(bool enabled) override;

  void* GetRtpVideoSender();

 private:
  scoped_refptr<RTCRtpSender> rtp_sender_;
  bool video_enabled_ = false;
  bool initialized_ = false;
  uint32_t ssrc_ = 0;
  lw_extra_VideoCodec codec_ = lw_extra_VideoCodec::kH264;

  // RtpVideoSenderInterface* - 延迟获取，由 SetVideoEncodedSend 触发
  // 不直接 include RtpVideoSenderInterface 头文件，用 void* 避免依赖
  void* rtp_video_sender_ = nullptr;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_LW_EXTRA_IMPL_HXX
