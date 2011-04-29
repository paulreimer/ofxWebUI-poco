#include "WebSocketHandler.h"
#include "WebSocketConfig.h"

#include "Poco/NObserver.h"
//#include "Poco/Exception.h"
#include <iostream>
#include <sstream>
#include <iterator>

#include "md5.h"

using Poco::Net::SocketReactor;
using Poco::Net::SocketAcceptor;
using Poco::Net::ReadableNotification;
using Poco::Net::WritableNotification;
using Poco::Net::ShutdownNotification;
using Poco::Net::ErrorNotification;
using Poco::Net::ServerSocket;
using Poco::Net::StreamSocket;
using Poco::NObserver;
using Poco::AutoPtr;


//--------------------------------------------------------------
WebSocketHandler::WebSocketHandler(StreamSocket& socket, SocketReactor& reactor)
: _socket(socket)
, _reactor(reactor)
{
	gotHandshake		= false;
	sentHandshake		= false;
	origin				= DEFAULT_WEBSOCKET_ORIGIN;
	service				= DEFAULT_WEBSOCKET_SERVICE;
	resource			= DEFAULT_WEBSOCKET_RESOURCE;
	host				= DEFAULT_WEBSOCKET_HOST;
	port				= DEFAULT_WEBSOCKET_PORT;
	protocol			= DEFAULT_WEBSOCKET_PROTOCOL;
	secure				= DEFAULT_WEBSOCKET_SECURE;
	readyState			= CONNECTING;

	bReplicateHeaders	= WEBSOCKET_ACCEPT_ALL_CONNECTIONS;
	bUseSizePreamble	= false;
	bWritable		= false;

	_reactor.addEventHandler(_socket, NObserver<WebSocketHandler, ReadableNotification>	(*this, &WebSocketHandler::onReadable));
	_reactor.addEventHandler(_socket, NObserver<WebSocketHandler, WritableNotification>	(*this, &WebSocketHandler::onWritable));
	_reactor.addEventHandler(_socket, NObserver<WebSocketHandler, ShutdownNotification>	(*this, &WebSocketHandler::onShutdown));
	_reactor.addEventHandler(_socket, NObserver<WebSocketHandler, ErrorNotification>	(*this, &WebSocketHandler::onError));
	_reactor.addEventHandler(_socket, NObserver<WebSocketHandler, IdleNotification>		(*this, &WebSocketHandler::onIdle));
}

//--------------------------------------------------------------
WebSocketHandler::~WebSocketHandler()
{
	requestCloseConnection();
	readyState = CLOSED;
	_reactor.removeEventHandler(_socket, NObserver<WebSocketHandler, ReadableNotification>	(*this, &WebSocketHandler::onReadable));
	_reactor.removeEventHandler(_socket, NObserver<WebSocketHandler, WritableNotification>	(*this, &WebSocketHandler::onWritable));
	_reactor.removeEventHandler(_socket, NObserver<WebSocketHandler, ShutdownNotification>	(*this, &WebSocketHandler::onShutdown));
	_reactor.removeEventHandler(_socket, NObserver<WebSocketHandler, ErrorNotification>		(*this, &WebSocketHandler::onError));
	_reactor.removeEventHandler(_socket, NObserver<WebSocketHandler, IdleNotification>		(*this, &WebSocketHandler::onIdle));
	delete [] buffer;
}

//--------------------------------------------------------------
void
WebSocketHandler::onReadable(const AutoPtr<ReadableNotification>& pNf)
{
	int n = _socket.receiveBytes(buffer, DEFAULT_WEBSOCKET_BUFFER_SIZE);
	const char* packet = handshake(buffer, n); // is a no-op if handshaken

	if (packet)
	{
		int begin_idx=UNSET;
		for (int i=0; i<n; i++)
		{
			if (begin_idx==UNSET && packet[i] == 0x00)
				begin_idx = i;
			else if (begin_idx!=UNSET && (unsigned char)packet[i] == 0xFF)
			{
				std::string frame(packet+begin_idx+1,	//skip 0x00
								  i-begin_idx-1);		//skip 0xFF

				onmessage(frame);

				begin_idx = UNSET;
			}
		}
	}
}

