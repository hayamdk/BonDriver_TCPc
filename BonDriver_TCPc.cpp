#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")

#include <winsock2.h>
#include <Windows.h>
//#include <sys/timeb.h>
//#include <time.h>
#include <shlwapi.h>
#include <stdio.h>
#include <locale.h>

#include "IBonDriver.h"
#include "IBonDriver2.h"

#define BUFSIZE				4*1024*1024
#define MSG_SIZE			188*256
#define MAX_SPACE_NUM		8
#define MAX_CHANNEL_NUM		64

WCHAR *chlist_ext = L".ChSet.txt";

/*static inline __int64 gettime()
{
	__int64 result;
	_timeb tv;
	_ftime(&tv);
	result = (__int64)tv.time * 1000;
	result += tv.millitm;
	return result;
}*/

typedef struct{
	WCHAR *chname;
	struct in_addr addr;
	USHORT port;
} ch_info_t;

typedef struct{
	WCHAR *spname;
	int n_chs;
	ch_info_t chs[MAX_CHANNEL_NUM];
} sp_info_t;

int n_sps = 0;
sp_info_t sps[MAX_CHANNEL_NUM];

class CTCPcTuner : public IBonDriver2
{
public:
	CTCPcTuner();
	virtual ~CTCPcTuner();

	// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);

	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);

	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);

	void PurgeTsStream(void);

	// IBonDriver2(暫定)
	LPCTSTR GetTunerName(void);

	const BOOL IsTunerOpening(void);

	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);

	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);

	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

	void Release(void);

	/////

private:
	int connected;
	int connect_failed;
	SOCKET sock;
	struct sockaddr_in addr;
	BYTE *buf;
	int buf_filled;
	int buf_pos;
	int opened;

	int curr_sp;
	int curr_ch;

	void minimize_buf();
	void recv_from_server();
	void connect_to_server();
	void disconnect_from_server();
};

extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
	return (IBonDriver *)(new CTCPcTuner);
}

CTCPcTuner::CTCPcTuner()
{
	buf = new BYTE[BUFSIZE];
	buf_filled = 0;
	buf_pos = 0;
	connected = 0;
	connect_failed = 0;

	curr_sp = 0;
	curr_ch = 0;
}

CTCPcTuner::~CTCPcTuner()
{
	delete buf;
}

const BOOL CTCPcTuner::OpenTuner()
{
	if (opened) {
		return FALSE;
	}
	opened = 1;
	return TRUE;
}

void CTCPcTuner::CloseTuner()
{
	disconnect_from_server();
	opened = 0;
}

const BOOL CTCPcTuner::SetChannel(const BYTE bCh)
{
	return FALSE;
}

const float CTCPcTuner::GetSignalLevel()
{
	if (connected) {
		return 10.0;
	}
	return 0.0;
}

void CTCPcTuner::minimize_buf()
{
	if (!connected) {
		return;
	}

	memmove(buf, &buf[buf_pos], buf_filled - buf_pos);
	buf_filled -= buf_pos;
	buf_pos = 0;
}

void CTCPcTuner::connect_to_server()
{
	int ret;
	fd_set fds;
	struct timeval tv;

	WSADATA wsad;

	if (WSAStartup(MAKEWORD(2, 0), &wsad) != 0) {
		MessageBox(NULL, L"WSAStartup() error!", L"error", MB_ICONSTOP);
		return;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) {
		MessageBox(NULL, L"socket() error!", L"error", MB_ICONSTOP);
		WSACleanup();
		return;
	}

	u_long val = 1;
	if (ioctlsocket(sock, FIONBIO, &val) != NO_ERROR) {
		MessageBox(NULL, L"ソケットをノンブロッキングモードに設定できません", L"error", MB_ICONSTOP);
		shutdown(sock, SD_BOTH);
		closesocket(sock);
		WSACleanup();
		return;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = sps[curr_sp].chs[curr_ch].port;
	addr.sin_addr = sps[curr_sp].chs[curr_ch].addr;

	/*WCHAR wcs[256];
	wsprintf(wcs, L"%d,%d %s(%X) %d", curr_sp, curr_ch, sps[curr_sp].chs[curr_ch].chname,
		addr.sin_addr,(int)ntohs(addr.sin_port));
	MessageBox(NULL, wcs, L"connect!", NULL);*/

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	FD_ZERO(&fds);
	FD_SET(sock, &fds);

	ret = connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
	if (ret == SOCKET_ERROR) {
		if (WSAGetLastError() == WSAEWOULDBLOCK) {
			if (select(0, &fds, NULL, NULL, &tv) == 1) {
				connected = 1;
				connect_failed = 0;
				return;
			}
		}
	}
	//MessageBox(NULL, L"connectに失敗", L"error", MB_ICONSTOP);
	connected = 0;
	connect_failed = 1;
}

static void send_end_msg(SOCKET sock)
{
	int ret;
	while (1) {
		/* 1文字送る = 終了の合図 */
		ret = send(sock, "A", 1, 0);
		if (ret == SOCKET_ERROR) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				/* sendがブロック以外の何らかのエラーで失敗したので終了 */
				break;
			}
		} else {
			/* sendに成功したので終了 */
			break;
		}
	}
}

