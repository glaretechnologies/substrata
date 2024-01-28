/*=====================================================================
CredentialManager.cpp
---------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "CredentialManager.h"


#include "../qt/QtUtils.h"
#include <AESEncryption.h>
#include <Base64.h>
#include <Exception.h>
#include <QtCore/QSettings>


#if USE_QT // TEMP HACK REFACTOR TODO
void CredentialManager::loadFromSettings(QSettings& settings)
{
	credentials.clear();

	// See if we have the old LoginDialog/username key
	if(settings.contains("LoginDialog/username"))
	{
		DomainCredentials cred;
		cred.domain = "substrata.info";
		cred.username = QtUtils::toStdString(settings.value("LoginDialog/username").toString());
		cred.encrypted_password = QtUtils::toStdString(settings.value("LoginDialog/password").toString());
		credentials[cred.domain] = cred;

		// Clear it
		settings.remove("LoginDialog/username");
		settings.remove("LoginDialog/password");
	}
		

	const int size = settings.beginReadArray("credentials");

	for(int i = 0; i < size; ++i)
	{
		settings.setArrayIndex(i);
		DomainCredentials cred;
		cred.domain = QtUtils::toStdString(settings.value("domain").toString());
		cred.username = QtUtils::toStdString(settings.value("username").toString());
		cred.encrypted_password = QtUtils::toStdString(settings.value("encrypted_password").toString());

		credentials[cred.domain] = cred;
	}
	settings.endArray();
}


void CredentialManager::saveToSettings(QSettings& settings)
{
	// See https://doc.qt.io/qt-5/qsettings.html#beginWriteArray
	settings.beginWriteArray("credentials");

	int i = 0;
	for(auto it = credentials.begin(); it != credentials.end(); ++it)
	{
		DomainCredentials& cred = it->second;

		settings.setArrayIndex(i);
		settings.setValue("domain", QtUtils::toQString(cred.domain));
		settings.setValue("username", QtUtils::toQString(cred.username));
		settings.setValue("encrypted_password", QtUtils::toQString(cred.encrypted_password));

		i++;
	}
	settings.endArray();
}
#endif


std::string CredentialManager::getUsernameForDomain(const std::string& domain)
{
	auto res = credentials.find(domain);
	if(res != credentials.end())
		return res->second.username;
	else
		return std::string();
}


std::string CredentialManager::getDecryptedPasswordForDomain(const std::string& domain)
{
	auto res = credentials.find(domain);
	if(res != credentials.end())
		return decryptPassword(res->second.encrypted_password);
	else
		return std::string();
}


void CredentialManager::setDomainCredentials(const std::string& domain, const std::string& username, const std::string& plaintext_password)
{
	DomainCredentials cred;
	cred.domain = domain;
	cred.username = username;
	cred.encrypted_password = encryptPassword(plaintext_password);

	credentials[cred.domain] = cred;
}


const std::string CredentialManager::decryptPassword(const std::string& cyphertext_base64)
{
	try
	{
		// Decode base64 to raw bytes.
		std::vector<unsigned char> cyphertex_binary;
		Base64::decode(cyphertext_base64, cyphertex_binary);

		// AES decrypt
		const std::string key = "RHJKEF_ZAepxYxYkrL3c6rWD";
		const std::string salt = "P6A3uZ4P";
		AESEncryption aes(key, salt);
		std::vector<unsigned char> plaintext_v = aes.decrypt(cyphertex_binary);

		// Convert to std::string
		std::string plaintext(plaintext_v.size(), '\0');
		if(!plaintext_v.empty())
			std::memcpy(&plaintext[0], plaintext_v.data(), plaintext_v.size());
		return plaintext;
	}
	catch(glare::Exception&)
	{
		return "";
	}
}


const std::string CredentialManager::encryptPassword(const std::string& password_plaintext)
{
	try
	{
		// Copy password to vector
		std::vector<unsigned char> plaintext_v(password_plaintext.size());
		if(!plaintext_v.empty())
			std::memcpy(&plaintext_v[0], password_plaintext.data(), password_plaintext.size());

		// AES encrypt
		const std::string key = "RHJKEF_ZAepxYxYkrL3c6rWD";
		const std::string salt = "P6A3uZ4P";
		AESEncryption aes(key, salt);
		std::vector<unsigned char> cyphertext = aes.encrypt(plaintext_v);

		// Encode in base64.
		std::string cyphertext_base64;
		Base64::encode(cyphertext.data(), cyphertext.size(), cyphertext_base64);

		return cyphertext_base64;
	}
	catch(glare::Exception&)
	{
		return "";
	}
}
