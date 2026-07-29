#ifndef LIB_WEBRTC_RTC_TYPES_HXX
#define LIB_WEBRTC_RTC_TYPES_HXX

#ifdef LIB_WEBRTC_API_EXPORTS
#define LIB_WEBRTC_API __declspec(dllexport)
#elif defined(LIB_WEBRTC_API_DLL)
#define LIB_WEBRTC_API __declspec(dllimport)
#elif !defined(WIN32)
#define LIB_WEBRTC_API __attribute__((visibility("default")))
#else
#define LIB_WEBRTC_API
#endif

#include "base/fixed_size_function.h"
#include "base/portable.h"
#include "base/refcount.h"
#include "base/scoped_ref_ptr.h"

namespace libwebrtc {

enum { kMaxIceServerSize = 8 };

// template <typename T>
// using vector = bsp::inlined_vector<T, 16, true>;

template <typename Key, typename T>
using map = std::map<Key, T>;

enum class MediaSecurityType { kSRTP_None = 0, kSDES_SRTP, kDTLS_SRTP };

enum class RTCMediaType { AUDIO, VIDEO, DATA, UNSUPPORTED };

using string = portable::string;

// template <typename Key, typename T>
// using map = portable::map<Key, T>;

template <typename T>
using vector = portable::vector<T>;

struct IceServer {
  string uri;
  string username;
  string password;
};

enum class IceTransportsType { kNone, kRelay, kNoHost, kAll };

enum class TcpCandidatePolicy {
  kTcpCandidatePolicyEnabled,
  kTcpCandidatePolicyDisabled
};

enum class CandidateNetworkPolicy {
  kCandidateNetworkPolicyAll,
  kCandidateNetworkPolicyLowCost
};

enum class RtcpMuxPolicy {
  kRtcpMuxPolicyNegotiate,
  kRtcpMuxPolicyRequire,
};

enum BundlePolicy {
  kBundlePolicyBalanced,
  kBundlePolicyMaxBundle,
  kBundlePolicyMaxCompat
};

enum class SdpSemantics { kPlanB, kUnifiedPlan };

// ==================== jteam 分支新增：UDP 端口范围配置 ====================

/**
 * @brief UDP 端口范围配置
 *
 * 用于控制 WebRTC ICE 候选所使用的 UDP 端口范围
 * 新增于 jteam 分支
 */
struct RTCPUdpPortRange {
  uint16_t min_port = 0;  // 最小端口号，0 表示不限制
  uint16_t max_port = 0;  // 最大端口号，0 表示不限制

  /**
   * @brief 检查端口范围是否有效
   * @return true 如果有效
   */
  bool IsValid() const {
    return min_port == 0 || max_port == 0 ||
           (min_port >= 1024 && max_port <= 65535 && min_port <= max_port);
  }
};

// ========================================================================

struct RTCConfiguration {
  IceServer ice_servers[kMaxIceServerSize];
  IceTransportsType type = IceTransportsType::kAll;
  BundlePolicy bundle_policy = BundlePolicy::kBundlePolicyBalanced;
  RtcpMuxPolicy rtcp_mux_policy = RtcpMuxPolicy::kRtcpMuxPolicyRequire;
  CandidateNetworkPolicy candidate_network_policy =
      CandidateNetworkPolicy::kCandidateNetworkPolicyAll;
  TcpCandidatePolicy tcp_candidate_policy =
      TcpCandidatePolicy::kTcpCandidatePolicyEnabled;

  int ice_candidate_pool_size = 0;

  MediaSecurityType srtp_type = MediaSecurityType::kDTLS_SRTP;
  SdpSemantics sdp_semantics = SdpSemantics::kUnifiedPlan;
  bool offer_to_receive_audio = true;
  bool offer_to_receive_video = true;

  bool disable_ipv6 = false;
  bool disable_ipv6_on_wifi = false;
  int max_ipv6_networks = 5;
  bool disable_link_local_networks = false;
  int screencast_min_bitrate = -1;
  bool enable_dscp = false;

  // private
  bool use_rtp_mux = true;
  uint32_t local_audio_bandwidth = 128;
  uint32_t local_video_bandwidth = 512;

  // ===== jteam 分支新增：UDP 端口范围配置 =====
  RTCPUdpPortRange udp_port_range;
};

struct SdpParseError {
 public:
  // The sdp line that causes the error.
  string line;
  // Explains the error.
  string description;
};

enum DesktopType { kScreen, kWindow };

struct RTCAudioOptions {
  RTCAudioOptions() {}

  bool echo_cancellation = true;

  bool auto_gain_control = true;

  bool noise_suppression = true;

  bool highpass_filter = false;
};

}  // namespace libwebrtc

#endif  // LIB_WEBRTC_RTC_TYPES_HXX
