#ifndef LIB_WEBRTC_RTC_LW_EXTRA_IMPL_HXX
#define LIB_WEBRTC_RTC_LW_EXTRA_IMPL_HXX

#include <map>
#include <memory>
#include <vector>

#include "api/environment/environment.h"
#include "api/rtp_parameters.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_lw_extra.h"
#include "rtc_rtp_transceiver.h"
#include "rtc_rtp_sender.h"
#include "rtc_rtp_receiver.h"
#include "rtc_peerconnection.h"
#include "rtc_video_track.h"
#include "rtc_audio_track.h"
#include "rtc_video_source.h"
#include "rtc_audio_source.h"
#include "rtc_types.h"

namespace libwebrtc {

// ==================== 自定义视频编码器 ====================

/**
 * @brief 自定义传递视频编码器
 *
 * 该编码器不进行实际的编码操作，而是直接将预编码的 H264/AV1 数据
 * 通过 EncodedImageCallback 传递给 RTP 层。
 */
class lw_extra_PassthroughVideoEncoder : public webrtc::VideoEncoder {
 public:
  lw_extra_PassthroughVideoEncoder();
  ~lw_extra_PassthroughVideoEncoder() override;

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings,
                     int32_t number_of_cores,
                     size_t max_payload_size) override;

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings,
                     const webrtc::VideoEncoder::Settings& settings) override;

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;

  int32_t Release() override;

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override;

  void SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) override;

  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

  /**
   * @brief 直接发送编码后的视频帧
   *
   * @param frame 编码后的视频帧数据
   * @return true 发送成功
   */
  bool SendEncodedFrame(const lw_extra_EncodedVideoFrame& frame);

  // 设置编码模式（H264 或 AV1）
  void SetCodec(lw_extra_VideoCodec codec);

 private:
  webrtc::EncodedImageCallback* callback_ = nullptr;
  webrtc::VideoCodec codec_settings_;
  lw_extra_VideoCodec codec_ = lw_extra_VideoCodec::kH264;
  bool initialized_ = false;
  uint32_t frame_id_ = 0;
};

// ==================== 自定义音频编码器 ====================

/**
 * @brief 自定义传递音频编码器
 *
 * 该编码器不进行实际的编码操作，而是直接将预编码的 Opus 数据
 * 通过 AudioPacketizationCallback 传递给 RTP 层。
 */
class lw_extra_PassthroughAudioEncoder : public webrtc::AudioEncoder {
 public:
  lw_extra_PassthroughAudioEncoder(int payload_type);
  ~lw_extra_PassthroughAudioEncoder() override;

  int SampleRateHz() const override;
  size_t NumChannels() const override;
  int RtpTimestampRateHz() const override;
  size_t Num10MsFramesInNextPacket() const override;
  size_t Max10MsFramesInAPacket() const override;
  int GetTargetBitrate() const override;

  void Reset() override;

  std::optional<std::pair<webrtc::TimeDelta, webrtc::TimeDelta>>
  GetFrameLengthRange() const override;

  /**
   * @brief 直接发送编码后的音频帧
   *
   * 将预编码的音频数据放入队列，等待 EncodeImpl 被 WebRTC 音频管道调用时发送
   *
   * @param frame 编码后的音频帧数据
   * @return true 数据已入队，false 入队失败
   */
  bool SendEncodedFrame(const lw_extra_EncodedAudioFrame& frame);

 protected:
  EncodedInfo EncodeImpl(uint32_t rtp_timestamp,
                         webrtc::ArrayView<const int16_t> audio,
                         webrtc::Buffer* encoded) override;

 private:
  int payload_type_;
  int sample_rate_hz_ = 48000;
  size_t num_channels_ = 2;
  bool initialized_ = false;

  // 编码音频帧队列，由 SendEncodedFrame 写入，EncodeImpl 消费
  struct PendingAudioFrame {
    std::vector<uint8_t> data;
    uint32_t timestamp;
  };
  std::vector<PendingAudioFrame> pending_frames_;
  webrtc::Mutex mutex_;
};

// ==================== 视频编码器工厂 ====================

/**
 * @brief 自定义视频编码器工厂
 *
 * 创建传递视频编码器实例
 */
