// LB-SChannel-Wrapper.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "mesock32.h"
#define SECURITY_WIN32
#include <security.h>
#include <schnlsp.h>

#define DLL_API EXTERN_C __declspec(dllexport)

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "mesock32.lib")

#define cbMaxMessage 12000
#define IO_BUFFER_SIZE  0x10000

typedef struct TLSCtxtWrapper
{
	Mesock32Socket sock;
	PCredHandle pCredHandle;
	PCtxtHandle pCtxtHandle;
	SecPkgContext_StreamSizes sizes;
	SecBuffer ExtraData;
	SecBuffer RemainingDecryptData;
} * PTLSCtxtWrapper;

DLL_API PTLSCtxtWrapper InitTLS(Mesock32Socket sock)
{
	PTLSCtxtWrapper pWrapper = new TLSCtxtWrapper();
	pWrapper->sock = sock;
	return pWrapper;
}

DLL_API void EndTLS(PTLSCtxtWrapper pWrapper)
{
	if (pWrapper->pCtxtHandle != NULL) {
		DeleteSecurityContext(pWrapper->pCtxtHandle);
		delete pWrapper->pCtxtHandle;
	}

	if (pWrapper->pCredHandle != NULL) {
		FreeCredentialHandle(pWrapper->pCredHandle);
		delete pWrapper->pCredHandle;
	}

	if (pWrapper->ExtraData.BufferType != SECBUFFER_EMPTY)
	{
		LocalFree(pWrapper->ExtraData.pvBuffer);
	}

	delete pWrapper;
}

DLL_API SECURITY_STATUS BeginTLSClientNoValidation(PTLSCtxtWrapper pWrapper)
{
	pWrapper->pCredHandle = new CredHandle();
	SCHANNEL_CRED sc = SCHANNEL_CRED();
	sc.dwVersion = SCHANNEL_CRED_VERSION;
	sc.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_NO_SERVERNAME_CHECK;

	return AcquireCredentialsHandle(NULL, const_cast<LPSTR>(UNISP_NAME), SECPKG_CRED_OUTBOUND, NULL,
		&sc, NULL, NULL, pWrapper->pCredHandle, NULL);
} 

