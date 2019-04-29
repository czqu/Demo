#include "stdafx.h"
#include<WinSock2.h>
#include<WS2tcpip.h>
#include <ws2bth.h>

#include "Connect.h"

#ifdef DEBUG
#define CER_PATH  "E:/project/WinSSLSocket/WinSSLSocket/cer/cacert.pem"
#define KEY_PATH  "E:/project/WinSSLSocket/WinSSLSocket/cer/privkey.pem"
#else
#define CER_PATH "cacert.pem"
#define KEY_PATH "privkey.pem"
#endif // DEBUG


DEFINE_GUID(g_guidServiceClass, 0x4E5877C0, 0x8297, 0x4AAE, 0xB7, 0xBD, 0x73, 0xA8, 0xCB, 0xC1, 0xED, 0xAF);

#define CXN_BDADDR_STR_LEN                17   
#define CXN_MAX_INQUIRY_RETRY             3
#define CXN_DELAY_NEXT_INQUIRY            15
#define CXN_SUCCESS                       0
#define CXN_ERROR                         1
#define CXN_DEFAULT_LISTEN_BACKLOG        4


#ifdef DEBUG
#define DEFAULT_PORT "2087"
#else
#define DEFAULT_PORT "2084"
#endif // DEBUG
#define VERSION "2"
#define MAX_REQUEST CXN_TRANSFER_DATA_LENGTH
#define BUF_SIZE 4096
#define WLAN_SUCCESS 0
#define WLAN_ERROR 1
wchar_t g_szRemoteName[BTH_MAX_NAME_SIZE + 1] = { 0 };
wchar_t g_szRemoteAddr[CXN_BDADDR_STR_LEN + 1] = { 0 };
int  g_ulMaxCxnCycles = 1;

Connect::Connect()

{
	bool ret=FALSE;
	m_fConnected = FALSE;
	GetCreditEvent = FALSE;
	memset(error_code, 0, sizeof(error_code));
	

	chCreditBuffer = (char*)malloc(CXN_TRANSFER_DATA_LENGTH * 2);
	pwzUsername = (PWSTR)malloc(CXN_TRANSFER_DATA_LENGTH);
	pwzPassword = (PWSTR)malloc(CXN_TRANSFER_DATA_LENGTH);
	
	if (chCreditBuffer == NULL || pwzUsername == NULL || pwzPassword == NULL)
	{
		
		exit(20001);
	}

	memset(chCreditBuffer, 0, CXN_TRANSFER_DATA_LENGTH * 2);
	memset(pwzUsername, 0, CXN_TRANSFER_DATA_LENGTH);
	memset(pwzPassword, 0, CXN_TRANSFER_DATA_LENGTH);
	ret=InitWlanConnectThread();
	ret=InitBluetoothConnectThread();

	if (!ret)
	{
		error_code[0] = 20002;
		
	}
	NotifyDowork();

}

Connect::~Connect()
{
	UnInitBluetoothConnectThread();
	UnInitWlanConnectThread();
}

