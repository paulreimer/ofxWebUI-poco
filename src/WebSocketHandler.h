#pragma once

#include "WebSocketConfig.h"

#include "Poco/Net/SocketReactor.h"
#include "Poco/Net/SocketAcceptor.h"
#include "Poco/Net/SocketNotification.h"
#include "Poco/Net/StreamSocket.h"

#include <queue>

using Poco::Net::SocketReactor;
using Poco::Net::SocketAcceptor;
using Poco::Net::ReadableNotification;
using Poco::Net::WritableNotification;
using Poco::Net::ErrorNotification;
using Poco::Net::ShutdownNotification;
using Poco::Net::IdleNotification;
using Poco::Net::StreamSocket;
using Poco::AutoPtr;

class WebSocketHandler
{
public:
	WebSocketHandler(StreamSocket& socket, SocketReactor& reactor);
	~WebSocketHandler();
	
	void send(const std::string& data);

	enum readyStates {
		CONNECTING	= 0,
		OPEN		= 1,
		CLOSING		= 2,
		CLOSED		= 3,
	} readyState;

protected:
	bool bUseSizePreamble;
	
	virtual void onopen()
	{}
	virtual void onclose()
	{}
	virtual void onmessage(const std::string& frame)
	{}
	virtual void onerror(bool wasClean=true)
	{}

	std::queue<std::string> messages;	

	std::string	origin;
	std::string	service;
	std::string	host;
	std::string	resource;
	std::string	protocol;
	std::vector<std::string> protocols;
	unsigned short port;
	bool secure;
	
private:
	void processMessages();

	void onReadable	(const AutoPtr<ReadableNotification>&	pNf);
	void onWritable	(const AutoPtr<WritableNotification>&	pNf);
	void onShutdown	(const AutoPtr<ShutdownNotification>&	pNf);
	void onError	(const AutoPtr<ErrorNotification>&		pNf);
	void onIdle		(const AutoPtr<IdleNotification>&		pNf);

	void requestCloseConnection();

	const char* handshake(const char* buffer, int n);
	std::string handshakeChallenge(std::vector<std::string> &keys, std::string bytes);

	std::string readHeader(const char* buffer=NULL, int n=0, std::string header="");
	std::string readResource(const char* buffer=NULL, int n=0);

	StreamSocket	_socket;
	SocketReactor&	_reactor;

	char		buffer[DEFAULT_WEBSOCKET_BUFFER_SIZE];
	bool		sentHandshake, gotHandshake;
	bool		bReplicateHeaders;
	bool		bWritable;
};

std::string md5(std::string& bytes, bool bLittleEndian=true);