static void wait_endof_socket(SOCKET sock)
{
	int ret;
	char tmpbuf[MSG_SIZE];
	while (1) {
		ret = recv(sock, tmpbuf, MSG_SIZE, 0);
		if (ret == SOCKET_ERROR) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				/* recvがブロック以外の何らかのエラーで失敗したので終了 */
				return;
			}
		} else if ( ret == 0 ) {
			/* ソケットのEOFなので終了 */
			return;
		}
	}
}

void CTCPcTuner::disconnect_from_server()
{
	//MessageBox(NULL, L"disconnect!", L"", MB_OK);

	if (connected) {
		send_end_msg(sock);
		wait_endof_socket(sock);
		shutdown(sock, SD_BOTH);
		closesocket(sock);
		WSACleanup();
	} else if (connect_failed) {
		shutdown(sock, SD_BOTH);
		closesocket(sock);
		WSACleanup();
	}

	connected = 0;
	connect_failed = 0;
}

void CTCPcTuner::recv_from_server()
{
	int ret;
	static unsigned char tmpbuf[MSG_SIZE];

	if (connect_failed) {
		return;
	}

	if (!connected) {
		connect_to_server();
	}

	minimize_buf();

	//printf("%d %d -> ", buf_pos, buf_filled);

	while(1) {
		ret = recv(sock, (char*)tmpbuf, MSG_SIZE, 0);
		if (ret == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK) {
				//printf("block!\n");
				/* 何も受信するものが無い */
			} else {
				/* ERROR */
				//MessageBox(NULL, L"recv error", L"", MB_OK);
			}
			break;
		} else {
			/* バッファが空いてるときのみコピー、そうじゃない場合は捨てる */
			if (buf_filled + ret <= BUFSIZE) {
				memcpy(&buf[buf_filled], tmpbuf, ret);
				buf_filled += ret;
			}
		}
	}

	//printf("%d %d\n", buf_pos, buf_filled);
}

const DWORD CTCPcTuner::WaitTsStream(const DWORD dwTimeOut)
{
	int ret;
	fd_set fds;
	struct timeval tv;

	if (connect_failed) {
		return WAIT_ABANDONED;
	}

	if (!connected) {
		connect_to_server();
	}

	tv.tv_sec = dwTimeOut/1000;
	tv.tv_usec = dwTimeOut%1000;

	FD_ZERO(&fds);
	FD_SET(sock, &fds);

	ret = select(0, &fds, NULL, NULL, &tv);

	if ( ret == 1) {
		/* 時間内にデータが到着した */
		recv_from_server();
		return WAIT_OBJECT_0;
	} else if ( ret == 0 ) {
		/* タイムアウト */
		return WAIT_TIMEOUT;
	}
	/* エラー */
	return WAIT_FAILED;
}

const DWORD CTCPcTuner::GetReadyCount()
{
	recv_from_server();
	return buf_filled / MSG_SIZE;
}

const BOOL CTCPcTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc;
	BOOL ret;
	ret = GetTsStream(&pSrc, pdwSize, pdwRemain);
	if (pdwSize > 0) {
		memcpy(pDst, pSrc, *pdwSize);
	}
	return ret;
}

const BOOL CTCPcTuner::GetTsStream(BYTE **pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	int remain;

	recv_from_server();
	remain = buf_filled - buf_pos;

	*pDst = buf;

	if (remain < MSG_SIZE) {
		*pdwSize = 0;
		*pdwRemain = 0;
		return FALSE;
	}

	buf_pos += MSG_SIZE;
	*pdwSize = MSG_SIZE;
	*pdwRemain = remain / MSG_SIZE;

	return TRUE;
}

void CTCPcTuner::PurgeTsStream()
{
	buf_filled = 0;
	buf_pos = 0;
}

void CTCPcTuner::Release()
{
	if (opened) {
		CloseTuner();
	}
	free(buf);
}

LPCTSTR CTCPcTuner::GetTunerName()
{
	return L"BonTCPc";
}

const BOOL CTCPcTuner::IsTunerOpening()
{
	return opened;
}

LPCTSTR CTCPcTuner::EnumTuningSpace(const DWORD dwSpace)
{
	if ((int)dwSpace < n_sps) {
		return sps[dwSpace].spname;
	}
	return NULL;
}

