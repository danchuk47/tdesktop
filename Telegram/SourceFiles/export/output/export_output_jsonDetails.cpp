#include "export/output/export_output_jsonDetails.h"
#include <QtCore/QDateTime>

namespace Export {
	namespace Output {
		using Context = std::shared_ptr<details::JsonContext>;

		JsonDataBuilder::JsonDataBuilder() :_context(new details::JsonContext()) {
		}

		QByteArray JsonDataBuilder::pushNesting(details::JsonContext::Type type) {
			_context->nesting.push_back(type);
			_currentNestingHadItem = false;
			return (type == details::JsonContext::kObject ? "{" : "[");
		}

		QByteArray JsonDataBuilder::prepareObjectItemStart(const QByteArray& key) {
			const auto guard = gsl::finally([&] { _currentNestingHadItem = true; });
			return (_currentNestingHadItem ? ",\n" : "\n")
				+ Indentation(_context)
				+ SerializeString(key)
				+ ": ";
		}

		QByteArray JsonDataBuilder::prepareArrayItemStart() {
			const auto guard = gsl::finally([&] { _currentNestingHadItem = true; });
			return (_currentNestingHadItem ? ",\n" : "\n") + Indentation(_context);
		}

		QByteArray JsonDataBuilder::popNesting() {
			Expects(!_context->nesting.empty());

			const auto type = details::JsonContext::Type(_context->nesting.back());
			_context->nesting.pop_back();

			_currentNestingHadItem = true;
			return '\n'
				+ Indentation(_context)
				+ (type == details::JsonContext::kObject ? '}' : ']');
		}

		QByteArray JsonDataBuilder::SerializeObject(const std::vector<std::pair<QByteArray, QByteArray>>& values) {
			return SerializeObject(_context, values);
		}

		bool JsonDataBuilder::isContextNestingEmpty()const {
			return _context->nesting.empty();
		}

		const Context& JsonDataBuilder::getContext()const {
			return _context;
		}

		QByteArray JsonDataBuilder::Indentation(const Context& context) {
			return Indentation(context->nesting.size());
		}

		QByteArray JsonDataBuilder::SerializeString(const QByteArray& value) {
			const auto size = value.size();
			const auto begin = value.data();
			const auto end = begin + size;

			auto result = QByteArray();
			result.reserve(2 + size * 4);
			result.append('"');
			for (auto p = begin; p != end; ++p) {
				const auto ch = *p;
				if (ch == '\n') {
					result.append("\\n", 2);
				}
				else if (ch == '\r') {
					result.append("\\r", 2);
				}
				else if (ch == '\t') {
					result.append("\\t", 2);
				}
				else if (ch == '"') {
					result.append("\\\"", 2);
				}
				else if (ch == '\\') {
					result.append("\\\\", 2);
				}
				else if (ch >= 0 && ch < 32) {
					result.append("\\x", 2).append('0' + (ch >> 4));
					const auto left = (ch & 0x0F);
					if (left >= 10) {
						result.append('A' + (left - 10));
					}
					else {
						result.append('0' + left);
					}
				}
				else if (ch == char(0xE2)
					&& (p + 2 < end)
					&& *(p + 1) == char(0x80)) {
					if (*(p + 2) == char(0xA8)) { // Line separator.
						result.append("\\u2028", 6);
					}
					else if (*(p + 2) == char(0xA9)) { // Paragraph separator.
						result.append("\\u2029", 6);
					}
					else {
						result.append(ch);
					}
				}
				else {
					result.append(ch);
				}
			}
			result.append('"');
			return result;
		}

		QByteArray JsonDataBuilder::SerializeDate(TimeId date) {
			return SerializeString(
				QDateTime::fromTime_t(date).toString(Qt::ISODate).toUtf8());
		}

		QByteArray JsonDataBuilder::SerializeObject(const Context& context,
			const std::vector<std::pair<QByteArray, QByteArray>>& values) {
			const auto indent = Indentation(context);

			context->nesting.push_back(details::JsonContext::kObject);
			const auto guard = gsl::finally([&] { context->nesting.pop_back(); });
			const auto next = '\n' + Indentation(context);

			auto first = true;
			auto result = QByteArray();
			result.append('{');
			for (const auto& [key, value] : values) {
				if (value.isEmpty()) {
					continue;
				}
				if (first) {
					first = false;
				}
				else {
					result.append(',');
				}
				result.append(next).append(SerializeString(key)).append(": ", 2);
				result.append(value);
			}
			result.append('\n').append(indent).append("}");
			return result;
		}

		QByteArray JsonDataBuilder::SerializeArray(const Context& context,
			const std::vector<QByteArray>& values) {
			const auto indent = Indentation(context->nesting.size());
			const auto next = '\n' + Indentation(context->nesting.size() + 1);

			auto first = true;
			auto result = QByteArray();
			result.append('[');
			for (const auto& value : values) {
				if (first) {
					first = false;
				}
				else {
					result.append(',');
				}
				result.append(next).append(value);
			}
			result.append('\n').append(indent).append("]");
			return result;
		}

		QByteArray JsonDataBuilder::Indentation(int size) {
			return QByteArray(size, ' ');
		}
	}
}