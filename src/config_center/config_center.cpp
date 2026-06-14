#include "config_center/config_center.h"

#include <fstream>

#include "common/constans.h"
#include "hv/hbase.h"
#include "zlog.h"

using namespace zFileTransfer;

const std::string g_cfg_str = R"(
[Settings]
signal_server_ip = api.crossdesk.cn
signal_server_port = 9099
coturn_server_port = 3478
client_id =
password =
)";

ConfigCenter& ConfigCenter::GetInstance() {
  static ConfigCenter instance;
  return instance;
}


ConfigCenter::ConfigCenter() {
  Init(CONFIG_FILE_PATH);
}


ConfigCenter::~ConfigCenter() {

}


void ConfigCenter::Init(std::string file_path) {
  if (!hv_exists(file_path.c_str())) {
    std::ofstream f(file_path);
    if (!f.is_open()) {
      LOG_ERROR("Open cfg file {} failed!", file_path);
      return;
    }
    f << g_cfg_str;
    f.close();
  }

  _ini_parser = std::make_unique<IniParser>();
  _ini_parser->LoadFromFile(file_path.c_str());
}


void ConfigCenter::SetSignalServerIp(std::string ip) {
  if (!_ini_parser) {
    return;
  }
  _ini_parser->SetValue("signal_server_ip", ip, "Settings");
  _ini_parser->Save();
}


std::string ConfigCenter::GetSignalServerIp() const {
  std::string ip = _ini_parser ? 
    _ini_parser->GetValue("signal_server_ip", "Settings") : "";
  return ip == "" ? SIGNAL_SERVER_IP : ip;
}


void ConfigCenter::SetSignalServerPort(int port) {
  if (!_ini_parser) {
    return;
  }
  _ini_parser->Set<int>("signal_server_port", port, "Settings");
  _ini_parser->Save();
}


int ConfigCenter::GetSignalServerPort() const {
  int port = _ini_parser ? 
    _ini_parser->Get<int>("signal_server_port", "Settings", SIGNAL_SERVER_PORT) : SIGNAL_SERVER_PORT;
  return port;
}


void ConfigCenter::SetCoturnServerPort(int port) {
  if (!_ini_parser) {
    return;
  }
  _ini_parser->Set<int>("coturn_server_port", port, "Settings");
  _ini_parser->Save();
}


int ConfigCenter::GetCoturnServerPort() const {
  int port = _ini_parser ? 
    _ini_parser->Get<int>("coturn_server_port", "Settings", COTURN_SERVER_PORT) : COTURN_SERVER_PORT;
  return port;
}


void ConfigCenter::SetClientId(std::string id) {
  if (!_ini_parser) {
    return;
  }
  _ini_parser->SetValue("client_id", id, "Settings");
  _ini_parser->Save();
}


std::string ConfigCenter::GetClientId() const {
  std::string id = _ini_parser ? 
    _ini_parser->GetValue("client_id", "Settings") : "";
  return id;
}


void ConfigCenter::SetPassword(std::string pwd) {
  if (!_ini_parser) {
    return;
  }
  _ini_parser->SetValue("password", pwd, "Settings");
  _ini_parser->Save();
}


std::string ConfigCenter::GetPassword() const {
  std::string pwd = _ini_parser ? 
    _ini_parser->GetValue("password", "Settings") : "";
  return pwd;
}
