#include "ws.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstring>

WsClient::~WsClient() { close(); }

bool WsClient::connect(const std::wstring& host, const std::wstring& path_and_query,
                       const std::wstring& origin, std::string& err) {
    close();

    HINTERNET session = WinHttpOpen(L"level1-marketTrend/1.0",
                                    WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { err = "WinHttpOpen failed: " + std::to_string(GetLastError()); return false; }
    session_ = session;

    WinHttpSetTimeouts(session, 5000, 5000, 5000, read_timeout_ms_);

    HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) {
        err = "WinHttpConnect failed: " + std::to_string(GetLastError());
        close();
        return false;
    }
    connect_ = conn;

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path_and_query.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) {
        err = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        close();
        return false;
    }

    // 文档要求：lpBuffer = NULL，dwBufferLength = 0
    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        err = "set upgrade option failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(req);
        close();
        return false;
    }
    std::wstring origin_hdr = L"Origin: " + origin;
    WinHttpAddRequestHeaders(req, origin_hdr.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        err = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(req);
        close();
        return false;
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        err = "WinHttpReceiveResponse failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(req);
        close();
        return false;
    }

    DWORD status = 0;
    DWORD sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) {
        err = "ws handshake status " + std::to_string(status);
        WinHttpCloseHandle(req);
        close();
        return false;
    }

    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    WinHttpCloseHandle(req);
    if (!ws) {
        err = "WinHttpWebSocketCompleteUpgrade failed: " + std::to_string(GetLastError());
        close();
        return false;
    }
    ws_ = ws;

    // 关闭 WinHTTP 自动 keepalive：服务端不接受协议层 ping，会把 20s 后的自动 ping
    // 当成非法包（{"msg":"request package is illegal"}）。心跳改由程序发文本 ping。
    DWORD ka = 0;
    WinHttpSetOption(ws, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &ka, sizeof(ka));
    return true;
}

bool WsClient::send_text(const std::string& text, std::string& err) {
    if (!ws_) { err = "ws not connected"; return false; }
    DWORD rc = WinHttpWebSocketSend(ws_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                    (PVOID)text.data(), (DWORD)text.size());
    if (rc != ERROR_SUCCESS) {
        err = "ws send failed: " + std::to_string(rc);
        return false;
    }
    return true;
}

int WsClient::recv(std::string& out, std::string& err) {
    if (!ws_) { err = "ws not connected"; return -1; }
    char buf[65536];
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        DWORD rc = WinHttpWebSocketReceive(ws_, buf, sizeof(buf), &read, &type);
        if (rc != ERROR_SUCCESS) {
            if (rc == ERROR_WINHTTP_TIMEOUT) { err.clear(); return 0; }
            err = "ws recv failed: " + std::to_string(rc);
            return -1;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { err = "ws closed by peer"; return -1; }
        pending_.append(buf, read);
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            out.swap(pending_);
            pending_.clear();
            return 1;
        }
    }
}

void WsClient::close() {
    if (ws_) {
        WinHttpWebSocketClose((HINTERNET)ws_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle((HINTERNET)ws_);
        ws_ = nullptr;
    }
    if (connect_) { WinHttpCloseHandle((HINTERNET)connect_); connect_ = nullptr; }
    if (session_) { WinHttpCloseHandle((HINTERNET)session_); session_ = nullptr; }
    pending_.clear();
}
