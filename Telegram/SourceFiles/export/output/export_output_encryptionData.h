#pragma once

#include "export/output/export_output_file.h"
#include "export/output/export_output_jsonDetails.h"
#include "export/output/export_output_stats.h"
#include "export/data/export_data_types.h"

namespace Export {
namespace Output {

	class JsonEncryptionDataWriter
	{
	public:
		JsonEncryptionDataWriter();
		virtual ~JsonEncryptionDataWriter() = default;

		Result writeDataToCache(const std::vector<Export::Data::UserEncryptionData>& cacheData);

		static QString getPathToCacheFile();

	private:
		std::unique_ptr<Stats> _stats;
		std::unique_ptr<File> _output;
		JsonDataBuilder _dataBuilder;
	};
}
}