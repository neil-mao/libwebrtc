#ifndef LIB_WEBRTC_RTC_LW_EXTRA_HXX
#define LIB_WEBRTC_RTC_LW_EXTRA_HXX

#include "base/refcount.h"
#include "rtc_rtp_transceiver.h"
#include "rtc_types.h"

namespace libwebrtc {

// ==================== 编码格式枚举 ====================

/**
 * @brief 视频编码格式
 */
enum class lw_extra_VideoCodec {
  kH264 = 0,
  kAV1 = 1,
};

/**
 * @brief 音频编码格式
 */
enum class lw_extra_AudioCodec {
  kOpus = 0,
};

// ==================== 编码后数据帧结构 ====================

/**
 * @brief 编码后的视频帧
 *
 * 用于发送和接收编码后的 H264/AV1 视频数据
 */
struct lw_extra_EncodedVideoFrame {
  lw_extra_VideoCodec codec;
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
  bool is_key_frame;
  int width;
  int height;
};

/**
 * @brief 编码后的音频帧
 *
 * 用于发送和接收编码后的 Opus 音频数据
 */
struct lw_extra_EncodedAudioFrame {
  lw_extra_AudioCodec codec;
  uint8_t* data;
  size_t size;
  uint32_t timestamp;
};

// ==================== 编码数据接收回调 ====================

/**
 * @brief 编码视频帧接收回调接口
 *
 * 用户可以通过继承此接口来接收编码后的视频帧（H264/AV1），
 * 而不是解码后的 RGBA/I420 图像。
 */
class lw_extra_EncodedVideoSink {
 public:
  virtual ~lw_extra_EncodedVideoSink() {}

  /**
   * @brief 当收到编码后的视频帧时调用
   *
   * @param frame 编码后的视频帧数据
   */
  virtual void OnEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) = 0;
};

/**
 * @brief 编码音频帧接收回调接口
 *
 * 用户可以通过继承此接口来接收编码后的音频帧（Opus），
 * 而不是解码后的 PCM 音频数据。
 */
class lw_extra_EncodedAudioSink {
 public:
  virtual ~lw_extra_EncodedAudioSink() {}

  /**
   * @brief 当收到编码后的音频帧时调用
   *
   * @param frame 编码后的音频帧数据
   */
  virtual void OnEncodedAudioFrame(const lw_extra_EncodedAudioFrame& frame) = 0;
};

// ==================== 编码数据发送接口 ====================

/**
 * @brief 编码数据发送接口
 *
 * 提供直接发送编码后的视频和音频数据的功能，
 * 数据将直接发送到 RTP 层，跳过 WebRTC 的编解码过程。
 *
 * 使用方式：
 * 1. 创建 PeerConnection 并建立连接
 * 2. 通过 GetRtpTransceiver() 获取 transceiver
 * 3. 通过 GetEncodedSender() 获取编码数据发送器
 * 4. 调用 SendEncodedVideoFrame() 或 SendEncodedAudioFrame() 发送数据
 */
class lw_extra_EncodedSender {
 public:
  virtual ~lw_extra_EncodedSender() {}

  /**
   * @brief 发送编码后的视频帧到 RTP
   *
   * @param frame 编码后的视频帧数据
   * @return true 发送成功，false 发送失败
   */
  virtual bool SendEncodedVideoFrame(const lw_extra_EncodedVideoFrame& frame) = 0;

  /**
   * @brief 发送编码后的音频帧到 RTP
   *
   * @param frame 编码后的音频帧数据
   * @return true 发送成功，false 发送失败
   */
  virtual bool SendEncodedAudioFrame(const lw_extra_EncodedAudioFrame& frame) = 0;

  /**
   * @brief 设置发送视频编码数据
   *
   * @param enabled true 启用编码数据发送，false 禁用
   */
  virtual void SetVideoEncodedSend(bool enabled) = 0;

  /**
   * @brief 设置发送音频编码数据
   *
   * @param enabled true 启用编码数据发送，false 禁用
   */
  virtual void SetAudioEncodedSend(bool enabled) = 0;
};

