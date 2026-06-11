#include "TransferManager.h"
#include <string.h>

#include "zlog.h"
#include "common/constans.h"
#include "file_transfer.h"
#include "config_center/config_center.h"

namespace zFileTransfer {
TransferManager::TransferManager() {

}


TransferManager::~TransferManager() {
  // TODO
  // if (peer_) {
  //   DestroyPeer(peer_);
  //   peer_ = nullptr;
  // }
}


void TransferManager::StartFileTransfer(const std::filesystem::path& file_path,
                                        const std::string& file_label,
                                        const std::string& remote_id,
                                        const std::string& password)
{
  if (!std::filesystem::is_regular_file(file_path)) {
    LOG_ERROR("File path [{}] is not a regular file", file_path.string().c_str());
    exit(-1);
  }
  
  _remote_id = remote_id;
  _remote_password = password;
  int ret = ConnectTo(remote_id, password);
  if (ret < 0) {
    LOG_ERROR("Failed to connect to remote peer [{}], ret={}", remote_id.c_str(), ret);
    return;
  }

  LOG_INFO("waitting for connection status...");
  int try_count = 0;
  while (_connection_status != ConnectionStatus::Connected && try_count < 10) {
    sleep(1);
    try_count++;
    if (try_count >= 10) {
      LOG_ERROR("Failed to connect to remote peer [{}], timeout", remote_id.c_str());
      exit(1);
    }
  }
  LOG_INFO("connection [{}] success", remote_id);

  // return; // TODO

  PeerPtr* peer = this->_peer;
  TransferManager* transfer_mgr_ptr = this;

  bool expected = false;
  if (!_file_transfer.file_sending_.compare_exchange_strong(expected, true)) {
    LOG_WARN("Another file transfer is in progress, please wait until it finishes");
    return;
  }

  std::thread([peer, file_path, transfer_mgr_ptr, remote_id](){
    FileTransferState* state = &transfer_mgr_ptr->_file_transfer;

    std::error_code ec;
    uint64_t total_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
      LOG_ERROR("Failed to get file size: {}", ec.message().c_str());
      state->file_sending_ = false;
      return;
    }

    state->file_sent_bytes_ = 0;
    state->file_total_bytes_ = total_size;
    state->file_send_rate_bps_ = 0;
    state->file_transfer_window_visible_ = true;
    {
      std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
      state->file_send_start_time_ = std::chrono::steady_clock::now();
      state->file_send_last_update_time_ = state->file_send_start_time_;
      state->file_send_last_bytes_ = 0;
    }

    LOG_INFO(
        "File transfer started: {} ({} bytes), file_sending_={}, "
        "total_bytes_={}",
        file_path.filename().string(), total_size, state->file_sending_.load(),
        state->file_total_bytes_.load());

    FileSender sender;
    uint32_t file_id = FileSender::NextFileId();

    {
      std::lock_guard<std::shared_mutex> lock(
          transfer_mgr_ptr->file_id_to_transfer_state_mutex_);
      transfer_mgr_ptr->file_id_to_transfer_state_[file_id] = state;
    }

    state->current_file_id_ = file_id;

    // Update file transfer list: mark as sending
    // Find the queued file that matches the exact file path
    {
      std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
      for (auto& info : state->file_transfer_list_) {
        if (info.file_path == file_path &&
            info.status == FileTransferState::FileTransferStatus::Queued) {
          info.status = FileTransferState::FileTransferStatus::Sending;
          info.file_id = file_id;
          info.file_size = total_size;
          info.sent_bytes = 0;
          break;
        }
      }
    }

    state->file_transfer_window_visible_ = true;

    // Progress will be updated via ACK from receiver
    int ret = sender.SendFile(
        file_path, file_path.filename().string(),
        [peer, remote_id](const char* buf, size_t sz) -> int {
          static int read_count = 0;
          read_count = read_count + sz;
          // LOG_INFO("***read_count: {}", read_count);
          return SendReliableDataFrameToPeer(
              peer, buf, sz, FILE_LABEL.c_str(), remote_id.c_str(),
              remote_id.size());
        },
        64 * 1024, file_id);

    // file_sending_ should remain true until we receive the final ACK from
    // receiver
    // On error, set file_sending_ to false immediately to allow next file
    if (ret != 0) {
      state->file_sending_ = false;
      state->file_transfer_window_visible_ = false;
      state->file_sent_bytes_ = 0;
      state->file_total_bytes_ = 0;
      state->file_send_rate_bps_ = 0;
      state->current_file_id_ = 0;

      // Unregister file_id mapping on error
      {
        std::lock_guard<std::shared_mutex> lock(
            transfer_mgr_ptr->file_id_to_transfer_state_mutex_);
        transfer_mgr_ptr->file_id_to_transfer_state_.erase(file_id);
      }

      // Update file transfer list: mark as failed
      {
        std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
        for (auto& info : state->file_transfer_list_) {
          if (info.file_id == file_id) {
            info.status = FileTransferState::FileTransferStatus::Failed;
            break;
          }
        }
      }

      LOG_ERROR("FileSender::SendFile failed for [{}], ret={}",
                file_path.string().c_str(), ret);
    }
  }).detach();
}


