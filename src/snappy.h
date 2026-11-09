#pragma once
#include <string>

// Snappy raw 格式解压（无帧头），用于 pack_type=2 的响应
bool snappy_uncompress(const std::string& in, std::string& out, std::string& err);
