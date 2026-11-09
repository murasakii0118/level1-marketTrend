#pragma once
#include <string>

// WinHTTP WebSocket 客户端（无需第三方库）
class WsClient {
public:
    WsClient() = default;
    ~WsClient();
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    bool connect(const std::wstring& host, const std::wstring& path_and_query,
                 const std::wstring& origin, std::string& err);
    bool send_text(const std::string& text, std::string& err);

    // 1 = 收到完整消息；0 = 超时无数据；-1 = 连接关闭/错误
    int recv(std::string& out, std::string& err);

    void close();
    bool is_open() const { return ws_ != nullptr; }

private:
    void* session_ = nullptr;
    void* connect_ = nullptr;
    void* ws_ = nullptr;
    std::string pending_;
    int read_timeout_ms_ = 1000;
};
