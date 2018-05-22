// LB-SChannel-Wrapper.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#define SECURITY_WIN32
#include <security.h>
#include <schnlsp.h>

#define DLL_API EXTERN_C __declspec(dllexport)
#define MESOCK32_API EXTERN_C __declspec(dllimport)
typedef ULONG Mesock32Socket;

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "mesock32.lib")

#define cbMaxMessage 12000
#define IO_BUFFER_SIZE  0x10000

typedef struct TLSCtxtWrapper
{
	Mesock32Socket sock;
	PCredHandle pCredHandle;
	PCtxtHandle pCtxtHandle;
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

	//TODO: Run handshake loop
}

DLL_API int GetMaxMessageSize()
{
	return cbMaxMessage;
}

DLL_API SECURITY_STATUS InitSecurityContext(PCredHandle phCredential, PCtxtHandle phContext, SEC_CHAR *pszTargetName,
	PVOID pvInputBuffer, ULONG cbInputBuffer, PCtxtHandle * pphNewContext, PVOID pvOutputBuffer, ULONG cbOutputBuffer)
{
	SecBufferDesc OutputBufDesc;
	SecBuffer OutputBuf;

	OutputBufDesc.ulVersion = SECBUFFER_VERSION;
	OutputBufDesc.cBuffers = 1;
	OutputBufDesc.pBuffers = &OutputBuf;

	OutputBuf.BufferType = SECBUFFER_TOKEN;
	OutputBuf.cbBuffer = cbOutputBuffer;
	OutputBuf.pvBuffer = pvOutputBuffer;

	ULONG context;

	if (phContext == NULL)  //First call to InitializeSecurityContext()
	{
		*pphNewContext = new CtxtHandle();
		return InitializeSecurityContext(phCredential, phContext, pszTargetName, ISC_REQ_CONFIDENTIALITY, 0,
			0, NULL, 0, *pphNewContext, &OutputBufDesc, &context, NULL);
	}
	else
	{
		SecBufferDesc InputBufDesc;
		SecBuffer InputBuf[2];

		InputBuf[0].BufferType = SECBUFFER_TOKEN;
		InputBuf[0].cbBuffer = cbInputBuffer;
		InputBuf[0].pvBuffer = pvInputBuffer;

		InputBuf[1] = SecBuffer();

		InputBufDesc.cBuffers = 2;
		InputBufDesc.pBuffers = InputBuf;
		InputBufDesc.ulVersion = SECBUFFER_VERSION;

		return InitializeSecurityContext(phCredential, phContext, pszTargetName, ISC_REQ_CONFIDENTIALITY, 0,
			0, &InputBufDesc, 0, *pphNewContext, &OutputBufDesc, &context, NULL);
	}
}

DLL_API SECURITY_STATUS FreeCredHandle(PCredHandle pCredHandle)
{
	return FreeCredentialsHandle(pCredHandle);
	delete pCredHandle;
}

DLL_API SECURITY_STATUS DeleteSecContext(PCtxtHandle phContext)
{
	return DeleteSecurityContext(phContext);
}