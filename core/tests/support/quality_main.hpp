#pragma once

#include "quality_scenario.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <string>

// Entry point shared by the quality test binaries. They collect a report per
// scenario and, given --quality-report=<path>, write the lot as JSON so CI can
// keep it as an artifact.

namespace test_util {

inline nlohmann::json& reports()
{
    static nlohmann::json r = nlohmann::json::array();
    return r;
}

inline void record(const QualityReport& r)
{
    reports().push_back(r.to_json());
    std::cout << "  " << r.scenario
              << " | expand " << r.audio.expand_rate
              << " | glitches " << r.audio.glitch_count
              << " | segSNR " << r.audio.seg_snr_db << " dB";
    if (r.video_enabled) {
        std::cout << " | PSNR " << r.video.psnr_avg_db << " dB"
                  << " | SSIM " << r.video.ssim_avg
                  << " | freezes " << r.video.freeze_count
                  << " | skew " << r.sync.median_skew_ms << " ms";
    }
    std::cout << " | target " << r.audio_stats.target_delay_ms << " ms\n";
}

} // namespace test_util

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    std::string report_path;
    const std::string flag = "--quality-report=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind(flag, 0) == 0) {
            report_path = arg.substr(flag.size());
        }
    }

    const int rc = RUN_ALL_TESTS();
    if (!report_path.empty()) {
        std::ofstream out(report_path);
        out << test_util::reports().dump(2) << "\n";
    }
    return rc;
}