HRESULT Connect::GetCredit()
{

	DWORD ret = 0;
	Json::Reader read;
	Json::Value root;


	read.parse(chCreditBuffer, root);
	std::string username = root["username"].asString();
	std::string passwd = root["passwd"].asString();

	size_t usernamesize = username.length();
	wchar_t *usernamebuffer = new wchar_t[usernamesize + 1];
	memset(usernamebuffer, 0, usernamesize + 1);
	ret=MultiByteToWideChar(CP_UTF8, 0, username.c_str(), usernamesize, usernamebuffer, usernamesize * sizeof(wchar_t));
	usernamebuffer[usernamesize] = 0;
	wcscpy(pwzUsername, usernamebuffer);
	if (ret == 0)
	{

		error_code[0] = 20003;
		error_code[1] =GetLastError();
	}

	size_t Passwordsize = passwd.length();
	wchar_t *Passwordbuffer = new wchar_t[Passwordsize + 1];
	memset(Passwordbuffer, 0, Passwordsize + 1);
	MultiByteToWideChar(CP_UTF8, 0, passwd.c_str(), Passwordsize, Passwordbuffer, Passwordsize * sizeof(wchar_t));
	if (ret == 0)
	{

		error_code[0] = 20003;
		error_code[1] = GetLastError();
		
	}
	Passwordbuffer[Passwordsize] = 0;
	wcscpy(pwzPassword, Passwordbuffer);
#ifndef SINGLE_MOUDLE
   _provider->OnConnectStatusChanged();
#endif
	return S_OK;

}
int Connect::BluetoothConnect()
{
	while (true)
	{
		// ÿ5s����һ�Σ�����ֱ��Ӱ����������
		DWORD dwRet = WaitForSingleObject(m_BluetoothConnectThreadNotifyEvent, 5000);

		// ����ѭ��5sû���¼������������κδ���
		if (dwRet == WAIT_TIMEOUT) {
			continue;
		}
		WSADATA     WSAData = { 0 };
		SOCKADDR_BTH RemoteBthAddr = { 0 };
		ULONG       ulRetCode = CXN_SUCCESS;

		if (CXN_SUCCESS == ulRetCode) {
			ulRetCode = WSAStartup(MAKEWORD(2, 2), &WSAData);
			if (CXN_SUCCESS != ulRetCode) {
				error_code[1] = 30001;
			}
		}

		ulRetCode = BluetoothServer(g_ulMaxCxnCycles);

		if (ulRetCode == CXN_ERROR)
		{
			error_code[0] = 30000;
		}
		return(int)ulRetCode;
		// �����¼�
		m_BluetoothConnectThreadNotifyEvent.Reset();
	}
}


