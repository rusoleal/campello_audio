#include <gtest/gtest.h>
#include <campello_audio/audio_engine.hpp>
#include <campello_audio/wav_source.hpp>
#include <campello_audio/low_pass_filter.hpp>
#include <campello_audio/high_pass_filter.hpp>
#include <campello_audio/echo_filter.hpp>
#include <campello_audio/reverb_filter.hpp>

using namespace systems::leal::campello_audio;

TEST(FilterConstruction, LowPassDefaults) {
    auto f = std::make_shared<LowPassFilter>();
    EXPECT_NE(f, nullptr);
}

TEST(FilterConstruction, HighPassDefaults) {
    auto f = std::make_shared<HighPassFilter>();
    EXPECT_NE(f, nullptr);
}

TEST(FilterConstruction, EchoDefaults) {
    auto f = std::make_shared<EchoFilter>();
    EXPECT_NE(f, nullptr);
}

TEST(FilterConstruction, ReverbDefaults) {
    auto f = std::make_shared<ReverbFilter>();
    EXPECT_NE(f, nullptr);
}

TEST(FilterAttach, AddToSource) {
    WavSource src;
    auto lpf = std::make_shared<LowPassFilter>(1000.0f, 1.0f);
    EXPECT_NO_THROW(src.addFilter(lpf, 0));
    EXPECT_NO_THROW(src.removeFilter(0));
}
