#pragma once

#include <base/bytes.h>
#include "main/main_session.h"

namespace MTP {
	struct ModExpFirst;
}

class UserData;

namespace DhExchangeKey {
	struct DhConfig {
		int version = 0;
		int g = 0;
		bytes::vector p;
	};

	class DHEncryptionKeyExchanger
	{
	public:
		DHEncryptionKeyExchanger(not_null<Main::Session*> session, int g, const bytes::vector& p);
		virtual ~DHEncryptionKeyExchanger() = default;

		bytes::const_span updateDhConfig(const MTPmessages_DhConfig& data);
		void requestEncryption(not_null<UserData*> userData, const MTP::ModExpFirst& modExpFirst);
		void acceptEncryption(int32 chatId, uint64 accessHash, const bytes::vector& modexp, uint64 fingerprint);
		MTP::ModExpFirst ñreateModExp(const bytes::const_span secretKey)const;
		bytes::vector ñreateEncryptionKey(const bytes::vector& g_a, const bytes::vector& secretKey)const;
		uint64 computeFingerprint(bytes::const_span encryptionKey)const;
		bool isEncryptionKeyValid(int64 fingerprint, bytes::const_span encryptionKey)const;
		inline const DhConfig* getDhConfig()const { return _dhConfig.get(); }

		static int getRandomNum();
		
	private:
		std::unique_ptr<DhConfig> _dhConfig;
		not_null<Main::Session*> _session;
	};
}
