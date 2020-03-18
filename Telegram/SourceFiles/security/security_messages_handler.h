#pragma once

#include "data/data_user.h"
#include "mtproto/mtproto_auth_key.h"

namespace security {
	class SecurityMessagesHandler 
	{
	public:
		explicit SecurityMessagesHandler(const EncryptionChatData* encryptionData);
		~SecurityMessagesHandler() = default;

		MTPString encryptMessage(const MTPString& message)const;
		MTPString decryptMessage(const MTPString& message)const;

	private:
		MTPint128 computeMsgKey(const mtpPrime* constData, uint32 dataLen)const;
		void encryptMessage(
			const mtpPrime* src,
			mtpPrime* dst,
			uint32 len,
			const MTPint128& msgkey)const;

	private:
		MTP::AuthKeyPtr _encryptionKey;

		static constexpr auto kMessageKeyPosition = 0;
		static constexpr auto kExternalHeaderIntsCount = 4U;
	};
}
