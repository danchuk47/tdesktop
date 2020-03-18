#include "dh_encryptionkey_exchanger.h"
#include <random>
#include "mtproto/mtproto_dh_utils.h"
#include "mtproto/mtproto_rpc_sender.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "logs.h"
#include "data/data_user.h"


using namespace DhExchangeKey;

DHEncryptionKeyExchanger::DHEncryptionKeyExchanger(not_null<Main::Session*> session, int g, const bytes::vector& p):
	_dhConfig(new DhConfig{0, g, p}),
	_session(session){
}

bytes::const_span DHEncryptionKeyExchanger::updateDhConfig(const MTPmessages_DhConfig& data) {
	const auto validRandom = [](const QByteArray& random) {
		if (random.size() != MTP::ModExpFirst::kRandomPowerSize) {
			return false;
		}
		return true;
	};
	return data.match([&](const MTPDmessages_dhConfig& data) -> bytes::const_span {
			auto primeBytes = bytes::make_vector(data.vp().v);
			if (!MTP::IsPrimeAndGood(primeBytes, data.vg().v)) {
				LOG(("API Error: bad p/g received in dhConfig."));
				return {};
			}
			else if (!validRandom(data.vrandom().v)) {
				return {};
			}
			_dhConfig->g = data.vg().v;
			_dhConfig->p = std::move(primeBytes);
			_dhConfig->version = data.vversion().v;
			return bytes::make_span(data.vrandom().v);
		}, [&](const MTPDmessages_dhConfigNotModified& data) -> bytes::const_span {
			if (!_dhConfig->g || _dhConfig->p.empty()) {
				LOG(("API Error: dhConfigNotModified on zero version."));
				return {};
			}
			else if (!validRandom(data.vrandom().v)) {
				return {};
			}
			return bytes::make_span(data.vrandom().v);
		});
}

int DHEncryptionKeyExchanger::getRandomNum() {
	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_int_distribution<std::mt19937::result_type> distMaxInt(1, INT_MAX);
	return distMaxInt(rng);
}

MTP::ModExpFirst DHEncryptionKeyExchanger::ñreateModExp(const bytes::const_span randomKey)const {
	return MTP::CreateModExp(_dhConfig->g, _dhConfig->p, randomKey);
}

bytes::vector DHEncryptionKeyExchanger::ñreateEncryptionKey(const bytes::vector& g_a, const bytes::vector& secretKey)const{
	auto computedEncryptionKey = MTP::CreateAuthKey(g_a, secretKey, _dhConfig->p);
	if (computedEncryptionKey.empty()) {
		LOG(("DH Exchange Error: Could not compute mod-exp final."));
		return bytes::vector();
	}

	return computedEncryptionKey;
}

void DHEncryptionKeyExchanger::requestEncryption(not_null<UserData*> userData, const MTP::ModExpFirst& modExpFirst) {
	MTPInputUser inputUser = userData->inputUser;
	const MTPbytes mtpModexp = MTP_bytes(modExpFirst.modexp);
	const int32 encryptionChatId = getRandomNum();

	_session->api().request(MTPmessages_RequestEncryption(
		inputUser,
		MTP_int(encryptionChatId),
		mtpModexp
	)).done([=](const MTPEncryptedChat& result) {
		LOG(("API Success: success on attempts to receive the MTPEncryptedChat."));
		result.match([&](const MTPDencryptedChatEmpty& data) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatEmpty."));
			}, [&](const MTPDencryptedChatWaiting& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatWaiting."));
				bytes::vector g_a = modExpFirst.modexp;
				bytes::vector secretKey = modExpFirst.randomPower;
				userData->setDataOfEncryptionChat(std::move(g_a), std::move(secretKey), encryptionChatId);
			}, [&](const MTPDencryptedChatRequested& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatRequested."));
			}, [&](const MTPDencryptedChat& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChat."));
			}, [&](const MTPDencryptedChatDiscarded& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChat."));
			});
	}).fail([=](const RPCError& error) {
		LOG(("API Error: failed on attempts to receive the MTPEncryptedChat."));
		if (error.type() == qstr("RANDOM_ID_DUPLICATE") && error.code() == 400) {
			requestEncryption(userData, modExpFirst);
		}
	}).send();
}

void DHEncryptionKeyExchanger::acceptEncryption(
	int32 chatId,
	uint64 accessHash,
	const bytes::vector& modexp,
	uint64 fingerprint) {

	const MTPbytes mtpModexp = MTP_bytes(modexp);
	MTPint mtpEncryptionChatId = MTP_int(chatId);
	MTPlong mtpAccess_hash = MTP_long(accessHash);
	MTPlong mtpFingerprint = MTP_long(fingerprint);
	MTPinputEncryptedChat mtpPeer = MTP_inputEncryptedChat(mtpEncryptionChatId, mtpAccess_hash);

	_session->api().request(MTPmessages_AcceptEncryption(
		mtpPeer,
		mtpModexp,
		mtpFingerprint
	)).done([&](const MTPEncryptedChat& result) {
		LOG(("API acceptEncryption Success: success on attempts to accept the MTPEncryptedChat."));
		result.match([&](const MTPDencryptedChatEmpty& data) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatEmpty."));
			}, [&](const MTPDencryptedChatWaiting& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatWaiting."));
			}, [&](const MTPDencryptedChatRequested& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatRequested."));
			}, [&](const MTPDencryptedChat& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChat."));
			}, [&](const MTPDencryptedChatDiscarded& result) {
				LOG(("API Success: MTPEncryptedChat = MTPDencryptedChatDiscarded."));
			});
	}).fail([=](const RPCError& error) {
		LOG(("API Error: failed on attempts to accept the MTPEncryptedChat."));
	}).send();
}

uint64 DHEncryptionKeyExchanger::computeFingerprint(bytes::const_span encryptionKey)const {
	constexpr auto kFingerprintDataSize = 256;
	Expects(encryptionKey.size() == kFingerprintDataSize);

	auto hash = openssl::Sha1(encryptionKey);
	return (gsl::to_integer<uint64>(hash[19]) << 56)
		| (gsl::to_integer<uint64>(hash[18]) << 48)
		| (gsl::to_integer<uint64>(hash[17]) << 40)
		| (gsl::to_integer<uint64>(hash[16]) << 32)
		| (gsl::to_integer<uint64>(hash[15]) << 24)
		| (gsl::to_integer<uint64>(hash[14]) << 16)
		| (gsl::to_integer<uint64>(hash[13]) << 8)
		| (gsl::to_integer<uint64>(hash[12]));
}

bool DHEncryptionKeyExchanger::isEncryptionKeyValid(int64 fingerprint, bytes::const_span encryptionKey)const {
	const int64 currentFingeprint = computeFingerprint(encryptionKey);
	return currentFingeprint == fingerprint;
}