LPCTSTR CTCPcTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if ((int)dwSpace < n_sps) {
		if ((int)dwChannel < sps[dwSpace].n_chs) {
			return sps[dwSpace].chs[dwChannel].chname;
		}
	}
	return NULL;
}

const BOOL CTCPcTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	WCHAR str[1024];
	swprintf_s(str, L"%d, %d -> %d, %d", curr_sp, curr_ch, (int)dwSpace, (int)dwChannel);
	//MessageBox(NULL, str, L"SetChannel", MB_OK);

	if ((int)dwSpace < n_sps) {
		if ((int)dwChannel < sps[dwSpace].n_chs) {
			if ( curr_sp != (int)dwSpace || curr_ch != (int)dwChannel ) {
				disconnect_from_server();
			}

			curr_sp = dwSpace;
			curr_ch = dwChannel;
			return TRUE;
		}
	}
	return FALSE;
}

const DWORD CTCPcTuner::GetCurSpace(void)
{
	return curr_sp;
}

const DWORD CTCPcTuner::GetCurChannel(void)
{
	return curr_ch;
}

void parse_channel_list(HINSTANCE hinstDLL)
{
	WCHAR path[MAX_PATH+1];
	WCHAR line[256], *ip_str, *port_str, last_ip_str[256];
	WCHAR chname[256];
	char ip_mbstr[256];
	FILE *fp;
	int len, sp, i, port;

	setlocale(LC_CTYPE, "jpn");

	GetModuleFileName(hinstDLL, path, MAX_PATH);
	PathRemoveExtension(path);
	PathAddExtension(path, chlist_ext);

	n_sps = 0;
	for (i = 0; i < MAX_SPACE_NUM; i++) {
		sps[i].spname = NULL;
		sps[i].n_chs = 0;
	}

	fp = _wfopen(path, L"rt");
	if (fp != NULL) {
		sp = 0;
		while( fgetws(line, 256-1, fp) != NULL ) {
			if ( (len = wcslen(line)) > 0 ) {
				if (line[len-1] == L'\n') {
					line[len-1] = L'\0'; /* 末尾の改行を削除 */
				}
			}

			if (line[0] == L';') { /* ;から始まる行はコメント */
				continue;
			} else if (line[0] == L'$') { /* $から始まる行はスペース名定義 */
				if (sp != 0 || sps[sp].n_chs != 0) {
					sp++;
				}
				if (sp >= MAX_SPACE_NUM) { /* MAX_SPACE_NUMを超えた分は無視 */
					break;
				}
				//mbstowcs(wcstr, &line[1], 256 - 1);
				sps[sp].spname = _wcsdup(&line[1]);
			} else {
				if (sps[sp].n_chs < MAX_CHANNEL_NUM) { /* MAX_CHANNEL_NUMを超えた分は無視 */
					ip_str = line;
					for (port_str = line; *port_str != L'\0'; port_str++) {
						if (*port_str == L':') {
							*port_str = L'\0';
							port_str++;
							break;
						}
					}

					if (wcslen(ip_str) != 0) {
						wcsncpy(last_ip_str, ip_str, 256 - 1);
					}

					if (port_str) {
						port = _wtoi(port_str);
					} else {
						port = 1234;
					}

					swprintf_s(chname, 256-1, L"%s:%d", last_ip_str, port);
					wcstombs(ip_mbstr, last_ip_str, 256 - 1);

					sps[sp].chs[sps[sp].n_chs].chname = _wcsdup(chname);
					sps[sp].chs[sps[sp].n_chs].addr.S_un.S_addr = inet_addr(ip_mbstr);
					sps[sp].chs[sps[sp].n_chs].port = htons(port);

					sps[sp].n_chs++;
				}
			}
		}
		if (sps[0].spname == NULL) {
			sps[0].spname = _wcsdup(L"");
		}
		fclose(fp);
		n_sps = sp + 1;
	} else {
		MessageBox(NULL, L"チャンネルリストを読み込めません", L"BonDriver_TCPc", MB_OK);
		sps[0].spname = _wcsdup(L"SPACE1");
		sps[0].n_chs = 1;
		sps[0].chs[0].chname = _wcsdup(L"127.0.0.1:1234");
		sps[0].chs[0].addr.S_un.S_addr = inet_addr("127.0.0.1");
		sps[0].chs[0].port = htons(1234);
		n_sps = 1;
	}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	int i, j;
	if (fdwReason == DLL_PROCESS_ATTACH) {
		parse_channel_list(hinstDLL);
	} else if (fdwReason == DLL_PROCESS_DETACH) {
		/* do nothing */
		for (i = 0; i < n_sps; i++) {
			for (j = 0; j < sps[i].n_chs; j++) {
				free(sps[i].chs[j].chname);
			}
			free(sps[i].spname);
		}
	}
	return TRUE;
}
