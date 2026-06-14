#ifndef CONSTANS_H_
#define CONSTANS_H_

#include <string>

const std::string FILE_LABEL = "file";
const std::string FILE_FEEDBACK_LABEL = "file_feedback";
const std::string TRANSFER_SAVE_PATH = "./download/";

const std::string CONFIG_FILE_PATH = "./zFileTransfer.conf";

const std::string SIGNAL_SERVER_IP = "api.crossdesk.cn";
const int SIGNAL_SERVER_PORT = 9099;
const int COTURN_SERVER_PORT = 3478;

enum RUN_MODEL {
  EN_DEFAULT = 0, // 默认模式。 运行命令 ./zFileTransfer
  EN_SENDER,      // 发送。 运行命令 ./zFileTransfer local_file_path  remote_id@password
  EN_RECVER       // 接收。 运行命令 ./zFileTransfer remote_id@password:remote_file_path
};

#endif  // CONSTANS_H_