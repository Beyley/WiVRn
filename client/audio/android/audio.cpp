/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "audio.h"
#include "wivrn_client.h"
#include "xr/instance.h"

#ifdef QUERY_AUDIO_JAVA
#include "application.h"
#include "jnipp.h"
#endif

#include "spdlog/spdlog.h"
#include <aaudio/AAudio.h>
#include <cassert>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void wivrn::android::audio::exit()
{
	exiting = true;
	if (speaker)
	{
		speaker_stop_ack.wait(false);
		AAudioStream_requestStop(speaker);
	}
	if (microphone)
	{
		microphone_stop_ack.wait(false);
		AAudioStream_requestStop(microphone);
	}
}

int32_t wivrn::android::audio::speaker_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
	{
		self->speaker_stop_ack = true;
		self->speaker_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	while (num_frames != 0)
	{
		// remaining bytes in existing buffer
		ptrdiff_t tmp_remain = self->speaker_tmp.payload.size_bytes();
		// limit to requested frames
		tmp_remain = std::min<ptrdiff_t>(tmp_remain, num_frames * frame_size);
		if (tmp_remain)
		{
			memcpy(audio_data, self->speaker_tmp.payload.data(), tmp_remain);
			self->speaker_tmp.payload = self->speaker_tmp.payload.subspan(tmp_remain);
			audio_data += tmp_remain;
			num_frames -= tmp_remain / frame_size;
		}
		else
		{
			auto tmp = self->output_buffer.read();
			if (not tmp)
			{
				memset(audio_data, 0, num_frames * frame_size);
				return AAUDIO_CALLBACK_RESULT_CONTINUE;
			}
			self->speaker_tmp = std::move(*tmp);
		}
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

int32_t wivrn::android::audio::microphone_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
	{
		self->microphone_stop_ack= true;
		self->microphone_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	xrt::drivers::wivrn::audio_data packet{
	        .timestamp = self->instance.now(),
	        .payload = std::span(audio_data, frame_size * num_frames),
	};

	self->session.send_control(packet);

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

wivrn::android::audio::audio(const xrt::drivers::wivrn::to_headset::audio_stream_description & desc, wivrn_session & session, xr::instance & instance) :
        session(session), instance(instance)
{
	AAudioStreamBuilder * builder;
	aaudio_result_t result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK)
	{
		throw std::runtime_error(std::string("Cannot create stream builder: ") + AAudio_convertResultToText(result));
	}

	if (desc.microphone)
	{
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
		AAudioStreamBuilder_setSampleRate(builder, desc.microphone->sample_rate);
		AAudioStreamBuilder_setChannelCount(builder, desc.microphone->num_channels);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

		AAudioStreamBuilder_setDataCallback(builder, &microphone_data_cb, this);

		result = AAudioStreamBuilder_openStream(builder, &microphone);
		if (result != AAUDIO_OK)
			spdlog::error("Cannot create input stream: {}", AAudio_convertResultToText(result));

		result = AAudioStream_requestStart(microphone);
		if (result == AAUDIO_OK)
			spdlog::info("Microphone stream started");
		else
		{
			AAudioStream_close(microphone);
			spdlog::warn("Microphone stream failed to start: {}", AAudio_convertResultToText(result));
		}
	}

	if (desc.speaker)
	{
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStreamBuilder_setSampleRate(builder, desc.speaker->sample_rate);
		AAudioStreamBuilder_setChannelCount(builder, desc.speaker->num_channels);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

		AAudioStreamBuilder_setDataCallback(builder, &speaker_data_cb, this);

		result = AAudioStreamBuilder_openStream(builder, &speaker);
		if (result != AAUDIO_OK)
			spdlog::error("Cannot create output stream: {}", AAudio_convertResultToText(result));

		AAudioStream_requestStart(speaker);
		if (result == AAUDIO_OK)
			spdlog::info("Speaker stream started");
		else
		{
			AAudioStream_close(speaker);
			spdlog::warn("Speaker stream failed to start: {}", AAudio_convertResultToText(result));
		}
	}
	AAudioStreamBuilder_delete(builder);
}

wivrn::android::audio::~audio()
{
	exit();

	for (auto stream: {speaker, microphone})
	{
		if (stream)
		{
			AAudioStream_close(stream);
		}
	}
}

void wivrn::android::audio::operator()(xrt::drivers::wivrn::audio_data && data)
{
	output_buffer.write(std::move(data));
}

void wivrn::android::audio::get_audio_description(xrt::drivers::wivrn::from_headset::headset_info_packet & info)
{
#ifdef QUERY_AUDIO_JAVA
	 * Incomplete code: query properties through java API
	jni::object<""> act(application::native_app()->activity->clazz);

	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");
	auto audio_service_id = ctx.klass().field<jni::string>("AUDIO_SERVICE");
	auto audio_manager = ctx.call<jni::object<"java/lang/Object">>("getSystemService", audio_service_id);

	auto devices = audio_manager.call<jni::array<jni::object<"android/media/AudioDeviceInfo">>>("getDevices", jni::Int(3));
	spdlog::info("enumerate audio devices");
	for (int i = 0 ; i < devices.size() ; ++i)
	{
		spdlog::info("{}", i);
		auto device = devices[i];
		auto id = device.call<jni::Int>("getId");
		spdlog::info("\tId: {}", id.value);
		auto type = device.call<jni::Int>("getType");
		spdlog::info("\tType: {}", type.value);
		auto address = device.call<jni::string>("getAddress");
		spdlog::info("\tAddress: {}", std::string(address));
		//auto name = device.call<jni::object<"java.lang.CharSequence">>("getProductName").call<jni::string>("toString");
		//spdlog::info("\tName: {}", std::string(name));
	}
#endif

	AAudioStreamBuilder * builder;

	aaudio_result_t result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK)
		return;

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStream * stream;
	result = AAudioStreamBuilder_openStream(builder, &stream);

	if (result == AAUDIO_OK)
	{
		info.speaker = {
		        .num_channels = (uint8_t)AAudioStream_getChannelCount(stream),
		        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream)};

		AAudioStream_close(stream);
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	result = AAudioStreamBuilder_openStream(builder, &stream);

	if (result == AAUDIO_OK)
	{
		info.microphone = {
		        .num_channels = 1, // Some headsets report 2 channels but then fail
		        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream),
		};

		AAudioStream_close(stream);
	}

	AAudioStreamBuilder_delete(builder);
}