//--------------------------------------------------------------
void
WebSocketHandler::onWritable(const AutoPtr<WritableNotification>& pNf)
{
	bWritable = true;
	if (readyState==OPEN)
		processMessages();
/*
	else {
		if (!sentHandshake)
			sendHandshake();
		
		if (!gotHandshake)
			getHandshake();
	}
*/
}

//--------------------------------------------------------------
void
WebSocketHandler::onIdle(const AutoPtr<IdleNotification>& pNf)
{
	if (readyState==OPEN)
		processMessages();
}

//--------------------------------------------------------------
void
WebSocketHandler::requestCloseConnection()
{
	if (readyState==OPEN)
	{
		std::string requestCloseFrame("\255\000");
		send(requestCloseFrame);
	}
}

//--------------------------------------------------------------
void
WebSocketHandler::onError(const AutoPtr<ErrorNotification>& pNf)
{
	std::cout << "Error'd" << std::endl;
	onerror();
//	requestCloseConnection();
}

//--------------------------------------------------------------
void
WebSocketHandler::onShutdown(const AutoPtr<ShutdownNotification>& pNf)
{
	std::cout << "Shutting down." << std::endl;
	onclose();
	requestCloseConnection();
	readyState = CLOSED;
}

//--------------------------------------------------------------
void
WebSocketHandler::send(const std::string& data)
{
	messages.push(data);
	processMessages();
}

//--------------------------------------------------------------
void
WebSocketHandler::processMessages()
{
	while (!messages.empty() && bWritable)
	{
		std::string& data = messages.front();
		if (bUseSizePreamble)
		{
			std::string sze_bytes;
			sze_bytes = (char)0x80;

			int s = data.size();
			while (s > 0x7F) {
				sze_bytes += (char)(s & 0x7F | 0x80);
				s >>= 7;
			}
			sze_bytes += s & 0x7F;
			
			data = sze_bytes + data;
		}
		else {
			data = (char)0x00 + data + (char)0xFF;
		}

		_socket.sendBytes(data.c_str(), data.size());
		bWritable = false;

		messages.pop();
	}
}

//--------------------------------------------------------------
std::string
WebSocketHandler::readHeader(const char* buffer, int n, std::string header)
{
	char *header_from=NULL, *header_to=NULL;
	header_from = strnstr(buffer, (header+": ").c_str(), n);
	if (header_from != NULL)
	{
		header_from += (header+": ").size();
		header_to = strnstr(header_from, CRLF, (buffer+n)-header_from);
		if (header_to != NULL)
			return std::string(header_from, header_to - header_from);
	}
	return "";
}

//--------------------------------------------------------------
std::string
WebSocketHandler::readResource(const char* buffer, int n)
{
	char *resource_from=NULL, *resource_to=NULL;		
	resource_from = strnstr(buffer, "GET ", n);

	if (resource_from != NULL)
	{
		resource_from += strlen("GET ");
		resource_to = strnstr(resource_from, " ", (buffer+n)-resource_from);
		if (resource_to != NULL)
			return std::string(resource_from, resource_to - resource_from);
	}
	return "";
}

