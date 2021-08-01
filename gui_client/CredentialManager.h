/*=====================================================================
CredentialManager.h
-------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include <map>
#include <string>
class QSettings;


struct DomainCredentials
{
	std::string domain;
	std::string username;
	std::string encrypted_password;
};


/*=====================================================================
CredentialManager
-----------------
Stores usernames and passwords for different server domains
=====================================================================*/
class CredentialManager
{
public:
	void loadFromSettings(QSettings& settings);
	void saveToSettings(QSettings& settings);

	std::string getUsernameForDomain(const std::string& domain); // Returns empty string if no stored username for domain
	std::string getDecryptedPasswordForDomain(const std::string& domain); // Returns empty string if no stored password for domain

	void setDomainCredentials(const std::string& domain, const std::string& username, const std::string& plaintext_password);

private:
	static const std::string decryptPassword(const std::string& cyphertext);
	static const std::string encryptPassword(const std::string& password);

	std::map<std::string, DomainCredentials> credentials;
};