// ==================== 编码数据接收接口 ====================

/**
 * @brief 编码数据接收接口
 *
 * 提供接收编码后的视频和音频数据的功能，
 * 用户可以获取原始编码数据（H264/AV1/Opus），而不是解码后的数据。
 *
 * 使用方式：
 * 1. 创建 PeerConnection 并建立连接
 * 2. 通过 GetRtpTransceiver() 获取 transceiver
 * 3. 通过 GetEncodedReceiver() 获取编码数据接收器
 * 4. 注册编码数据接收回调
 * 5. 当有编码数据到达时，回调函数会被触发
 */
class lw_extra_EncodedReceiver {
 public:
  virtual ~lw_extra_EncodedReceiver() {}

  /**
   * @brief 注册编码视频帧接收回调
   *
   * @param sink 接收回调指针，设为 nullptr 时取消注册
   */
  virtual void SetEncodedVideoSink(lw_extra_EncodedVideoSink* sink) = 0;

  /**
   * @brief 注册编码音频帧接收回调
   *
   * @param sink 接收回调指针，设为 nullptr 时取消注册
   */
  virtual void SetEncodedAudioSink(lw_extra_EncodedAudioSink* sink) = 0;

  /**
   * @brief 设置接收编码视频数据
   *
   * @param enabled true 启用编码数据接收，false 禁用
   */
  virtual void SetVideoEncodedReceive(bool enabled) = 0;

  /**
   * @brief 设置接收编码音频数据
   *
   * @param enabled true 启用编码数据接收，false 禁用
   */
  virtual void SetAudioEncodedReceive(bool enabled) = 0;
};

// ==================== 扩展的 RTP Transceiver 接口 ====================

/**
 * @brief 扩展的 RTP Transceiver 接口
 *
 * 提供获取编码数据发送器和接收器的方法
 */
class lw_extra_RtpTransceiver {
 public:
  virtual ~lw_extra_RtpTransceiver() {}

  /**
   * @brief 获取编码数据发送器
   *
   * @return 编码数据发送器指针，如果不支持则返回 nullptr
   */
  virtual lw_extra_EncodedSender* GetEncodedSender() = 0;

  /**
   * @brief 获取编码数据接收器
   *
   * @return 编码数据接收器指针，如果不支持则返回 nullptr
   */
  virtual lw_extra_EncodedReceiver* GetEncodedReceiver() = 0;

  /**
   * @brief 获取底层的 RTCRtpTransceiver
   *
   * @return 底层的 RTCRtpTransceiver 指针
   */
  virtual scoped_refptr<RTCRtpTransceiver> GetRtpTransceiver() = 0;
};

// ==================== 扩展的 PeerConnection 接口 ====================

/**
 * @brief 扩展的 PeerConnection 接口
 *
 * 在 RTCPeerConnection 基础上扩展，支持编码数据的收发功能
 */
class lw_extra_PeerConnection {
 public:
  virtual ~lw_extra_PeerConnection() {}

/**
 * @brief 通过媒体类型获取扩展的 Transceiver
   *
   * @param media_type 媒体类型（AUDIO 或 VIDEO）
   * @return 扩展的 Transceiver 指针，如果不支持则返回 nullptr
   */
  virtual lw_extra_RtpTransceiver* GetTransceiverByMediaType(
      RTCMediaType media_type) = 0;

  /**
   * @brief 通过 Mid 获取扩展的 Transceiver
   *
   * @param mid Media ID
   * @return 扩展的 Transceiver 指针，如果不支持则返回 nullptr
   */
  virtual lw_extra_RtpTransceiver* GetTransceiverByMid(const string& mid) = 0;

  /**
   * @brief 获取所有扩展的 Transceiver 列表
   *
   * @return 扩展的 Transceiver 列表
   */
  virtual vector<lw_extra_RtpTransceiver*> GetAllTransceivers() = 0;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_LW_EXTRA_HXX
