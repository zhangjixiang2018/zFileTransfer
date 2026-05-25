# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指引。

## 构建

```bash
docker exec -it zjx_rtc bash
cd /code/zFileTransfer
xmake b -vy --root zFileTransfer
```

## 架构

基于 MiniRTC（轻量级 WebRTC 库）的 P2P 文件传输命令行工具。

### 数据流

1. TransferManager 创建 Peer → 连接信令服务器（`api.crossdesk.cn:9099`）→ 等待 SignalConnected
2. 通过 `JoinConnection` 以 `remote_id@password` 格式建立 P2P 连接
3. FileSender 读取文件，按 64KB 分块，附加 FileChunkHeader，通过 minirtc 可靠数据通道发送
4. 对端 FileReceiver 重组分块写入磁盘，每块回送 FileTransferAck
5. 发送端通过 ACK 追踪进度，速率使用 70/30 指数移动平均

### 核心组件

- **TransferManager**（`src/TransferManager.h/cpp`）：管理 Peer 生命周期和所有 minirtc 回调（静态方法，user_data 转换）
- **FileSender**（`src/file_transfer.h/cpp`）：无状态发送器，SendFile() 读文件 → BuildChunk() 构建分块 → SendFunc 回调发送
- **FileReceiver**（`src/file_transfer.h/cpp`）：维护 per-file FileContext（contexts_ map），seekp 处理乱序，每块发 ACK
- **minirtc**（`submodules/minirtc/`）：Git 子模块，提供 WebRTC C API，静态库依赖

### 文件传输协议（pragma pack 1）

- **FileChunkHeader**（magic `0x4A4E544D`）：file_id + offset + total_size + chunk_size + name_len + flags（bit0=首块, bit1=尾块）
- **FileTransferAck**（magic `0x4A4E5443`）：file_id + acked_offset + total_size + flags（bit0=完成）

### 数据通道标签

`"file"` 用于分块，`"file_feedback"` 用于 ACK，定义在 `src/common/constans.h`。