class lw_extra_PassthroughVideoEncoderFactory
    : public webrtc::VideoEncoderFactory {
 public:
  lw_extra_PassthroughVideoEncoderFactory();
  ~lw_extra_PassthroughVideoEncoderFactory() override;

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

  /**
   * @brief 获取最后创建的编码器
   *
   * @return 编码器指针
   */
  lw_extra_PassthroughVideoEncoder* GetLastEncoder();

 private:
  lw_extra_PassthroughVideoEncoder* last_encoder_ = nullptr;
};

// ==================== 音频编码器工厂 ====================

/**
 * @brief 自定义音频编码器工厂
 *
 * 创建传递音频编码器实例
 */
class lw_extra_PassthroughAudioEncoderFactory
    : public webrtc::AudioEncoderFactory {
 public:
  lw_extra_PassthroughAudioEncoderFactory();
  ~lw_extra_PassthroughAudioEncoderFactory() override;

  void AddRef() const override { ref_count_++; }
  webrtc::RefCountReleaseStatus Release() const override {
    if (--ref_count_ == 0) delete this;
    return webrtc::RefCountReleaseStatus::kDroppedLastRef;
  }

  std::vector<webrtc::AudioCodecSpec> GetSupportedEncoders() override;

  std::optional<webrtc::AudioCodecInfo> QueryAudioEncoder(
      const webrtc::SdpAudioFormat& format) override;

  std::unique_ptr<webrtc::AudioEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpAudioFormat& format,
      webrtc::AudioEncoderFactory::Options options) override;

  /**
   * @brief 获取最后创建的编码器
   *
   * @return 编码器指针
   */
  lw_extra_PassthroughAudioEncoder* GetLastEncoder();

 private:
  lw_extra_PassthroughAudioEncoder* last_encoder_ = nullptr;
  mutable int ref_count_ = 0;
};

// ==================== 编码数据发送器实现 ====================

/**
 * @brief 编码数据发送器实现类
 */
class lw_extra_EncodedSenderImpl : public lw_extra_EncodedSender {
 public:
  lw_extra_EncodedSenderImpl(
      scoped_refptr<RTCRtpSender> rtp_sender,
      lw_extra_PassthroughVideoEncoder* video_encoder,
      lw_extra_PassthroughAudioEncoder* audio_encoder);
  ~lw_extra_EncodedSenderImpl() override;

  bool SendEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) override;

  bool SendEncodedAudioFrame(const lw_extra_EncodedAudioFrame& frame) override;

  void SetVideoEncodedSend(bool enabled) override;

  void SetAudioEncodedSend(bool enabled) override;

 private:
  scoped_refptr<RTCRtpSender> rtp_sender_;
  lw_extra_PassthroughVideoEncoder* video_encoder_;
  lw_extra_PassthroughAudioEncoder* audio_encoder_;
  bool video_enabled_ = false;
  bool audio_enabled_ = false;
};

// ==================== 编码数据接收器实现 ====================

/**
 * @brief 编码数据接收器实现类
 */
class lw_extra_EncodedReceiverImpl : public lw_extra_EncodedReceiver {
 public:
  lw_extra_EncodedReceiverImpl(
      scoped_refptr<RTCRtpReceiver> rtp_receiver);
  ~lw_extra_EncodedReceiverImpl() override;

  void SetEncodedVideoSink(lw_extra_EncodedVideoSink* sink) override;

  void SetEncodedAudioSink(lw_extra_EncodedAudioSink* sink) override;

  void SetVideoEncodedReceive(bool enabled) override;

  void SetAudioEncodedReceive(bool enabled) override;

  // 内部方法：处理接收到的编码视频帧
  void OnEncodedVideoFrameReceived(const uint8_t* data, size_t size,
                                    uint32_t timestamp, bool is_key_frame,
                                    int width, int height,
                                    lw_extra_VideoCodec codec);

  // 内部方法：处理接收到的编码音频帧
  void OnEncodedAudioFrameReceived(const uint8_t* data, size_t size,
                                    uint32_t timestamp,
                                    lw_extra_AudioCodec codec);

 private:
  scoped_refptr<RTCRtpReceiver> rtp_receiver_;
  lw_extra_EncodedVideoSink* video_sink_ = nullptr;
  lw_extra_EncodedAudioSink* audio_sink_ = nullptr;
  bool video_enabled_ = false;
  bool audio_enabled_ = false;
};

