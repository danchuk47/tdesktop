/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_json.h"

#include "export/output/export_output_result.h"
#include "export/data/export_data_types.h"
#include "core/utils.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>

namespace Export {
namespace Output {
namespace {

using Context = std::shared_ptr<details::JsonContext>;;

QByteArray StringAllowEmpty(const Data::Utf8String &data) {
	return data.isEmpty() ? data : JsonDataBuilder::SerializeString(data);
}

QByteArray StringAllowNull(const Data::Utf8String &data) {
	return data.isEmpty() ? QByteArray("null") : JsonDataBuilder::SerializeString(data);
}

QByteArray SerializeText(
		const Context &context,
		const std::vector<Data::TextPart> &data) {
	using Type = Data::TextPart::Type;

	if (data.empty()) {
		return JsonDataBuilder::SerializeString("");
	}

	context->nesting.push_back(details::JsonContext::kArray);

	const auto text = ranges::view::all(
		data
	) | ranges::view::transform([&](const Data::TextPart &part) {
		if (part.type == Type::Text) {
			return JsonDataBuilder::SerializeString(part.text);
		}
		const auto typeString = [&] {
			switch (part.type) {
			case Type::Unknown: return "unknown";
			case Type::Mention: return "mention";
			case Type::Hashtag: return "hashtag";
			case Type::BotCommand: return "bot_command";
			case Type::Url: return "link";
			case Type::Email: return "email";
			case Type::Bold: return "bold";
			case Type::Italic: return "italic";
			case Type::Code: return "code";
			case Type::Pre: return "pre";
			case Type::TextUrl: return "text_link";
			case Type::MentionName: return "mention_name";
			case Type::Phone: return "phone";
			case Type::Cashtag: return "cashtag";
			case Type::Underline: return "underline";
			case Type::Strike: return "strikethrough";
			case Type::Blockquote: return "blockquote";
			}
			Unexpected("Type in SerializeText.");
		}();
		const auto additionalName = (part.type == Type::MentionName)
			? "user_id"
			: (part.type == Type::Pre)
			? "language"
			: (part.type == Type::TextUrl)
			? "href"
			: "none";
		const auto additionalValue = (part.type == Type::MentionName)
			? part.additional
			: (part.type == Type::Pre || part.type == Type::TextUrl)
			? JsonDataBuilder::SerializeString(part.additional)
			: QByteArray();
		return JsonDataBuilder::SerializeObject(context, {
			{ "type", JsonDataBuilder::SerializeString(typeString) },
			{ "text", JsonDataBuilder::SerializeString(part.text) },
			{ additionalName, additionalValue },
		});
	}) | ranges::to_vector;

	context->nesting.pop_back();

	if (data.size() == 1 && data[0].type == Data::TextPart::Type::Text) {
		return text[0];
	}
	return JsonDataBuilder::SerializeArray(context, text);
}

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

QByteArray FormatFilePath(const Data::File &file) {
	return file.relativePath.toUtf8();
}

QByteArray SerializeMessage(
		const Context &context,
		const Data::Message &message,
		const std::map<Data::PeerId, Data::Peer> &peers,
		const QString &internalLinksDomain) {
	using namespace Data;

	if (message.media.content.is<UnsupportedMedia>()) {
		return JsonDataBuilder::SerializeObject(context, {
			{ "id", Data::NumberToString(message.id) },
			{ "type", JsonDataBuilder::SerializeString("unsupported") }
		});
	}

	const auto peer = [&](PeerId peerId) -> const Peer& {
		if (const auto i = peers.find(peerId); i != end(peers)) {
			return i->second;
		}
		static auto empty = Peer{ User() };
		return empty;
	};
	const auto user = [&](int32 userId) -> const User& {
		if (const auto result = peer(UserPeerId(userId)).user()) {
			return *result;
		}
		static auto empty = User();
		return empty;
	};
	const auto chat = [&](int32 chatId) -> const Chat& {
		if (const auto result = peer(ChatPeerId(chatId)).chat()) {
			return *result;
		}
		static auto empty = Chat();
		return empty;
	};

	auto values = std::vector<std::pair<QByteArray, QByteArray>>{
	{ "id", NumberToString(message.id) },
	{
		"type",
		JsonDataBuilder::SerializeString(message.action.content ? "service" : "message")
	},
	{ "date", JsonDataBuilder::SerializeDate(message.date) },
	{ "edited", JsonDataBuilder::SerializeDate(message.edited) },
	};

	context->nesting.push_back(details::JsonContext::kObject);
	const auto serialized = [&] {
		context->nesting.pop_back();
		return JsonDataBuilder::SerializeObject(context, values);
	};

	const auto pushBare = [&](
			const QByteArray &key,
			const QByteArray &value) {
		if (!value.isEmpty()) {
			values.emplace_back(key, value);
		}
	};
	const auto push = [&](const QByteArray &key, const auto &value) {
		if constexpr (std::is_arithmetic_v<std::decay_t<decltype(value)>>) {
			pushBare(key, Data::NumberToString(value));
		} else {
			const auto wrapped = QByteArray(value);
			if (!wrapped.isEmpty()) {
				pushBare(key, JsonDataBuilder::SerializeString(wrapped));
			}
		}
	};
	const auto wrapPeerName = [&](PeerId peerId) {
		return StringAllowNull(peer(peerId).name());
	};
	const auto wrapUserName = [&](int32 userId) {
		return StringAllowNull(user(userId).name());
	};
	const auto pushFrom = [&](const QByteArray &label = "from") {
		if (message.fromId) {
			pushBare(label, wrapUserName(message.fromId));
			pushBare(label+"_id", Data::NumberToString(message.fromId));
		}
	};
	const auto pushReplyToMsgId = [&](
			const QByteArray &label = "reply_to_message_id") {
		if (message.replyToMsgId) {
			push(label, message.replyToMsgId);
		}
	};
	const auto pushUserNames = [&](
			const std::vector<int32> &data,
			const QByteArray &label = "members") {
		auto list = std::vector<QByteArray>();
		for (const auto userId : data) {
			list.push_back(wrapUserName(userId));
		}
		pushBare(label, JsonDataBuilder::SerializeArray(context, list));
	};
	const auto pushActor = [&] {
		pushFrom("actor");
	};
	const auto pushAction = [&](const QByteArray &action) {
		push("action", action);
	};
	const auto pushTTL = [&](
			const QByteArray &label = "self_destruct_period_seconds") {
		if (const auto ttl = message.media.ttl) {
			push(label, ttl);
		}
	};

	using SkipReason = Data::File::SkipReason;
	const auto pushPath = [&](
			const Data::File &file,
			const QByteArray &label,
			const QByteArray &name = QByteArray()) {
		Expects(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);

		push(label, [&]() -> QByteArray {
			const auto pre = name.isEmpty() ? QByteArray() : name + ' ';
			switch (file.skipReason) {
			case SkipReason::Unavailable:
				return pre + "(File unavailable, please try again later)";
			case SkipReason::FileSize:
				return pre + "(File exceeds maximum size. "
					"Change data exporting settings to download.)";
			case SkipReason::FileType:
				return pre + "(File not included. "
					"Change data exporting settings to download.)";
			case SkipReason::None: return FormatFilePath(file);
			}
			Unexpected("Skip reason while writing file path.");
		}());
	};
	const auto pushPhoto = [&](const Image &image) {
		pushPath(image.file, "photo");
		if (image.width && image.height) {
			push("width", image.width);
			push("height", image.height);
		}
	};

	message.action.content.match([&](const ActionChatCreate &data) {
		pushActor();
		pushAction("create_group");
		push("title", data.title);
		pushUserNames(data.userIds);
	}, [&](const ActionChatEditTitle &data) {
		pushActor();
		pushAction("edit_group_title");
		push("title", data.title);
	}, [&](const ActionChatEditPhoto &data) {
		pushActor();
		pushAction("edit_group_photo");
		pushPhoto(data.photo.image);
	}, [&](const ActionChatDeletePhoto &data) {
		pushActor();
		pushAction("delete_group_photo");
	}, [&](const ActionChatAddUser &data) {
		pushActor();
		pushAction("invite_members");
		pushUserNames(data.userIds);
	}, [&](const ActionChatDeleteUser &data) {
		pushActor();
		pushAction("remove_members");
		pushUserNames({ data.userId });
	}, [&](const ActionChatJoinedByLink &data) {
		pushActor();
		pushAction("join_group_by_link");
		pushBare("inviter", wrapUserName(data.inviterId));
	}, [&](const ActionChannelCreate &data) {
		pushActor();
		pushAction("create_channel");
		push("title", data.title);
	}, [&](const ActionChatMigrateTo &data) {
		pushActor();
		pushAction("migrate_to_supergroup");
	}, [&](const ActionChannelMigrateFrom &data) {
		pushActor();
		pushAction("migrate_from_group");
		push("title", data.title);
	}, [&](const ActionPinMessage &data) {
		pushActor();
		pushAction("pin_message");
		pushReplyToMsgId("message_id");
	}, [&](const ActionHistoryClear &data) {
		pushActor();
		pushAction("clear_history");
	}, [&](const ActionGameScore &data) {
		pushActor();
		pushAction("score_in_game");
		pushReplyToMsgId("game_message_id");
		push("score", data.score);
	}, [&](const ActionPaymentSent &data) {
		pushAction("send_payment");
		push("amount", data.amount);
		push("currency", data.currency);
		pushReplyToMsgId("invoice_message_id");
	}, [&](const ActionPhoneCall &data) {
		pushActor();
		pushAction("phone_call");
		if (data.duration) {
			push("duration_seconds", data.duration);
		}
		using Reason = ActionPhoneCall::DiscardReason;
		push("discard_reason", [&] {
			switch (data.discardReason) {
			case Reason::Busy: return "busy";
			case Reason::Disconnect: return "disconnect";
			case Reason::Hangup: return "hangup";
			case Reason::Missed: return "missed";
			}
			return "";
		}());
	}, [&](const ActionScreenshotTaken &data) {
		pushActor();
		pushAction("take_screenshot");
	}, [&](const ActionCustomAction &data) {
		pushActor();
		push("information_text", data.message);
	}, [&](const ActionBotAllowed &data) {
		pushAction("allow_sending_messages");
		push("reason_domain", data.domain);
	}, [&](const ActionSecureValuesSent &data) {
		pushAction("send_passport_values");
		auto list = std::vector<QByteArray>();
		for (const auto type : data.types) {
			list.push_back(JsonDataBuilder::SerializeString([&] {
				using Type = ActionSecureValuesSent::Type;
				switch (type) {
				case Type::PersonalDetails: return "personal_details";
				case Type::Passport: return "passport";
				case Type::DriverLicense: return "driver_license";
				case Type::IdentityCard: return "identity_card";
				case Type::InternalPassport: return "internal_passport";
				case Type::Address: return "address_information";
				case Type::UtilityBill: return "utility_bill";
				case Type::BankStatement: return "bank_statement";
				case Type::RentalAgreement: return "rental_agreement";
				case Type::PassportRegistration:
					return "passport_registration";
				case Type::TemporaryRegistration:
					return "temporary_registration";
				case Type::Phone: return "phone_number";
				case Type::Email: return "email";
				}
				return "";
			}()));
		}
		pushBare("values", JsonDataBuilder::SerializeArray(context, list));
	}, [&](const ActionContactSignUp &data) {
		pushActor();
		pushAction("joined_telegram");
	}, [&](const ActionPhoneNumberRequest &data) {
		pushActor();
		pushAction("requested_phone_number");
	}, [](std::nullopt_t) {});

	if (!message.action.content) {
		pushFrom();
		push("author", message.signature);
		if (message.forwardedFromId) {
			pushBare(
				"forwarded_from",
				wrapPeerName(message.forwardedFromId));
		} else if (!message.forwardedFromName.isEmpty()) {
			pushBare(
				"forwarded_from",
				StringAllowNull(message.forwardedFromName));
		}
		if (message.savedFromChatId) {
			pushBare("saved_from", wrapPeerName(message.savedFromChatId));
		}
		pushReplyToMsgId();
		if (message.viaBotId) {
			const auto username = FormatUsername(
				user(message.viaBotId).username);
			if (!username.isEmpty()) {
				push("via_bot", username);
			}
		}
	}

	message.media.content.match([&](const Photo &photo) {
		pushPhoto(photo.image);
		pushTTL();
	}, [&](const Document &data) {
		pushPath(data.file, "file");
		if (data.thumb.width > 0) {
			pushPath(data.thumb.file, "thumbnail");
		}
		const auto pushType = [&](const QByteArray &value) {
			push("media_type", value);
		};
		if (data.isSticker) {
			pushType("sticker");
			push("sticker_emoji", data.stickerEmoji);
		} else if (data.isVideoMessage) {
			pushType("video_message");
		} else if (data.isVoiceMessage) {
			pushType("voice_message");
		} else if (data.isAnimated) {
			pushType("animation");
		} else if (data.isVideoFile) {
			pushType("video_file");
		} else if (data.isAudioFile) {
			pushType("audio_file");
			push("performer", data.songPerformer);
			push("title", data.songTitle);
		}
		if (!data.isSticker) {
			push("mime_type", data.mime);
		}
		if (data.duration) {
			push("duration_seconds", data.duration);
		}
		if (data.width && data.height) {
			push("width", data.width);
			push("height", data.height);
		}
		pushTTL();
	}, [&](const SharedContact &data) {
		pushBare("contact_information", JsonDataBuilder::SerializeObject(context, {
			{ "first_name", JsonDataBuilder::SerializeString(data.info.firstName) },
			{ "last_name", JsonDataBuilder::SerializeString(data.info.lastName) },
			{
				"phone_number",
				JsonDataBuilder::SerializeString(FormatPhoneNumber(data.info.phoneNumber))
			}
		}));
		if (!data.vcard.content.isEmpty()) {
			pushPath(data.vcard, "contact_vcard");
		}
	}, [&](const GeoPoint &data) {
		pushBare(
			"location_information",
			data.valid ? JsonDataBuilder::SerializeObject(context, {
			{ "latitude", NumberToString(data.latitude) },
			{ "longitude", NumberToString(data.longitude) },
			}) : QByteArray("null"));
		pushTTL("live_location_period_seconds");
	}, [&](const Venue &data) {
		push("place_name", data.title);
		push("address", data.address);
		if (data.point.valid) {
			pushBare("location_information", JsonDataBuilder::SerializeObject(context, {
				{ "latitude", NumberToString(data.point.latitude) },
				{ "longitude", NumberToString(data.point.longitude) },
			}));
		}
	}, [&](const Game &data) {
		push("game_title", data.title);
		push("game_description", data.description);
		if (data.botId != 0 && !data.shortName.isEmpty()) {
			const auto bot = user(data.botId);
			if (bot.isBot && !bot.username.isEmpty()) {
				push("game_link", internalLinksDomain.toUtf8()
					+ bot.username
					+ "?game="
					+ data.shortName);
			}
		}
	}, [&](const Invoice &data) {
		push("invoice_information", JsonDataBuilder::SerializeObject(context, {
			{ "title", JsonDataBuilder::SerializeString(data.title) },
			{ "description", JsonDataBuilder::SerializeString(data.description) },
			{ "amount", NumberToString(data.amount) },
			{ "currency", JsonDataBuilder::SerializeString(data.currency) },
			{ "receipt_message_id", (data.receiptMsgId
				? NumberToString(data.receiptMsgId)
				: QByteArray()) }
		}));
	}, [&](const Poll &data) {
		context->nesting.push_back(details::JsonContext::kObject);
		const auto answers = ranges::view::all(
			data.answers
		) | ranges::view::transform([&](const Poll::Answer &answer) {
			context->nesting.push_back(details::JsonContext::kArray);
			auto result = JsonDataBuilder::SerializeObject(context, {
				{ "text", JsonDataBuilder::SerializeString(answer.text) },
				{ "voters", NumberToString(answer.votes) },
				{ "chosen", answer.my ? "true" : "false" },
			});
			context->nesting.pop_back();
			return result;
		}) | ranges::to_vector;
		const auto serialized = JsonDataBuilder::SerializeArray(context, answers);
		context->nesting.pop_back();

		pushBare("poll", JsonDataBuilder::SerializeObject(context, {
			{ "question", JsonDataBuilder::SerializeString(data.question) },
			{ "closed", data.closed ? "true" : "false" },
			{ "total_voters", NumberToString(data.totalVotes) },
			{ "answers", serialized }
		}));
	}, [](const UnsupportedMedia &data) {
		Unexpected("Unsupported message.");
	}, [](std::nullopt_t) {});

	pushBare("text", SerializeText(context, message.text));

	return serialized();
}

} // namespace

Result JsonWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	Expects(_output == nullptr);
	Expects(settings.path.endsWith('/'));

