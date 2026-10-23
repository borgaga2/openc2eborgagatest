#include "SDLMixerBackend.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>

TEST(SDLMixerBackend, StoppedCallbacksDuringChannelUpdates) {
	// A short, silent 8-bit mono WAV. No installed game assets are needed.
	std::array<uint8_t, 76> wav = {
		'R', 'I', 'F', 'F', 68, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0, 1, 0, 8, 0,
		'd', 'a', 't', 'a', 32, 0, 0, 0};
	std::fill(wav.begin() + 44, wav.end(), 128);
	auto* audio = SDLMixerBackend::get_instance();
	// Repeat initialization too, including shutdown with a looping track active.
	for (int cycle = 0; cycle < 2; ++cycle) {
		audio->init();
		// A zero-duration fade invokes the stopped callback synchronously. It
		// must neither reacquire our held mutex nor destroy SDL's in-use track.
		auto immediate = audio->play_wav_data("immediate-stop-test", wav.data(), wav.size(), true);
		EXPECT_EQ(audio->audio_channel_get_state(immediate), AUDIO_PLAYING);
		audio->audio_channel_fade_out(immediate, 0);
		EXPECT_EQ(audio->audio_channel_get_state(immediate), AUDIO_STOPPED);
		std::vector<AudioChannel> channels;
		for (int i = 0; i < 512; ++i) {
			channels.push_back(audio->play_wav_data("callback-test", wav.data(), wav.size(), i % 4 == 0));
			EXPECT_TRUE(channels.back());
			if (i % 4 == 0)
				EXPECT_EQ(audio->audio_channel_get_state(channels.back()), AUDIO_PLAYING);
			for (size_t j = channels.size() > 8 ? channels.size() - 8 : 0; j < channels.size(); ++j) {
				// These calls take SDL's mixer lock while the audio thread can be
				// finishing another channel and entering the stopped callback.
				audio->audio_channel_set_volume(channels[j], 0.5f);
				audio->audio_channel_set_pan(channels[j], 0.25f);
				audio->audio_channel_get_state(channels[j]);
			}
			if (channels.size() > 8) {
				audio->audio_channel_fade_out(channels[channels.size() - 9], 1);
			}
			SDL_Delay(1);
		}
		for (auto channel : channels)
			audio->audio_channel_stop(channel);
		SDL_Delay(100);
		for (auto channel : channels)
			EXPECT_EQ(audio->audio_channel_get_state(channel), AUDIO_STOPPED);
		auto looping = audio->play_wav_data("shutdown-test", wav.data(), wav.size(), true);
		EXPECT_EQ(audio->audio_channel_get_state(looping), AUDIO_PLAYING);
		audio->shutdown();
		EXPECT_EQ(audio->audio_channel_get_state(looping), AUDIO_STOPPED);
	}
}
