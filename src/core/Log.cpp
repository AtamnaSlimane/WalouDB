
#include "waloudb/core/Log.h"
#include "spdlog/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace WalouDB {

static std::shared_ptr<spdlog::logger> s_CoreLogger;

void Log::Init() {
  s_CoreLogger = spdlog::stdout_color_mt("WALOUDB");

  s_CoreLogger->set_level(spdlog::level::trace);
  spdlog::set_pattern("%^[%T] %n: %v%$");

  s_CoreLogger->info("Walou logging initialized");
}

std::shared_ptr<spdlog::logger> &Log::GetCoreLogger() { return s_CoreLogger; }

} // namespace WalouDB