	_settings = base::duplicate(settings);
	_environment = environment;
	_stats = stats;
	_output = fileWithRelativePath(mainFileRelativePath());

	auto block = _dataBuilder.pushNesting(details::JsonContext::kObject);
	block.append(_dataBuilder.prepareObjectItemStart("about"));
	block.append(JsonDataBuilder::SerializeString(_environment.aboutTelegram));
	return _output->writeBlock(block);
}

Result JsonWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_output != nullptr);

	const auto &info = data.user.info;
	return _output->writeBlock(
		_dataBuilder.prepareObjectItemStart("personal_information")
		+ _dataBuilder.SerializeObject({
		{ "user_id", Data::NumberToString(data.user.id) },
		{ "first_name", JsonDataBuilder::SerializeString(info.firstName) },
		{ "last_name", JsonDataBuilder::SerializeString(info.lastName) },
		{
			"phone_number",
			JsonDataBuilder::SerializeString(Data::FormatPhoneNumber(info.phoneNumber))
		},
		{
			"username",
			(!data.user.username.isEmpty()
				? JsonDataBuilder::SerializeString(FormatUsername(data.user.username))
				: QByteArray())
		},
		{
			"bio",
			(!data.bio.isEmpty()
				? JsonDataBuilder::SerializeString(data.bio)
				: QByteArray())
		},
	}));
}