ULONG  Connect::BluetoothServer(_In_ int iMaxCxnCycles)
{
	ULONG           ulRetCode = CXN_SUCCESS;
	int             iAddrLen = sizeof(SOCKADDR_BTH);
	int             iCxnCount = 0;
	UINT            iLengthReceived = 0;
	UINT            uiTotalLengthReceived;
	size_t          cbInstanceNameSize = 0;
	char *          pszDataBuffer = NULL;
	char *          pszDataBufferIndex = NULL;
	wchar_t *       pszInstanceName = NULL;
	wchar_t         szThisComputerName[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD           dwLenComputerName = MAX_COMPUTERNAME_LENGTH + 1;
	SOCKET          LocalSocket = INVALID_SOCKET;
	SOCKET          ClientSocket = INVALID_SOCKET;
	WSAQUERYSET     wsaQuerySet = { 0 };
	SOCKADDR_BTH    SockAddrBthLocal = { 0 };
	LPCSADDR_INFO   lpCSAddrInfo = NULL;
	HRESULT         res;


	lpCSAddrInfo = (LPCSADDR_INFO)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		sizeof(CSADDR_INFO));
	if (NULL == lpCSAddrInfo) {
		error_code[1] = 30002;
		ulRetCode = CXN_ERROR;

	}

	if (CXN_SUCCESS == ulRetCode) {

		if (!GetComputerName(szThisComputerName, &dwLenComputerName)) {
			error_code[1] = 30003;
			ulRetCode = CXN_ERROR;
		}
	}


	if (CXN_SUCCESS == ulRetCode) {
		LocalSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
		if (INVALID_SOCKET == LocalSocket) {

			ulRetCode = CXN_ERROR;
		}
	}

	if (CXN_SUCCESS == ulRetCode) {


		SockAddrBthLocal.addressFamily = AF_BTH;
		SockAddrBthLocal.port = BT_PORT_ANY;


		if (SOCKET_ERROR == bind(LocalSocket,
			(struct sockaddr *) &SockAddrBthLocal,
			sizeof(SOCKADDR_BTH))) {

			ulRetCode = CXN_ERROR;
		}
	}

	if (CXN_SUCCESS == ulRetCode) {

		ulRetCode = getsockname(LocalSocket,
			(struct sockaddr *)&SockAddrBthLocal,
			&iAddrLen);
		if (SOCKET_ERROR == ulRetCode) {

			ulRetCode = CXN_ERROR;
		}
	}

	if (CXN_SUCCESS == ulRetCode) {

		lpCSAddrInfo[0].LocalAddr.iSockaddrLength = sizeof(SOCKADDR_BTH);
		lpCSAddrInfo[0].LocalAddr.lpSockaddr = (LPSOCKADDR)&SockAddrBthLocal;
		lpCSAddrInfo[0].RemoteAddr.iSockaddrLength = sizeof(SOCKADDR_BTH);
		lpCSAddrInfo[0].RemoteAddr.lpSockaddr = (LPSOCKADDR)&SockAddrBthLocal;
		lpCSAddrInfo[0].iSocketType = SOCK_STREAM;
		lpCSAddrInfo[0].iProtocol = BTHPROTO_RFCOMM;


		ZeroMemory(&wsaQuerySet, sizeof(WSAQUERYSET));
		wsaQuerySet.dwSize = sizeof(WSAQUERYSET);
		wsaQuerySet.lpServiceClassId = (LPGUID)&g_guidServiceClass;


		res = StringCchLength(szThisComputerName, sizeof(szThisComputerName), &cbInstanceNameSize);
		if (FAILED(res)) {

			ulRetCode = CXN_ERROR;
		}
	}

	if (CXN_SUCCESS == ulRetCode) {
		cbInstanceNameSize += sizeof(CXN_INSTANCE_STRING) + 1;
		pszInstanceName = (LPWSTR)HeapAlloc(GetProcessHeap(),
			HEAP_ZERO_MEMORY,
			cbInstanceNameSize);
		if (NULL == pszInstanceName) {

			ulRetCode = CXN_ERROR;
		}
	}

	if (CXN_SUCCESS == ulRetCode) {
		StringCbPrintf(pszInstanceName, cbInstanceNameSize, L"%s %s", szThisComputerName, CXN_INSTANCE_STRING);
		wsaQuerySet.lpszServiceInstanceName = pszInstanceName;
		wsaQuerySet.lpszComment = (LPWSTR)_T("RemoteFingerprint Service ");
		wsaQuerySet.dwNameSpace = NS_BTH;
		wsaQuerySet.dwNumberOfCsAddrs = 1;
		wsaQuerySet.lpcsaBuffer = lpCSAddrInfo;


		if (SOCKET_ERROR == WSASetService(&wsaQuerySet, RNRSERVICE_REGISTER, 0)) {

			ulRetCode = CXN_ERROR;
		}
	}


	if (CXN_SUCCESS == ulRetCode) {
		if (SOCKET_ERROR == listen(LocalSocket, CXN_DEFAULT_LISTEN_BACKLOG)) {

			ulRetCode = CXN_ERROR;
		}
	}

	if (CXN_SUCCESS == ulRetCode) {

		for (iCxnCount = 0;
			(CXN_SUCCESS == ulRetCode) && ((iCxnCount < iMaxCxnCycles) || (iMaxCxnCycles == 0));
			iCxnCount++) {


			ClientSocket = accept(LocalSocket, NULL, NULL);
			if (INVALID_SOCKET == ClientSocket) {

				ulRetCode = CXN_ERROR;
				break;
			}


			BOOL bContinue = TRUE;
			pszDataBuffer = (char *)HeapAlloc(GetProcessHeap(),
				HEAP_ZERO_MEMORY,
				CXN_TRANSFER_DATA_LENGTH);
			if (NULL == pszDataBuffer) {

				ulRetCode = CXN_ERROR;
				break;
			}
			pszDataBufferIndex = pszDataBuffer;
			uiTotalLengthReceived = 0;
			while (bContinue && (uiTotalLengthReceived < CXN_TRANSFER_DATA_LENGTH)) {

				iLengthReceived = recv(ClientSocket,
					(char *)pszDataBufferIndex,
					(CXN_TRANSFER_DATA_LENGTH - uiTotalLengthReceived),
					0);

				switch (iLengthReceived) {
				case 0:
					bContinue = FALSE;
					break;

				case SOCKET_ERROR:

					bContinue = FALSE;
					ulRetCode = CXN_ERROR;
					break;

				default:


					if (iLengthReceived > (CXN_TRANSFER_DATA_LENGTH - uiTotalLengthReceived)) {

						bContinue = FALSE;
						ulRetCode = CXN_ERROR;
						break;

					}

					pszDataBufferIndex += iLengthReceived;
					uiTotalLengthReceived += iLengthReceived;
					break;
				}
			}

			if (CXN_SUCCESS == ulRetCode) {

				strcpy_s(chCreditBuffer, strlen(pszDataBuffer) + 1, pszDataBuffer);
				GetCreditEvent = TRUE;
				m_fConnected = TRUE;
				GetCredit();


				if (SOCKET_ERROR == closesocket(ClientSocket)) {


					ulRetCode = CXN_ERROR;

				}
				else {

					ClientSocket = INVALID_SOCKET;
				}
			}
		}
	}

	if (INVALID_SOCKET != ClientSocket) {
		closesocket(ClientSocket);
		ClientSocket = INVALID_SOCKET;
	}

	if (INVALID_SOCKET != LocalSocket) {
		closesocket(LocalSocket);
		LocalSocket = INVALID_SOCKET;
	}

	if (NULL != lpCSAddrInfo) {
		HeapFree(GetProcessHeap(), 0, lpCSAddrInfo);
		lpCSAddrInfo = NULL;
	}
	if (NULL != pszInstanceName) {
		HeapFree(GetProcessHeap(), 0, pszInstanceName);
		pszInstanceName = NULL;
	}

	if (NULL != pszDataBuffer) {
		HeapFree(GetProcessHeap(), 0, pszDataBuffer);
		pszDataBuffer = NULL;
	}

	return(ulRetCode);
}

