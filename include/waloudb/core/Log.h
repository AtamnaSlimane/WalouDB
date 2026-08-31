#pragma once

#include <memory>
#include <spdlog/logger.h>

namespace WalouDB {

class Log {
public:
  static void Init();

  static std::shared_ptr<spdlog::logger> &GetCoreLogger();
};

} // namespace WalouDB

#define WALOU_TRACE(...) ::WalouDB::Log::GetCoreLogger()->trace(__VA_ARGS__)

#define WALOU_INFO(...) ::WalouDB::Log::GetCoreLogger()->info(__VA_ARGS__)

#define WALOU_WARN(...) ::WalouDB::Log::GetCoreLogger()->warn(__VA_ARGS__)

#define WALOU_ERROR(...) ::WalouDB::Log::GetCoreLogger()->error(__VA_ARGS__)

#define WALOU_CRITICAL(...)                                                    \
  ::WalouDB::Log::GetCoreLogger()->critical(__VA_ARGS__)
