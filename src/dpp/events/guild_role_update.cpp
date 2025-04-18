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
#include <dpp/guild.h>
#include <dpp/role.h>
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
void guild_role_update::handle(discord_client* client, json &j, const std::string &raw) {
	json &d = j["d"];
	dpp::snowflake guild_id = snowflake_not_null(&d, "guild_id");
	dpp::guild* g = dpp::find_guild(guild_id);
	if (client->creator->cache_policy.role_policy == dpp::cp_none) {
		dpp::role r;
		r.fill_from_json(guild_id, &d);
		if (!client->creator->on_guild_role_update.empty()) {
			dpp::guild_role_update_t gru(client->owner, client->shard_id, raw);
			gru.updating_guild = g ? *g : guild{};
			gru.updating_guild.id = guild_id;
			gru.updated = r;
			client->creator->queue_work(1, [c = client->creator, gru]() {
				c->on_guild_role_update.call(gru);
			});
		}
	} else {
		json& role = d["role"];
		dpp::role *r = dpp::find_role(snowflake_not_null(&role, "id"));
		if (r) {
			r->fill_from_json(g->id, &role);
			if (!client->creator->on_guild_role_update.empty()) {
				dpp::guild_role_update_t gru(client->owner, client->shard_id, raw);
				gru.updating_guild = g ? *g : guild{};
				gru.updating_guild.id = guild_id;
				gru.updated = *r;
				gru.updated.id = r->id;
				client->creator->queue_work(1, [c = client->creator, gru]() {
					c->on_guild_role_update.call(gru);
				});
			}
		}
	}
}

};