HRESULT Connect::WlanConnect() {
	int hr = -1;
	while (true)
	{
		// ÿ5s����һ�Σ�����ֱ��Ӱ����������
		DWORD dwRet = WaitForSingleObject(m_WlanConnectThreadNotifyEvent, 5000);

		// ����ѭ��5sû���¼������������κδ���
		if (dwRet == WAIT_TIMEOUT) {
			continue;
		}


		//ssl

		SSL_CTX *ctx;
		/* SSL ���ʼ�� */
		SSL_library_init();
		/* �������� SSL �㷨 */
		OpenSSL_add_all_algorithms();
		/* �������� SSL ������Ϣ */
		SSL_load_error_strings();
		/* ����һ�� SSL_CTX  */
		ctx = SSL_CTX_new(TLS_server_method());
		
		if (ctx == NULL) {
			//ERR_print_errors_fp(stdout);
			exit(100086);
		}
		
		InitSSL(ctx);




		WSADATA wsadata;
		SOCKET ListenSocket = INVALID_SOCKET;
		SOCKET ClientSocket = INVALID_SOCKET;
		char szRequest[MAX_REQUEST];
		struct  addrinfo *result = NULL, hints;
		int iResult;
		iResult = WSAStartup(MAKEWORD(2, 2), &wsadata);
		if (iResult != 0)
		{
			return 4;

		}
		//��ַ
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;


		//��ȡ������ַ

		iResult = getaddrinfo(NULL,
			DEFAULT_PORT,
			&hints,
			&result);
		if (iResult != 0)
		{

			WSACleanup();
			return 5;
		}

		//����socket
		ListenSocket = socket(
			result->ai_family,
			result->ai_socktype,
			result->ai_protocol);
		if (ListenSocket == INVALID_SOCKET)
		{

			freeaddrinfo(result);
			WSACleanup();
			return 6;

			//
		}
		//�󶨵��˿�

		iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
		iResult = WSAStartup(MAKEWORD(2, 2), &wsadata);
		if (iResult == SOCKET_ERROR)
		{
			printf("%d\n", WSAGetLastError());

			freeaddrinfo(result);
			closesocket(ListenSocket);
			WSACleanup();
			return 7;

			//
		}

		freeaddrinfo(result);//����ʹ��
		//��ʼ����
		iResult = listen(ListenSocket, SOMAXCONN);
		if (iResult == SOCKET_ERROR)
		{

#ifdef DEBUG
			printf("%d\n", WSAGetLastError());
#endif // DEBUG
			closesocket(ListenSocket);


			WSACleanup();
			return 8;

			//
		}
		while (1)
		{
			SSL* ssl;
			//���տͻ��˵�����
			iResult = WSAStartup(MAKEWORD(2, 2), &wsadata);
			if (iResult != 0)
			{
				return 9;

			}
			ClientSocket = accept(ListenSocket, NULL, NULL);

			if (ClientSocket == INVALID_SOCKET)
			{
#ifdef DEBUG
				printf("%d\n", WSAGetLastError());
#endif // DEBUG
				closesocket(ListenSocket);
				return 2;
				break;

			}
			send(ClientSocket, VERSION,sizeof(VERSION),0);
			/* ���� ctx ����һ���µ� SSL */
			ssl = SSL_new(ctx);
			/* �������û��� socket ���뵽 SSL */
			SSL_set_fd(ssl, ClientSocket);
			/* ���� SSL ���� */
			if (SSL_accept(ssl) == -1) {
				//	perror("accept");
				closesocket(ClientSocket);
				break;
			}


			int len;

			memset(szRequest, 0, sizeof(szRequest));
		//	char test[1024];
			len = SSL_read(ssl, szRequest, MAX_REQUEST);
#ifdef DEBUG
			int ret = SSL_get_error(ssl, len);
			
#endif // DEBUG

		

			if (len > 0)
			{
				for (int i = len + 1; i < sizeof(szRequest) / sizeof(char); i++)
				{
					szRequest[i] = '\0';
				}
				strcpy_s(chCreditBuffer, sizeof(szRequest), szRequest);

				GetCreditEvent = TRUE;
				m_fConnected = TRUE;
				GetCredit();
				
			}

			else
			{
				UnInitSSL(ssl);
				closesocket(ClientSocket);
				
			}
			
			UnInitSSL(ssl);
		}
		
		WSACleanup();
		// �����¼�
		m_WlanConnectThreadNotifyEvent.Reset();

	}

}



