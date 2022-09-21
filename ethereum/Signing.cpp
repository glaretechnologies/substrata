/*=====================================================================
Signing.cpp
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "Signing.h"


#include "RLP.h"
#include <ConPrint.h>
#include <Exception.h>
#include <StringUtils.h>
#include <Keccak256.h>
#include <ContainerUtils.h>
#include <CryptoRNG.h>
#include "../secp256k1-master/include/secp256k1.h"
#include "../secp256k1-master/include/secp256k1_recovery.h"


namespace Signing
{


static std::string hex_to_string(const std::string& s)
{
	const std::vector<unsigned char> v = StringUtils::convertHexToBinary(s);
	std::string res(v.size(), '\0');
	if(v.size() > 0)
		std::memcpy(&res[0], &v[0], v.size());
	return res;
}


struct Secp256k1Context
{
	Secp256k1Context() : ctx(NULL) {}
	~Secp256k1Context() { if(ctx != NULL) secp256k1_context_destroy(ctx); }
	secp256k1_context* ctx;
};


// Recover an Ethereum address from a signature and message
// Adapted from https://ethereum.stackexchange.com/questions/75903/ecrecover-in-c
EthAddress ecrecover(const std::string& sig, const std::string& msg) // hex-encoded sig, plain text msg
{
	std::string _sig = hex_to_string(sig.substr(2)); // strip 0x

	if(_sig.size() != 65)
		throw glare::Exception("Invalid sig size");

	int v = _sig[64];
	_sig = _sig.substr(0, 64);

	if(v > 3)
		v -= 27;

	Secp256k1Context context;
	context.ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	secp256k1_ecdsa_recoverable_signature rawSig;
	if(!secp256k1_ecdsa_recoverable_signature_parse_compact(context.ctx, &rawSig, (const unsigned char*)_sig.data(), v))
		throw glare::Exception("Cannot parse compact");

	const int HASH_SIZE = 32;
	uint8 hash[HASH_SIZE];

	const std::string wrapped_msg = std::string("\x19") + "Ethereum Signed Message:\n" + toString(msg.size()) + msg;

	Keccak256::hash(wrapped_msg, hash); // hash wrapped message

	secp256k1_pubkey rawPubkey;
	if(!secp256k1_ecdsa_recover(context.ctx, &rawPubkey, &rawSig, hash))
		throw glare::Exception("Cannot recover key");

	uint8_t pubkey[65];
	size_t biglen = 65;

	secp256k1_ec_pubkey_serialize(context.ctx, pubkey, &biglen, &rawPubkey, SECP256K1_EC_UNCOMPRESSED);

	std::vector<uint8> out(pubkey + 1, pubkey + 65);

	std::vector<uint8> pubkey_bytes_hash = Keccak256::hash(out);
	// pubkey_bytes_hash is 32 bytes.
	assert(pubkey_bytes_hash.size() == 32);

	return EthAddress(pubkey_bytes_hash.data() + 12); // We want the last 20 bytes
}


Signature sign(const std::vector<uint8>& msg_hash, const std::vector<uint8>& private_key)
{
	Secp256k1Context context;
	context.ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);

	secp256k1_ecdsa_signature result_sig;
	int recovery_id = 0;
	const int res = secp256k1_ecdsa_sign(context.ctx, &result_sig, msg_hash.data(), private_key.data(), /*noncefp=*/NULL, /*noncedata=*/NULL, &recovery_id);
	if(res != 1)
		throw glare::Exception("ecdsa sign failed.");

	// Convert to compacted 64-byte sig form.
	std::vector<uint8> sigdata(64);
	secp256k1_ecdsa_signature_serialize_compact(context.ctx, sigdata.data(), &result_sig);

	// Copy into sig.r and s
	Signature sig;
	std::memcpy(sig.r.data, &sigdata[0], 32);
	std::memcpy(sig.s.data, &sigdata[32], 32);
	sig.v = recovery_id;
	return sig;
}