Result JsonWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_output != nullptr);

	auto block = _dataBuilder.prepareObjectItemStart("profile_pictures");
	return _output->writeBlock(block + _dataBuilder.pushNesting(details::JsonContext::kArray));
}

Result JsonWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	Expects(_output != nullptr);
	Expects(!data.list.empty());

	auto block = QByteArray();
	for (const auto &userpic : data.list) {
		using SkipReason = Data::File::SkipReason;
		const auto &file = userpic.image.file;
		Assert(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);
		const auto path = [&]() -> Data::Utf8String {
			switch (file.skipReason) {
			case SkipReason::Unavailable:
				return "(Photo unavailable, please try again later)";
			case SkipReason::FileSize:
				return "(Photo exceeds maximum size. "
					"Change data exporting settings to download.)";
			case SkipReason::FileType:
				return "(Photo not included. "
					"Change data exporting settings to download.)";
			case SkipReason::None: return FormatFilePath(file);
			}
			Unexpected("Skip reason while writing photo path.");
		}();
		block.append(_dataBuilder.prepareArrayItemStart());
		block.append(_dataBuilder.SerializeObject({
			{
				"date",
				userpic.date ? _dataBuilder.SerializeDate(userpic.date) : QByteArray()
			},
			{
				"photo",
				JsonDataBuilder::SerializeString(path)
			},
		}));
	}
	return _output->writeBlock(block);
}

