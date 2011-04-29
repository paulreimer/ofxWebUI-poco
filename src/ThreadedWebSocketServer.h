#pragma once

#include "ofThread.h"
#include "WebSocketConfig.h"

#include "Poco/Net/SocketReactor.h"
using Poco::Net::SocketReactor;

template <class HandlerT>
class WebSocketReactorThread 
: public ofThread
{
public:
	WebSocketReactorThread()
	{
		port = DEFAULT_WEBSOCKET_PORT;
	}

	~WebSocketReactorThread()
	{
		stop();
	}
	
	void run()
	{
		if (!isThreadRunning())
			startThread(false, false); // blocking, non-verbose
	}

	void stop()
	{
		if (isThreadRunning())
		{
			reactor.stop();
			stopThread();
		}
	}		

	void threadedFunction()
	{
		Poco::Net::ServerSocket svs(port);
		Poco::Net::SocketAcceptor<HandlerT> acceptor(svs, reactor);
		
		reactor.run();
	}
	
	unsigned short port;
	
private:
	SocketReactor reactor;
};