SECURITY_STATUS RunHandshakeLoop(PTLSCtxtWrapper pWrapper, BOOL read)
{
	//TODO: Run handshake loop
	SecBufferDesc InputBufDesc, OutputBufDesc;
	SecBuffer InputBuf[2], OutputBuf;
	PVOID inpBuf = LocalAlloc(LMEM_FIXED, IO_BUFFER_SIZE);
	PVOID received = 0;
	int bufCount = 0;
	DWORD dwFlagsRet = 0;

	if (inpBuf == NULL)
	{
		return SEC_E_INTERNAL_ERROR;
	}

	SECURITY_STATUS scRet = SEC_I_CONTINUE_NEEDED;

	while (scRet == SEC_I_CONTINUE_NEEDED ||
		scRet == SEC_E_INCOMPLETE_MESSAGE)
	{
		if (bufCount == 0 || scRet == SEC_E_INCOMPLETE_MESSAGE)
		{
			if (read)
			{
				received = Receive(pWrapper->sock, IO_BUFFER_SIZE - bufCount, 0);
				int size = strnlen_s(reinterpret_cast<const char*>(received), IO_BUFFER_SIZE - bufCount);
				if (size == 0)
				{
					return SEC_E_INTERNAL_ERROR;
				}

				int dest = reinterpret_cast<int>(inpBuf) + bufCount;
				MoveMemory(inpBuf, received, size);
				bufCount += size;
			}
			else {
				read = TRUE;
			}
		}

		InputBufDesc.cBuffers = 2;
		InputBufDesc.pBuffers = InputBuf;
		InputBufDesc.ulVersion = SECBUFFER_VERSION;

		InputBuf[0].BufferType = SECBUFFER_TOKEN;
		InputBuf[0].pvBuffer = inpBuf;
		InputBuf[0].cbBuffer = bufCount;

		OutputBufDesc.cBuffers = 1;
		OutputBufDesc.pBuffers = NULL;
		OutputBufDesc.ulVersion = SECBUFFER_VERSION;

		OutputBuf.BufferType = SECBUFFER_EMPTY;
		OutputBuf.cbBuffer = 0;
		OutputBuf.pvBuffer = NULL;

		scRet = InitializeSecurityContext(pWrapper->pCredHandle, pWrapper->pCtxtHandle, NULL, ISC_REQ_CONFIDENTIALITY |
			ISC_REQ_ALLOCATE_MEMORY, 0, 0, &InputBufDesc, 0, NULL, &OutputBufDesc, &dwFlagsRet, NULL);

		if (scRet == SEC_E_OK || scRet == SEC_I_CONTINUE_NEEDED ||
			FAILED(scRet) && (dwFlagsRet & ISC_REQ_EXTENDED_ERROR))
		{
			if (OutputBuf.cbBuffer != 0 && OutputBuf.pvBuffer != NULL)
			{
				if (!Send(pWrapper->sock, OutputBuf.pvBuffer))
				{
					return SEC_E_INTERNAL_ERROR;
				}
			}
		}

		if (scRet == SEC_E_INCOMPLETE_MESSAGE) continue;

		if (scRet == SEC_E_OK)
		{
			if (InputBuf[1].BufferType == SECBUFFER_EXTRA)
			{
				pWrapper->ExtraData.pvBuffer = LocalAlloc(LMEM_FIXED,
					InputBuf[1].cbBuffer);

				if (pWrapper->ExtraData.pvBuffer == NULL)
				{
					return SEC_E_INTERNAL_ERROR;
				}

				MoveMemory(pWrapper->ExtraData.pvBuffer, InputBuf[1].pvBuffer,
					InputBuf[1].cbBuffer);

				pWrapper->ExtraData.cbBuffer = InputBuf[1].cbBuffer;
				pWrapper->ExtraData.BufferType = SECBUFFER_TOKEN;
			}
			else
			{
				pWrapper->ExtraData.cbBuffer = 0;
				pWrapper->ExtraData.pvBuffer = NULL;
				pWrapper->ExtraData.BufferType = SECBUFFER_EMPTY;
			}
		}

		break;

		if (FAILED(scRet))
		{
			return scRet;
		}

		if (InputBuf[1].BufferType == SECBUFFER_EXTRA)
		{
			MoveMemory(inpBuf, InputBuf[1].pvBuffer, InputBuf[1].cbBuffer);
			bufCount = InputBuf[1].cbBuffer;
		}
	} //while scRet ==

	LocalFree(inpBuf);
	return scRet;
}

DLL_API SECURITY_STATUS PerformClientHandshake(PTLSCtxtWrapper pWrapper, LPSTR pServerName)
{
	SecBufferDesc OutputBufDesc;
	SecBuffer OutputBuf;

	OutputBufDesc.ulVersion = SECBUFFER_VERSION;
	OutputBufDesc.cBuffers = 1;
	OutputBufDesc.pBuffers = &OutputBuf;

	OutputBuf.BufferType = SECBUFFER_TOKEN;
	OutputBuf.cbBuffer = 0;
	OutputBuf.pvBuffer = NULL;

	pWrapper->pCtxtHandle = new CtxtHandle();
	SECURITY_STATUS scRet = InitializeSecurityContext(pWrapper->pCredHandle, NULL, pServerName,
		ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY, 0, 0, NULL, 0, pWrapper->pCtxtHandle, &OutputBufDesc,
		NULL, NULL);

	if (scRet != SEC_I_CONTINUE_NEEDED) {
		return scRet;
	}

	if (OutputBuf.cbBuffer != 0 && OutputBuf.pvBuffer != NULL)
	{
		if (!Send(pWrapper->sock, OutputBuf.pvBuffer))
		{
			return SEC_E_INTERNAL_ERROR;
		}

		FreeContextBuffer(OutputBuf.pvBuffer);
	}

	scRet = RunHandshakeLoop(pWrapper, TRUE);

	SECURITY_STATUS qcaRet = QueryContextAttributes(pWrapper->pCtxtHandle, SECPKG_ATTR_STREAM_SIZES, &pWrapper->sizes);
	if (qcaRet != SEC_E_OK)
	{
		return qcaRet;
	}

	return scRet;
}

