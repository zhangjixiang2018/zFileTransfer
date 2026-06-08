#ifndef CONFIG_CENTER_H_
#define CONFIG_CENTER_H_

#include <string>
#include <memory>

#include "hv/iniparser.h"

class ConfigCenter {
public:
  static ConfigCenter& GetInstance();

  ~ConfigCenter();

  void Init(std::string file_path);

  void SetSignalServerIp(std::string ip);
  std::string GetSignalServerIp() const;

  void SetSignalServerPort(int port);
  int GetSignalServerPort() const;
  
  void SetCoturnServerPort(int port);
  int GetCoturnServerPort() const;

  void SetClientId(std::string id);
  std::string GetClientId() const;

  void SetPassword(std::string pwd);
  std::string GetPassword() const;

private:
  ConfigCenter();
  
  std::unique_ptr<IniParser>  _ini_parser;
};

#endif