#ifndef TRANSFERMANAGER_H_
#define TRANSFERMANAGER_H_
#include <string>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <chrono>
#include <queue>
#include <filesystem>
#include <vector>
#include <shared_mutex>
#include "minirtc.h"

namespace zFileTransfer {

class TransferManager {
public:
  struct FileTransferState {
    std::atomic<bool> file_sending_ = false;
    std::atomic<uint64_t> file_sent_bytes_ = 0;
    std::atomic<uint64_t> file_total_bytes_ = 0;
    std::atomic<uint32_t> file_send_rate_bps_ = 0;
    std::mutex file_transfer_mutex_;
    std::chrono::steady_clock::time_point file_send_start_time_;
    std::chrono::steady_clock::time_point file_send_last_update_time_;
    uint64_t file_send_last_bytes_ = 0;
    bool file_transfer_window_visible_ = false;
    bool file_transfer_window_hovered_ = false;
    std::atomic<uint32_t> current_file_id_{0};

    struct QueuedFile {
      std::filesystem::path file_path;
      std::string file_label;
      std::string remote_id;
    };
    std::queue<QueuedFile> file_send_queue_;
    std::mutex file_queue_mutex_;

    enum class FileTransferStatus { Queued, Sending, Completed, Failed };

    struct FileTransferInfo {
      std::string file_name;
      std::filesystem::path file_path;
      uint64_t file_size = 0;
      FileTransferStatus status = FileTransferStatus::Queued;
      uint64_t sent_bytes = 0;
      uint32_t file_id = 0;
      uint32_t rate_bps = 0;
    };
    std::vector<FileTransferInfo> file_transfer_list_;
    std::mutex file_transfer_list_mutex_;
  };

  TransferManager();

  ~TransferManager();

  int Start();

  void StartFileTransfer(const std::filesystem::path& file_path,
                          const std::string& file_label,
                          const std::string& remote_id,
                          const std::string& password);

  
  SignalStatus GetSignalStatus() const { return _signal_status; }
  // TODO 配置文件，使用libhv

public:
  static void OnReceiveVideoBufferCb(const XVideoFrame* video_frame,
                                     const char* user_id, size_t user_id_size,
                                     const char* src_id, size_t src_id_size,
                                     void* user_data);

  static void OnReceiveAudioBufferCb(const char* data, size_t size,
                                     const char* user_id, size_t user_id_size,
                                     const char* src_id, size_t src_id_size,
                                     void* user_data);

  static void OnReceiveDataBufferCb(const char* data, size_t size,
                                    const char* user_id, size_t user_id_size,
                                    const char* src_id, size_t src_id_size,
                                    void* user_data);

  static void OnSignalStatusCb(SignalStatus status, const char* user_id,
                               size_t user_id_size, void* user_data);

  static void OnSignalMessageCb(const char* message, size_t size,
                                void* user_data);

  static void OnConnectionStatusCb(ConnectionStatus status, const char* user_id,
                                   size_t user_id_size, void* user_data);

  static void OnNetStatusReport(const char* client_id, size_t client_id_size,
                                TraversalMode mode,
                                const XNetTrafficStats* net_traffic_stats,
                                const char* user_id, const size_t user_id_size,
                                void* user_data);  

private:
  int CreateConnectionPeer();

  int ConnectTo(const std::string& remote_id, const std::string& password);

private:
  std::string       _signal_server_ip{"api.crossdesk.cn"};
  int               _signal_server_port{9099};
  int               _coturn_server_port{3478};
  Params            _params;
  char              _client_id[10] = "";
  char              _password_saved[7] = "";
  char              _client_id_with_password[17] = "";
  std::string       _remote_id = "";
  std::string       _remote_password = "";

  PeerPtr*          _peer = nullptr;
  FileTransferState _file_transfer;

  SignalStatus      _signal_status = SignalStatus::SignalClosed;
  ConnectionStatus  _connection_status = ConnectionStatus::Disconnected;

  std::shared_mutex file_id_to_transfer_state_mutex_;
  std::unordered_map<uint32_t, FileTransferState*> file_id_to_transfer_state_;
};
} 

#endif