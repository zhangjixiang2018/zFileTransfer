#include <iostream>
#include <unistd.h>
#include "log/zlog.h"
#include "TransferManager.h"
#include "common/constans.h"

using namespace std;
using namespace zFileTransfer;

int main(int argc, char** argv) {
  cout << "Hello World!" << endl;

  TransferManager transfer_manager;
  transfer_manager.Start();
  LOG_INFO("waitting for signal connected...");
  while (transfer_manager.GetSignalStatus() != SignalStatus::SignalConnected) {
    sleep(1);
  }
  LOG_INFO("signal connected");

  if (argc == 4) {
    std::string file_path = argv[1];
    std::string remote_id = argv[2];
    std::string password = argv[3];
    transfer_manager.StartFileTransfer(file_path, FILE_LABEL, remote_id, password);
  } else if(argc != 1) {
    cout << "Usage: " << argv[0] << " <file_path> <remote_id> <password>" << endl;
    exit(1);
  }

  while (1) {
    sleep(1);
  }
  return 0;
}