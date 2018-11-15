#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>


#pragma comment (lib, "Ws2_32.lib")


#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"

struct IoOperationData {
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	CHAR buffer[DEFAULT_BUFLEN];
	DWORD bytesSent;
	DWORD bytesRecv;
};

struct ConnectionData {
	SOCKET socket;
};


DWORD WINAPI ServerWorkerThread(LPVOID pCompletionPort);


int __cdecl main(void)
{
	int error;

	WSADATA wsaData;
	error = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (error != 0) {
		printf("WSAStartup failed with error: %d\n", error);
		return EXIT_FAILURE;
	}

	// ������� ���� ����������
	HANDLE hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hCompletionPort == NULL) {
		printf("CreateIoCompletionPort failed with error %d\n", GetLastError());
		WSACleanup();
		return EXIT_FAILURE;
	}

	// ���������� ���������� ����������� � �������
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);

	// ������� ������� ������ � ����������� �� ���������� �����������, �� ��� ������ �� ���������
	for (int i = 0; i < (int)systemInfo.dwNumberOfProcessors * 2; ++i) {
		// ������� ����� � �������� � ���� ���� ����������
		DWORD threadId;
		HANDLE hThread = CreateThread(NULL, 0, ServerWorkerThread, hCompletionPort, 0, &threadId);
		if (hThread == NULL) {
			printf("CreateThread() failed with error %d\n", GetLastError());
			WSACleanup();
			CloseHandle(hCompletionPort);
			return EXIT_FAILURE;
		}

		// ��������� ���������� ������, ����� ��� ���� �� �����������
		CloseHandle(hThread);
	}

	struct addrinfo hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// ����������� ����� � ����� �����
	struct addrinfo *localAddr = NULL;
	error = getaddrinfo(NULL, DEFAULT_PORT, &hints, &localAddr);
	if (error != 0) {
		printf("getaddrinfo failed with error: %d\n", error);
		WSACleanup();
		return EXIT_FAILURE;
	}

	SOCKET listenSocket = WSASocketW(localAddr->ai_family, localAddr->ai_socktype, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(localAddr);
		WSACleanup();
		return EXIT_FAILURE;
	}

	// ����������� ����� TCP � ������ � ���� �����������
	error = bind(listenSocket, localAddr->ai_addr, (int)localAddr->ai_addrlen);
	if (error == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(localAddr);
		closesocket(listenSocket);
		WSACleanup();
		return EXIT_FAILURE;
	}

	freeaddrinfo(localAddr);

	error = listen(listenSocket, SOMAXCONN);
	if (error == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		WSACleanup();
		return EXIT_FAILURE;
	}

	// ��������� ���������� � ��������� �� � ������ ����������
	for ( ; ; ) {
		SOCKET clientSocket = WSAAccept(listenSocket, NULL, NULL, NULL, 0);
		if (clientSocket == SOCKET_ERROR) {
			printf("WSAAccept failed with error %d\n", WSAGetLastError());
			return EXIT_FAILURE;
		}

		ConnectionData *pConnData = new ConnectionData;
		pConnData->socket = clientSocket;
		// ��������� ���������� ����� � ������ ����������
		if (CreateIoCompletionPort((HANDLE)clientSocket, hCompletionPort, (ULONG_PTR)pConnData, 0) == NULL) {
			printf("CreateIoCompletionPort failed with error %d\n", GetLastError());
			return EXIT_FAILURE;
		}

		//  ������� ��������� ��� �������� �����-������ � ��������� ���������
		IoOperationData *pIoData = new IoOperationData;
		ZeroMemory(&(pIoData->overlapped), sizeof(OVERLAPPED));
		pIoData->bytesSent = 0;
		pIoData->bytesRecv = 0;
		pIoData->wsaBuf.len = DEFAULT_BUFLEN;
		pIoData->wsaBuf.buf = pIoData->buffer;

		DWORD flags = 0;
		DWORD bytesRecv;
		if (WSARecv(clientSocket, &(pIoData->wsaBuf), 1, &bytesRecv, &flags, &(pIoData->overlapped), NULL) == SOCKET_ERROR) {
			if (WSAGetLastError() != ERROR_IO_PENDING) {
				printf("WSARecv failed with error %d\n", WSAGetLastError());
				return EXIT_FAILURE;
			}
		}
	}

}


DWORD WINAPI ServerWorkerThread(LPVOID pCompletionPort)
{
	HANDLE hCompletionPort = (HANDLE)pCompletionPort;

	for ( ; ; ) {
		DWORD bytesTransferred;
		ConnectionData *pConnectionData;
		IoOperationData *pIoData;
		if (GetQueuedCompletionStatus(hCompletionPort, &bytesTransferred,
			(PULONG_PTR)&pConnectionData, (LPOVERLAPPED *)&pIoData, INFINITE) == 0) {
			printf("GetQueuedCompletionStatus() failed with error %d\n", GetLastError());
			return 0;
		}

		// ��������, �� ���� �� ������� � ������� � �� ���� �� ������� ����������
		if (bytesTransferred == 0) {
			closesocket(pConnectionData->socket);
			delete pConnectionData;
			delete pIoData;
			continue;
		}

		// ���� bytesRecv ����� 0, �� �� ������ ��������� ������ �� �������
		// � ����������� ������ WSARecv()
		if (pIoData->bytesRecv == 0) {
			pIoData->bytesRecv = bytesTransferred;
			pIoData->bytesSent = 0;
		} else {
			pIoData->bytesSent += bytesTransferred;
		}

		if (pIoData->bytesRecv > pIoData->bytesSent) {

			DWORD bytesSent;
			// �������� �������� ������ �� ����-����� WSASend()
			// ��� ��� WSASend() ����� ��������� �� ��� ������, �� �� ����������
			// ���������� ������ �� ������ ���� �� ����� ���������� ���
			ZeroMemory(&(pIoData->overlapped), sizeof(OVERLAPPED));
			pIoData->wsaBuf.buf = pIoData->buffer + pIoData->bytesSent;
			pIoData->wsaBuf.len = pIoData->bytesRecv - pIoData->bytesSent;
			if (WSASend(pConnectionData->socket, &(pIoData->wsaBuf), 1, &bytesSent, 0, &(pIoData->overlapped), NULL) == SOCKET_ERROR) {
				if (WSAGetLastError() != ERROR_IO_PENDING) {
					printf("WSASend failed with error %d\n", WSAGetLastError());
					return 0;
				}
			}

		} else {

			DWORD bytesRecv;
			pIoData->bytesRecv = 0;
			// ����� ��� ������ ����������, �������� ������ �����-������ �� ������ WSARecv()
			DWORD flags = 0;
			ZeroMemory(&(pIoData->overlapped), sizeof(OVERLAPPED));
			pIoData->wsaBuf.len = DEFAULT_BUFLEN;
			pIoData->wsaBuf.buf = pIoData->buffer;
			if (WSARecv(pConnectionData->socket, &(pIoData->wsaBuf), 1, &bytesRecv, &flags, &(pIoData->overlapped), NULL) == SOCKET_ERROR) {
				if (WSAGetLastError() != ERROR_IO_PENDING) {
					printf("WSARecv failed with error %d\n", WSAGetLastError());
					return 0;
				}
			}

		}
	}
}
