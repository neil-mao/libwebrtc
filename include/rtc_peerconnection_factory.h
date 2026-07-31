#ifndef LIB_WEBRTC_RTC_PEERCONNECTION_FACTORY_HXX
#define LIB_WEBRTC_RTC_PEERCONNECTION_FACTORY_HXX

#include "rtc_audio_source.h"
#include "rtc_audio_track.h"
#include "rtc_types.h"
#ifdef RTC_DESKTOP_DEVICE
#include "rtc_desktop_device.h"
#endif
#include "rtc_media_stream.h"
#include "rtc_mediaconstraints.h"
#include "rtc_video_device.h"
#include "rtc_video_source.h"

namespace libwebrtc {

class RTCPeerConnection;
class RTCAudioDevice;
class RTCAudioProcessing;
class RTCVideoDevice;
class RTCRtpCapabilities;

class RTCPeerConnectionFactory : public RefCountInterface {
 public:
  virtual bool Initialize() = 0;

  virtual bool Terminate() = 0;

  virtual scoped_refptr<RTCPeerConnection> Create(
      const RTCConfiguration& configuration,
      scoped_refptr<RTCMediaConstraints> constraints) = 0;

  virtual void Delete(scoped_refptr<RTCPeerConnection> peerconnection) = 0;

  virtual scoped_refptr<RTCAudioDevice> GetAudioDevice() = 0;

  virtual scoped_refptr<RTCAudioProcessing> GetAudioProcessing() = 0;

  virtual scoped_refptr<RTCVideoDevice> GetVideoDevice() = 0;
#ifdef RTC_DESKTOP_DEVICE
  virtual scoped_refptr<RTCDesktopDevice> GetDesktopDevice() = 0;
#endif
  virtual scoped_refptr<RTCAudioSource> CreateAudioSource(
      const string audio_source_label,
      RTCAudioSource::SourceType source_type =
          RTCAudioSource::SourceType::kMicrophone,
        RTCAudioOptions options = RTCAudioOptions()) = 0;

  virtual scoped_refptr<RTCVideoSource> CreateVideoSource(
      scoped_refptr<RTCVideoCapturer> capturer, const string video_source_label,
      scoped_refptr<RTCMediaConstraints> constraints) = 0;

  virtual scoped_refptr<RTCVideoSource> CreateCustomVideoSource(string video_source_label,
      scoped_refptr<RTCMediaConstraints> constraints) = 0;

#ifdef RTC_DESKTOP_DEVICE
  virtual scoped_refptr<RTCVideoSource> CreateDesktopSource(
      scoped_refptr<RTCDesktopCapturer> capturer,
      const string video_source_label,
      scoped_refptr<RTCMediaConstraints> constraints) = 0;
#endif
  virtual scoped_refptr<RTCAudioTrack> CreateAudioTrack(
      scoped_refptr<RTCAudioSource> source, const string track_id) = 0;

  virtual scoped_refptr<RTCVideoTrack> CreateVideoTrack(
      scoped_refptr<RTCVideoSource> source, const string track_id) = 0;

  virtual scoped_refptr<RTCMediaStream> CreateStream(
      const string stream_id) = 0;

  virtual scoped_refptr<RTCRtpCapabilities> GetRtpSenderCapabilities(
      RTCMediaType media_type) = 0;

  virtual scoped_refptr<RTCRtpCapabilities> GetRtpReceiverCapabilities(
      RTCMediaType media_type) = 0;

  /**
   * @brief 设置下一次 AddTrack 使用的编码器类型（单次生效的标记）
   *
   * 在 AddTrack/ReNegotiation 前调用。标记会被下一次 encoder Create()
   * 消费并自动复位，因此不影响后续 RGBA track 使用内置编码器。
   *
   * 必须在 AddTrack 或 create_answer 之前调用。
   *
   * @param video true = 下一个 video track 用 passthrough encoder
   * @param audio true = 下一个 audio track 用 passthrough encoder
   */
  virtual void SetNextEncoderPassthrough(bool video, bool audio) = 0;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_PEERCONNECTION_FACTORY_HXX