Result JsonWriter::writeUserpicsEnd() {
	Expects(_output != nullptr);

	return _output->writeBlock(_dataBuilder.popNesting());
}

Result JsonWriter::writeContactsList(const Data::ContactsList &data) {
	Expects(_output != nullptr);

	if (const auto result = writeSavedContacts(data); !result) {
		return result;
	} else if (const auto result = writeFrequentContacts(data); !result) {
		return result;
	}
	return Result::Success();
}

Result JsonWriter::writeSavedContacts(const Data::ContactsList &data) {
	Expects(_output != nullptr);

	auto block = _dataBuilder.prepareObjectItemStart("contacts");
	block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
	block.append(_dataBuilder.prepareObjectItemStart("about"));
	block.append(_dataBuilder.SerializeString(_environment.aboutContacts));
	block.append(_dataBuilder.prepareObjectItemStart("list"));
	block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
	for (const auto index : Data::SortedContactsIndices(data)) {
		const auto &contact = data.list[index];
		block.append(_dataBuilder.prepareArrayItemStart());

		if (contact.firstName.isEmpty()
			&& contact.lastName.isEmpty()
			&& contact.phoneNumber.isEmpty()) {
			block.append(_dataBuilder.SerializeObject({
				{ "date", _dataBuilder.SerializeDate(contact.date) }
			}));
		} else {
			block.append(_dataBuilder.SerializeObject({
				{ "user_id", Data::NumberToString(contact.userId) },
				{ "first_name", _dataBuilder.SerializeString(contact.firstName) },
				{ "last_name", _dataBuilder.SerializeString(contact.lastName) },
				{
					"phone_number",
					_dataBuilder.SerializeString(
						Data::FormatPhoneNumber(contact.phoneNumber))
				},
				{ "date", _dataBuilder.SerializeDate(contact.date) }
			}));
		}
	}
	block.append(_dataBuilder.popNesting());
	return _output->writeBlock(block + _dataBuilder.popNesting());
}