int TransferManager::Start() {
  int ret = CreateConnectionPeer();
  return ret;
} 

void TransferManager::OnReceiveAudioBufferCb(const char* data, size_t size,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data) {
  LOG_INFO("recv audio data");
}


void TransferManager::OnReceiveDataBufferCb(const char* data, size_t size,
                                   const char* user_id, size_t user_id_size,
                                   const char* src_id, size_t src_id_size,
                                   void* user_data) 
{
  // LOG_INFO("OnReceiveDataBufferCb");
  TransferManager* transfer_mgr_ptr = (TransferManager*)user_data;
  if (!transfer_mgr_ptr) {
    return;
  }

  std::string source_id = std::string(src_id, src_id_size);
  if (source_id == FILE_LABEL) {
    std::string remote_user_id = std::string(user_id, user_id_size);

    static FileReceiver receiver;
    // Update output directory from config
    std::string configured_path = TRANSFER_SAVE_PATH;
    if (!configured_path.empty()) {
      receiver.SetOutputDir(std::filesystem::u8path(configured_path));
    } else if (receiver.OutputDir().empty()) {
      receiver = FileReceiver();  // re-init with default desktop path
    }
    receiver.SetOnSendAck([transfer_mgr_ptr,
                           remote_user_id](const FileTransferAck& ack) -> int {
      return SendReliableDataFrame(
          transfer_mgr_ptr->_peer, reinterpret_cast<const char*>(&ack),
          sizeof(FileTransferAck), FILE_FEEDBACK_LABEL.c_str());
    });

    receiver.OnData(data, size);
    return;
  } else if (source_id == FILE_FEEDBACK_LABEL) {
    if (size < sizeof(FileTransferAck)) {
      LOG_ERROR("FileTransferAck: buffer too small, size={}", size);
      return;
    }

    FileTransferAck ack{};
    memcpy(&ack, data, sizeof(FileTransferAck));

    if (ack.magic != kFileAckMagic) {
      LOG_ERROR(
          "FileTransferAck: invalid magic, got 0x{:08X}, expected 0x{:08X}",
          ack.magic, kFileAckMagic);
      return;
    }

    TransferManager::FileTransferState* state = nullptr;
    {
      std::shared_lock lock(transfer_mgr_ptr->file_id_to_transfer_state_mutex_);
      auto it = transfer_mgr_ptr->file_id_to_transfer_state_.find(ack.file_id);
      if (it != transfer_mgr_ptr->file_id_to_transfer_state_.end()) {
        state = it->second;
      }
    }

    if (!state) {
      LOG_WARN("FileTransferAck: no props/state found for file_id={}",
                ack.file_id);
      return;
    }

    if ((ack.flags & 0x01) == 0) {
      // 每秒更新一次进度
      const auto now = std::chrono::steady_clock::now();
      std::chrono::steady_clock::time_point last_time;
      {
        std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
        last_time = state->file_send_last_update_time_;
      }
      const double interval_seconds =
          std::chrono::duration<double>(now - last_time).count();
      if (interval_seconds < 1.0) {
        return;
      }
    }

    // Update progress based on ACK
    state->file_sent_bytes_ = ack.acked_offset;
    state->file_total_bytes_ = ack.total_size;

    uint32_t rate_bps = 0;
    {
      // Global transfer: no per-connection bitrate available.
      // Estimate send rate from ACKed bytes delta over time.
      const uint32_t current_rate = state->file_send_rate_bps_.load();
      uint32_t estimated_rate_bps = 0;
      const auto now = std::chrono::steady_clock::now();

      uint64_t last_bytes = 0;
      std::chrono::steady_clock::time_point last_time;
      {
        std::lock_guard<std::mutex> lock(state->file_transfer_mutex_);
        last_bytes = state->file_send_last_bytes_;
        last_time = state->file_send_last_update_time_;
      }

      if (state->file_sending_.load() && ack.acked_offset >= last_bytes) {
        const uint64_t delta_bytes = ack.acked_offset - last_bytes;
        const double delta_seconds =
            std::chrono::duration<double>(now - last_time).count();

        if (delta_seconds > 0.0 && delta_bytes > 0) {
          const double bps =
              (static_cast<double>(delta_bytes) * 8.0) / delta_seconds;
          if (bps > 0.0) {
            const double capped =
                (std::min)(bps, static_cast<double>(
                                    (std::numeric_limits<uint32_t>::max)()));
            estimated_rate_bps = static_cast<uint32_t>(capped);
          }
        }
      }

      if (estimated_rate_bps > 0 && current_rate > 0) {
        // 70% old + 30% new for smoother display
        rate_bps = static_cast<uint32_t>(current_rate * 0.7 +
                                          estimated_rate_bps * 0.3);
      } else if (estimated_rate_bps > 0) {
        rate_bps = estimated_rate_bps;
      } else {
        rate_bps = current_rate;
      }
    }

    state->file_send_rate_bps_ = rate_bps;
    state->file_send_last_bytes_ = ack.acked_offset;
    auto now = std::chrono::steady_clock::now();
    state->file_send_last_update_time_ = now;
    {
      uint64_t remaining_bytes = ack.total_size - ack.acked_offset;
      double seconds_left = 0.0;
      if (rate_bps > 0) {
          seconds_left = remaining_bytes * 8.0 / rate_bps;
      }

      // 速率单位
      double rate_kbps = rate_bps / 8.0 / 1024;
      double rate_mbps = rate_kbps / 1024;

      std::string rate_str;
      if (rate_mbps >= 1.0)
          rate_str = fmt::format("{:.2f} MB/s", rate_mbps);
      else
          rate_str = fmt::format("{:.2f} KB/s", rate_kbps);

      // ETA
      std::string eta_str;
      if (seconds_left > 0) {
          int sec = static_cast<int>(seconds_left);
          int h = sec / 3600;
          int m = (sec % 3600) / 60;
          int s = sec % 60;

          if (h > 0)
              eta_str = fmt::format("{}h {}m {}s", h, m, s);
          else if (m > 0)
              eta_str = fmt::format("{}m {}s", m, s);
          else
              eta_str = fmt::format("{}s", s);
      } else {
          eta_str = "N/A";
      }

      LOG_INFO(
          "File transfer progress: file_id={}, sent={}/{} ({:.2f}%), rate={}, ETA={}",
          ack.file_id,
          ack.acked_offset,
          ack.total_size,
          ack.total_size > 0 ? (ack.acked_offset * 100.0 / ack.total_size) : 0,
          rate_str,
          eta_str
      );      
    }

    // Update file transfer list: update progress and rate
    {
      // TODO, 这里没有执行，传多个文件、文件夹时需要处理
      std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
      for (auto& info : state->file_transfer_list_) {
        if (info.file_id == ack.file_id) {
          info.sent_bytes = ack.acked_offset;
          info.file_size = ack.total_size;
          info.rate_bps = rate_bps;
          LOG_INFO("File transfer progress: file_id={}, file_name={}, sent_bytes={}, "
                   "total_size={}, rate_bps={}",
                   ack.file_id, info.file_name, ack.acked_offset, ack.total_size, rate_bps);
          break;
        }
      }
    }

    // Check if transfer is completed
    if ((ack.flags & 0x01) != 0) {
      // Transfer completed - receiver has finished receiving the file
      // Reopen window if it was closed by user
      state->file_transfer_window_visible_ = true;
      state->file_sending_ = false;  // Mark sending as finished
      LOG_INFO(
          "File transfer completed via ACK, file_id={}, total_size={}, "
          "acked_offset={}",
          ack.file_id, ack.total_size, ack.acked_offset);

      // Update file transfer list: mark as completed
      {
        std::lock_guard<std::mutex> lock(state->file_transfer_list_mutex_);
        for (auto& info : state->file_transfer_list_) {
          if (info.file_id == ack.file_id) {
            info.status =
                TransferManager::FileTransferState::FileTransferStatus::Completed;
            info.sent_bytes = ack.total_size;
            break;
          }
        }
      }

      // Unregister file_id mapping after completion
      {
        std::lock_guard<std::shared_mutex> lock(
            transfer_mgr_ptr->file_id_to_transfer_state_mutex_);
        transfer_mgr_ptr->file_id_to_transfer_state_.erase(ack.file_id);
      }
    }

    return;
  }
}


