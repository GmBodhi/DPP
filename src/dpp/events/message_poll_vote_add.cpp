/************************************************************************************
 *
 * D++, A Lightweight C++ library for Discord
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2021 Craig Edwards and D++ contributors 
 * (https://github.com/brainboxdotcc/DPP/graphs/contributors)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/
#include <dpp/discordevents.h>
#include <dpp/cluster.h>
#include <dpp/message.h>
#include <dpp/stringops.h>
#include <dpp/json.h>


namespace dpp::events {


/**
 * @brief Handle event
 * 
 * @param client Websocket client (current shard)
 * @param j JSON data for the event
 * @param raw Raw JSON string
 */
void message_poll_vote_add::handle(discord_client* client, json &j, const std::string &raw) {

	if (!client->creator->on_message_poll_vote_add.empty()) {
		json d = j["d"];
		dpp::message_poll_vote_add_t vote(client->owner, client->shard_id, raw);
		vote.user_id = snowflake_not_null(&j, "user_id");
		vote.message_id = snowflake_not_null(&j, "message_id");
		vote.channel_id = snowflake_not_null(&j, "channel_id");
		vote.guild_id = snowflake_not_null(&j, "guild_id");
		vote.answer_id = int32_not_null(&j, "answer_id");
		client->creator->queue_work(1, [c = client->creator, vote]() {
			c->on_message_poll_vote_add.call(vote);
		});
	}
}

};