Result JsonWriter::writeFrequentContacts(const Data::ContactsList &data) {
	Expects(_output != nullptr);

	auto block = _dataBuilder.prepareObjectItemStart("frequent_contacts");
	block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
	block.append(_dataBuilder.prepareObjectItemStart("about"));
	block.append(_dataBuilder.SerializeString(_environment.aboutFrequent));
	block.append(_dataBuilder.prepareObjectItemStart("list"));
	block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
	const auto writeList = [&](
			const std::vector<Data::TopPeer> &peers,
			Data::Utf8String category) {
		for (const auto &top : peers) {
			const auto type = [&] {
				if (const auto chat = top.peer.chat()) {
					return chat->username.isEmpty()
						? (chat->isBroadcast
							? "private_channel"
							: (chat->isSupergroup
								? "private_supergroup"
								: "private_group"))
						: (chat->isBroadcast
							? "public_channel"
							: "public_supergroup");
				}
				return "user";
			}();
			block.append(_dataBuilder.prepareArrayItemStart());
			block.append(_dataBuilder.SerializeObject({
				{ "id", Data::NumberToString(top.peer.id()) },
				{ "category", _dataBuilder.SerializeString(category) },
				{ "type", _dataBuilder.SerializeString(type) },
				{ "name",  StringAllowNull(top.peer.name()) },
				{ "rating", Data::NumberToString(top.rating) },
			}));
		}
	};
	writeList(data.correspondents, "people");
	writeList(data.inlineBots, "inline_bots");
	writeList(data.phoneCalls, "calls");
	block.append(_dataBuilder.popNesting());
	return _output->writeBlock(block + _dataBuilder.popNesting());
}

