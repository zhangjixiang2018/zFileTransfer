#include <iostream>
#include <unistd.h>
#include <string>
#include "log/zlog.h"
#include "TransferManager.h"
#include "common/constans.h"

using namespace std;
using namespace zFileTransfer;


int main(int argc, char** argv) {
  cout << "Hello World!" << endl;

  // 解析命令行参数
  RUN_MODEL run_mode = EN_DEFAULT;
  std::string file_path;
  std::string remote_id;
  std::string password;

  if (argc == 1) {
    run_mode = EN_DEFAULT;
  } else if (argc == 2) {
    // 接收模式: ./zFileTransfer remote_id@password:remote_file_path
    run_mode = EN_RECVER;
    std::string arg = argv[1];
    size_t at_pos = arg.find('@');
    size_t colon_pos = arg.find(':');
    if (at_pos == std::string::npos || colon_pos == std::string::npos || at_pos >= colon_pos) {
      LOG_ERROR("invalid format, expected: remote_id@password:remote_file_path");
      exit(1);
    }
    remote_id = arg.substr(0, at_pos);
    password = arg.substr(at_pos + 1, colon_pos - at_pos - 1);
    file_path = arg.substr(colon_pos + 1);
  } else if (argc == 3) {
    // 发送模式: ./zFileTransfer local_file_path remote_id@password
    run_mode = EN_SENDER;
    file_path = argv[1];
    std::string arg = argv[2];
    size_t at_pos = arg.find('@');
    if (at_pos == std::string::npos) {
      LOG_ERROR("invalid format, expected: remote_id@password");
      exit(1);
    }
    remote_id = arg.substr(0, at_pos);
    password = arg.substr(at_pos + 1);
  } else {
    cout << "Usage:" << endl;
    cout << "  " << argv[0] << "                              (default mode)" << endl;
    cout << "  " << argv[0] << " <file> <id>@<password>        (sender mode)" << endl;
    cout << "  " << argv[0] << " <id>@<password>:<remote_file>  (receiver mode)" << endl;
    exit(1);
  }

  LOG_INFO("waitting for signal connected...");
  TransferManager transfer_manager;
  transfer_manager.Start();
  int try_cnt = 0;
  while (transfer_manager.GetSignalStatus() != SignalStatus::SignalConnected) {
    sleep(1);
    try_cnt++;
    if (try_cnt > 10) {
      LOG_ERROR("connect signal server fail!");
      exit(-1);
    }
  }
  LOG_INFO("signal connect success!");

  int ret = 0;

  // 根据模式执行
  switch (run_mode) {
    case EN_SENDER:
      ret = transfer_manager.StartFileTransfer(file_path, remote_id, password);
      break;
    case EN_RECVER:
      ret = transfer_manager.RequestFile(file_path, remote_id, password);
      break;
    case EN_DEFAULT:
    default:
      LOG_INFO("default mode, waiting for incoming connections...");
      break;
  }

  if (ret != 0) {
    exit(-1);
  }

  while (1) {
    sleep(1);
  }
  return 0;
}