#include "startup_profile.h"

#include <cstdio>

namespace neomifes::app {

bool StartupProfile::writeJson(const std::filesystem::path& out) const {
    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, out.c_str(), L"wb") != 0 || fp == nullptr) {
        return false;
    }
    // Field names match the schema consumed by tests/integration/startup_measure_test.
    std::fprintf(fp,
        "{\n"
        "  \"winMainEnterNs\": %lld,\n"
        "  \"windowCreatedNs\": %lld,\n"
        "  \"firstPaintNs\": %lld,\n"
        "  \"measuredExitNs\": %lld,\n"
        "  \"workingSetBytesAtFirstPaint\": %llu,\n"
        "  \"privateWorkingSetBytesAtFirstPaint\": %llu\n"
        "}\n",
        static_cast<long long>(winMainEnterNs),
        static_cast<long long>(windowCreatedNs),
        static_cast<long long>(firstPaintNs),
        static_cast<long long>(measuredExitNs),
        static_cast<unsigned long long>(workingSetBytesAtFirstPaint),
        static_cast<unsigned long long>(privateWorkingSetBytesAtFirstPaint));
    std::fclose(fp);
    return true;
}

}  // namespace neomifes::app