Result JsonWriter::writeSessionsList(const Data::SessionsList &data) {
	Expects(_output != nullptr);

	if (const auto result = writeSessions(data); !result) {
		return result;
	} else if (const auto result = writeWebSessions(data); !result) {
		return result;
	}
	return Result::Success();
}

Result JsonWriter::writeOtherData(const Data::File &data) {
	Expects(_output != nullptr);
	Expects(data.skipReason == Data::File::SkipReason::None);
	Expects(!data.relativePath.isEmpty());

	QFile f(pathWithRelativePath(data.relativePath));
	if (!f.open(QIODevice::ReadOnly)) {
		return Result(Result::Type::FatalError, f.fileName());
	}
	const auto content = f.readAll();
	if (content.isEmpty()) {
		return Result::Success();
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(content, &error);
	if (error.error != QJsonParseError::NoError) {
		return Result(Result::Type::FatalError, f.fileName());
	}
	auto block = _dataBuilder.prepareObjectItemStart("other_data");
	Fn<void(const QJsonObject &data)> pushObject;
	Fn<void(const QJsonArray &data)> pushArray;
	Fn<void(const QJsonValue &data)> pushValue;
	pushObject = [&](const QJsonObject &data) {
		block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
		for (auto i = data.begin(); i != data.end(); ++i) {
			if ((*i).type() != QJsonValue::Undefined) {
				block.append(_dataBuilder.prepareObjectItemStart(i.key().toUtf8()));
				pushValue(*i);
			}
		}
		block.append(_dataBuilder.popNesting());
	};
	pushArray = [&](const QJsonArray &data) {
		block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
		for (auto i = data.begin(); i != data.end(); ++i) {
			if ((*i).type() != QJsonValue::Undefined) {
				block.append(_dataBuilder.prepareArrayItemStart());
				pushValue(*i);
			}
		}
		block.append(_dataBuilder.popNesting());
	};
	pushValue = [&](const QJsonValue &data) {
		switch (data.type()) {
		case QJsonValue::Null:
			block.append("null");
			return;
		case QJsonValue::Bool:
			block.append(data.toBool() ? "true" : "false");
			return;
		case QJsonValue::Double:
			block.append(Data::NumberToString(data.toDouble()));
			return;
		case QJsonValue::String:
			block.append(_dataBuilder.SerializeString(data.toString().toUtf8()));
			return;
		case QJsonValue::Array:
			return pushArray(data.toArray());
		case QJsonValue::Object:
			return pushObject(data.toObject());
		}
		Unexpected("Type of json valuein JsonWriter::writeOtherData.");
	};
	if (document.isObject()) {
		pushObject(document.object());
	} else {
		pushArray(document.array());
	}
	return _output->writeBlock(block);
}

Result JsonWriter::writeSessions(const Data::SessionsList &data) {
	Expects(_output != nullptr);

	auto block = _dataBuilder.prepareObjectItemStart("sessions");
	block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
	block.append(_dataBuilder.prepareObjectItemStart("about"));
	block.append(_dataBuilder.SerializeString(_environment.aboutSessions));
	block.append(_dataBuilder.prepareObjectItemStart("list"));
	block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
	for (const auto &session : data.list) {
		block.append(_dataBuilder.prepareArrayItemStart());
		block.append(_dataBuilder.SerializeObject({
			{ "last_active", _dataBuilder.SerializeDate(session.lastActive) },
			{ "last_ip", _dataBuilder.SerializeString(session.ip) },
			{ "last_country", _dataBuilder.SerializeString(session.country) },
			{ "last_region", _dataBuilder.SerializeString(session.region) },
			{
				"application_name",
				StringAllowNull(session.applicationName)
			},
			{
				"application_version",
				StringAllowEmpty(session.applicationVersion)
			},
			{ "device_model", _dataBuilder.SerializeString(session.deviceModel) },
			{ "platform", _dataBuilder.SerializeString(session.platform) },
			{ "system_version", _dataBuilder.SerializeString(session.systemVersion) },
			{ "created", _dataBuilder.SerializeDate(session.created) },
		}));
	}
	block.append(_dataBuilder.popNesting());
	return _output->writeBlock(block + _dataBuilder.popNesting());
}

Result JsonWriter::writeWebSessions(const Data::SessionsList &data) {
	Expects(_output != nullptr);

	auto block = _dataBuilder.prepareObjectItemStart("web_sessions");
	block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
	block.append(_dataBuilder.prepareObjectItemStart("about"));
	block.append(_dataBuilder.SerializeString(_environment.aboutWebSessions));
	block.append(_dataBuilder.prepareObjectItemStart("list"));
	block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
	for (const auto &session : data.webList) {
		block.append(_dataBuilder.prepareArrayItemStart());
		block.append(_dataBuilder.SerializeObject({
			{ "last_active", _dataBuilder.SerializeDate(session.lastActive) },
			{ "last_ip", _dataBuilder.SerializeString(session.ip) },
			{ "last_region", _dataBuilder.SerializeString(session.region) },
			{ "bot_username", StringAllowNull(session.botUsername) },
			{ "domain_name", StringAllowNull(session.domain) },
			{ "browser", _dataBuilder.SerializeString(session.browser) },
			{ "platform", _dataBuilder.SerializeString(session.platform) },
			{ "created", _dataBuilder.SerializeDate(session.created) },
		}));
	}
	block.append(_dataBuilder.popNesting());
	return _output->writeBlock(block + _dataBuilder.popNesting());
}

Result JsonWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return Result::Success();
}

