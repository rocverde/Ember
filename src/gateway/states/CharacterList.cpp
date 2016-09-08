/*
 * Copyright (c) 2016 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CharacterList.h"
#include "../Config.h"
#include "../Locator.h"
#include "../ClientHandler.h"
#include "../RealmQueue.h"
#include "../CharacterService.h"
#include "../ClientConnection.h"
#include "../FilterTypes.h"
#include <logger/Logging.h>
#include <game_protocol/Packets.h>
#include <game_protocol/Opcodes.h>
#include <spark/temp/Account_generated.h>
#include <memory>
#include <vector>

namespace em = ember::messaging;

namespace ember { namespace character_list {

namespace {

void send_character_list_fail(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::SMSG_CHAR_CREATE response;
	response.result = protocol::ResultCode::AUTH_UNAVAILABLE;
	ctx->connection->send(response);
}

void send_character_list(ClientContext* ctx, std::vector<Character>& characters) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	// emulate a quirk of the retail server
	if(Locator::config()->list_zone_hide) {
		for(auto& c : characters) {
			if(c.first_login) {
				c.zone = 0;
			}
		}
	}

	protocol::SMSG_CHAR_ENUM response;
	response.characters = std::move(characters);
	ctx->connection->send(response);
}

void send_character_rename(ClientContext* ctx, protocol::ResultCode result,
                           std::uint64_t id, const std::string& name) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::SMSG_CHAR_RENAME response;
	response.result = result;
	response.id = id;
	response.name = name;
	ctx->connection->send(response);
}

void handle_char_rename(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_CHAR_RENAME packet;

	if(!ctx->handler->packet_deserialise(packet, *ctx->buffer)) {
		return;
	}

	Locator::character()->rename_character(ctx->account_id, packet.id, packet.name,
	                                       [ctx](em::character::Status status,
	                                                   protocol::ResultCode res, std::uint64_t id,
	                                                   const std::string& name) {
		ctx->connection->socket().get_io_service().dispatch([=]() {
			LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

			protocol::ResultCode result = protocol::ResultCode::CHAR_NAME_FAILURE;

			if(status == em::character::Status::OK) {
				result = res;
			}

			if(result == protocol::ResultCode::RESPONSE_SUCCESS) {
				send_character_rename(ctx, result, id, name);
			} else {
				send_character_rename(ctx, result, 0, "");
			}
		});
	});
}

void handle_char_enum(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	Locator::character()->retrieve_characters(ctx->account_id, [ctx](em::character::Status status,
	                                                                       std::vector<Character> characters) {
		ctx->connection->socket().get_io_service().dispatch([=, characters = std::move(characters)]() mutable {
			if(status == em::character::Status::OK) {
				send_character_list(ctx, characters);
			} else {
				send_character_list_fail(ctx);
			}
		});
	});
}

void send_character_delete(ClientContext* ctx, protocol::ResultCode result) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::SMSG_CHAR_DELETE response;
	response.result = result;
	ctx->connection->send(response);
}

void send_character_create(ClientContext* ctx, protocol::ResultCode result) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::SMSG_CHAR_CREATE response;
	response.result = result;
	ctx->connection->send(response);
}

void handle_char_create(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_CHAR_CREATE packet;

	if(!ctx->handler->packet_deserialise(packet, *ctx->buffer)) {
		return;
	}

	Locator::character()->create_character(ctx->account_id, packet.character,
	                                       [ctx](em::character::Status status,
	                                                   boost::optional<protocol::ResultCode> result) {
		ctx->connection->socket().get_io_service().dispatch([ctx, status, result]() {
			if(status == em::character::Status::OK) {
				send_character_create(ctx, *result);
			} else {
				send_character_create(ctx, protocol::ResultCode::CHAR_CREATE_ERROR);
			}
		});
	});
}

void handle_char_delete(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	protocol::CMSG_CHAR_DELETE packet;

	if(!ctx->handler->packet_deserialise(packet, *ctx->buffer)) {
		return;
	}

	Locator::character()->delete_character(ctx->account_id, packet.id,
	                                       [ctx](em::character::Status status,
	                                                   boost::optional<protocol::ResultCode> result) {
		ctx->connection->socket().get_io_service().dispatch([ctx, status, result]() {
			if(status == em::character::Status::OK) {
				send_character_delete(ctx, *result);
			} else {
				send_character_delete(ctx, protocol::ResultCode::CHAR_DELETE_FAILED);
			}
		});
	});
}

void handle_login(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;

	ctx->handler->state_update(ClientState::IN_WORLD);

	protocol::CMSG_PLAYER_LOGIN packet;

	if(!ctx->handler->packet_deserialise(packet, *ctx->buffer)) {
		return;
	}
}

void unhandled_packet(ClientContext* ctx) {
	LOG_TRACE_FILTER_GLOB(LF_NETWORK) << __func__ << LOG_ASYNC;
	ctx->buffer->skip(ctx->header->size - sizeof(protocol::ClientOpcodes));
}

} // unnamed

void enter(ClientContext* ctx) {}

void update(ClientContext* ctx) {
	switch(ctx->header->opcode) {
		case protocol::ClientOpcodes::CMSG_CHAR_ENUM:
			handle_char_enum(ctx);
			break;
		case protocol::ClientOpcodes::CMSG_CHAR_CREATE:
			handle_char_create(ctx);
			break;
		case protocol::ClientOpcodes::CMSG_CHAR_DELETE:
			handle_char_delete(ctx);
			break;
		case protocol::ClientOpcodes::CMSG_CHAR_RENAME:
			handle_char_rename(ctx);
			break;
		case protocol::ClientOpcodes::CMSG_PLAYER_LOGIN:
			handle_login(ctx);
			break;
		default:
			unhandled_packet(ctx);
	}
}

void handle_event(ClientContext* ctx, const Event* event) {

}

void exit(ClientContext* ctx) {
	if(ctx->state == ClientState::SESSION_CLOSED) {
		//--test;
		Locator::queue()->free_slot();
	}
}

}} // character_list, ember