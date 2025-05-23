#include <dpp/dpp.h>
#include <iomanip>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ogg/ogg.h>
#include <opus/opusfile.h>

int main(int argc, char const *argv[]) {
	/* Load an ogg opus file into memory.
	 * The bot expects opus packets to be 2 channel stereo, 48000Hz.
	 * 
	 * You may use ffmpeg to encode songs to ogg opus:
	 * ffmpeg -i /path/to/song -c:a libopus -ar 48000 -ac 2 -vn -b:a 96K /path/to/opus.ogg 
	 */

	dpp::cluster bot("token");

	bot.on_log(dpp::utility::cout_logger());

	/* The event is fired when someone issues your commands */
	bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {

		/* Check which command they ran */
		if (event.command.get_command_name() == "join") {

			/* Get the guild */
			dpp::guild* g = dpp::find_guild(event.command.guild_id);

			/* Attempt to connect to a voice channel, returns false if we fail to connect. */
			if (!g->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
				event.reply("You don't seem to be in a voice channel!");
				return;
			}
			
			/* Tell the user we joined their channel. */
			event.reply("Joined your channel!");
		} else if (event.command.get_command_name() == "play") {

			/* Get the voice channel the bot is in, in this current guild. */
			dpp::voiceconn* v = event.from()->get_voice(event.command.guild_id);

			/* If the voice channel was invalid, or there is an issue with it, then tell the user. */
			if (!v || !v->voiceclient || !v->voiceclient->is_ready()) {
				event.reply("There was an issue with getting the voice channel. Make sure I'm in a voice channel!");
				return;
			}

			ogg_sync_state oy; 
			ogg_stream_state os;
			ogg_page og;
			ogg_packet op;
			OpusHead header;
			char *buffer;

			FILE *fd;

			fd = fopen("/path/to/opus.ogg", "rb");

			fseek(fd, 0L, SEEK_END);
			size_t sz = ftell(fd);
			rewind(fd);

			ogg_sync_init(&oy);

			buffer = ogg_sync_buffer(&oy, sz);
			fread(buffer, 1, sz, fd);

			ogg_sync_wrote(&oy, sz);

			/**
			 * We must first verify that the stream is indeed ogg opus
			 * by reading the header and parsing it
			 */
			if (ogg_sync_pageout(&oy, &og) != 1) {
				fprintf(stderr,"Does not appear to be ogg stream.\n");
				exit(1);
			}

			ogg_stream_init(&os, ogg_page_serialno(&og));

			if (ogg_stream_pagein(&os,&og) < 0) {
				fprintf(stderr,"Error reading initial page of ogg stream.\n");
				exit(1);
			}

			if (ogg_stream_packetout(&os,&op) != 1) {
				fprintf(stderr,"Error reading header packet of ogg stream.\n");
				exit(1);
			}

			/* We must ensure that the ogg stream actually contains opus data */
			if (!(op.bytes > 8 && !memcmp("OpusHead", op.packet, 8))) {
				fprintf(stderr,"Not an ogg opus stream.\n");
				exit(1);
			}

			/* Parse the header to get stream info */
			int err = opus_head_parse(&header, op.packet, op.bytes);
			if (err) {
				fprintf(stderr,"Not a ogg opus stream\n");
				exit(1);
			}

			/* Now we ensure the encoding is correct for Discord */
			if (header.channel_count != 2 && header.input_sample_rate != 48000) {
				fprintf(stderr,"Wrong encoding for Discord, must be 48000Hz sample rate with 2 channels.\n");
				exit(1);
			}

			/* Now loop though all the pages and send the packets to the vc */
			while (ogg_sync_pageout(&oy, &og) == 1) {
				ogg_stream_init(&os, ogg_page_serialno(&og));

				if(ogg_stream_pagein(&os,&og)<0) {
					fprintf(stderr,"Error reading page of Ogg bitstream data.\n");
					exit(1);
				}

				while (ogg_stream_packetout(&os,&op) != 0) {

					/* Read remaining headers */
					if (op.bytes > 8 && !memcmp("OpusHead", op.packet, 8)) {
						int err = opus_head_parse(&header, op.packet, op.bytes);
						if (err) {
							fprintf(stderr,"Not a ogg opus stream\n");
							exit(1);
						}

						if (header.channel_count != 2 && header.input_sample_rate != 48000) {
							fprintf(stderr,"Wrong encoding for Discord, must be 48000Hz sample rate with 2 channels.\n");
							exit(1);
						}

						continue;
					}

					/* Skip the opus tags */
					if (op.bytes > 8 && !memcmp("OpusTags", op.packet, 8))
						continue; 

					/* Send the audio */
					int samples = opus_packet_get_samples_per_frame(op.packet, 48000);

					v->voiceclient->send_audio_opus(op.packet, op.bytes, samples / 48);
				}
			}

			/* Cleanup */
			ogg_stream_clear(&os);
			ogg_sync_clear(&oy);

			event.reply("Finished playing the audio file!");
		}
	});

	bot.on_ready([&bot](const dpp::ready_t & event) {
		if (dpp::run_once<struct register_bot_commands>()) {
			/* Create a new command. */
			dpp::slashcommand joincommand("join", "Joins your voice channel.", bot.me.id);
			dpp::slashcommand playcommand("play", "Plays an ogg file.", bot.me.id);

			bot.global_bulk_command_create({ joincommand, playcommand });
		}
	});
	
	/* Start bot */
	bot.start(dpp::st_wait);

	return 0;
}