Result JsonWriter::writeDialogStart(const Data::DialogInfo &data) {
	Expects(_output != nullptr);

	const auto result = validateDialogsMode(data.isLeftChannel);
	if (!result) {
		return result;
	}

	using Type = Data::DialogInfo::Type;
	const auto TypeString = [](Type type) {
		switch (type) {
		case Type::Unknown: return "";
		case Type::Self: return "saved_messages";
		case Type::Personal: return "personal_chat";
		case Type::Bot: return "bot_chat";
		case Type::PrivateGroup: return "private_group";
		case Type::PrivateSupergroup: return "private_supergroup";
		case Type::PublicSupergroup: return "public_supergroup";
		case Type::PrivateChannel: return "private_channel";
		case Type::PublicChannel: return "public_channel";
		}
		Unexpected("Dialog type in TypeString.");
	};

	auto block = _dataBuilder.prepareArrayItemStart();
	block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
	if (data.type != Type::Self) {
		block.append(_dataBuilder.prepareObjectItemStart("name")
			+ StringAllowNull(data.name));
	}
	block.append(_dataBuilder.prepareObjectItemStart("type")
		+ StringAllowNull(TypeString(data.type)));
	block.append(_dataBuilder.prepareObjectItemStart("id")
		+ Data::NumberToString(data.peerId));
	block.append(_dataBuilder.prepareObjectItemStart("messages"));
	block.append(_dataBuilder.pushNesting(details::JsonContext::kArray));
	return _output->writeBlock(block);
}