void TransferManager::OnReceiveVideoBufferCb(const XVideoFrame* video_frame,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data) {
  LOG_INFO("recv video data");
}


void TransferManager::OnSignalStatusCb(SignalStatus status, const char* user_id,
                              size_t user_id_size, void* user_data) 
{
  TransferManager* transfer_mgr_ptr = static_cast<TransferManager*>(user_data);
  if (transfer_mgr_ptr == nullptr) {
    LOG_ERROR("transfer_mgr_ptr is nullptr");
    exit(1);
    return;
  }
  switch (status) {
    case SignalStatus::SignalConnecting:
      LOG_INFO("recv signal status, user_id[{}], status [Connecting]", user_id);
      transfer_mgr_ptr->_signal_status = status;
      break;
    case SignalStatus::SignalConnected:
      LOG_INFO("recv signal status, user_id[{}], status [Connected]", user_id);
      transfer_mgr_ptr->_signal_status = status;
      break;
    case SignalStatus::SignalFailed:
      LOG_INFO("recv signal status, user_id[{}], status [Failed]", user_id);
      exit(1);
      break;
    case SignalStatus::SignalClosed:
      LOG_INFO("recv signal status, user_id[{}], status [Closed]", user_id);
      exit(1);
      break;
    case SignalStatus::SignalReconnecting:
      LOG_INFO("recv signal status, user_id[{}], status [Reconnecting]", user_id);
      break;
    case SignalStatus::SignalServerClosed:
      LOG_INFO("recv signal status, user_id[{}], status [ServerClosed]", user_id);
      exit(1);
      break;
    default:
      LOG_ERROR("recv signal status, user_id[{}], status [{}]", user_id, (int)status);
      exit(1);
  }
}


