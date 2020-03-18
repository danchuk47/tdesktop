#include "export/output/export_output_encryptionData.h"
#include "export/output/export_output_result.h"

namespace Export {
	namespace Output {
		JsonEncryptionDataWriter::JsonEncryptionDataWriter(): 
			_stats(new Stats()),
			_output(new File(getPathToCacheFile(), _stats.get())){
		}

		Result JsonEncryptionDataWriter::writeDataToCache(const std::vector<Export::Data::UserEncryptionData>& cacheData) {
			Expects(_output != nullptr);

			auto block = _dataBuilder.pushNesting(details::JsonContext::kObject);

			block.append(_dataBuilder.prepareObjectItemStart("encData_list"));
			block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
			for (const auto userItem : cacheData) {
				block.append(_dataBuilder.prepareArrayItemStart());

				block.append(_dataBuilder.SerializeObject({
					{ "user_id", Data::NumberToString(userItem.user_id) },
					{ "g", Data::NumberToString(userItem.g) },
					{ "p", _dataBuilder.SerializeString(userItem.p.toBase64(QByteArray::Base64Encoding)) },
					{ "g_a", _dataBuilder.SerializeString(userItem.g_a.toBase64(QByteArray::Base64Encoding)) },
					{ "g_b", _dataBuilder.SerializeString(userItem.g_b.toBase64(QByteArray::Base64Encoding)) },
					{ "secretKey", _dataBuilder.SerializeString(userItem.secretKey.toBase64(QByteArray::Base64Encoding)) },
					{ "encryptionKey", _dataBuilder.SerializeString(userItem.encryptionKey.toBase64(QByteArray::Base64Encoding)) },
					{ "encryptionChatId", Data::NumberToString(userItem.encryptionChatId) },
					{ "dhConfigVersion", Data::NumberToString(userItem.dhConfigVersion) }
				}));
			}
			block.append(_dataBuilder.popNesting());

			block.append(_dataBuilder.popNesting());
			Assert(_dataBuilder.isContextNestingEmpty());
			return _output->writeBlock(block);
		}

		QString JsonEncryptionDataWriter::getPathToCacheFile() {

			QString resultPath = QDir::currentPath();
			QString relativePath = "tdata/user_data/cache/encUserData.json";
			resultPath += resultPath.endsWith('/') ? relativePath : ("/" + relativePath);

			return resultPath;
		}
	}
}