DLL_API SECURITY_STATUS EncryptSend(PTLSCtxtWrapper pWrapper, LPCSTR message)
{
	PSecPkgContext_StreamSizes sizes = &pWrapper->sizes;
	int messageSize = strnlen_s(message, sizes->cbMaximumMessage);

	int maxMessageBlobSize = sizes->cbHeader + sizes->cbMaximumMessage +
		sizes->cbTrailer;

	PBYTE sendBuf = (PBYTE)LocalAlloc(LMEM_FIXED, maxMessageBlobSize);

	MoveMemory(sendBuf + sizes->cbHeader, message, messageSize);

	SecBufferDesc MessageDesc;
	SecBuffer MsgBuffer[4];

	MsgBuffer[0].BufferType = SECBUFFER_STREAM_HEADER;
	MsgBuffer[0].cbBuffer = sizes->cbHeader;
	MsgBuffer[0].pvBuffer = sendBuf;

	MsgBuffer[1].BufferType = SECBUFFER_DATA;
	MsgBuffer[1].pvBuffer = sendBuf + sizes->cbHeader;
	MsgBuffer[1].cbBuffer = messageSize;

	MsgBuffer[2].BufferType = SECBUFFER_STREAM_TRAILER;
	MsgBuffer[2].cbBuffer = sizes->cbTrailer;
	MsgBuffer[2].pvBuffer = sendBuf + sizes->cbHeader + messageSize;

	MsgBuffer[3].BufferType = SECBUFFER_EMPTY;
	MsgBuffer[3].cbBuffer = 0;
	MsgBuffer[3].pvBuffer = NULL;

	SECURITY_STATUS scRet = EncryptMessage(pWrapper->pCtxtHandle, 0, &MessageDesc, 0);
	if (FAILED(scRet))
	{
		LocalFree(sendBuf);
		return scRet;
	}

	if (!Send(pWrapper->sock, sendBuf))
	{
		LocalFree(sendBuf);
		return SEC_E_INTERNAL_ERROR;
	}

	LocalFree(sendBuf);
	return scRet;
}

DLL_API SECURITY_STATUS DecryptReceive(PTLSCtxtWrapper pWrapper, LPSTR buffer, ULONG bufLen)
{
	SECURITY_STATUS scRet;
	SecBufferDesc MessageBufDesc;
	SecBuffer MsgBuffer[4];

	//See if there's anything left to return from our last DecryptReceive() call
	PSecBuffer pRemnant = &pWrapper->RemainingDecryptData;
	if (pRemnant->BufferType != SECBUFFER_EMPTY)
	{
		CopyMemory(buffer, pRemnant->pvBuffer, min(bufLen, pRemnant->cbBuffer));
		if (pRemnant->cbBuffer > bufLen)
		{
			MoveMemory(pRemnant->pvBuffer, (char*)pRemnant->pvBuffer + bufLen, pRemnant->cbBuffer - bufLen);
			pRemnant->cbBuffer = pRemnant->cbBuffer - bufLen;
			return SEC_E_OK;
		}
		else
		{
			LocalFree(pRemnant->pvBuffer);
			pRemnant->pvBuffer = NULL;
			pRemnant->cbBuffer = 0;
			pRemnant->BufferType = SECBUFFER_EMPTY;
		}
	}


}