#include <iostream>
#include <winsock.h>

using namespace std;

#define PORT 9909

struct sockaddr_in srv;
int nClientSocket;


int main()
{
	int nRet = 0;

	//Initialize the WSA  variables
	WSADATA ws;
	if (WSAStartup(MAKEWORD(2, 2), &ws) < 0)
	{
		cout  << "WSA Failed to initialize" << endl;
		WSACleanup();
		return (EXIT_FAILURE);
	}
	
	//Initialize the Socket
	nClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (nClientSocket < 0)
	{
		cout  << "The socket not opened" << endl;
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	srv.sin_family = AF_INET;
	srv.sin_port = htons(PORT);
	//srv.sin_addr.s_addr = INADDR_ANY;         //same ip as system
	srv.sin_addr.s_addr = inet_addr("127.0.0.1");
	memset(&(srv.sin_zero), 0, 8);



	nRet = connect(nClientSocket, (sockaddr*)&srv, sizeof(srv));
	if (nRet < 0)
	{
		cout << "Failed to connect" << endl;
		WSACleanup();
		exit(EXIT_FAILURE);
	}
	else
	{
		cout << endl << "Connected to the Server" << endl;
		char buff[255] = { 0, };
		recv(nClientSocket, buff, 255, 0);
		cout << "Just press any key to see the message from Server" << endl;
		getchar();
		cout << "\n ---------|  " << buff << "  |---------\n";

		cout << "Now send your message to Server: ";
		//Talk to the Server
		while(1)
		{
			fgets(buff, 256, stdin);
			send(nClientSocket, buff, 256, 0);
			cout << "Press any Key to get the respond from Server .. " << endl;
			getchar();
			recv(nClientSocket, buff, 256, 0);
			cout << buff << endl << "Now Send next message: ";
		}
	}

	
}