DWORD  WINAPI Connect::BluetoothConnectThread(
	LPVOID lpParameter)
{
	if (lpParameter == NULL) {
		return 0;
	}

	Connect *lpThis = reinterpret_cast<Connect *>(lpParameter);
	return lpThis->BluetoothConnect();


}
DWORD WINAPI Connect::WlanConnectThread(
	LPVOID lpParameter
)
{


	if (lpParameter == NULL) {
		return 0;
	}

	Connect *lpThis = reinterpret_cast<Connect *>(lpParameter);
	return lpThis->WlanConnect();


}

bool Connect::InitBluetoothConnectThread() {
	// �����¼�
	BOOL bRet = m_BluetoothConnectThreadNotifyEvent.Create(NULL, TRUE, FALSE, NULL);
	if (!bRet) {
		return false;
	}

	// ����ķ�ʽ�����߳�
	m_bThread = CreateThread(NULL, 0, &Connect::BluetoothConnectThread, this, CREATE_SUSPENDED, NULL);
	if (NULL == m_bThread) {
		return false;
	}

	// �����߳�
	ResumeThread(m_bThread);
	return true;


}
bool Connect::UnInitBluetoothConnectThread() {

	// ֪ͨ�̴߳�������
	if (m_BluetoothConnectThreadNotifyEvent != NULL) {
		m_BluetoothConnectThreadNotifyEvent.Set();
	}

	if (m_bThread != NULL)
	{
		// Ԥ��100ms���̴߳��������ݣ�100ms�Ǹ���ֵ
		WaitForSingleObject(m_bThread, 100);
		CloseHandle(m_bThread);
		m_bThread = NULL;
	}

	return true;

}

