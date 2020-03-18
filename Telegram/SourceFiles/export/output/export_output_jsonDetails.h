#pragma once

#include "base/basic_types.h"

namespace Export {
	namespace Output {
		namespace details {

			struct JsonContext {
				using Type = bool;
				static constexpr auto kObject = Type(true);
				static constexpr auto kArray = Type(false);

				// Always fun to use std::vector<bool>.
				std::vector<Type> nesting;
			};

		} // namespace details

		class JsonDataBuilder {
		public:
			using Context = std::shared_ptr<details::JsonContext>;
			JsonDataBuilder();
			~JsonDataBuilder() = default;

			[[nodiscard]] QByteArray pushNesting(details::JsonContext::Type type);
			[[nodiscard]] QByteArray popNesting();
			[[nodiscard]] QByteArray prepareObjectItemStart(const QByteArray& key);
			[[nodiscard]] QByteArray prepareArrayItemStart();
			[[nodiscard]] QByteArray SerializeObject(const std::vector<std::pair<QByteArray, QByteArray>>& values);
			[[nodiscard]] bool isContextNestingEmpty()const;
			[[nodiscard]] const Context& getContext()const;
			

			[[nodiscard]] static QByteArray SerializeString(const QByteArray& value);
			[[nodiscard]] static QByteArray SerializeDate(TimeId date);
			[[nodiscard]] static QByteArray SerializeObject(const Context& context,
				const std::vector<std::pair<QByteArray, QByteArray>>& values);
			[[nodiscard]] static QByteArray SerializeArray(const Context& context,
				const std::vector<QByteArray>& values);

		private:
			[[nodiscard]] static QByteArray Indentation(const Context& context);
			[[nodiscard]] static QByteArray Indentation(int size);

		private:
			Context _context;
			bool _currentNestingHadItem = false;
		};

	}
}
