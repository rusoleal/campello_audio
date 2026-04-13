#include <gtest/gtest.h>
#include <campello_audio/constants/attenuation_model.hpp>
#include <campello_audio/constants/wave_form.hpp>
#include <campello_audio/constants/loop_mode.hpp>
#include <campello_audio/constants/resample_quality.hpp>

using namespace systems::leal::campello_audio;

TEST(AttenuationModel, Values) {
    EXPECT_NE(AttenuationModel::None,        AttenuationModel::Linear);
    EXPECT_NE(AttenuationModel::Inverse,     AttenuationModel::Exponential);
    EXPECT_EQ(AttenuationModel::None,        AttenuationModel::None);
}

TEST(WaveForm, Values) {
    EXPECT_NE(WaveForm::Sine,     WaveForm::Square);
    EXPECT_NE(WaveForm::Saw,      WaveForm::Triangle);
    EXPECT_NE(WaveForm::Noise,    WaveForm::Sine);
    EXPECT_EQ(WaveForm::Triangle, WaveForm::Triangle);
}

TEST(LoopMode, Values) {
    EXPECT_NE(LoopMode::None,     LoopMode::Loop);
    EXPECT_NE(LoopMode::Loop,     LoopMode::PingPong);
    EXPECT_EQ(LoopMode::PingPong, LoopMode::PingPong);
}

TEST(ResampleQuality, Values) {
    EXPECT_NE(ResampleQuality::Point,     ResampleQuality::Linear);
    EXPECT_NE(ResampleQuality::Linear,    ResampleQuality::CatmullRom);
    EXPECT_EQ(ResampleQuality::CatmullRom, ResampleQuality::CatmullRom);
}
