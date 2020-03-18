#include "security/security_messages_handler.h"
#include "mtproto/core_types.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "dh/dh_encryptionkey_exchanger.h"
#include "base/openssl_help.h"

using namespace security;
using namespace MTP;
using namespace MTP::details;

SecurityMessagesHandler::SecurityMessagesHandler(const EncryptionChatData* encryptionData) {
	if (encryptionData != nullptr && !encryptionData->encryptionKey.empty()) {
		AuthKey::Data encryptionKeyData;
		AuthKey::FillData(encryptionKeyData, encryptionData->encryptionKey);
		_encryptionKey.reset(new AuthKey(encryptionKeyData));
	}
}

MTPString SecurityMessagesHandler::encryptMessage(const MTPString& message)const {
	if (_encryptionKey.get() == nullptr) {
		LOG(("Warning: attempt to encrypt message when the encryption key is not created."));
		return message;
	}

	constexpr uint kPrefixInts = kExternalHeaderIntsCount;
	mtpBuffer packet;

	SerializedRequest request = SerializedRequest::Serialize(message);
	mtpMsgId msgId = static_cast<mtpMsgId>(DhExchangeKey::DHEncryptionKeyExchanger::getRandomNum());
	request.setMsgId(msgId);
	request.addPadding(false, false);
	const uint32 fullSize = request->size();

	const MTPint128 msgKey = computeMsgKey(request->constData(), fullSize);

	packet.reserve(kExternalHeaderIntsCount + fullSize);
	packet.resize(kExternalHeaderIntsCount);
	*reinterpret_cast<MTPint128*>(&packet[kMessageKeyPosition]) = msgKey;
	packet.resize(kExternalHeaderIntsCount + fullSize);

	encryptMessage(request->constData(), &packet[kPrefixInts], fullSize, msgKey);

	const char* encSrcData = reinterpret_cast<const char*>(&packet[0]);
	const int encSrcDataLen = (kExternalHeaderIntsCount + fullSize) * sizeof(mtpPrime);

	QByteArray encryptedArray = QByteArray::fromRawData(encSrcData, encSrcDataLen);
	QString encryptedMessage(encryptedArray.toBase64(QByteArray::Base64Encoding));

	return MTP_string(encryptedMessage);
}

MTPString SecurityMessagesHandler::decryptMessage(const MTPString& message)const {
	if (_encryptionKey.get() == nullptr) {
		LOG(("Warning: attempt to decrypt message when the encryption key is not created."));
		return message;
	}
	QByteArray encryptedMessage = QByteArray::fromBase64(message.v);

	uint32 intsCount = uint32(encryptedMessage.size() / static_cast<uint32>(sizeof(mtpPrime)));
	const mtpPrime* ints = reinterpret_cast<const mtpPrime*>(encryptedMessage.constData());

	if (intsCount <= kExternalHeaderIntsCount) {
		LOG(("Warning: size of encrypted message is to small."));
		return message;
	}

	const mtpPrime* encryptedInts = ints + kExternalHeaderIntsCount;
	uint encryptedIntsCount = (intsCount - kExternalHeaderIntsCount) & ~0x03U;
	uint encryptedBytesCount = encryptedIntsCount * static_cast<int>(sizeof(mtpPrime));
	QByteArray decryptedBuffer(encryptedBytesCount, Qt::Uninitialized);
	const MTPint128 msgKey = *reinterpret_cast<const MTPint128*>(&ints[kMessageKeyPosition]);

	if (encryptedIntsCount <= SerializedRequest::kMessageBodyPosition) {
		LOG(("Warning: size of encrypted message is to small."));
		return message;
	}

	aesIgeDecrypt(encryptedInts, decryptedBuffer.data(), encryptedBytesCount, _encryptionKey, msgKey);

	const mtpPrime* decryptedInts = reinterpret_cast<const mtpPrime*>(decryptedBuffer.constData());
	int32 messageLength = decryptedInts[SerializedRequest::kMessageLengthPosition];
	const mtpPrime* decryptedMessageBody = &decryptedInts[SerializedRequest::kMessageBodyPosition];

	if (msgKey != computeMsgKey(decryptedInts, intsCount - kExternalHeaderIntsCount)) {
		LOG(("Warning: checking of the message key failed."));
		return message;
	}

	MTPString result;
	const mtpPrime* srcFrom = decryptedMessageBody;
	const mtpPrime* srcEnd = srcFrom + messageLength;
	if (!result.read(srcFrom, srcEnd)) {
		LOG(("Warning: can not deserialize the decrypted data to the MTPString."));
		return message;
	}

	return result;
}

void SecurityMessagesHandler::encryptMessage(const mtpPrime* src,	mtpPrime* dst,	uint32 len,	const MTPint128& msgKey) const{

	MTPint256 aesKey, aesIV;
	_encryptionKey->prepareAES(msgKey, aesKey, aesIV, false);

	aesIgeEncryptRaw(
		src,
		dst,
		len * sizeof(mtpPrime),
		static_cast<const void*>(&aesKey),
		static_cast<const void*>(&aesIV));
}

MTPint128 SecurityMessagesHandler::computeMsgKey(const mtpPrime* constData, uint32 dataLen)const {
	uchar encryptedSHA256[32];
	MTPint128& msgKey(*(MTPint128*)(encryptedSHA256 + 8));

	SHA256_CTX msgKeyLargeContext;
	SHA256_Init(&msgKeyLargeContext);
	SHA256_Update(&msgKeyLargeContext, _encryptionKey->partForMsgKey(true), 32);
	SHA256_Update(&msgKeyLargeContext, constData, dataLen * sizeof(mtpPrime));
	SHA256_Final(encryptedSHA256, &msgKeyLargeContext);

	return msgKey;
}