Result JsonWriter::validateDialogsMode(bool isLeftChannel) {
	const auto mode = isLeftChannel
		? DialogsMode::Left
		: DialogsMode::Chats;
	if (_dialogsMode == mode) {
		return Result::Success();
	} else if (_dialogsMode != DialogsMode::None) {
		if (const auto result = writeChatsEnd(); !result) {
			return result;
		}
	}
	_dialogsMode = mode;
	return writeChatsStart(
		isLeftChannel ? "left_chats" : "chats",
		(isLeftChannel
			? _environment.aboutLeftChats
			: _environment.aboutChats));
}

Result JsonWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	Expects(_output != nullptr);

	auto block = QByteArray();
	for (const auto &message : data.list) {
		if (Data::SkipMessageByDate(message, _settings)) {
			continue;
		}
		block.append(_dataBuilder.prepareArrayItemStart() + SerializeMessage(
			_dataBuilder.getContext(),
			message,
			data.peers,
			_environment.internalLinksDomain));
	}
	return block.isEmpty() ? Result::Success() : _output->writeBlock(block);
}

Result JsonWriter::writeDialogEnd() {
	Expects(_output != nullptr);

	auto block = _dataBuilder.popNesting();
	return _output->writeBlock(block + _dataBuilder.popNesting());
}

Result JsonWriter::writeDialogsEnd() {
	return writeChatsEnd();
}

Result JsonWriter::writeChatsStart(
		const QByteArray &listName,
		const QByteArray &about) {
	Expects(_output != nullptr);

	auto block = _dataBuilder.prepareObjectItemStart(listName);
	block.append(_dataBuilder.pushNesting(details::JsonContext::kObject));
	block.append(_dataBuilder.prepareObjectItemStart("about"));
	block.append(_dataBuilder.SerializeString(about));
	block.append(_dataBuilder.prepareObjectItemStart("list"));
	return _output->writeBlock(block + _dataBuilder.pushNesting(details::JsonContext::kArray));
}

Result JsonWriter::writeChatsEnd() {
	Expects(_output != nullptr);

	auto block = _dataBuilder.popNesting();
	return _output->writeBlock(block + _dataBuilder.popNesting());
}

Result JsonWriter::finish() {
	Expects(_output != nullptr);

	auto block = _dataBuilder.popNesting();
	Assert(_dataBuilder.isContextNestingEmpty());
	return _output->writeBlock(block);
}

QString JsonWriter::mainFilePath() {
	return pathWithRelativePath(mainFileRelativePath());
}

QString JsonWriter::mainFileRelativePath() const {
	return "result.json";
}

QString JsonWriter::pathWithRelativePath(const QString &path) const {
	return _settings.path + path;
}

std::unique_ptr<File> JsonWriter::fileWithRelativePath(
		const QString &path) const {
	return std::make_unique<File>(pathWithRelativePath(path), _stats);
}

} // namespace Output
} // namespace Export
