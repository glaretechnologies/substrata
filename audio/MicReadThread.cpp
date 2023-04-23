/*=====================================================================
MicReadThread.cpp
-----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "MicReadThread.h"


#include "AudioEngine.h"
#include <utils/CircularBuffer.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/ComObHandle.h>
#include <networking/UDPSocket.h>
#include <networking/Networking.h>
#if defined(_WIN32)
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Mmreg.h>
#include <devpkey.h>
#include <Functiondiscoverykeys_devpkey.h>
#endif
#include <opus.h>
#include <Timer.h>


#if defined(_WIN32)
static inline void throwOnError(HRESULT hres)
{
	if(FAILED(hres))
		throw glare::Exception("Error: " + PlatformUtils::COMErrorString(hres));
}
#endif


namespace glare
{


MicReadThread::MicReadThread(ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue_, Reference<UDPSocket> udp_socket_, UID client_avatar_uid_, const std::string& server_hostname_, int server_port_)
:	out_msg_queue(out_msg_queue_), udp_socket(udp_socket_), client_avatar_uid(client_avatar_uid_), server_hostname(server_hostname_), server_port(server_port_)
{
}


MicReadThread::~MicReadThread()
{
}


void MicReadThread::doRun()
{
	PlatformUtils::setCurrentThreadNameIfTestsEnabled("MicReadThread");

#if defined(_WIN32)

	try
	{
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

		IMMDeviceCollection* pCollection = NULL;
		hr = enumerator->EnumAudioEndpoints(
			capture_loopback ? eRender : eCapture, DEVICE_STATE_ACTIVE,
			&pCollection);
		throwOnError(hr);

		UINT count;
		hr = pCollection->GetCount(&count);
		throwOnError(hr);

		std::wstring use_device_id;

		// Each loop iteration prints the name of an endpoint device.
		for(UINT i = 0; i < count; i++)
		{
			// Get pointer to endpoint number i.
			ComObHandle<IMMDevice> endpoint;
			hr = pCollection->Item(i, &endpoint.ptr);
			throwOnError(hr);

			// Get the endpoint ID string.
			LPWSTR pwszID = NULL;
			hr = endpoint->GetId(&pwszID);
			throwOnError(hr);

			if(i == 0) // NOTE: Change this to select audio device (for loopback headphone selection)
				use_device_id = pwszID;

			ComObHandle<IPropertyStore> props;
			hr = endpoint->OpenPropertyStore(
				STGM_READ, &props.ptr);
			throwOnError(hr);

			PROPVARIANT varName;
			PropVariantInit(&varName); // Initialize container for property value.

			// Get the endpoint's friendly-name property.
			hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
			throwOnError(hr);

			// Print endpoint friendly name and endpoint ID.
			conPrint("Endpoint " + toString((int)i) + ": \"" + StringUtils::WToUTF8String(varName.pwszVal) + "\" (" + StringUtils::WToUTF8String(pwszID) + ")\n");

			CoTaskMemFree(pwszID);
			PropVariantClear(&varName);
		}

		ComObHandle<IMMDevice> device;
		enumerator->GetDevice(use_device_id.c_str(), &device.ptr);
		throwOnError(hr);

		//hr = pEnumerator->GetDefaultAudioEndpoint(
		//	eCapture, // dataFlow
		//	eConsole, 
		//	&pDevice);
		//THROW_ON_ERROR(hr)

		ComObHandle<IAudioClient> audio_client;
		hr = device->Activate(
			//IID_IAudioClient, 
			__uuidof(IAudioClient), 
			CLSCTX_ALL,
			NULL, 
			(void**)&audio_client.ptr);
		throwOnError(hr);

		LPWSTR device_id = NULL;
		hr = device->GetId(&device_id);
		throwOnError(hr);

		IPropertyStore* properties = NULL;
		hr = device->OpenPropertyStore(STGM_READ, &properties);
		throwOnError(hr);

		PROPVARIANT prop_val;
		PropVariantInit(&prop_val);
		properties->GetValue(PKEY_Device_FriendlyName, &prop_val);

		WAVEFORMATEXTENSIBLE* format = NULL;
		hr = audio_client->GetMixFormat((WAVEFORMATEX**)&format);
		throwOnError(hr);

		//printVar((int)format->Format.nSamplesPerSec);

		const REFERENCE_TIME hnsRequestedDuration = 10000000; // REFERENCE_TIME time units per second

		hr = audio_client->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			capture_loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, // streamflags - note the needed AUDCLNT_STREAMFLAGS_LOOPBACK
			hnsRequestedDuration,
			0,
			(WAVEFORMATEX*)format,
			NULL);
		throwOnError(hr);

		const uint32 sampling_rate = (uint32)((WAVEFORMATEX*)format)->nSamplesPerSec;
		printVar(sampling_rate);

		// Get the size of the allocated buffer.
		UINT32 bufferFrameCount;
		hr = audio_client->GetBufferSize(&bufferFrameCount);
		throwOnError(hr);

		ComObHandle<IAudioCaptureClient> capture_client;
		hr = audio_client->GetService(
			__uuidof(IAudioCaptureClient),
			(void**)&capture_client.ptr);
		if(hr == AUDCLNT_E_WRONG_ENDPOINT_TYPE)
			conPrint("ERROR: AUDCLNT_E_WRONG_ENDPOINT_TYPE");
		throwOnError(hr);

		hr = audio_client->Start();  // Start recording.
		throwOnError(hr);

		//----------------------------------------------------------------------------------------

		//-------------------------------------- Opus init --------------------------------------------------

		// TODO: resample instead of throwing an error, if sampling rate is not supported.
		if(!((sampling_rate == 8000) || (sampling_rate == 12000) || (sampling_rate == 16000) || (sampling_rate == 24000) ||(sampling_rate == 48000))) // Sampling rates Opus encoder supports.
			throw glare::Exception("Microphone device used an unsupported sampling rate: " + toString(sampling_rate) + " hz.");

		int opus_error = 0;
		OpusEncoder* opus_encoder = opus_encoder_create(
			sampling_rate, // sampling rate
			1, // channels
			OPUS_APPLICATION_VOIP, // application
			&opus_error
		);
		if(opus_error != OPUS_OK)
			throw glare::Exception("opus_encoder_create failed.");


		//const int ret = opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(512000));
		//if(ret != OPUS_OK)
		//	throw glare::Exception("opus_encoder_ctl failed.");
		//-------------------------------------- Opus init --------------------------------------------------

		out_msg_queue->enqueue(new AudioStreamToServerStartedMessage(sampling_rate));

		//-------------------------------------- UDP socket init --------------------------------------------------

		const std::vector<IPAddress> server_ips = Networking::doDNSLookup(server_hostname);
		const IPAddress server_ip = server_ips[0];


		std::vector<uint8> encoded_data(100000);

		std::vector<float> pcm_buffer(48000);
		size_t write_index = 0; // Write index in to pcm_buffer

		std::vector<uint8> packet;

		uint32 seq_num = 0;

		Timer time_since_last_stream_to_server_msg_sent;

		//------------------------ Process audio output stream ------------------------
		while(die == 0) // Keep reading audio until we are told to quit
		{
			if(time_since_last_stream_to_server_msg_sent.elapsed() > 2.0)
			{
				// Re-send, in case other clients connect
				out_msg_queue->enqueue(new AudioStreamToServerStartedMessage(sampling_rate));
				time_since_last_stream_to_server_msg_sent.reset();
			}


			while(die == 0) // Loop while there is data to be read immediately:
			{
				// Get the available data in the shared buffer.
				BYTE* p_data;
				uint32 num_frames_available;
				DWORD flags;
				hr = capture_client->GetBuffer(
					&p_data,
					&num_frames_available,
					&flags, NULL, NULL);

				if(hr == AUDCLNT_S_BUFFER_EMPTY)
					break;
				throwOnError(hr);

				const int frames_to_copy = myMin((int)pcm_buffer.size() - (int)write_index, (int)num_frames_available);

				if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					// conPrint("Silent");

					for(int i=0; i<frames_to_copy; i++)
						pcm_buffer[write_index++] = 0.f;
				}
				else
				{
					// Copy the available capture data to the audio sink.
					const float* const src_data = (const float*)p_data;

					for(int i=0; i<frames_to_copy; i++)
					{
						const float left  = src_data[i*2 + 0];
						const float right = src_data[i*2 + 1];
						const float mixed = (left + right) * 0.5f;
						pcm_buffer[write_index++] = mixed;
					}
				}

				// "To encode a frame, opus_encode() or opus_encode_float() must be called with exactly one frame (2.5, 5, 10, 20, 40 or 60 ms) of audio data:"
				// We will use 10ms frames.
				const size_t samples_per_frame = sampling_rate / 100;

				// Encode the PCM data with Opus.  Writes to encoded_data.
				while(write_index >= samples_per_frame)
				{
					const opus_int32 encoded_B = opus_encode_float(
						opus_encoder,
						pcm_buffer.data(),
						(int)samples_per_frame, // frame size (in samples)
						encoded_data.data(), // data
						(opus_int32)encoded_data.size() // max_data_bytes
					);
					//printVar(encoded_B);
					if(encoded_B < 0)
						throw glare::Exception("opus_encode failed: " + toString(encoded_B));

					// Remove first samples_per_frame samples from pcm_buffer, copy remaining data to front of buffer
					for(size_t z=samples_per_frame; z<write_index; ++z)
						pcm_buffer[z - samples_per_frame] = pcm_buffer[z];

					write_index -= samples_per_frame;

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


				hr = capture_client->ReleaseBuffer(num_frames_available);
				throwOnError(hr);
			}
			//-------------------------------------------------------
		}


		opus_encoder_destroy(opus_encoder);
	}
	catch(glare::Exception& e)
	{
		conPrint("MicReadThread::doRun() Excep: " + e.what());
	}

#else // else if !defined(_WIN32)

	conPrint("Microphone reading only supported on Windows currently.");

#endif
}


} // end namespace glare
