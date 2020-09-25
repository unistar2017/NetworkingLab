#include <iostream>
#include <winsock.h>

using namespace std;

#define PORT 9909

struct sockaddr_in srv;
fd_set fr, fw, fe;
int nMaxFd;
int nSocket;
int nArrClient[5];

void ProccessNewMessage(int nClientSocket)
{
	cout << "Processing the new message from Client Socket : " << nClientSocket << endl;
	char buff[256 + 1] = { 0, };
	int nRet = recv(nClientSocket, buff, 256, 0);

	if (nRet < 0)
	{
		cout << "Something Wrong happend ...... Closing the connection for client: " << nClientSocket << endl;
		closesocket(nClientSocket);
		for (int i = 0; i < 5; i++)
		{
			if (nArrClient[i] == nClientSocket)
			{
				nArrClient[i] = 0;
				break;
			}
		}
	}
	else
	{
		cout << "The message recieved from the Client is:  " << buff << endl;
		//Send the respose
		send(nClientSocket, "Processed your Request", 23, 0);
		cout << endl << "***********************************************";
	}
}

void ProccessTheNewRequest()
{
	//New connection request
	if (FD_ISSET(nSocket, &fr))
	{
		int nLen = sizeof(struct sockaddr);
		int nClientSocket = accept(nSocket, NULL, &nLen);
		if (nClientSocket > 0)
		{
			//Put it into client fd_set.
			int i;
			for (i = 0; i < 5; i++)
			{
				if (nArrClient[i] == 0)
				{
					nArrClient[i] = nClientSocket;
					send(nClientSocket, "Got the connection done Successfully", 37, 0);
					break;
				}
			}
			if (i == 5)
			{
				cout << endl << "No space for a new connection" << endl;
			}
		}
	}
	else
	{
		for (int i = 0; i < 5; i++)
		{
			if (FD_ISSET(nArrClient[i], &fr))
			{
				//Got the new message from Client
				//Just recv n message
				//Just queue that for new worker of your server
				ProccessNewMessage(nArrClient[i]);
			}
		}
	}
}

int main()
{
	int nRet = 0;

	//Initialize the WSA  variables
	WSADATA ws;
	if (WSAStartup(MAKEWORD(2, 2), &ws) < 0)
	{
		cout << endl << "WSA Failed to initialize" << endl;
	}
	else
	{
		cout << endl << "WSA initialize" << endl;
	}



	////Get Host Name
	//char strHostName[32] = { 0, };
	//int nRet = gethostname(strHostName, 32);
	//
	//if (nRet < 0)
	//{
	//	cout << "Call Failed" << endl;
	//}
	//else
	//{
	//	cout << "The Name of the host: " << strHostName << endl;
	//}


	//Initialize the Socket
	nSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (nSocket < 0)
	{
		cout << endl << "The socket not opened" << endl;
		WSACleanup();
		exit(EXIT_FAILURE);
	}
	else
	{
		cout << endl << "The Socket opened successfully" << endl << nSocket << endl;
	}

	//Initialize the neviroment for socketaddr structure
	srv.sin_family = AF_INET;
	srv.sin_port = htons(PORT);
	srv.sin_addr.s_addr = INADDR_ANY;         //same ip as system
	// srv.sin_addr.sin_addr = inet_addr("127.0.0.1")
	memset(&(srv.sin_zero), 0, 8);

	//Setsockopt
	int nOptVal = 0;
	int nOptLen = sizeof(nOptVal); 
	nRet = setsockopt(nSocket, SOL_SOCKET, SO_REUSEADDR, (const char*) & nOptVal, nOptLen);
	if (!nRet)
	{
		cout << endl << "The setsockopt call successful." << endl;
	}
	else
	{
		cout << endl << "The setsockopt call failed." << endl;
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	//Blocking and Non Blocking Sockets
	//optval = 0 means blocking and !=0 means non blocking


	//Bind the socket to local port
	nRet = bind(nSocket, (sockaddr*)&srv, sizeof(sockaddr));
	if (nRet < 0)
	{
		cout << endl << "Failed to bind to local port" << endl;
		WSACleanup();
		exit(EXIT_FAILURE);
	}
	else
	{
		cout << endl << "Successfully bind to local port" << endl;
	}

	//Listen the request from client (queues the requests)
	nRet = listen(nSocket, 5);
	if (nRet < 0)
	{
		cout << endl << "Failed to start listen to local port" << endl;
		WSACleanup();
		exit(EXIT_FAILURE);
	}
	else
	{
		cout << endl << "Started listening to local port" << endl;
	}

	nMaxFd = nSocket;
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	while (1)
	{
		FD_ZERO(&fr);
		FD_ZERO(&fw);
		FD_ZERO(&fe);

		FD_SET(nSocket, &fr);
		FD_SET(nSocket, &fe);

		for (int i = 0; i <= 5; i++)
		{
			if (nArrClient[i] != 0)
			{
				FD_SET(nArrClient[i], &fr);
				FD_SET(nArrClient[i], &fe);
			}
		}

		//Keep waiting for new requests and proceed as per the request
		nRet = select(nMaxFd + 1, &fr, &fw, &fe, &tv);
		if (nRet > 0)
		{
			//When someone connect or communicated with message over a dedicated connection
			cout << "Data on port ...... Proccessing now ...." << endl;
			ProccessTheNewRequest();
			//break;
			//Proccess the request.
			/*if (FD_ISSET(nSocket, &fe))
			{
				cout << "There is an exeption" << endl;
			}
			if (FD_ISSET(nSocket, &fw))
			{
				cout << "Ready to write something" << endl;

			}
			if (FD_ISSET(nSocket, &fr))
			{
				cout << "Ready to read, Something new at the port" << endl;
			}
			break;*/
		}
		else if (nRet == 0)
		{
			//No connection oe communication request made or you can say that none of the socket description
			// are ready
		}
		else
		{
			//It failed and your application should show some useful message
			cout << endl << "I failed" << endl;
			WSACleanup();
			exit(EXIT_FAILURE);
		}
	}
}
