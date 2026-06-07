# zFileTransfer

基于 MiniRTC 的 P2P 文件传输命令行工具，通过 WebRTC 数据通道进行可靠的文件传输。

## 功能特性

- **P2P 文件传输**：基于 WebRTC 的点对点文件传输，无需中转服务器
- **分块传输**：支持大文件分块传输（默认 64KB 每块）
- **可靠传输**：内置 ACK 确认机制，确保数据完整性
- **乱序处理**：支持分块乱序到达，自动重组
- **进度追踪**：实时传输进度和速率统计
- **断点续传**：基于 offset 的分块机制支持断点续传


## 构建说明

### 1. 克隆项目（包含子模块）

```bash
git clone --recursive https://github.com/yourusername/zFileTransfer.git

git submodule update --init --recursive

cd zFileTransfer
```


### 2. 构建项目

```bash
xmake -b -vy --root zFileTransfer
```

## 使用方法

### 基本用法

```bash
# 接收端启动
./zFileTransfer

# 发送端启动
./zFileTransfer <file_path> <remote_id> <password>

# 示例
./zFileTransfer /path/to/file.txt user123 pass456
```

### 参数说明

- `file_path`：要发送的文件路径
- `remote_id`：远程用户的 ID
- `password`：连接密码

### 接收文件

接收端会自动将文件保存到当前目录的`download`目录下，如果文件已存在会自动添加时间戳后缀。