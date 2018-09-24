#include "stdafx.h"
#include <unordered_map>
#include <mutex>
#include "ServerCert.h"
#include "CertHelper.h"
#include "SSLServer.h"

// Create credentials (a handle to a credential context) from a certificate
SECURITY_STATUS CreateCredentialsFromCertificate(PCredHandle phCreds, PCCERT_CONTEXT pCertContext)
{
	DebugMsg("CreateCredentialsFromCertificate 0x%.8x '%S'.", pCertContext, GetCertName(pCertContext));

	// Build Schannel credential structure.
	SCHANNEL_CRED   SchannelCred = { 0 };
	SchannelCred.dwVersion = SCHANNEL_CRED_VERSION;
	SchannelCred.cCreds = 1;
	SchannelCred.paCred = &pCertContext;
	SchannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
	SchannelCred.dwFlags = SCH_USE_STRONG_CRYPTO;

	SECURITY_STATUS Status;
	TimeStamp       tsExpiry;
	// Get a handle to the SSPI credential
	Status = CSSLServer::SSPI()->AcquireCredentialsHandle(
		NULL,                   // Name of principal
		UNISP_NAME,           // Name of package
		SECPKG_CRED_INBOUND,    // Flags indicating use
		NULL,                   // Pointer to logon ID
		&SchannelCred,          // Package specific data
		NULL,                   // Pointer to GetKey() func
		NULL,                   // Value to pass to GetKey()
		phCreds,                // (out) Cred Handle
		&tsExpiry);             // (out) Lifetime (optional)

	if (Status != SEC_E_OK)
	{
		DWORD dw = GetLastError();
		if (Status == SEC_E_UNKNOWN_CREDENTIALS)
			DebugMsg("**** Error: 'Unknown Credentials' returned by AcquireCredentialsHandle. Be sure app has administrator rights. LastError=%d", dw);
		else
			DebugMsg("**** Error 0x%x returned by AcquireCredentialsHandle. LastError=%d.", Status, dw);
		return Status;
	}

	return SEC_E_OK;
}

// Global items used by the GetCredHandleFor function
std::mutex GetCredHandleForLock;
std::unordered_map<std::string, CredentialHandle> credMap = std::unordered_map<std::string, CredentialHandle>();

SECURITY_STATUS GetCredHandleFor(CString serverName, SelectServerCertType SelectServerCert, PCredHandle phCreds)
{
	std::string localServerName;
	if (serverName.IsEmpty()) // There was no hostname supplied
		localServerName = CW2A(GetHostName());
	else
		localServerName = CW2A(serverName);

	std::lock_guard<std::mutex> lock(GetCredHandleForLock); // unordered_map is not thread safe, so make this function single thread

	auto got = credMap.find(localServerName);

	if (got == credMap.end())
	{
		// There were no credentials stored for that host, create some and add them
		PCCERT_CONTEXT pCertContext = NULL;
		SECURITY_STATUS status = SEC_E_INTERNAL_ERROR;
		if (SelectServerCert)
		{
			status = SelectServerCert(pCertContext, (LPCTSTR)serverName);
			if (FAILED(status))
			{
				DebugMsg("SelectServerCert returned an error = 0x%08x", status);
				return SEC_E_INTERNAL_ERROR;
			}
		}
		else
			status = CertFindServerCertificateByName(pCertContext, (LPCTSTR)serverName); // Add "true" to look in user store, "false", or nothing looks in machine store
		if (SUCCEEDED(status))
		{
			CredHandle hServerCred{};
			status = CreateCredentialsFromCertificate(&hServerCred, pCertContext);
			credMap.emplace(localServerName, hServerCred); // The server credentials are owned by the map now
			*phCreds = hServerCred;
			return SEC_E_OK;
		}
		else
		{
			DebugMsg("Failed handling server initialization, error = 0x%08x", status);
			return SEC_E_INTERNAL_ERROR;
		}
	}
	else // We already have credentials for this one
	{
		*phCreds = (got->second).get();
		return SEC_E_OK;
	}
}