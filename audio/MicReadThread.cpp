/*=====================================================================
MicReadThread.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "MicReadThread.h"


#include "AudioEngine.h"
#include "../gui_client/ThreadMessages.h"
#include <utils/CircularBuffer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/ComObHandle.h>
#include <utils/ContainerUtils.h>
#include <utils/RuntimeCheck.h>
#include <networking/UDPSocket.h>
#include <networking/Networking.h>
#if defined(_WIN32)
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Mmreg.h>
#include <devpkey.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#endif
#include <opus.h>
#include <Timer.h>
#include "../rtaudio/RtAudio.h"


#if defined(_WIN32)
#define USE_RT_AUDIO 0
#else
#define USE_RT_AUDIO 1
#endif


#if defined(_WIN32)
static inline void throwOnError(HRESULT hres)
{
	if(FAILED(hres))
		throw glare::Exception("Error: " + PlatformUtils::COMErrorString(hres));
}
#endif


namespace glare
{


MicReadThread::MicReadThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<UDPSocket> udp_socket_, UID client_avatar_uid_, const std::string& server_hostname_, int server_port_, const std::string& input_device_name_)
:	out_msg_queue(out_msg_queue_), udp_socket(udp_socket_), client_avatar_uid(client_avatar_uid_), server_hostname(server_hostname_), server_port(server_port_), input_device_name(input_device_name_)
{
}


MicReadThread::~MicReadThread()
{
}


#if USE_RT_AUDIO
static int rtAudioCallback(void* output_buffer, void* input_buffer, unsigned int n_buffer_frames, double stream_time, RtAudioStreamStatus status, void* user_data)
{
	MicReadThread* mic_read_thread = (MicReadThread*)user_data;

	// The RTAudio input stream is created with RTAUDIO_FLOAT32 and nChannels = 1, so input_buffer should just be an array of uninterleaved floats.
	{
		Lock lock(mic_read_thread->buffer_mutex);

		ContainerUtils::append(mic_read_thread->callback_buffer, /*data=*/(const float*)input_buffer, /*size=*/n_buffer_frames);
	}

	return 0;
}
#endif


void MicReadThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("MicReadThread");

	conPrint("MicReadThread started...");


	try
	{
#if defined(_WIN32) && !USE_RT_AUDIO
		//----------------------------- Initialise loopback or microphone Audio capture ------------------------------------
		// See https://learn.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream

		const bool capture_loopback = false; // if false, capture microphone

		ComObHandle<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(
			__uuidof(MMDeviceEnumerator),
			NULL,
			CLSCTX_ALL, 
			__uuidof(IMMDeviceEnumerator),
			(void**)&enumerator.ptr);
		throwOnError(hr);

		ComObHandle<IMMDevice> device;
		if(input_device_name == "Default")
		{
			hr = enumerator->GetDefaultAudioEndpoint(
				eCapture, // dataFlow
				eConsole, 
				&device.ptr);
			throwOnError(hr);
		}
		else
		{
			// Iterate over endpoints, get ID of endpoint whose name matches input_device_name.

			ComObHandle<IMMDeviceCollection> collection;
			hr = enumerator->EnumAudioEndpoints(
				capture_loopback ? eRender : eCapture, DEVICE_STATE_ACTIVE,
				&collection.ptr);
			throwOnError(hr);

			UINT count;
			hr = collection->GetCount(&count);
			throwOnError(hr);

			std::wstring use_device_id;
			for(UINT i = 0; i < count; i++)
			{
				// Get pointer to endpoint number i.
				ComObHandle<IMMDevice> endpoint;
				hr = collection->Item(i, &endpoint.ptr);
				throwOnError(hr);

				// Get the endpoint ID string.
				LPWSTR endpoint_id = NULL;
				hr = endpoint->GetId(&endpoint_id);
				throwOnError(hr);

				ComObHandle<IPropertyStore> props;
				hr = endpoint->OpenPropertyStore(STGM_READ, &props.ptr);
				throwOnError(hr);

				// Get the endpoint's friendly-name property.
				PROPVARIANT endpoint_name;
				PropVariantInit(&endpoint_name); // Initialize container for property value.
				hr = props->GetValue(PKEY_Device_FriendlyName, &endpoint_name); 
				throwOnError(hr);

				// conPrint("Audio endpoint " + toString(i) + ": \"" + StringUtils::WToUTF8String(endpoint_name.pwszVal) + "\" (" + StringUtils::WToUTF8String(endpoint_id) + ")");

				if(input_device_name == StringUtils::WToUTF8String(endpoint_name.pwszVal))
					use_device_id = endpoint_id;

				CoTaskMemFree(endpoint_id);
				PropVariantClear(&endpoint_name);
			}

			if(use_device_id.empty())
				throw glare::Exception("Could not find device '" + input_device_name + "' (it may have been removed)");

			hr = enumerator->GetDevice(use_device_id.c_str(), &device.ptr);
			throwOnError(hr);
		}

		// Get friendly name of the device we chose
		std::string selected_dev_name;
		{
			ComObHandle<IPropertyStore> props;
			hr = device->OpenPropertyStore(STGM_READ, &props.ptr);
			throwOnError(hr);

			PROPVARIANT endpoint_name;
			PropVariantInit(&endpoint_name); // Initialize container for property value.
			hr = props->GetValue(PKEY_Device_FriendlyName, &endpoint_name); // Get the endpoint's friendly-name property.
			throwOnError(hr);

			selected_dev_name = StringUtils::WToUTF8String(endpoint_name.pwszVal);

			PropVariantClear(&endpoint_name);
		}

		out_msg_queue->enqueue(new LogMessage("Chose audio input device: '" + selected_dev_name + "'."));

		ComObHandle<IAudioClient> audio_client;
		hr = device->Activate(
			__uuidof(IAudioClient), 
			CLSCTX_ALL,
			NULL, 
			(void**)&audio_client.ptr);
		throwOnError(hr);

		WAVEFORMATEXTENSIBLE* mix_format = NULL;
		hr = audio_client->GetMixFormat((WAVEFORMATEX**)&mix_format);
		throwOnError(hr);

		if(mix_format->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
			throw glare::Exception("wFormatTag was not WAVE_FORMAT_EXTENSIBLE");

		WAVEFORMATEXTENSIBLE format;
		std::memcpy(&format, mix_format, sizeof(WAVEFORMATEXTENSIBLE));

		const REFERENCE_TIME hnsRequestedDuration = 10000000; // REFERENCE_TIME time units per second

		hr = audio_client->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			capture_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, // streamflags - note the needed AUDCLNT_STREAMFLAGS_LOOPBACK
			hnsRequestedDuration,
			0,
			(WAVEFORMATEX*)&format,
			NULL);
		throwOnError(hr);

		// Currently we only handle float formats
		if(format.SubFormat != MFAudioFormat_Float)
			throw glare::Exception("Subformat was not MFAudioFormat_Float");

		if(format.Format.wBitsPerSample != 32)
			throw glare::Exception("wBitsPerSample was not 32");

		const uint32 capture_sampling_rate = format.Format.nSamplesPerSec;
		const uint32 num_channels = format.Format.nChannels;

		ComObHandle<IAudioCaptureClient> capture_client;
		hr = audio_client->GetService(
			__uuidof(IAudioCaptureClient),
			(void**)&capture_client.ptr);
		if(hr == AUDCLNT_E_WRONG_ENDPOINT_TYPE)
			conPrint("ERROR: AUDCLNT_E_WRONG_ENDPOINT_TYPE");
		throwOnError(hr);

		out_msg_queue->enqueue(new LogMessage("Starting listening on device: '" + selected_dev_name + "', capture sampling rate: " + toString(capture_sampling_rate) + " hz, num channels: " + toString(num_channels)));

		hr = audio_client->Start();  // Start recording.
		throwOnError(hr);

		//----------------------------------------------------------------------------------------------------------------------

#else

		//--------------------------------- Use RTAudio to do the audio capture ------------------------------------------------
#if _WIN32
		const RtAudio::Api rtaudio_api = RtAudio::WINDOWS_DS;
#elif defined(OSX)
		const RtAudio::Api rtaudio_api = RtAudio::MACOSX_CORE;
#else // else linux:
		const RtAudio::Api rtaudio_api = RtAudio::LINUX_PULSE;
#endif

		RtAudio audio(rtaudio_api);

		unsigned int use_device_id = 0;
		if(input_device_name == "Default")
		{
			use_device_id = audio.getDefaultInputDevice();
		}
		else
		{
			const std::vector<unsigned int> device_ids = audio.getDeviceIds();

			for(size_t i=0; i<device_ids.size(); ++i)
			{
				const RtAudio::DeviceInfo info = audio.getDeviceInfo(device_ids[i]);
				if((info.inputChannels > 0) && info.name == input_device_name)
					use_device_id = device_ids[i];
			}
		}

		if(use_device_id == 0)
			throw glare::Exception("Could not find device '" + input_device_name + "' (it may have been removed)");

		const std::string selected_dev_name = audio.getDeviceInfo(use_device_id).name;
		out_msg_queue->enqueue(new LogMessage("Chose audio input device: '" + selected_dev_name + "'."));

		unsigned int desired_sample_rate = 48000;

		RtAudio::StreamParameters parameters;
		parameters.deviceId = use_device_id;
		parameters.nChannels = 1;
		parameters.firstChannel = 0;
		unsigned int buffer_frames = 256; // 256 sample frames. NOTE: might be changed by openStream() below.

		RtAudio::StreamOptions stream_options;
		stream_options.flags = RTAUDIO_MINIMIZE_LATENCY;

		RtAudioErrorType rtaudio_res = audio.openStream(/*outputParameters=*/NULL, /*input parameters=*/&parameters, RTAUDIO_FLOAT32, desired_sample_rate, &buffer_frames, rtAudioCallback, /*userdata=*/this, &stream_options);
		if(rtaudio_res != RTAUDIO_NO_ERROR)
			throw glare::Exception("Error opening audio stream: code: " + toString((int)rtaudio_res));

		const unsigned int capture_sampling_rate = audio.getStreamSampleRate(); // Get actual sample rate used.

		out_msg_queue->enqueue(new LogMessage("Starting listening on device: '" + selected_dev_name + "', capture sampling rate: " + toString(capture_sampling_rate) + " hz, num channels: 1"));

		rtaudio_res = audio.startStream();
		if(rtaudio_res != RTAUDIO_NO_ERROR)
			throw glare::Exception("Error opening audio stream: code: " + toString((int)rtaudio_res));
		//----------------------------------------------------------------------------------------------------------------------
#endif

		//-------------------------------------- Opus init --------------------------------------------------
		uint32 opus_sampling_rate = capture_sampling_rate;
		if(!((opus_sampling_rate == 8000) || (opus_sampling_rate == 12000) || (opus_sampling_rate == 16000) || (opus_sampling_rate == 24000) ||(opus_sampling_rate == 48000))) // Sampling rates Opus encoder supports.
			opus_sampling_rate = 48000; // We will resample to 48000 hz.

		int opus_error = 0;
		OpusEncoder* opus_encoder = opus_encoder_create(
			opus_sampling_rate, // sampling rate
			1, // channels
			OPUS_APPLICATION_VOIP, // application
			&opus_error
		);
		if(opus_error != OPUS_OK)
			throw glare::Exception("opus_encoder_create failed.");


		//const int ret = opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(512000));
		//if(ret != OPUS_OK)
		//	throw glare::Exception("opus_encoder_ctl failed.");
		//-------------------------------------- End Opus init --------------------------------------------------

		out_msg_queue->enqueue(new AudioStreamToServerStartedMessage(opus_sampling_rate));

		//-------------------------------------- UDP socket init --------------------------------------------------

		const std::vector<IPAddress> server_ips = Networking::doDNSLookup(server_hostname);
		const IPAddress server_ip = server_ips[0];


		std::vector<uint8> encoded_data(100000);

		std::vector<float> pcm_buffer;
		const size_t max_pcm_buffer_size = 48000;

		std::vector<float> resampled_pcm_buffer;

		std::vector<uint8> packet;

		uint32 seq_num = 0;

		Timer time_since_last_stream_to_server_msg_sent;

		//------------------------ Process audio output stream ------------------------
		while(die == 0) // Keep reading audio until we are told to quit
		{
			if(time_since_last_stream_to_server_msg_sent.elapsed() > 2.0)
			{
				// Re-send, in case other clients connect
				out_msg_queue->enqueue(new AudioStreamToServerStartedMessage(opus_sampling_rate));
				time_since_last_stream_to_server_msg_sent.reset();
			}


			while(die == 0) // Loop while there is data to be read immediately:
			{
				
#if defined(_WIN32) && !USE_RT_AUDIO
				PlatformUtils::Sleep(2);//(0.5 * 480.0 / 48000.0) * 1000);

				Timer timer;
				// Get the available data in the shared buffer.
				BYTE* p_data;
				uint32 num_frames_available;
				DWORD flags;
				hr = capture_client->GetBuffer(
					&p_data,
					&num_frames_available,
					&flags, NULL, NULL);

				//conPrint("GetBuffer took " + timer.elapsedString());

				if(hr == AUDCLNT_S_BUFFER_EMPTY)
				{
					//conPrint("AUDCLNT_S_BUFFER_EMPTY");
					break;
				}
				throwOnError(hr);

				//printVar(num_frames_available);

				const int frames_to_copy = myMin((int)max_pcm_buffer_size - (int)pcm_buffer.size(), (int)num_frames_available);
				const size_t write_index = pcm_buffer.size();
				pcm_buffer.resize(pcm_buffer.size() + frames_to_copy);
				assert(pcm_buffer.size() <= max_pcm_buffer_size);

				if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					//conPrint("Silent");

					for(int i=0; i<frames_to_copy; i++)
						pcm_buffer[write_index + i] = 0.f;
				}
				else
				{
					// Copy the available capture data to the audio sink.
					// Mix multiple channel audio data to a single channel.
					const float* const src_data = (const float*)p_data;

					if(num_channels == 1)
					{
						for(int i=0; i<frames_to_copy; i++)
							pcm_buffer[write_index + i] = src_data[i];
					}
					else if(num_channels == 2)
					{
						for(int i=0; i<frames_to_copy; i++)
						{
							const float left  = src_data[i*2 + 0];
							const float right = src_data[i*2 + 1];
							const float mixed = (left + right) * 0.5f;
							pcm_buffer[write_index + i] = mixed;
						}
					}
					else
					{
						for(int i=0; i<frames_to_copy; i++)
						{
							float sum = 0;
							for(uint32 c=0; c<num_channels; ++c)
							{
								sum += src_data[i*num_channels + c];
							}
							pcm_buffer[write_index + i] = sum * (1.f / num_channels);
						}
					}
				}
#else
				PlatformUtils::Sleep(2);

				{
					Lock lock(buffer_mutex);

					const int frames_to_copy = myMin((int)max_pcm_buffer_size - (int)pcm_buffer.size(), (int)callback_buffer.size());
					runtimeCheck(frames_to_copy >= 0);
					const size_t write_index = pcm_buffer.size();
					pcm_buffer.resize(pcm_buffer.size() + frames_to_copy);
					assert(pcm_buffer.size() <= max_pcm_buffer_size);

					for(int i=0; i<frames_to_copy; i++)
						pcm_buffer[write_index + i] = callback_buffer[i];

					//removeNItemsFromFront(callback_buffer, frames_to_copy);
					callback_buffer.clear();
				}
#endif

				// "To encode a frame, opus_encode() or opus_encode_float() must be called with exactly one frame (2.5, 5, 10, 20, 40 or 60 ms) of audio data:"
				// We will use 10ms frames.
				const size_t opus_samples_per_frame    = opus_sampling_rate    / 100;
				const size_t capture_samples_per_frame = capture_sampling_rate / 100;

				// Encode the PCM data with Opus.  Writes to encoded_data.
				size_t cur_i = 0;
				while((pcm_buffer.size() - cur_i) >= capture_samples_per_frame) // While there are at least samples_per_frame items in pcm_buffer.
				{
					if(opus_sampling_rate != capture_sampling_rate)
					{
						// Resample
						resampled_pcm_buffer.resize(opus_samples_per_frame);
						
						//const int ratio = 48000 / stream_info->sampling_rate;
						const float ratio = (float)capture_sampling_rate / (float)opus_sampling_rate;

						runtimeCheck(cur_i + capture_samples_per_frame - 1 < pcm_buffer.size());
						for(int i=0; i<opus_samples_per_frame; ++i)
							resampled_pcm_buffer[i] = pcm_buffer[cur_i + myClamp((int)(i * ratio), /*lower bound=*/0, /*upper bound=*/(int)capture_samples_per_frame - 1)];
					}

					const opus_int32 encoded_B = opus_encode_float(
						opus_encoder,
						(opus_sampling_rate != capture_sampling_rate) ? resampled_pcm_buffer.data() : &pcm_buffer[cur_i],
						(int)opus_samples_per_frame, // frame size (in samples)
						encoded_data.data(), // data
						(opus_int32)encoded_data.size() // max_data_bytes
					);
					//printVar(encoded_B);
					if(encoded_B < 0)
						throw glare::Exception("opus_encode failed: " + toString(encoded_B));

					cur_i += capture_samples_per_frame;

					// Form packet
					const size_t header_size_B = sizeof(uint32) * 3;
					packet.resize(header_size_B + encoded_B);

					// Write packet type (1 = voice)
					const uint32 packet_type = 1;
					std::memcpy(packet.data(), &packet_type, sizeof(uint32));

					// Write client UID
					const uint32 client_avatar_uid_uint32 = (uint32)client_avatar_uid.value();
					std::memcpy(packet.data() + 4, &client_avatar_uid_uint32, sizeof(uint32));

					// Write sequence number
					std::memcpy(packet.data() + 8, &seq_num, sizeof(uint32));
					seq_num++;

					if(encoded_B > 0)
						std::memcpy(packet.data() + header_size_B, encoded_data.data(), encoded_B);

					// Send packet to server
					udp_socket->sendPacket(packet.data(), packet.size(), server_ip, server_port);
				}

				// Remove first cur_i samples from pcm_buffer, copy remaining data to front of buffer
				ContainerUtils::removeNItemsFromFront(pcm_buffer, cur_i);

#if defined(_WIN32) && !USE_RT_AUDIO
				hr = capture_client->ReleaseBuffer(num_frames_available);
				throwOnError(hr);
#endif
			}
			//-------------------------------------------------------
		}

		opus_encoder_destroy(opus_encoder);

#if USE_RT_AUDIO
		if(audio.isStreamOpen())
		{
			if(audio.isStreamRunning())
				audio.stopStream();

			audio.closeStream();
		}
#endif
	}
	catch(glare::Exception& e)
	{
		conPrint("MicReadThread::doRun() Excep: " + e.what());
		out_msg_queue->enqueue(new LogMessage("MicReadThread: " + e.what()));
	}

	conPrint("MicReadThread finished.");
}


} // end namespace glare
