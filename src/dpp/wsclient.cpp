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
#include <string>
#include <iostream>
#include <fstream>
#include <dpp/wsclient.h>
#include <dpp/utility.h>
#include <dpp/httpsclient.h>
#include <dpp/discordevents.h>
#include <dpp/cluster.h>

namespace dpp {

constexpr unsigned char WS_MASKBIT = (1u << 7u);
constexpr unsigned char WS_FINBIT = (1u << 7u);
constexpr unsigned char WS_PAYLOAD_LENGTH_MAGIC_LARGE = 126;
constexpr unsigned char WS_PAYLOAD_LENGTH_MAGIC_HUGE = 127;
constexpr size_t WS_MAX_PAYLOAD_LENGTH_SMALL = 125;
constexpr size_t WS_MAX_PAYLOAD_LENGTH_LARGE = 65535;
constexpr size_t MAXHEADERSIZE = sizeof(uint64_t) + 2;

websocket_client::websocket_client(cluster* creator, const std::string& hostname, const std::string& port, const std::string& urlpath, ws_opcode opcode)
	: ssl_connection(creator, hostname, port),
	  state(HTTP_HEADERS),
	  path(urlpath),
	  data_opcode(opcode),
	  timed_out(false),
	  timeout(time(nullptr) + 5)
{
	uint64_t k = (time(nullptr) * time(nullptr));
	/* A 64 bit value as hex with leading zeroes is always 16 chars.
	 *
	 * The request MUST include a header field with the name
	 * |Sec-WebSocket-Key|.  The value of this header field MUST be a
	 * nonce consisting of a randomly selected 16-byte value that has
	 * been base64-encoded (see [Section 4 of
	 * [RFC4648]](https://datatracker.ietf.org/doc/html/rfc4648#section-4)).
	 * The nonce MUST be selected randomly for each connection.
	 */
	key = to_hex<uint64_t>(k);
	key = base64_encode(reinterpret_cast<const unsigned char*>(key.c_str()), key.length());
}

void websocket_client::connect()
{
	state = HTTP_HEADERS;
	/* Send headers synchronously */
	this->socket_write(
		"GET " + this->path + " HTTP/1.1\r\n"
		"Host: " + this->hostname + "\r\n"
		"pragma: no-cache\r\n"
		"User-Agent: " + http_version + "\r\n"
		"Upgrade: WebSocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: " + this->key + "\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n"
	);
}

bool websocket_client::handle_frame(const std::string& buffer, ws_opcode opcode)
{
	/* This is a stub for classes that derive the websocket client */
	return true;
}

size_t websocket_client::fill_header(unsigned char* outbuf, size_t sendlength, ws_opcode opcode)
{
	size_t pos = 0;
	outbuf[pos++] = WS_FINBIT | opcode;

	if (sendlength <= WS_MAX_PAYLOAD_LENGTH_SMALL) {
		outbuf[pos++] = (unsigned int)sendlength;
	} else if (sendlength <= WS_MAX_PAYLOAD_LENGTH_LARGE) {
		outbuf[pos++] = WS_PAYLOAD_LENGTH_MAGIC_LARGE;
		outbuf[pos++] = (sendlength >> 8) & 0xff;
		outbuf[pos++] = sendlength & 0xff;
	} else {
		outbuf[pos++] = WS_PAYLOAD_LENGTH_MAGIC_HUGE;
		const uint64_t len = sendlength;
		for (int i = sizeof(uint64_t)-1; i >= 0; i--) {
			outbuf[pos++] = ((len >> i*8) & 0xff);
		}
	}

	/* Masking - We don't care about masking, but discord insists on it. We send a mask of 0x00000000 because
	 * any value XOR 0 is itself, meaning we dont have to waste time and effort on this crap.
	 */
	outbuf[1] |= WS_MASKBIT;
	outbuf[pos++] = 0;
	outbuf[pos++] = 0;
	outbuf[pos++] = 0;
	outbuf[pos++] = 0;

	return pos;
}


void websocket_client::write(const std::string_view data, ws_opcode _opcode)
{
	if ((_opcode == OP_AUTO ? this->data_opcode : _opcode) == OP_TEXT) {
		log(dpp::ll_trace, std::string("W: ") + data.data());
	} else {
		log(dpp::ll_trace, "W: <binary frame> size=" + std::to_string(data.length()));
	}
	if (state == HTTP_HEADERS) {
		/* Simple write */
		ssl_connection::socket_write(data);
	} else {
		unsigned char out[MAXHEADERSIZE];
		size_t s = this->fill_header(out, data.length(), _opcode == OP_AUTO ? this->data_opcode : _opcode);
		std::string header((const char*)out, s);
		ssl_connection::socket_write(header);
		ssl_connection::socket_write(data);
	}
}

bool websocket_client::handle_buffer(std::string& buffer)
{
	if (state == HTTP_HEADERS) {
		/* We can expect Discord to end all packets with this.
		 * If they don't, something is wrong and we should abort.
		 */
		if (buffer.find("\r\n\r\n") == std::string::npos) {
			return false;
		}

		/* Got all headers, proceed to new state */

		/* Get headers string */
		std::string headers = buffer.substr(0, buffer.find("\r\n\r\n"));

		/* Modify buffer, remove headers section */
		buffer.erase(0, buffer.find("\r\n\r\n") + 4);

		/* Process headers into map */
		std::vector<std::string> h = utility::tokenize(headers);

		/* No headers? Something aint right. */
		if (h.empty()) {
			return false;
		}

		std::string status_line = h[0];
		h.erase(h.begin());
		std::vector<std::string> status = utility::tokenize(status_line, " ");
		/* HTTP/1.1 101 Switching Protocols */
		if (status.size() >= 3 && status[1] == "101") {
			for(auto &hd : h) {
				std::string::size_type sep = hd.find(": ");
				if (sep != std::string::npos) {
					std::string key = hd.substr(0, sep);
					std::string value = hd.substr(sep + 2, hd.length());
					http_headers[key] = value;
				}
			}

			state = CONNECTED;
		} else if (status.size() < 3) {
			log(ll_warning, "Malformed HTTP response on websocket");
			return false;
		} else if (status[1] != "200" && status[1] != "204") {
			log(ll_warning, "Received unhandled code: " + status[1]);
			return false;
		}
	} else if (state == CONNECTED) {
		/* Process packets until we can't (buffer will erase data until parseheader returns false) */
		try {
			while (this->parseheader(buffer)) { }
		}
		catch (const std::exception &e) {
			log(ll_debug, "Receiving exception: " + std::string(e.what()));
			return false;
		}
	}

	return true;
}

ws_state websocket_client::get_state() const
{
	return this->state;
}

bool websocket_client::parseheader(std::string& data)
{
	if (data.size() < 4) {
		/* Not enough data to form a frame yet */
		return false;
	}

	unsigned char opcode = data[0];
	switch (opcode & ~WS_FINBIT) {
		case OP_CONTINUATION:
		case OP_TEXT:
		case OP_BINARY:
		case OP_PING:
		case OP_PONG: {
			unsigned char len1 = data[1];
			unsigned int payloadstartoffset = 2;

			if (len1 & WS_MASKBIT) {
				len1 &= ~WS_MASKBIT;
				payloadstartoffset += 2;
				/* We don't handle masked data, because discord doesn't send it */
				return true;
			}

			/* 6 bit ("small") length frame */
			uint64_t len = len1;

			if (len1 == WS_PAYLOAD_LENGTH_MAGIC_LARGE) {
				/* 24 bit ("large") length frame */
				if (data.length() < 8) {
					/* We don't have a complete header yet */
					return false;
				}

				unsigned char len2 = (unsigned char)data[2];
				unsigned char len3 = (unsigned char)data[3];
				len = (len2 << 8) | len3;

				payloadstartoffset += 2;
			} else if (len1 == WS_PAYLOAD_LENGTH_MAGIC_HUGE) {
				/* 64 bit ("huge") length frame */
				if (data.length() < 10) {
					/* We don't have a complete header yet */
					return false;
				}
				len = 0;
				for (int v = 2, shift = 56; v < 10; ++v, shift -= 8) {
					unsigned char l = (unsigned char)data[v];
					len |= (uint64_t)(l & 0xff) << shift;
				}
				payloadstartoffset += 8;
			}

			if (data.length() < payloadstartoffset + len) {
				/* We don't have a complete frame yet */
				return false;
			}

			/* If we received a ping, we need to handle it. */
			if ((opcode & ~WS_FINBIT) == OP_PING) {
				handle_ping(data.substr(payloadstartoffset, len));
			} else if ((opcode & ~WS_FINBIT) != OP_PONG) { /* Otherwise, handle everything else apart from a PONG. */
				/* Pass this frame to the deriving class */
				if (!this->handle_frame(data.substr(payloadstartoffset, len), static_cast<ws_opcode>(opcode & ~WS_FINBIT))) {
					return false;
				}
			}

			/* Remove this frame from the input buffer */
			data.erase(data.begin(), data.begin() + payloadstartoffset + len);

			return true;
		}
		break;

		case OP_CLOSE: {
			uint16_t error = data[2] & 0xff;
			error <<= 8;
			error |= (data[3] & 0xff);
			this->error(error);
			return false;
		}
		break;

		default: {
			this->error(0);
			return false;
		}
		break;
	}

	return false;
}

void websocket_client::one_second_timer()
{
	time_t now = time(nullptr);

	if (((now % 20) == 0) && (state == CONNECTED)) {
		/* For sending pings, we send with payload */
		unsigned char out[MAXHEADERSIZE];
		std::string payload = "keepalive";
		size_t s = this->fill_header(out, payload.length(), OP_PING);
		std::string header((const char*)out, s);
		ssl_connection::socket_write(header);
		ssl_connection::socket_write(payload);
	}

	/* Handle timeouts for connect(), SSL negotiation and HTTP negotiation */
	if (!timed_out && sfd != INVALID_SOCKET) {
		if (!tcp_connect_done && now >= timeout) {
			log(ll_trace, "Websocket connection timed out: connect()");
			timed_out = true;
			this->close();
		} else if (tcp_connect_done && !connected && now >= timeout && this->state != CONNECTED) {
			log(ll_trace, "Websocket connection timed out: SSL handshake");
			timed_out = true;
			this->close();
		} else if (now >= timeout && this->state != CONNECTED) {
			log(ll_trace, "Websocket connection timed out: HTTP negotiation");
			timed_out = true;
			this->close();
		}
	}
}

void websocket_client::handle_ping(const std::string &payload)
{
	/* For receiving pings we echo back their payload with the type OP_PONG */
	unsigned char out[MAXHEADERSIZE];
	size_t s = this->fill_header(out, payload.length(), OP_PONG);
	std::string header((const char*)out, s);
	ssl_connection::socket_write(header);
	ssl_connection::socket_write(payload);
}

void websocket_client::send_close_packet()
{
	/* This is a 16 bit value representing 1000 in hex (0x03E8), network order.
	 * For an error/close frame, this is all we need to send, just two bytes
	 * and the header. We do this on shutdown of a websocket for graceful close.
	 */
	std::string payload = "\x03\xE8";
	unsigned char out[MAXHEADERSIZE];

	size_t s = this->fill_header(out, payload.length(), OP_CLOSE);
	std::string header((const char*)out, s);
	ssl_connection::socket_write(header);
	ssl_connection::socket_write(payload);
}

void websocket_client::error(uint32_t errorcode)
{
}

void websocket_client::on_disconnect()
{
}

void websocket_client::close()
{
	log(ll_trace, "websocket_client::close()");
	this->on_disconnect();
	this->state = HTTP_HEADERS;
	ssl_connection::close();
}

}