//--------------------------------------------------------------
const char*
WebSocketHandler::handshake(const char* buffer, int n)
{
	if (!sentHandshake)
	{
		std::string challenge((char*)buffer+n-8, 8);

		std::cout << "got handshake " << std::endl;
		
		if (bReplicateHeaders)
		{
			std::string socket = readHeader(buffer, n, "Host");			
			std::string::size_type colon = socket.find_last_of(":");

			if (colon != std::string::npos)
			{
				host		= socket.substr(0, colon);
				port		= atoi(socket.substr(colon+1).c_str());
			}
			origin		= readHeader(buffer, n, "Origin");
			resource	= readResource(buffer, n);

			std::stringstream service_stream;
			service_stream << (secure? "wss://":"ws://") << host << ":" << port << resource;
			service = service_stream.str();
		}

		std::string headerPrefix;
		std::string checksum, challengeResponse;
		std::vector<std::string> keys;
		
		keys.push_back(readHeader(buffer, n, "Sec-WebSocket-Key1"));
		keys.push_back(readHeader(buffer, n, "Sec-WebSocket-Key2"));
		
		if (keys[0].size() && keys[1].size())
		{
			// MD5 sum of concatenation of numbers in Sec-WebSocket-Key{1,2}
			// each divided by number of spaces within
			challengeResponse		= handshakeChallenge(keys, challenge);
			headerPrefix	= "Sec-";
		}
		
		std::string websocketHandshake = std::string(HANDSHAKE_PREAMBLE)
		+ headerPrefix + "WebSocket-Location: "+ service		+ CRLF
		+ headerPrefix + "WebSocket-Origin: "	+ origin		+ CRLF;

		std::string protocol_recv = readHeader(buffer, n, "Sec-WebSocket-Protocol");
		std::string protocol_send;
		if (protocols.size() && protocol_recv.size())
		{
			std::istringstream ss(protocol_recv);
			std::vector<std::string> protocols_recv;
			std::copy(std::istream_iterator<std::string>(ss),
					  std::istream_iterator<std::string>(),
					  std::back_inserter<std::vector<std::string> >(protocols_recv));

			std::vector<std::string>::iterator protocol_iter;
			for (int i=0; i<protocols_recv.size(); ++i)
			{
				protocol_iter = std::find(protocols.begin(), protocols.end(), protocols_recv[i]);
				if (protocol_iter != protocols.end())
				{
					if (!protocol.size())
						protocol = *protocol_iter;

					protocol_send += *protocol_iter + " ";
				}
			}
			
			websocketHandshake += "Sec-WebSocket-Protocol: " + protocol_send + CRLF;
		}
		checksum = md5(challengeResponse);
		websocketHandshake += CRLF + checksum;

		_socket.sendBytes(websocketHandshake.c_str(), websocketHandshake.size());

		std::cout << "sent handshake." << std::endl;
		sentHandshake = true;
	}

	if (!gotHandshake)
	{
		char* found = NULL;
		found = strnstr(buffer, HANDSHAKE_TERMINATION, n);
		if (found != NULL)
		{
			std::cout << "got handshake." << std::endl;
			gotHandshake = true;
			readyState = OPEN;
			onopen();

			found += strlen(HANDSHAKE_TERMINATION);
		}
		return found;
	}

	return buffer;
}

//--------------------------------------------------------------
std::string
WebSocketHandler::handshakeChallenge(std::vector<std::string> &keys, std::string bytes)
{
	std::vector<uint32_t>	digits(keys.size(), 0);
	std::vector<uint32_t>	spaces(keys.size(), 0);

	for (int i=0; i<keys.size(); ++i)
	{
		for (int j=0; j<keys[i].size(); ++j)
		{
			char &c = keys[i][j];
			if ('0' <= c && c <= '9')
				digits[i] = digits[i]*10 + (c-'0');
			else if (c == ' ')
				spaces[i]++;
		}
	}

	std::string challengeResponse;
	for (int i=0; i<keys.size(); ++i)
	{
		if (spaces[i]==0 || (digits[i] % spaces[i]) != 0)
			return "";

		uint32_t number = htonl(digits[i] / spaces[i]);
		challengeResponse += std::string((char*)&number, sizeof(uint32_t));
	}

	challengeResponse += bytes;
	return challengeResponse;
}

//--------------------------------------------------------------
std::string
md5(std::string &bytes, bool bLittleEndian)
{
	std::string md5(16, 0);

	md5_state_t state;
	md5_byte_t digest[16];
	md5_init(&state);
	md5_append(&state, (const md5_byte_t*)bytes.c_str(), bytes.size());
	md5_finish(&state, digest);

	if (bLittleEndian)
		md5 = std::string((char*)digest, 16);
	else
		for (int i=0; i<16; ++i)
			md5[16-1-i] = digest[i];

	return md5;
}