void TransferManager::OnSignalMessageCb(const char* message, size_t size,
                               void* user_data) {
  LOG_INFO("recv signal message");
}


void TransferManager::OnConnectionStatusCb(ConnectionStatus status, const char* user_id,
                                  const size_t user_id_size, void* user_data) 
{
  TransferManager* transfer_mgr_ptr = static_cast<TransferManager*>(user_data);
  if (transfer_mgr_ptr == nullptr) {
    LOG_ERROR("transfer_mgr_ptr is nullptr");
    exit(1);
    return;
  }
  switch (status) {
    case ConnectionStatus::Connecting:
      transfer_mgr_ptr->_connection_status = status;
      LOG_INFO("recv connection status, user_id[{}], status [Connecting]", user_id);
      break;
    case ConnectionStatus::Connected:
      transfer_mgr_ptr->_connection_status = status;
      LOG_INFO("recv connection status, user_id[{}], status [Connected]", user_id);
      break;
    case Gathering:
      LOG_INFO("recv connection status, user_id[{}], status [Gathering]", user_id);
      break;
    case ConnectionStatus::Disconnected:
      LOG_INFO("recv connection status, user_id[{}], status [Disconnected]", user_id);
      exit(1);
      break;
    case ConnectionStatus::Failed:
      LOG_INFO("recv connection status, user_id[{}], status [Failed]", user_id);
      exit(1);
      break;
    case ConnectionStatus::IncorrectPassword:
      LOG_INFO("recv connection status, user_id[{}], status [IncorrectPassword]", user_id);
      exit(1);
      break;
    case ConnectionStatus::NoSuchTransmissionId:
      LOG_INFO("recv connection status, user_id[{}], status [NoSuchTransmissionId]", user_id);
      exit(1);
      break;
    default:
      LOG_ERROR("recv connection status, user_id[{}], status [{}]", user_id, (int)status);
  }
}


