/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/output/export_output_abstract.h"
#include "export/output/export_output_file.h"
#include "export/output/export_output_jsonDetails.h"
#include "export/export_settings.h"
#include "export/data/export_data_types.h"

namespace Export {
namespace Output {

class JsonWriter : public AbstractWriter {
public:
	Format format() override {
		return Format::Json;
	}

	Result start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) override;

	Result writePersonal(const Data::PersonalInfo &data) override;

	Result writeUserpicsStart(const Data::UserpicsInfo &data) override;
	Result writeUserpicsSlice(const Data::UserpicsSlice &data) override;
	Result writeUserpicsEnd() override;

	Result writeContactsList(const Data::ContactsList &data) override;

	Result writeSessionsList(const Data::SessionsList &data) override;

	Result writeOtherData(const Data::File &data) override;

	Result writeDialogsStart(const Data::DialogsInfo &data) override;
	Result writeDialogStart(const Data::DialogInfo &data) override;
	Result writeDialogSlice(const Data::MessagesSlice &data) override;
	Result writeDialogEnd() override;
	Result writeDialogsEnd() override;

	Result finish() override;

	QString mainFilePath() override;

private:
	enum class DialogsMode {
		None,
		Chats,
		Left,
	};

	[[nodiscard]] QString mainFileRelativePath() const;
	[[nodiscard]] QString pathWithRelativePath(const QString &path) const;
	[[nodiscard]] std::unique_ptr<File> fileWithRelativePath(
		const QString &path) const;

	[[nodiscard]] Result writeSavedContacts(const Data::ContactsList &data);
	[[nodiscard]] Result writeFrequentContacts(const Data::ContactsList &data);

	[[nodiscard]] Result writeSessions(const Data::SessionsList &data);
	[[nodiscard]] Result writeWebSessions(const Data::SessionsList &data);

	[[nodiscard]] Result validateDialogsMode(bool isLeftChannel);
	[[nodiscard]] Result writeChatsStart(
		const QByteArray &listName,
		const QByteArray &about);
	[[nodiscard]] Result writeChatsEnd();

	Settings _settings;
	Environment _environment;
	Stats *_stats = nullptr;

	JsonDataBuilder _dataBuilder;
	DialogsMode _dialogsMode = DialogsMode::None;

	std::unique_ptr<File> _output;

};

} // namespace Output
} // namespace Export