// ==================== 扩展 RTP Transceiver 实现 ====================

/**
 * @brief 扩展的 RTP Transceiver 实现类
 */
class lw_extra_RtpTransceiverImpl : public lw_extra_RtpTransceiver {
 public:
  lw_extra_RtpTransceiverImpl(
      scoped_refptr<RTCRtpTransceiver> transceiver,
      lw_extra_PassthroughVideoEncoderFactory* video_factory,
      lw_extra_PassthroughAudioEncoderFactory* audio_factory);
  ~lw_extra_RtpTransceiverImpl() override;

  lw_extra_EncodedSender* GetEncodedSender() override;

  lw_extra_EncodedReceiver* GetEncodedReceiver() override;

  scoped_refptr<RTCRtpTransceiver> GetRtpTransceiver() override;

 private:
  scoped_refptr<RTCRtpTransceiver> transceiver_;
  lw_extra_PassthroughVideoEncoderFactory* video_factory_;
  lw_extra_PassthroughAudioEncoderFactory* audio_factory_;
  std::unique_ptr<lw_extra_EncodedSenderImpl> encoded_sender_;
  std::unique_ptr<lw_extra_EncodedReceiverImpl> encoded_receiver_;
  bool initialized_ = false;

  void EnsureInitialized();
};

// ==================== 扩展 PeerConnection 实现 ====================

/**
 * @brief 扩展的 PeerConnection 实现类
 */
class lw_extra_PeerConnectionImpl : public lw_extra_PeerConnection {
 public:
  lw_extra_PeerConnectionImpl(
      scoped_refptr<RTCPeerConnection> peer_connection,
      lw_extra_PassthroughVideoEncoderFactory* video_factory,
      lw_extra_PassthroughAudioEncoderFactory* audio_factory);
  ~lw_extra_PeerConnectionImpl() override;

  lw_extra_RtpTransceiver* GetTransceiverByMediaType(
      RTCMediaType media_type) override;

  lw_extra_RtpTransceiver* GetTransceiverByMid(const string& mid) override;

  vector<lw_extra_RtpTransceiver*> GetAllTransceivers() override;

 private:
  scoped_refptr<RTCPeerConnection> peer_connection_;
  lw_extra_PassthroughVideoEncoderFactory* video_factory_;
  lw_extra_PassthroughAudioEncoderFactory* audio_factory_;
  std::map<std::string, std::unique_ptr<lw_extra_RtpTransceiverImpl>> transceivers_;

  lw_extra_RtpTransceiverImpl* CreateOrGetTransceiver(
      scoped_refptr<RTCRtpTransceiver> transceiver);
};

// ==================== 扩展工具类 ====================

/**
 * @brief 扩展工具类
 *
 * 提供静态方法来创建和管理带有扩展功能的 PeerConnection
 */
class lw_extra_Utils {
 public:
  /**
   * @brief 创建带有扩展功能的 PeerConnection
   *
   * @param peer_connection 原始的 PeerConnection
   * @return 扩展的 PeerConnection 指针，调用者负责释放
   */
  static lw_extra_PeerConnection* CreateExtendedPeerConnection(
      scoped_refptr<RTCPeerConnection> peer_connection);

  /**
   * @brief 设置 UDP 端口范围
   *
   * @param peer_connection 原始的 PeerConnection
   * @param port_range 端口范围配置
   * @return true 设置成功
   */
  static bool SetUdpPortRange(
      scoped_refptr<RTCPeerConnection> peer_connection,
      const RTCPUdpPortRange& port_range);

  /**
   * @brief 获取视频编码器工厂
   *
   * @return 视频编码器工厂指针
   */
  static lw_extra_PassthroughVideoEncoderFactory* GetVideoEncoderFactory();

  /**
   * @brief 获取音频编码器工厂
   *
   * @return 音频编码器工厂指针
   */
  static lw_extra_PassthroughAudioEncoderFactory* GetAudioEncoderFactory();

 private:
  static std::unique_ptr<lw_extra_PassthroughVideoEncoderFactory>
      video_encoder_factory_;
  static lw_extra_PassthroughAudioEncoderFactory*
      audio_encoder_factory_;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_LW_EXTRA_IMPL_HXX