void TransferManager::OnNetStatusReport(const char* client_id, size_t client_id_size,
                               TraversalMode mode,
                               const XNetTrafficStats* net_traffic_stats,
                               const char* user_id, const size_t user_id_size,
                               void* user_data) {
  // LOG_INFO("recv Net Status Report client id[{}], user id[{}], mode[{}]", client_id, user_id, int(mode));
  // TODO 看看还有哪些值得关注的信息
  TransferManager* mgr = (TransferManager*)user_data;
  if (!mgr) {
    return;
  }

  if (strchr(client_id, '@') != nullptr && strchr(user_id, '-') == nullptr) {
    LOG_INFO("**client id: [{}]", client_id);
    std::string id, password;
    const char* at_pos = strchr(client_id, '@');
    if (at_pos == nullptr) {
      id = client_id;
      password.clear();
    } else {
      id.assign(client_id, at_pos - client_id);
      password = at_pos + 1;
    }

    memset(mgr->_client_id, 0, sizeof(mgr->_client_id));
    strncpy(mgr->_client_id, id.c_str(), sizeof(mgr->_client_id) - 1);
    mgr->_client_id[sizeof(mgr->_client_id) - 1] = '\0';

    memset(mgr->_password_saved, 0, sizeof(mgr->_password_saved));
    strncpy(mgr->_password_saved, password.c_str(), sizeof(mgr->_password_saved) - 1);
    mgr->_password_saved[sizeof(mgr->_password_saved) - 1] = '\0';

    memset(mgr->_client_id_with_password, 0, sizeof(mgr->_client_id_with_password));
    strncpy(mgr->_client_id_with_password, client_id, sizeof(mgr->_client_id_with_password) - 1);
    mgr->_client_id_with_password[sizeof(mgr->_client_id_with_password) - 1] = '\0';
    LOG_INFO("client id: [{} {}]", mgr->_client_id, mgr->_password_saved);
    ConfigCenter& config_center = ConfigCenter::GetInstance();
    config_center.SetClientId(mgr->_client_id);
    config_center.SetPassword(mgr->_password_saved);

    // LOG_INFO("password: [{}]", mgr->_password_saved);
  }

  if (net_traffic_stats) {
    // TODO data_outbound_stats.bitrate 与实际发送速率不一致
    // LOG_INFO("##client id: [{}]", client_id);
    // auto data_inbound_stats = net_traffic_stats->data_inbound_stats;
    // auto data_outbound_stats = net_traffic_stats->data_outbound_stats;
    // LOG_INFO("data inbound stats: [{} {} {}]", data_inbound_stats.bitrate, data_inbound_stats.rtp_packet_count, data_inbound_stats.loss_rate);
    // LOG_INFO("data outbound stats: [{} {}]", data_outbound_stats.bitrate, data_outbound_stats.rtp_packet_count);
  }
}


