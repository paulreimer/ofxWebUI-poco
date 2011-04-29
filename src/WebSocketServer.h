#pragma once

#include "WebSocketReactorThread.h"

template <class HandlerT>
class WebSocketServer
: public WebSocketReactorThread<HandlerT>
{
public:
	void setup()
	{
		WebSocketReactorThread<HandlerT>::run();
	}
	void destroy()
	{
		WebSocketReactorThread<HandlerT>::stop();
	}
};