bool Connect::InitWlanConnectThread() {
	// �����¼�
	BOOL bRet = m_WlanConnectThreadNotifyEvent.Create(NULL, TRUE, FALSE, NULL);
	if (!bRet) {
		return false;
	}

	// ����ķ�ʽ�����߳�
	m_wThread = CreateThread(NULL, 0, &Connect::WlanConnectThread, this, CREATE_SUSPENDED, NULL);
	if (NULL == m_wThread) {
		return false;
	}

	// �����߳�
	ResumeThread(m_wThread);
	return true;


}
bool Connect::UnInitWlanConnectThread() {

	// ֪ͨ�̴߳�������
	if (m_WlanConnectThreadNotifyEvent != NULL) {
		m_WlanConnectThreadNotifyEvent.Set();
	}

	if (m_wThread != NULL)
	{
		// Ԥ��100ms���̴߳��������ݣ�100ms�Ǹ���ֵ
		WaitForSingleObject(m_wThread, 100);
		CloseHandle(m_wThread);
		m_wThread = NULL;
	}

	return true;

}

HRESULT Connect::NotifyDowork()
{

	m_BluetoothConnectThreadNotifyEvent.Set();    // ֪ͨ�̸߳��������ˣ�
	m_WlanConnectThreadNotifyEvent.Set();
	return S_OK;

}
#ifndef SINGLE_MOUDLE
void Connect::Initialize(CProvider *_pcpro)

{

	_provider = _pcpro;
}
#endif

void Connect::Restart()
{
	UnInitBluetoothConnectThread();
	UnInitWlanConnectThread();
	Sleep(100);

	m_fConnected = FALSE;
	GetCreditEvent = FALSE;

	memset(chCreditBuffer, 0, CXN_TRANSFER_DATA_LENGTH);
	memset(pwzUsername, 0, CXN_TRANSFER_DATA_LENGTH);
	memset(pwzPassword, 0, CXN_TRANSFER_DATA_LENGTH);
	InitWlanConnectThread();
	InitBluetoothConnectThread();
	NotifyDowork();
}

HRESULT  Connect::InitSSL(SSL_CTX *ctx) {


	/* �����û�������֤�飬 ��֤���������͸��ͻ��ˡ� ֤��������й�Կ */
	if (SSL_CTX_use_certificate_file(ctx, CER_PATH, SSL_FILETYPE_PEM) <= 0) {
		ERR_print_errors_fp(stdout);
		return S_FALSE;
	}
	/* �����û�˽Կ */
	if (SSL_CTX_use_PrivateKey_file(ctx, KEY_PATH, SSL_FILETYPE_PEM) <= 0) {
		ERR_print_errors_fp(stdout);
		return S_FALSE;
	}
	/* ����û�˽Կ�Ƿ���ȷ */
	if (!SSL_CTX_check_private_key(ctx)) {
		ERR_print_errors_fp(stdout);
		return S_FALSE;
	}
	return S_OK;
}
HRESULT  Connect::UnInitSSL(SSL *ssl)

{
	/* �ر� SSL ���� */
	SSL_shutdown(ssl);
	/* �ͷ� SSL */
	SSL_free(ssl);
	/* �ر� socket */


	return S_OK;
}