int TransferManager::CreateConnectionPeer() {
  // TODO
  ConfigCenter& config_center = ConfigCenter::GetInstance();
  std::string client_id = config_center.GetClientId();
  std::string password = config_center.GetPassword();
  if (!client_id.empty() && !password.empty()) {
    std::string id_with_pasw = client_id + "@" + password;
    memset(_client_id_with_password, 0, sizeof(_client_id_with_password));
    strncpy(_client_id_with_password, id_with_pasw.c_str(), sizeof(_client_id_with_password) - 1);
    _client_id_with_password[sizeof(_client_id_with_password) - 1] = '\0';    
  }

  _signal_server_ip = config_center.GetSignalServerIp();
  _signal_server_port = config_center.GetSignalServerPort();
  _coturn_server_port = config_center.GetCoturnServerPort();
  _params.user_id = _client_id_with_password;

  LOG_INFO("signal server ip: [{}], signal_server_port: [{}], coturn_server_port: [{}], client_id_with_password: [{}]",
    _signal_server_ip, _signal_server_port, _coturn_server_port, _client_id_with_password);

  _params.use_cfg_file = false;
  strncpy((char*)_params.signal_server_ip, _signal_server_ip.c_str(),
          sizeof(_params.signal_server_ip) - 1);
  _params.signal_server_ip[sizeof(_params.signal_server_ip) - 1] = '\0';
  _params.signal_server_port = _signal_server_port;
  strncpy((char*)_params.stun_server_ip, _signal_server_ip.c_str(),
          sizeof(_params.stun_server_ip) - 1);
  _params.stun_server_ip[sizeof(_params.stun_server_ip) - 1] = '\0';
  _params.stun_server_port = _coturn_server_port;
  strncpy((char*)_params.turn_server_ip, _signal_server_ip.c_str(),
          sizeof(_params.turn_server_ip) - 1);
  _params.turn_server_ip[sizeof(_params.turn_server_ip) - 1] = '\0';
  _params.turn_server_port = _coturn_server_port;
  strncpy((char*)_params.turn_server_username, "crossdesk",
          sizeof(_params.turn_server_username) - 1);
  _params.turn_server_username[sizeof(_params.turn_server_username) - 1] = '\0';
  strncpy((char*)_params.turn_server_password, "crossdeskpw",
          sizeof(_params.turn_server_password) - 1);
  _params.turn_server_password[sizeof(_params.turn_server_password) - 1] = '\0';

  strncpy(_params.log_path, "./logs/",sizeof(_params.log_path) - 1);
  _params.log_path[sizeof(_params.log_path) - 1] = '\0';

  _params.hardware_acceleration = false;
  _params.av1_encoding = false;
  _params.enable_turn = false;
  _params.enable_srtp = false;
  _params.video_quality = VideoQuality::QualityHigh;

  _params.on_receive_video_buffer = nullptr;
  _params.on_receive_data_buffer = OnReceiveDataBufferCb;
  _params.on_receive_audio_buffer = nullptr;
  _params.on_receive_video_frame = nullptr;
  _params.on_signal_status = OnSignalStatusCb;
  _params.on_signal_message = OnSignalMessageCb;
  _params.on_connection_status = OnConnectionStatusCb;
  _params.on_net_status_report = OnNetStatusReport;

  _params.user_data = this;

  _peer = CreatePeer(&_params);
  if (_peer) {
    LOG_INFO("Create peer instance [{}] successful", _client_id);
    Init(_peer);
    LOG_INFO("Peer [{}] init finish", _client_id);
  } else {
    LOG_INFO("Create peer [{}] instance failed", _client_id);
  }

  AddDataStream(_peer, FILE_LABEL.c_str(), true);
  AddDataStream(_peer, FILE_FEEDBACK_LABEL.c_str(), true);

  return 0;
}


int TransferManager::ConnectTo(const std::string& remote_id, const std::string& password) {
  std::string remote_id_with_pwd = remote_id + "@" + password; 
  int ret = JoinConnection(_peer, remote_id_with_pwd.c_str());
  return ret;
}


} 