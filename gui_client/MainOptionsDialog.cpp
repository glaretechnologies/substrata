/*=====================================================================
MainOptionsDialog.cpp
---------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MainOptionsDialog.h"


#include "../qt/QtUtils.h"
#include "../qt/SignalBlocker.h"
#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>
#include <utils/ComObHandle.h>
#include <utils/PlatformUtils.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>
#include "../rtaudio/RtAudio.h"

#if defined(_WIN32)
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Mmreg.h>
#include <devpkey.h>
#include <Functiondiscoverykeys_devpkey.h>
#endif


#if defined(_WIN32)
static inline void throwOnError(HRESULT hres)
{
	if(FAILED(hres))
		throw glare::Exception("Error: " + PlatformUtils::COMErrorString(hres));
}
#endif


static std::vector<std::string> getAudioInputDeviceNames()
{
	std::vector<std::string> names;
	names.push_back("Default");

#if defined(_WIN32)

	try
	{
		//----------------------------- Enumerate audio input devices ------------------------------------
		// See https://learn.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream

		ComObHandle<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(
			__uuidof(MMDeviceEnumerator),
			NULL,
			CLSCTX_ALL, 
			__uuidof(IMMDeviceEnumerator),
			(void**)&enumerator.ptr);
		throwOnError(hr);

		ComObHandle<IMMDeviceCollection> collection;
		hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection.ptr);
		throwOnError(hr);

		UINT count;
		hr = collection->GetCount(&count);
		throwOnError(hr);

		// Each loop iteration prints the name of an endpoint device.
		for(UINT i = 0; i < count; i++)
		{
			// Get pointer to endpoint number i.
			ComObHandle<IMMDevice> endpoint;
			hr = collection->Item(i, &endpoint.ptr);
			throwOnError(hr);

			// Get the endpoint ID string.
			LPWSTR pwszID = NULL;
			hr = endpoint->GetId(&pwszID);
			throwOnError(hr);

			ComObHandle<IPropertyStore> props;
			hr = endpoint->OpenPropertyStore(STGM_READ, &props.ptr);
			throwOnError(hr);

			PROPVARIANT varName;
			PropVariantInit(&varName); // Initialize container for property value.

			// Get the endpoint's friendly-name property.
			hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
			throwOnError(hr);

			// Print endpoint friendly name and endpoint ID.
			//conPrint("Endpoint " + toString((int)i) + ": \"" + StringUtils::WToUTF8String(varName.pwszVal) + "\" (" + StringUtils::WToUTF8String(pwszID) + ")\n");
			names.push_back(StringUtils::WToUTF8String(varName.pwszVal));

			CoTaskMemFree(pwszID);
			PropVariantClear(&varName);
		}
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

#else // else if !defined(_WIN32):
	
	// Use RTAudio to do the query.
#if _WIN32
	const RtAudio::Api rtaudio_api = RtAudio::WINDOWS_DS;
#elif defined(OSX)
	const RtAudio::Api rtaudio_api = RtAudio::MACOSX_CORE;
#else // else linux:
	const RtAudio::Api rtaudio_api = RtAudio::LINUX_PULSE;
#endif

	RtAudio audio(rtaudio_api);

	const std::vector<unsigned int> device_ids = audio.getDeviceIds();

	for(size_t i=0; i<device_ids.size(); ++i)
	{
		const RtAudio::DeviceInfo info = audio.getDeviceInfo(device_ids[i]);
		if(info.inputChannels > 0)
			names.push_back(info.name);
	}

#endif

	return names;
}


MainOptionsDialog::MainOptionsDialog(QSettings* settings_)
:	settings(settings_)
{
	setupUi(this);

	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	connect(this->useCustomCacheDirCheckBox, SIGNAL(toggled(bool)), this, SLOT(customCacheDirCheckBoxChanged(bool)));

	this->customCacheDirFileSelectWidget->setSettingsKey("options/lastCacheDirFileSelectDir");
	this->customCacheDirFileSelectWidget->setType(FileSelectWidget::Type_Directory);

	const bool use_custom_cache_dir = settings->value(useCustomCacheDirKey(), /*default val=*/false).toBool();

	SignalBlocker::setValue(this->loadDistanceDoubleSpinBox,		settings->value(objectLoadDistanceKey(),	/*default val=*/2000.0).toDouble());
	SignalBlocker::setChecked(this->shadowsCheckBox,				settings->value(shadowsKey(),				/*default val=*/true).toBool());
	SignalBlocker::setChecked(this->MSAACheckBox,					settings->value(MSAAKey(),					/*default val=*/true).toBool());
	SignalBlocker::setChecked(this->bloomCheckBox,					settings->value(BloomKey(),					/*default val=*/true).toBool());
	
	const bool limit_FPS = settings->value(limitFPSKey(), /*default val=*/false).toBool();
	SignalBlocker::setChecked(this->limitFPSCheckBox,				limit_FPS);
	SignalBlocker::setValue(this->FPSLimitSpinBox,					settings->value(FPSLimitKey(),				/*default val=*/60).toInt());
	this->FPSLimitSpinBox->setEnabled(limit_FPS);

	SignalBlocker::setChecked(this->useCustomCacheDirCheckBox,		use_custom_cache_dir);
	
	this->customCacheDirFileSelectWidget->setFilename(				settings->value(customCacheDirKey()).toString());

	this->customCacheDirFileSelectWidget->setEnabled(use_custom_cache_dir);

	this->startLocationURLLineEdit->setText(						settings->value(startLocationURLKey()).toString());


	const auto dev_names = getAudioInputDeviceNames();
	for(size_t i=0; i<dev_names.size(); ++i)
		inputDeviceComboBox->addItem(QtUtils::toQString(dev_names[i]));

	inputDeviceComboBox->setCurrentIndex(inputDeviceComboBox->findText(settings->value(inputDeviceNameKey(), "Default").toString()));

	inputVolumeScaleHorizontalSlider->setValue(						settings->value(inputScaleFactorNameKey(), 100).toInt());
}


