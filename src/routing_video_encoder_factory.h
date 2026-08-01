#ifndef LIB_WEBRTC_ROUTING_VIDEO_ENCODER_FACTORY_HXX
#define LIB_WEBRTC_ROUTING_VIDEO_ENCODER_FACTORY_HXX

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

// ── 调试日志开关 ────────────────────────────────────────────
// 设置环境变量 LIBWEBRTC_LW_LOG=1 开启 [routing] 日志，默认关闭
inline bool lw_log_enabled() {
    static int enabled = -1;
    if (enabled < 0) {
        const char* v = getenv("LIBWEBRTC_LW_LOG");
        enabled = (v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y')) ? 1 : 0;
    }
    return enabled == 1;
}
#define LW_LOG(fmt, ...) do { \
    if (lw_log_enabled()) fprintf(stderr, fmt, ##__VA_ARGS__); \
} while(0)

#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/environment/environment.h"

namespace libwebrtc {

class lw_extra_PassthroughVideoEncoderFactory;
class lw_extra_PassthroughAudioEncoderFactory;

/**
 * @brief 路由视频编码器工厂
 *
 * 同时持有 builtin 和 passthrough 两个子工厂。
 * 根据 next_is_passthrough_ 原子标记决定 Create() 返回哪种 encoder。
 * 标记通过 exchange(false) 单次消费，自动复位。
 *
 * 使用方式:
 *   factory->SetNextPassthrough(true);   // 在 AddTrack 前打标记
 *   pc->AddTrack(encoded_track, ...);    // WebRTC 内部调 Create() → passthrough encoder
 *   // 后续 RGBA track 不受影响（标记已自动复位）
 */
class RoutingVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  RoutingVideoEncoderFactory(
      std::unique_ptr<webrtc::VideoEncoderFactory> builtin,
      std::unique_ptr<lw_extra_PassthroughVideoEncoderFactory> passthrough)
      : builtin_(std::move(builtin)),
        passthrough_(std::move(passthrough)) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    // passthrough 格式排在前面，使 H264 成为首选 codec。
    // 这样 VideoStreamEncoder 创建 encoder 时直接传 format="H264"，
    // VideoSendStream 内部 RTP packetizer 按 H264 配置，正确打包 H264 NAL。
    auto formats = passthrough_->GetSupportedFormats();  // H264, AV1 排前面
    auto b = builtin_->GetSupportedFormats();
    formats.insert(formats.end(), b.begin(), b.end());
    return formats;
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override {
    // exchange: 读取当前值并复位为 false —— 每次标记只影响一个 track
    bool use_passthrough = next_is_passthrough_.exchange(false);
    LW_LOG("[routing] Video Create: passthrough=%d format=%s\n",
        (int)use_passthrough, format.name.c_str());
    if (use_passthrough) {
      return passthrough_->Create(env, format);
    }
    return builtin_->Create(env, format);
  }

  /// 设置下一个 Create() 返回 passthrough encoder（单次生效）
  void SetNextPassthrough(bool v) {
    LW_LOG("[routing] Video SetNextPassthrough: %d\n", (int)v);
    next_is_passthrough_.store(v);
  }

  lw_extra_PassthroughVideoEncoderFactory* passthrough_factory() {
    return passthrough_.get();
  }

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> builtin_;
  std::unique_ptr<lw_extra_PassthroughVideoEncoderFactory> passthrough_;
  std::atomic<bool> next_is_passthrough_{false};
};

/**
 * @brief 路由音频编码器工厂
 *
 * 与 RoutingVideoEncoderFactory 同样的标记驱动模式。
 * QueryAudioEncoder 不消费标记（返回两个工厂的并集），
 * 只有 Create() 消费标记。
 */
class RoutingAudioEncoderFactory : public webrtc::AudioEncoderFactory {
 public:
  RoutingAudioEncoderFactory(
      webrtc::scoped_refptr<webrtc::AudioEncoderFactory> builtin,
      lw_extra_PassthroughAudioEncoderFactory* passthrough)
      : builtin_(builtin), passthrough_(passthrough) {}

  ~RoutingAudioEncoderFactory() override = default;

  // RefCountInterface
  void AddRef() const override { ref_count_++; }
  webrtc::RefCountReleaseStatus Release() const override {
    if (--ref_count_ == 0) {
      delete this;
      return webrtc::RefCountReleaseStatus::kDroppedLastRef;
    }
    return webrtc::RefCountReleaseStatus::kOtherRefsRemained;
  }

  std::vector<webrtc::AudioCodecSpec> GetSupportedEncoders() override {
    // passthrough 格式排在前面，使 Opus 成为首选 codec
    auto specs = passthrough_->GetSupportedEncoders();
    auto b = builtin_->GetSupportedEncoders();
    specs.insert(specs.end(), b.begin(), b.end());
    return specs;
  }

  // QueryAudioEncoder 不消费标记 — 返回两个工厂的并集
  // （SDP 协商阶段调用，必须在编码器创建之前看到所有支持的格式）
  std::optional<webrtc::AudioCodecInfo> QueryAudioEncoder(
      const webrtc::SdpAudioFormat& format) override {
    auto result = builtin_->QueryAudioEncoder(format);
    if (result) return result;
    return passthrough_->QueryAudioEncoder(format);
  }

  // Create() 消费标记 — 单次生效
  std::unique_ptr<webrtc::AudioEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpAudioFormat& format,
      webrtc::AudioEncoderFactory::Options options) override {
    bool use_passthrough = next_is_passthrough_.exchange(false);
    LW_LOG("[routing] Audio Create: passthrough=%d format=%s pt=%d\n",
        (int)use_passthrough, format.name.c_str(), options.payload_type);
    if (use_passthrough) {
      return passthrough_->Create(env, format, options);
    }
    return builtin_->Create(env, format, options);
  }

  /// 设置下一个 Create() 返回 passthrough encoder（单次生效）
  void SetNextPassthrough(bool v) {
    LW_LOG("[routing] Audio SetNextPassthrough: %d\n", (int)v);
    next_is_passthrough_.store(v);
  }

 private:
  webrtc::scoped_refptr<webrtc::AudioEncoderFactory> builtin_;
  lw_extra_PassthroughAudioEncoderFactory* passthrough_;
  mutable std::atomic<int> ref_count_{0};
  std::atomic<bool> next_is_passthrough_{false};
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_ROUTING_VIDEO_ENCODER_FACTORY_HXX
