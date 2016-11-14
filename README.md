# kcp-server
a simple server for kcp

## options for server
	port:						udp port for listen
	keep_session_time:			keep alive time for one session
	package_recv_cb_func: 		when package received, callback this func
	session_kick_cb_func:		when session kick by system, callback this func
	error_log_reporter			call this func when need report some error log

## Usage
```cpp
KCPServer server;
server.Start();
while(true) {
	isleep(1);
	server.Update();
}
```