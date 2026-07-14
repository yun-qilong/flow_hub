#include "utils/SysLog.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/stat.h>

namespace utils
{

// ===== SysLog =====

namespace
{

std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":" << std::setw(2)
        << tm_buf.tm_min << ":" << std::setw(2) << tm_buf.tm_sec << "." << std::setw(3)
        << ms.count();
    return oss.str();
}

std::string initFileName()
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    std::ostringstream oss;
    oss << "flowhub_" << std::setfill('0') << std::setw(4) << (tm_buf.tm_year + 1900)
        << std::setw(2) << (tm_buf.tm_mon + 1) << std::setw(2) << tm_buf.tm_mday << "_"
        << std::setw(2) << tm_buf.tm_hour << std::setw(2) << tm_buf.tm_min << std::setw(2)
        << tm_buf.tm_sec << ".log";
    return oss.str();
}

void ensureDir(const std::string &dir)
{
    mkdir(dir.c_str(), 0755);
}

} // namespace

const char *SysLog::levelLabel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::ERR:
        return "ERR";
    case LogLevel::WRN:
        return "WRN";
    case LogLevel::INFO:
        return "INF";
    case LogLevel::DBG:
        return "DBG";
    }
    return "???";
}

const char *SysLog::featureLabel(LogFeature f)
{
    switch (f)
    {
    case LogFeature::AICHAT:
        return "AIC";
    case LogFeature::AIDISCUSS:
        return "AID";
    }
    return "F??";
}

SysLog::SysLog(std::string logDir, uint32_t maxSizeKb)
    : logDir_(std::move(logDir)), maxSizeBytes_(static_cast<size_t>(maxSizeKb) * 1024)
{
    ensureDir(logDir_);
    openNewFile();
}

void SysLog::log(LogLevel level, const std::string &msg)
{
    if (not shouldLog(level))
    {
        return;
    }
    write(levelLabel(level), msg);
}

void SysLog::logFeature(LogFeature feature, const std::string &msg)
{
    write(featureLabel(feature), msg);
}

void SysLog::write(const char *label, const std::string &msg)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (not file_.is_open())
    {
        return;
    }

    auto line = "[" + std::string(label) + "] [" + timestamp() + "] " + msg + "\n";
    file_ << line;
    file_.flush();

    checkRotation();
}

void SysLog::checkRotation()
{
    auto pos = file_.tellp();
    if (pos >= 0 and static_cast<size_t>(pos) >= maxSizeBytes_)
    {
        file_.close();
        auto currentPath = logDir_ + "/" + currentFile_;
        auto rotatedPath = logDir_ + "/" + currentFile_ + ".1";
        rename(currentPath.c_str(), rotatedPath.c_str());
        openNewFile();
    }
}

void SysLog::openNewFile()
{
    currentFile_ = initFileName();
    auto path = logDir_ + "/" + currentFile_;
    file_.open(path, std::ios::out | std::ios::app);
}

// ===== Factory =====

SysLog *createSysLog()
{
    return new SysLog(kLogDir, kMaxSizeKb);
}

} // namespace utils