// See Signer::sign, https://github.com/trustwallet/wallet-core/blob/master/src/Ethereum/Signer.cpp
void signTransaction(EthTransaction& trans, const std::vector<uint8>& private_key, int chain_id)
{
	// Hash the transaction data to get a 32-byte hash digest.
	const std::vector<uint8> digest = RLP::transactionHash(trans, UInt256(chain_id));

	// conPrint("digest: " + StringUtils::convertByteArrayToHexString(digest));

	// Sign the transaction hash digest with our private key.
	const Signature sig = sign(digest, private_key);

	// Store the signature variables v, r, s back into the transaction
	trans.v = UInt256(chain_id * 2 + 35 + sig.v);
	trans.r = sig.r;
	trans.s = sig.s;
}


}; // End namespace Signing


#if BUILD_TESTS


#include "../utils/TestUtils.h"


void Signing::test()
{
	// Test signing

	// Test with example values from https://github.com/ethereum/EIPs/blob/master/EIPS/eip-155.md
	{
		std::vector<uint8> priv_key = StringUtils::convertHexToBinary("4646464646464646464646464646464646464646464646464646464646464646");

		EthTransaction trans;
		trans.nonce = UInt256(9);
		trans.gas_price = UInt256(20000000000); // 20 * 10**9
		trans.gas_limit = UInt256(21000);
		trans.to = EthAddress::parseFromHexString("3535353535353535353535353535353535353535");
		trans.value = UInt256(1000000000000000000ULL); // 10**18
		trans.data = std::vector<uint8>();

		signTransaction(trans, priv_key, 1);

		const std::vector<uint8> signed_transaction_encoded = RLP::encodeSignedTransaction(trans);

		conPrint("signed_transaction_encoded: " + StringUtils::convertByteArrayToHexString(signed_transaction_encoded));

		const std::vector<uint8> target_signed_tx = StringUtils::convertHexToBinary("f86c098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a76400008025a028ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276a067cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83");

		testAssert(signed_transaction_encoded == target_signed_tx);
	}


	// Test ecrecover()
	try
	{
		// This signature was generated by MetaMask
		const std::string signature = "0x9889f19bdb00e55b060359d8701337e6f3c7003f3fb5fffbfac1c2dfb65755f61baa1ed12b96366aff3d292809341a42a8ab5c86226f5c6298c0e6389272634a1c";
		const std::string msg = "Please sign this message to confirm you own the Ethereum account.\n(Unique string: 2674eb985d1b31dea52958114387ef19)";

		const EthAddress address = ecrecover(signature, msg);

		conPrint(address.toHexStringWith0xPrefix());
		testAssert(address.toHexStringWith0xPrefix() == "0x8a76e943f2298af27a98327de94f519c27816e55");
	}
	catch(glare::Exception& e)
	{
		failTest(e.what());
	}

	// Test that a different signature does not result in the same address
	try
	{
		// Changed first digit of signature:
		const std::string signature = "0x1889f19bdb00e55b060359d8701337e6f3c7003f3fb5fffbfac1c2dfb65755f61baa1ed12b96366aff3d292809341a42a8ab5c86226f5c6298c0e6389272634a1c";
		const std::string msg = "Please sign this message to confirm you own the Ethereum account.\n(Unique string: 2674eb985d1b31dea52958114387ef19)";
		const EthAddress address = ecrecover(signature, msg);
		conPrint(address.toHexStringWith0xPrefix());
		testAssert(address.toHexStringWith0xPrefix() != "0x8a76e943f2298af27a98327de94f519c27816e55");
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}

	// Test that a different message does not result in the same address
	try
	{
		const std::string signature = "0x9889f19bdb00e55b060359d8701337e6f3c7003f3fb5fffbfac1c2dfb65755f61baa1ed12b96366aff3d292809341a42a8ab5c86226f5c6298c0e6389272634a1c";
		const std::string msg = "Xlease sign this message to confirm you own the Ethereum account.\n(Unique string: 2674eb985d1b31dea52958114387ef19)"; // Changed first letter of msg.
		const EthAddress address = ecrecover(signature, msg);
		conPrint(address.toHexStringWith0xPrefix());
		testAssert(address.toHexStringWith0xPrefix() != "0x8a76e943f2298af27a98327de94f519c27816e55");
	}
	catch(glare::Exception& e)
	{
		conPrint(e.what());
	}
}


#endif // BUILD_TESTS