MainOptionsDialog::~MainOptionsDialog()
{}


void MainOptionsDialog::accepted()
{
	settings->setValue(objectLoadDistanceKey(),						this->loadDistanceDoubleSpinBox->value());
	settings->setValue(shadowsKey(),								this->shadowsCheckBox->isChecked());
	settings->setValue(MSAAKey(),									this->MSAACheckBox->isChecked());
	settings->setValue(BloomKey(),									this->bloomCheckBox->isChecked());
	settings->setValue(limitFPSKey(),								this->limitFPSCheckBox->isChecked());
	settings->setValue(FPSLimitKey(),								this->FPSLimitSpinBox->value());
	settings->setValue(useCustomCacheDirKey(),						this->useCustomCacheDirCheckBox->isChecked());

	settings->setValue(customCacheDirKey(),							this->customCacheDirFileSelectWidget->filename());

	settings->setValue(startLocationURLKey(),						this->startLocationURLLineEdit->text());

	settings->setValue(inputDeviceNameKey(),						this->inputDeviceComboBox->currentText());
	settings->setValue(inputScaleFactorNameKey(),					this->inputVolumeScaleHorizontalSlider->value());
}


void MainOptionsDialog::customCacheDirCheckBoxChanged(bool checked)
{
	this->customCacheDirFileSelectWidget->setEnabled(checked);
}


void MainOptionsDialog::on_inputDeviceComboBox_currentIndexChanged(int index)
{
	// Reset volume scale back to 100 since we changed device.
	inputVolumeScaleHorizontalSlider->setValue(100);
}


void MainOptionsDialog::on_inputVolumeScaleHorizontalSlider_valueChanged(int new_value)
{
	inputVolumeScaleLabel->setText(QtUtils::toQString(toString(new_value) + " %"));
}


std::string MainOptionsDialog::getInputDeviceName(const QSettings* settings)
{
	return QtUtils::toStdString(settings->value(MainOptionsDialog::inputDeviceNameKey(), "Default").toString());
}


float MainOptionsDialog::getInputScaleFactor(const QSettings* settings)
{
	return settings->value(MainOptionsDialog::inputScaleFactorNameKey(), /*default val=*/100).toInt() * 0.01f; // NOTE: stored in percent in settings
}


bool MainOptionsDialog::getShowMinimap(const QSettings* settings)
{
	return settings->value(showMinimapKey(), /*default val=*/true).toBool();
}
