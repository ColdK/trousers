
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004, 2005
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tss/tss.h"
#include "spi_internal_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"
#include "tss/trousers.h"

TSS_UUID SRK_UUID = { 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 1 } };

TSS_RESULT
Tspi_TPM_CreateEndorsementKey(TSS_HTPM hTPM,	/*  in */
			      TSS_HKEY hKey,	/*  in */
			      TSS_VALIDATION * pValidationData	/*  in, out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;

	TCPA_NONCE antiReplay;
	TCPA_DIGEST digest;
	TSS_RESULT result;
	UINT32 ekSize;
	BYTE *ek;
	BYTE verifyInternally = 0;
	TCPA_KEY dummyKey;
	UINT16 offset;
	TCPA_DIGEST hash;
	BYTE hashBlob[1024];
	AnObject *anObject;
	TCPA_RSAKEY_OBJECT *keyObject;
	UINT32 newEKSize;
	BYTE *newEK;
	TSS_HCONTEXT tspContext;


	if ((result = obj_checkType_2(hTPM, TSS_OBJECT_TYPE_TPM, hKey, TSS_OBJECT_TYPE_RSAKEY)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_2(hTPM, hKey, &tcsContext)))
		return result;

	if ((result = Tspi_GetAttribData(hKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB, &ekSize, &ek)))
		return result;

	offset = 0;
	Trspi_UnloadBlob_KEY(tspContext, &offset, ek, &dummyKey);

	offset = 0;
	Trspi_LoadBlob_KEY_PARMS(&offset, ek, &dummyKey.algorithmParms);
	ekSize = offset;

	if (pValidationData == NULL)
		verifyInternally = 1;

	if (verifyInternally) {
/* 		memset( antiReplay.nonce, 0xBB, 20 );		//change to random */
		if ((result = internal_GetRandomNonce(tcsContext, &antiReplay))) {
			LogError1("Failed to create random nonce");
			return TSS_E_INTERNAL_ERROR;
		}
	} else
		memcpy(antiReplay.nonce, pValidationData->rgbExternalData, 20);

	if ((result = TCSP_CreateEndorsementKeyPair(tcsContext, antiReplay,
						   ekSize, ek, &newEKSize, &newEK, &digest)))
		return result;

	if (verifyInternally) {
		offset = 0;
		Trspi_LoadBlob(&offset, newEKSize, hashBlob, newEK);
		Trspi_LoadBlob(&offset, 20, hashBlob, antiReplay.nonce);

		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hash.digest);

		if (memcmp(hash.digest, digest.digest, 20)) {
			LogError1("Internal verification failed");
			return TSS_E_INTERNAL_ERROR;
		}
	} else {
		pValidationData->ulDataLength = newEKSize;
		pValidationData->rgbData = calloc_tspi(tspContext, newEKSize);
		if (pValidationData->rgbData == NULL) {
			LogError("malloc of %d bytes failed.", newEKSize);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbData, newEK, newEKSize);
		memcpy(&pValidationData->rgbData[ekSize], antiReplay.nonce, 20);

		pValidationData->ulValidationLength = 20;
		pValidationData->rgbValidationData = calloc_tspi(tspContext, 20);
		if (pValidationData == NULL) {
			LogError("malloc of %d bytes failed.", 20);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbValidationData, digest.digest, 20);
	}
	anObject = getAnObjectByHandle(hKey);
	if (anObject == NULL || anObject->memPointer == NULL) {
		LogError1("internal object not found with handle matching hKey");
		return TSS_E_INTERNAL_ERROR;
	}

	keyObject = anObject->memPointer;

	offset = 0;
	if ((result = Trspi_UnloadBlob_KEY_PARMS(tspContext, &offset, newEK, &keyObject->tcpaKey.algorithmParms)))
		return result;
	if ((result = Trspi_UnloadBlob_STORE_PUBKEY(tspContext, &offset, newEK, &keyObject->tcpaKey.pubKey)))
		return result;

	free(newEK);

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_GetPubEndorsementKey(TSS_HTPM hTPM,	/*  in */
			      BOOL fOwnerAuthorized,	/*  in */
			      TSS_VALIDATION * pValidationData, /* in, out */
			      TSS_HKEY * phEndorsementPubKey	/*  out */
    )
{
	TCPA_DIGEST digest;
	TSS_RESULT result;
	TCS_CONTEXT_HANDLE tcsContext;
	TCS_AUTH ownerAuth;
	UINT16 offset;
	BYTE hashblob[1000];
	TSS_HPOLICY hPolicy;
	UINT32 pubEKSize;
	BYTE *pubEK;
	TCPA_NONCE antiReplay;
	TCPA_DIGEST checkSum;
	TSS_HOBJECT retKey;
	TCPA_RSAKEY_OBJECT *keyObject;
	TSS_HCONTEXT tspContext;
	AnObject *tmpObj = NULL;
	TCPA_PUBKEY pubKey;


	if (phEndorsementPubKey == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT) {
		LogError1("Unable to get TSP handle from TCS context");
		return TSS_E_INTERNAL_ERROR;
	}

	if (fOwnerAuthorized) {
		if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hPolicy)))
			return result;

		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_OwnerReadPubek, hashblob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

		if ((result = secret_PerformAuth_OIAP(hPolicy, digest, &ownerAuth)))
			return result;

		if ((result = TCSP_OwnerReadPubek(tcsContext, &ownerAuth, &pubEKSize, &pubEK)))
			return result;

		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, result, hashblob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_OwnerReadPubek, hashblob);
		Trspi_LoadBlob(&offset, pubEKSize, hashblob, pubEK);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, digest, &ownerAuth)))
			return result;
	} else {
		if (pValidationData == NULL) {
			if ((result = internal_GetRandomNonce(tcsContext, &antiReplay))) {
				LogDebug1("Failed to generate random nonce");
				return TSS_E_INTERNAL_ERROR;
			}
		} else {
			if (pValidationData->ulExternalDataLength != SHA1_HASH_SIZE)
				return TSS_E_BAD_PARAMETER;

			memcpy(antiReplay.nonce, pValidationData->rgbExternalData, SHA1_HASH_SIZE);
		}

		/* call down to the TPM */
		if ((result = TCSP_ReadPubek(tcsContext, antiReplay, &pubEKSize, &pubEK, &checkSum)))
			return result;

		/* validate the returned hash, or set up the return so that the user can */
		if (pValidationData == NULL) {
			offset = 0;
			Trspi_LoadBlob(&offset, pubEKSize, hashblob, pubEK);
			Trspi_LoadBlob(&offset, SHA1_HASH_SIZE, hashblob, antiReplay.nonce);
			Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

			/* check validation of the entire pubkey structure */
			if (memcmp(digest.digest, checkSum.digest, SHA1_HASH_SIZE)) {
				/* validation failed, unload the pubEK in order to hash
				 * just the pubKey portion of the pubEK. This is done on
				 * Atmel chips specifically.
				 */
				offset = 0;
				Trspi_UnloadBlob_PUBKEY(tspContext, &offset, pubEK, &pubKey);

				offset = 0;
				Trspi_LoadBlob(&offset, pubKey.pubKey.keyLength, hashblob, pubKey.pubKey.key);
				Trspi_LoadBlob(&offset, SHA1_HASH_SIZE, hashblob, antiReplay.nonce);
				Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

				if (memcmp(digest.digest, checkSum.digest, SHA1_HASH_SIZE))
					return TSS_E_VALIDATION_FAILED;
			}
		} else {
			/* validate the entire TCPA_PUBKEY structure */
			pValidationData->ulDataLength = pubEKSize + SHA1_HASH_SIZE;
			pValidationData->rgbData = calloc_tspi(tspContext, pValidationData->ulDataLength);
			if (pValidationData->rgbData == NULL) {
				LogError("malloc of %d bytes failed.", pValidationData->ulDataLength);
				return TSS_E_OUTOFMEMORY;
			}

			memcpy(pValidationData->rgbData, pubEK, pValidationData->ulDataLength);
			memcpy(&(pValidationData->rgbData[pValidationData->ulDataLength]),
					antiReplay.nonce, SHA1_HASH_SIZE);

			pValidationData->ulValidationLength = SHA1_HASH_SIZE;
			pValidationData->rgbValidationData = calloc_tspi(tspContext, SHA1_HASH_SIZE);
			if (pValidationData->rgbValidationData == NULL) {
				LogError("malloc of %d bytes failed.", SHA1_HASH_SIZE);
				return TSS_E_OUTOFMEMORY;
			}

			memcpy(pValidationData->rgbValidationData, checkSum.digest, SHA1_HASH_SIZE);
		}
	}

	if ((result = Tspi_Context_CreateObject(tspContext, TSS_OBJECT_TYPE_RSAKEY,
					TSS_KEY_SIZE_2048 | TSS_KEY_TYPE_LEGACY, &retKey)))
		return result;

	tmpObj = getAnObjectByHandle(retKey);
	if (tmpObj && tmpObj->memPointer) {
		keyObject = tmpObj->memPointer;
	} else {
		free(pubEK);
		LogError1("internal mem pointer for key is invalid!");
		return TSS_E_INTERNAL_ERROR;
	}

	offset = 0;
	if ((result = Trspi_UnloadBlob_KEY_PARMS(tspContext, &offset, pubEK, &keyObject->tcpaKey.algorithmParms)))
		return result;
	if ((result = Trspi_UnloadBlob_STORE_PUBKEY(tspContext, &offset, pubEK, &keyObject->tcpaKey.pubKey)))
		return result;

	*phEndorsementPubKey = retKey;

	free(pubEK);

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_TakeOwnership(TSS_HTPM hTPM,	/*  in */
		       TSS_HKEY hKeySRK,	/*  in */
		       TSS_HKEY hEndorsementPubKey	/*  in */
    )
{
	TCS_AUTH privAuth;

	BYTE encOwnerAuth[256];
	UINT32 encOwnerAuthLength;
	BYTE encSRKAuth[256];
	UINT32 encSRKAuthLength;
	UINT16 offset;

	BYTE hashblob[1024];
	TCPA_DIGEST digest;
	TSS_RESULT rc;
	TCS_CONTEXT_HANDLE tcsContext;
	UINT32 srkKeyBlobLength;
	BYTE *srkKeyBlob;
	TSS_HPOLICY hOwnerPolicy;
	UINT32 newSrkBlobSize;
	BYTE *newSrkBlob;
	BYTE oldAuthDataUsage;
	TSS_HKEY hPubEK;

	/****************************
	 *	The first step is to get context and to get the SRK Key Blob.
	 *		If these succeed, then the auth should be init'd.
	 *******************************/

	if (hEndorsementPubKey == NULL_HKEY) {
		if ((rc = Tspi_TPM_GetPubEndorsementKey(hTPM, FALSE, NULL, &hPubEK))) {
			return rc;
		}
	} else {
		hPubEK = hEndorsementPubKey;
	}

	/* ---  Get context */
	if ((rc =
	    obj_checkType_3(hTPM, TSS_OBJECT_TYPE_TPM, hKeySRK,
				       TSS_OBJECT_TYPE_RSAKEY,
				       hPubEK, TSS_OBJECT_TYPE_RSAKEY)))
		return rc;

	if ((rc = obj_isConnected_3(hTPM, hKeySRK, hPubEK, &tcsContext)))
		return rc;

	/* ---  Get the srkKeyData */
	if ((rc = Tspi_GetAttribData(hKeySRK,
				    TSS_TSPATTRIB_KEY_BLOB,
				    TSS_TSPATTRIB_KEYBLOB_BLOB, &srkKeyBlobLength, &srkKeyBlob)))
		return rc;

	/* ---  Oh boy...hardcoded blob stuff */
	/* ---  Need to check for Atmel bug where authDataUsage is changed */
	oldAuthDataUsage = srkKeyBlob[10];
	LogDebug("oldAuthDataUsage is %.2X.  Wait to see if it changes", oldAuthDataUsage);

	/****************************
	 *	Now call the module that will encrypt the secrets.  This
	 *		will either get the secrets from the policy objects or
	 *		use the callback function to encrypt the secrets
	 *******************************/

	if ((rc = secret_TakeOwnership(hPubEK,	/* ---  Handle to the public key */
				      hTPM,	/* ---  Handle to the TPM object */
				      hKeySRK,	/* ---  Handle to the SRK Key Object */
				      &privAuth,	/* ---  The auth Structure already init'd */
				      &encOwnerAuthLength,	/* ---  The out size */
				      encOwnerAuth,	/* ---  The encrypted Owner Auth */
				      &encSRKAuthLength,	/* --- */
				      encSRKAuth)))	/* ---      The encrypted SRK Auth */
		return rc;

	/****************************
	 *	Now, take ownership is ready to call.  The auth structure should be complete
	 *		and the encrypted data structures should be ready
	 *******************************/

	if ((rc = TCSP_TakeOwnership(tcsContext,
				TCPA_PID_OWNER,
				encOwnerAuthLength,
				encOwnerAuth,
				encSRKAuthLength,
				encSRKAuth,
				srkKeyBlobLength,
				srkKeyBlob,
				&privAuth,
				&newSrkBlobSize,
				&newSrkBlob)))
		return rc;

	/****************************
	 *	The final step is to validate the return Auth
	 *******************************/

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, rc, hashblob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_TakeOwnership, hashblob);
	Trspi_LoadBlob(&offset, newSrkBlobSize, hashblob, newSrkBlob);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	if ((rc = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hOwnerPolicy)))
		return rc;
	if ((rc = secret_ValidateAuth_OIAP(hOwnerPolicy, digest, &privAuth)))
		return rc;

	/* ---  Now that it's all happy, stuff the keyBlob into the object */
	/* ---  If atmel, need to adjust the authDataUsage if it changed */
	if (oldAuthDataUsage != newSrkBlob[10]) {	/* hardcoded blob stuff */
		LogDebug1("auth data usage changed. Atmel bug. Fixing in key object");
		newSrkBlob[10] = oldAuthDataUsage;	/* this will fix it  */
	}

	Tspi_SetAttribData(hKeySRK, TSS_TSPATTRIB_KEY_BLOB,
			   TSS_TSPATTRIB_KEYBLOB_BLOB, newSrkBlobSize, newSrkBlob);

	return rc;
}

TSS_RESULT
Tspi_TPM_CollateIdentityRequest(TSS_HTPM hTPM,	/*  in */
				TSS_HKEY hKeySRK,	/*  in */
				TSS_HKEY hCAPubKey,	/*  in */
				UINT32 ulIdentityLabelLength,	/*  in  */
				BYTE * rgbIdentityLabelData,	/*  in */
				TSS_HKEY hIdentityKey,	/*  in */
				TSS_HKEY hSymKey,	/*  in */
				UINT32 * pulTcpaIdentityReqLength,	/*  out */
				BYTE ** prgbTcpaIdentityReq	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_ENCAUTH encAuthUsage;
	TCPA_ENCAUTH encAuthMig;
	BYTE sharedSecret[20];
	TCS_AUTH srkAuth;
	TCS_AUTH ownerAuth;
	TCPA_PUBKEY pubkey;
	TCPA_RESULT result;
	UINT16 offset;
	BYTE hashblob[0x2000];
	TCPA_DIGEST digest;
	TSS_HPOLICY hSRKPolicy, hIDPolicy, hCAPolicy, hTPMPolicy;
	UINT32 idKeySize;
	BYTE *idKey;
	UINT32 caKeySize;
	BYTE *caKey;
	TCPA_NONCE nonceEvenOSAP;
	TCPA_KEY keyContainer;
	TCPA_CHOSENID_HASH chosenIDHash = { { 0, } };
	UINT32 pcIdentityBindingSize;
	BYTE *prgbIdentityBinding;
	UINT32 pcEndorsementCredentialSize;
	BYTE *prgbEndorsementCredential;
	UINT32 pcPlatformCredentialSize;
	BYTE *prgbPlatformCredential;
	UINT32 NpcConformanceCredentialSize;
	BYTE *prgbConformanceCredential;
	BYTE caPubKey[512];
	UINT32 caPubKeySize;
	UINT32 tAESSIZE;
	BYTE *tAESkey;
	TSS_HENCDATA hEncData;
	TSS_HCONTEXT tspContext;
	UINT32 symKeySize;
	BYTE *symKey;
	TSS_HPOLICY hIDMigPolicy;
	UINT32 usesAuth;
	TCS_AUTH *pSrkAuth = &srkAuth;

	if (pulTcpaIdentityReqLength == NULL || prgbTcpaIdentityReq == NULL)
		return TSS_E_BAD_PARAMETER;

	LogDebug1("SM DEBUG 1");

	if ((result = obj_checkType_3(hTPM, TSS_OBJECT_TYPE_TPM, hKeySRK,
				       TSS_OBJECT_TYPE_RSAKEY, hCAPubKey, TSS_OBJECT_TYPE_RSAKEY)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	/* ---        Get and verify the context */
	if ((result = obj_isConnected_3(hTPM, hKeySRK, hCAPubKey, &tcsContext)))
		return result;

	/* ---        Get Policies */
	if ((result = Tspi_GetPolicyObject(hKeySRK, TSS_POLICY_USAGE, &hSRKPolicy)))
		return result;

	if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hTPMPolicy)))
		return result;

	if ((result = Tspi_GetPolicyObject(hCAPubKey, TSS_POLICY_USAGE, &hCAPolicy)))
		return result;

	if ((result = Tspi_GetPolicyObject(hIdentityKey, TSS_POLICY_USAGE, &hIDPolicy)))
		return result;

	if ((result = Tspi_GetPolicyObject(hIdentityKey, TSS_POLICY_MIGRATION, &hIDMigPolicy)))
		return result;

	/*  Hash the label */
	Trspi_Hash(TSS_HASH_SHA1, ulIdentityLabelLength, rgbIdentityLabelData, chosenIDHash.digest);

	/* ---        Get Key blobs */
	if ((result = Tspi_GetAttribData(hIdentityKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB, &idKeySize, &idKey)))
		return result;

	if ((result = Tspi_GetAttribData(hCAPubKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB, &caKeySize, &caKey)))
		return result;

	/* ---        Take the PUBKEY portion out of the TCPA_KEY caPubKey and put it into 'pubkey' */
	offset = 0;
	Trspi_UnloadBlob_KEY(tspContext, &offset, caKey, &keyContainer);

	offset = 0;
	Trspi_LoadBlob_KEY_PARMS(&offset, caPubKey, &keyContainer.algorithmParms);
	Trspi_LoadBlob_STORE_PUBKEY(&offset, caPubKey, &keyContainer.pubKey);
	caPubKeySize = offset;

	offset = 0;
	Trspi_UnloadBlob_PUBKEY(tspContext, &offset, caPubKey, &pubkey);

	/* ---        Start OSAP */
	if ((result = secret_PerformXOR_OSAP(hTPMPolicy, hIDPolicy, hIDMigPolicy, 0,
				   TCPA_ET_OWNER, FIXED_SRK_KEY_HANDLE,
				   &encAuthUsage, &encAuthMig, sharedSecret,
				   &ownerAuth, &nonceEvenOSAP)))
		return result;

	/* ---        Hash the Auth data */

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_MakeIdentity, hashblob);
	Trspi_LoadBlob(&offset, 20, hashblob, encAuthUsage.encauth);
	Trspi_LoadBlob(&offset, 20, hashblob, chosenIDHash.digest);
	Trspi_LoadBlob(&offset, idKeySize, hashblob, idKey);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	if (((result = policy_UsesAuth(hSRKPolicy, &usesAuth))))
		return result;

	LogDebug1("SM DEBUG 2");

	/* ---        Do the Auth's */
	if (usesAuth == FALSE) {
		pSrkAuth = NULL;
	} else {
		if ((result = secret_PerformAuth_OIAP(hSRKPolicy, digest, &srkAuth)))
			return result;
		pSrkAuth = &srkAuth;
	}

	if ((result = secret_PerformAuth_OSAP(hTPMPolicy, hIDPolicy, hIDMigPolicy, 0,
				    sharedSecret, &ownerAuth, digest.digest, nonceEvenOSAP))) {
		TCSP_TerminateHandle(tcsContext, srkAuth.AuthHandle);
		return result;
	}

	LogDebug1("SM DEBUG 3");

	/* ---        Send the Command */
	/* tcs/tcskcm.c:1479:TCPA_RESULT TCSP_MakeIdentity  */

	if ((result = TCSP_MakeIdentity(tcsContext,	/*  in  */
				       encAuthUsage,	/*  in */
				       chosenIDHash,	/*  in */
				       idKeySize,	/* in */
				       idKey,	/* in */
				       pSrkAuth,	/*  in, out */
				       &ownerAuth,	/*  in, out */
				       &idKeySize,	/*  out */
				       &idKey,	/*  out */
				       &pcIdentityBindingSize,	/*  out */
				       &prgbIdentityBinding,	/*  out */
				       &pcEndorsementCredentialSize,	/*  out */
				       &prgbEndorsementCredential,	/*  out */
				       &pcPlatformCredentialSize,	/*  out */
				       &prgbPlatformCredential,	/*  out */
				       &NpcConformanceCredentialSize,	/*  out */
				       &prgbConformanceCredential	/*  out */
				)))
		return result;

	LogDebug("SM DEBUG 3B rc=%d", result);

	/* ---        Validate */

	LogDebug("SM DEBUG 3B hsize = %d %d",
		   4 + 4 + idKeySize + 4, pcIdentityBindingSize);

	if (pcIdentityBindingSize > 0x2000) {
		LogDebug1("SM DEBUG size is too BIG. ABORT");
		return 1;
	}

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashblob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_MakeIdentity, hashblob);
	Trspi_LoadBlob(&offset, idKeySize, hashblob, idKey);
	Trspi_LoadBlob_UINT32(&offset, pcIdentityBindingSize, hashblob);
	Trspi_LoadBlob(&offset, pcIdentityBindingSize, hashblob, prgbIdentityBinding);
	/* Trspi_LoadBlob( &offset, pcIdentityBindingSize, prgbIdentityBinding,hashblob ); */

	LogDebug1("SM DEBUG 3B2");

	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	LogDebug1("SM DEBUG 3C");

	if ((result = secret_ValidateAuth_OSAP(hTPMPolicy, hIDPolicy, hIDMigPolicy,
				     sharedSecret, &ownerAuth, digest.digest, nonceEvenOSAP)))
		return result;

	LogDebug1("SM DEBUG 3D");

	if (usesAuth == TRUE) {
		if ((result = secret_ValidateAuth_OIAP(hSRKPolicy, digest, &srkAuth)))
			return result;
	}

	LogDebug1("SM DEBUG 3E");

	/* ---        Push the new key into the existing object */
	if ((result = Tspi_SetAttribData(hIdentityKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB, idKeySize, idKey)))
		return result;

	LogDebug1("SM DEBUG 4");

	/* /////////////////////////////////////////////////////////// */

	/*  encrypt the symmetric key with the identity pubkey */
	/*  generate the symmetric key */
	tAESSIZE = 16;
	if ((result = Tspi_TPM_GetRandom(hTPM,	/*  in */
					tAESSIZE,	/*  in 3DES size */
					&tAESkey	/*  out */
				)))
		return result;

	LogDebug1("SM DEBUG 5 <<<<<<<<<<<<<<<<<<<<<<<<<<<");

	if ((result = Tspi_Context_CreateObject(tspContext, TSS_OBJECT_TYPE_ENCDATA, 0,	/*  will be type empty */
					       &hEncData)))
		return result;

	LogDebug1("SM DEBUG 5A <<<<<<<<<<<<<<<<<<<<<<<<<<<");

	/*  encrypt the aeskey */
	/*  tspi/spi_data.c:3:TSS_RESULT Tspi_Data_Bind */

	if ((result = Tspi_Data_Bind(hEncData,	/*  in */
				    hCAPubKey,	/*  in */
				    tAESSIZE,	/*  in */
				    tAESkey	/*  in */
				)))
		return result;

	LogDebug1("SM DEBUG 5B <<<<<<<<<<<<<<<<<<<<<<<<<<<");

	/* ---        Set encdata with the encrypted aes key */
	if ((result = Tspi_GetAttribData(hSymKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB, &symKeySize, &symKey)))
		return result;

	LogDebug1("SM DEBUG 5C <<<<<<<<<<<<<<<<<<<<<<<<<<<");

	offset = 0;
	Trspi_UnloadBlob_KEY(tspContext, &offset, symKey, &keyContainer);

	keyContainer.encSize = tAESSIZE;
	keyContainer.encData = malloc(tAESSIZE);
	if (keyContainer.encData == NULL) {
		LogError("malloc of %d bytes failed.", tAESSIZE);
		return TSS_E_OUTOFMEMORY;
	}

	memcpy(keyContainer.encData, tAESkey, tAESSIZE);

	offset = 0;
	Trspi_LoadBlob_KEY(&offset, symKey, &keyContainer);

	LogDebug1("SM DEBUG 6 <<<<<<<<<<<<<<<<<<<<<<<<<<<");

	if ((result = Tspi_SetAttribData(hSymKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB, symKeySize + tAESSIZE, symKey)))
		return result;

	LogDebug1("SM DEBUG 7");

#if 0
	////////////////////////////////////////////////////////////////////////////
	// Now 3DES encrypt the rest of the credentials
	//
	//
	//    AES_Encrypt( All the creds )
	//
	//    push resulting blob into prgbTcpaIdentityReq
	//
#endif

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_ActivateIdentity(TSS_HTPM hTPM,	/*  in */
			  TSS_HKEY hIdentKey,	/*  in */
			  UINT32 ulAsymCAContentsBlobLength,	/*  in */
			  BYTE * rgbAsymCAContentsBlob,	/*  in */
			  UINT32 * pulCredentialLength,	/*  out */
			  BYTE ** prgbCredential	/*  out */
    )
{
	TCS_AUTH idKeyAuth;
	TCS_AUTH ownerAuth;
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_HPOLICY hIDPolicy, hTPMPolicy;
	UINT16 offset;
	BYTE hashblob[0x1000];
	TCPA_DIGEST digest;
	TSS_RESULT result;
	TCS_KEY_HANDLE tcsKeyHandle;
	UINT32 usesAuth;
	TCS_AUTH *pIDKeyAuth;

	if (pulCredentialLength == NULL || prgbCredential == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_2(hTPM, TSS_OBJECT_TYPE_TPM, hIdentKey,
				       TSS_OBJECT_TYPE_RSAKEY)))
		return result;

	if ((result = obj_isConnected_2(hTPM, hIdentKey, &tcsContext)))
		return result;

	tcsKeyHandle = getTCSKeyHandle(hIdentKey);
	if (tcsKeyHandle == NULL_HKEY)
		return TSS_E_KEY_NOT_LOADED;

	if ((result = Tspi_GetPolicyObject(hIdentKey, TSS_POLICY_USAGE, &hIDPolicy)))
		return result;

	if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hTPMPolicy)))
		return result;

	if ((result = policy_UsesAuth(hIDPolicy, &usesAuth)))
		return result;

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_ActivateTPMIdentity, hashblob);
	Trspi_LoadBlob_UINT32(&offset, ulAsymCAContentsBlobLength, hashblob);
	Trspi_LoadBlob(&offset, ulAsymCAContentsBlobLength, hashblob, rgbAsymCAContentsBlob);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	if (usesAuth == 1) {
		if ((result = secret_PerformAuth_OIAP(hIDPolicy, digest, &idKeyAuth)))
			return result;
		pIDKeyAuth = &idKeyAuth;
	} else {
		pIDKeyAuth = NULL;
	}

	if ((result = secret_PerformAuth_OIAP(hTPMPolicy, digest, &ownerAuth))) {
		if (usesAuth == 1)
			TCSP_TerminateHandle(tcsContext, idKeyAuth.AuthHandle);
		return result;
	}

	if ((result = TCSP_ActivateTPMIdentity(tcsContext,	/* in */
					      tcsKeyHandle,	/* in */
					      ulAsymCAContentsBlobLength,/* in */
					      rgbAsymCAContentsBlob,	/* in */
					      pIDKeyAuth,	/* in, out */
					      &ownerAuth,	/* in, out */
					      pulCredentialLength,	/* out */
					      prgbCredential)))	/* out */
		return result;

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashblob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_ActivateTPMIdentity, hashblob);
	Trspi_LoadBlob(&offset, *pulCredentialLength, hashblob, *prgbCredential);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	if (usesAuth == 1) {
		if ((result = secret_ValidateAuth_OIAP(hIDPolicy, digest, &idKeyAuth)))
			return result;
	}

	if ((result = secret_ValidateAuth_OIAP(hTPMPolicy, digest, &ownerAuth)))
		return result;

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_ClearOwner(TSS_HTPM hTPM,	/*  in */
		    BOOL fForcedClear	/*  in */
    )
{
	TCPA_RESULT result;
	TCS_AUTH auth;
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_DIGEST hashDigest;
	BYTE *hashBlob;
	UINT16 offset;
	TSS_HPOLICY hPolicy;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if (fForcedClear) {	/*  TPM_OwnerClear */
		if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hPolicy)))
			return result;

		/* ===  Now do some Hash'ing */
		offset = 0;
		hashBlob = malloc(sizeof(UINT32));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_OwnerClear, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);
		/* ===  hashDigest now has the hash result       */

		if ((result = secret_PerformAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;

		if ((result = TCSP_OwnerClear(tcsContext, &auth)))
			return result;

		/* validate auth */
		offset = 0;
		hashBlob = malloc(2 * sizeof(UINT32));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", 2 * sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_OwnerClear, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;
	} else {
		if ((result = TCSP_ForceClear(tcsContext)))
			return result;
	}
	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_SetStatus(TSS_HTPM hTPM,	/*  in */
		   TSS_FLAG statusFlag,	/*  in */
		   BOOL fTpmState	/*  in */
    )
{
	TCS_AUTH auth;
	TSS_RESULT result;
	BYTE *hashBlob;
	UINT16 offset;
	TCPA_DIGEST hashDigest;
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_HPOLICY hPolicy;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hPolicy)))
		return result;

	switch (statusFlag) {
	case TSS_TPMSTATUS_DISABLEOWNERCLEAR:

		hashBlob = malloc(sizeof(UINT32));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_DisableOwnerClear, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_PerformAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;

		if ((result = TCSP_DisableOwnerClear(tcsContext, &auth)))
			return result;

		offset = 0;
		hashBlob = malloc(2 * sizeof(UINT32));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", 2 * sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_DisableOwnerClear, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;
		break;
	case TSS_TPMSTATUS_DISABLEFORCECLEAR:
		result = TCSP_DisableForceClear(tcsContext);
		break;
	case TSS_TPMSTATUS_OWNERSETDISABLE:

		hashBlob = malloc(sizeof(UINT32) + sizeof(TSS_BOOL));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(UINT32) + sizeof(TSS_BOOL));
			return TSS_E_OUTOFMEMORY;
		}
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_OwnerSetDisable, hashBlob);
		hashBlob[(offset++)] = fTpmState;
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_PerformAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;

		if ((result = TCSP_OwnerSetDisable(tcsContext, fTpmState, &auth)))
			return result;

		offset = 0;
		hashBlob = malloc(8);
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", 2 * sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_OwnerSetDisable, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;
		break;
	case TSS_TPMSTATUS_PHYSICALDISABLE:
		result = TCSP_PhysicalDisable(tcsContext);
		break;
	case TSS_TPMSTATUS_PHYSICALSETDEACTIVATED:
		result = TCSP_PhysicalSetDeactivated(tcsContext, fTpmState);
		break;
	case TSS_TPMSTATUS_SETTEMPDEACTIVATED:
		result = TCSP_SetTempDeactivated(tcsContext);
		break;
	case TSS_TPMSTATUS_SETOWNERINSTALL:
		result = TCSP_SetOwnerInstall(tcsContext, fTpmState);
		break;
	case TSS_TPMSTATUS_DISABLEPUBEKREAD:

		hashBlob = malloc(sizeof(UINT32));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_DisablePubekRead, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_PerformAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;

		if ((result = TCSP_DisablePubekRead(tcsContext, &auth)))
			return result;

		offset = 0;
		hashBlob = malloc(2 * sizeof(UINT32));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", 2 * sizeof(UINT32));
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_DisablePubekRead, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, hashDigest, &auth)))
			return result;
		break;
	default:
		return TSS_E_BAD_PARAMETER;
		break;
	}

	return result;
}

BOOL
MakeMeABOOL(UINT32 i)
{
	if (i)
		return TRUE;
	else
		return FALSE;
}

BOOL
InvertMe(UINT32 i)
{
	if (i)
		return FALSE;
	else
		return TRUE;
}

TSS_RESULT
Tspi_TPM_GetStatus(TSS_HTPM hTPM,	/*  in */
		   TSS_FLAG statusFlag,	/*  in */
		   BOOL * pfTpmState	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_HPOLICY hPolicy;
	TCS_AUTH auth;
	TSS_RESULT result;
	UINT32 nonVolFlags;
	UINT32 volFlags;
	TCPA_VERSION version;
	TCPA_DIGEST digest;
	BYTE hashBlob[128];
	UINT16 offset;

	LogDebug1("Tspi_TPM_GetStatus");

	if (pfTpmState == NULL)
		return TSS_E_BAD_PARAMETER;

	for (;;) {
		if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
			break;	/* return result; */

		if ((result = obj_isConnected_1(hTPM, &tcsContext)))
			break;	/* return result; */

		if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hPolicy)))
			break;	/* return result; */

		UINT32ToArray(TPM_ORD_GetCapabilityOwner, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, sizeof(UINT32), hashBlob, digest.digest);

		if ((result = secret_PerformAuth_OIAP(hPolicy, digest, &auth)))
			break;	/* return result; */

		break;
	}
	if (result) {
		LogDebug("Failed GetStatus with result 0x%.8X", result);
		return result;
	}

	if ((result = TCSP_GetCapabilityOwner(tcsContext,	/*  in */
					     &auth,	/*  out */
					     &version,	/*  out */
					     &nonVolFlags,	/*  out */
					     &volFlags	/*  out */
	    )))
		return result;

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_GetCapabilityOwner, hashBlob);
	Trspi_LoadBlob_TCPA_VERSION(&offset, hashBlob, version);
	Trspi_LoadBlob_UINT32(&offset, nonVolFlags, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, volFlags, hashBlob);

	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

	if ((result = secret_ValidateAuth_OIAP(hPolicy, digest, &auth)))
		return result;

	switch (statusFlag) {
	case TSS_TPMSTATUS_DISABLEOWNERCLEAR:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000010);
		break;
	case TSS_TPMSTATUS_DISABLEFORCECLEAR:
		*pfTpmState = MakeMeABOOL(volFlags & 0x00000002);
		break;
	case TSS_TPMSTATUS_DISABLED:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000001);
		break;
/* 	case TSS_TPMSTATUS_DEACTIVATED: */
	case TSS_TPMSTATUS_PHYSICALSETDEACTIVATED:
	case TSS_TPMSTATUS_SETTEMPDEACTIVATED:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000004);
		break;
		break;

	case TSS_TPMSTATUS_SETOWNERINSTALL:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000002);	/* not sure if this is right */
		break;

	case TSS_TPMSTATUS_DISABLEPUBEKREAD:
		*pfTpmState = InvertMe(MakeMeABOOL(nonVolFlags & 0x00000080));
		break;

	case TSS_TPMSTATUS_ALLOWMAINTENANCE:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000020);
		break;
	case TSS_TPMSTATUS_PHYSPRES_LIFETIMELOCK:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000040);
		break;
	case TSS_TPMSTATUS_PHYSPRES_HWENABLE:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000080);
		break;
	case TSS_TPMSTATUS_PHYSPRES_CMDENABLE:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000100);
		break;
	case TSS_TPMSTATUS_CEKP_USED:
		*pfTpmState = MakeMeABOOL(nonVolFlags & 0x00000200);
		break;
	case TSS_TPMSTATUS_PHYSPRESENCE:
		*pfTpmState = MakeMeABOOL(volFlags & 0x00000004);
		break;
	case TSS_TPMSTATUS_PHYSPRES_LOCK:
		*pfTpmState = MakeMeABOOL(volFlags & 0x00000008);
		break;

	default:
		return TSS_E_BAD_PARAMETER;
		break;
	}

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_SelfTestFull(TSS_HTPM hTPM	/*  in */
    )
{
	TSS_RESULT result;
	TCS_CONTEXT_HANDLE tcsContext;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	return TCSP_SelfTestFull(tcsContext);
}

TSS_RESULT
Tspi_TPM_CertifySelfTest(TSS_HTPM hTPM,	/*  in */
			 TSS_HKEY hKey,	/*  in */
			 TSS_VALIDATION * pValidationData	/*  in, out */
    )
{

	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_RESULT result;
	TCS_AUTH keyAuth;
	UINT16 offset = 0;
	BYTE *hashBlob;
	TCPA_DIGEST hash;
	TCPA_NONCE antiReplay;
	UINT32 outDataSize;
	BYTE *outData;
	TSS_HPOLICY hPolicy;
	TCS_KEY_HANDLE keyTCSKeyHandle;
	BYTE verifyInternally = 0;
	BYTE *keyData = NULL;
	UINT32 keyDataSize;
	TCPA_KEY keyContainer;
	TCS_AUTH *pKeyAuth;
	BOOL useAuth;
	TSS_HCONTEXT tspContext;

	if ((result = obj_checkType_2(hTPM, TSS_OBJECT_TYPE_TPM, hKey, TSS_OBJECT_TYPE_RSAKEY)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_2(hTPM, hKey, &tcsContext)))
		return result;

	if ((result = Tspi_GetPolicyObject(hKey, TSS_POLICY_USAGE, &hPolicy)))
		return result;

	if ((result = policy_UsesAuth(hPolicy, &useAuth)))
		return result;

	keyTCSKeyHandle = getTCSKeyHandle(hKey);
	if (keyTCSKeyHandle == NULL_HKEY)
		return TSS_E_KEY_NOT_LOADED;

	if (pValidationData == NULL)
		verifyInternally = 1;

	if (verifyInternally) {
		if ((result = internal_GetRandomNonce(tcsContext, &antiReplay))) {
			LogError1("Failed creating random nonce");
			return TSS_E_INTERNAL_ERROR;
		}
	} else
		memcpy(antiReplay.nonce, pValidationData->rgbExternalData, 20);

	if (useAuth) {
		LogDebug1("Uses Auth");

		/* ===  now setup the auth's */
		hashBlob = malloc(sizeof(UINT32) + sizeof(TCPA_NONCE));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(UINT32) + sizeof(TCPA_NONCE));
			return TSS_E_OUTOFMEMORY;
		}
		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_CertifySelfTest, hashBlob);
		Trspi_LoadBlob(&offset, sizeof(TCPA_NONCE), hashBlob, antiReplay.nonce);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hash.digest);
		free(hashBlob);

		if ((result = secret_PerformAuth_OIAP(hPolicy, hash, &keyAuth)))
			return result;
		pKeyAuth = &keyAuth;
	} else {
		LogDebug1("No Auth");
		pKeyAuth = NULL;
	}

	if ((result = TCSP_CertifySelfTest(tcsContext,	/*  in */
					  keyTCSKeyHandle,	/* hCertifyingKey,                     // in */
					  antiReplay,	/*  in */
					  pKeyAuth,	/* &keyAuth,                          // in, out */
					  &outDataSize,	/*  out  data signature */
					  &outData	/*  out */
	    )))
		return result;

	/* =============================== */
	/*      validate auth */
	if (useAuth) {
		offset = 0;
		hashBlob = malloc((3 * sizeof(UINT32)) + outDataSize);
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", (3 * sizeof(UINT32)) + outDataSize);
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_CertifySelfTest, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, outDataSize, hashBlob);
		Trspi_LoadBlob(&offset, outDataSize, hashBlob, outData);
		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hash.digest);
		free(hashBlob);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, hash, &keyAuth)))
			return result;
	}

	if (verifyInternally) {
		if ((result = Tspi_GetAttribData(hKey, TSS_TSPATTRIB_KEY_BLOB,
				       TSS_TSPATTRIB_KEYBLOB_BLOB, &keyDataSize, &keyData))) {
			LogError1("Failed call to GetAttribData to get key blob");
			return TSS_E_INTERNAL_ERROR;
		}

		offset = 0;
		Trspi_UnloadBlob_KEY(tspContext, &offset, keyData, &keyContainer);

		offset = 0;
		hashBlob = malloc(sizeof(UINT32) + sizeof(TCPA_NONCE) + strlen("Test Passed"));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", sizeof(UINT32) + sizeof(TCPA_NONCE)
					+ strlen("Test Passed"));
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob(&offset, strlen("Test Passed"), hashBlob, "Test Passed");
		Trspi_LoadBlob(&offset, sizeof(TCPA_NONCE), hashBlob, antiReplay.nonce);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_CertifySelfTest, hashBlob);

		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hash.digest);
		free(hashBlob);

		if ((result = Trspi_Verify(TSS_HASH_SHA1, hash.digest, 20,
					 keyContainer.pubKey.key, keyContainer.pubKey.keyLength,
					 outData, outDataSize)))
			return TSS_E_VERIFICATION_FAILED;

	} else {
		pValidationData->ulDataLength = sizeof(TCPA_NONCE) + sizeof(UINT32) + strlen("Test Passed");
		pValidationData->rgbData = calloc_tspi(tspContext, pValidationData->ulDataLength);
		if (pValidationData->rgbData == NULL) {
			LogError("malloc of %d bytes failed.", pValidationData->ulDataLength);
			return TSS_E_OUTOFMEMORY;
		}
		offset = 0;
		Trspi_LoadBlob(&offset, strlen("Test Passed"), pValidationData->rgbData, "Test Passed");
		Trspi_LoadBlob(&offset, sizeof(TCPA_NONCE), pValidationData->rgbData, antiReplay.nonce);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_CertifySelfTest, pValidationData->rgbData);
		pValidationData->ulValidationLength = outDataSize;
		pValidationData->rgbValidationData = calloc_tspi(tspContext, outDataSize);
		if (pValidationData->rgbValidationData == NULL) {
			LogError("malloc of %d bytes failed.", pValidationData->ulValidationLength);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbValidationData, outData, outDataSize);
	}

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_GetTestResult(TSS_HTPM hTPM,	/*  in */
		       UINT32 * pulTestResultLength,	/*  out */
		       BYTE ** prgbTestResult	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_RESULT result;

	if (pulTestResultLength == NULL || prgbTestResult == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	return TCSP_GetTestResult(tcsContext, pulTestResultLength, prgbTestResult);
}

TSS_RESULT
Tspi_TPM_GetCapability(TSS_HTPM hTPM,	/*  in */
		       TSS_FLAG capArea,	/*  in */
		       UINT32 ulSubCapLength,	/*  in */
		       BYTE * rgbSubCap,	/*  in */
		       UINT32 * pulRespDataLength,	/*  out */
		       BYTE ** prgbRespData	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_HCONTEXT tspContext;
	TCPA_CAPABILITY_AREA tcsCapArea;
	UINT32 tcsSubCap;
	UINT32 tcsSubCapContainer;
	TSS_RESULT result;
	UINT32 nonVolFlags, volFlags, respLen;
	TCPA_VERSION version;
	TCPA_DIGEST digest;
	BYTE hashBlob[128], *respData;
	UINT16 offset;
	TSS_HPOLICY hPolicy;
	TCS_AUTH auth;
	TSS_BOOL fOwnerAuth = FALSE; /* flag for caps that need owner auth */

	if (pulRespDataLength == NULL || prgbRespData == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	/* Verify the caps and subcaps */
	switch (capArea) {
	case TSS_TPMCAP_ORD:
		if ((ulSubCapLength != sizeof(UINT32)) && rgbSubCap)
			return TSS_E_BAD_PARAMETER;

		tcsCapArea = TCPA_CAP_ORD;
		tcsSubCap = *(UINT32 *)rgbSubCap;
		break;
	case TSS_TPMCAP_FLAG:
		fOwnerAuth = TRUE;
		break;
	case TSS_TPMCAP_ALG:	/*  Queries whether an algorithm is supported. */
		if ((ulSubCapLength != sizeof(UINT32)) && rgbSubCap)
			return TSS_E_BAD_PARAMETER;

		tcsCapArea = TCPA_CAP_ALG;
		tcsSubCap = *(UINT32 *)rgbSubCap;
		break;
	case TSS_TPMCAP_PROPERTY:	/*     Determines a physical property of the TPM. */
		if ((ulSubCapLength != sizeof(UINT32)) && rgbSubCap)
			return TSS_E_BAD_PARAMETER;

		tcsCapArea = TCPA_CAP_PROPERTY;
		tcsSubCapContainer = *(UINT32 *)rgbSubCap;

		if (tcsSubCapContainer == TSS_TPMCAP_PROP_PCR) {
			tcsSubCap = TCPA_CAP_PROP_PCR;
		} else if (tcsSubCapContainer == TSS_TPMCAP_PROP_DIR) {
			tcsSubCap = TCPA_CAP_PROP_DIR;
		} else if (tcsSubCapContainer == TSS_TPMCAP_PROP_SLOTS) {
			tcsSubCap = TCPA_CAP_PROP_SLOTS;
		} else if (tcsSubCapContainer == TSS_TPMCAP_PROP_MANUFACTURER) {
			tcsSubCap = TCPA_CAP_PROP_MANUFACTURER;
		} else
			return TSS_E_BAD_PARAMETER;
		break;
	case TSS_TPMCAP_VERSION:	/*      Queries the current TPM version. */
		tcsCapArea = TCPA_CAP_VERSION;
		break;
	default:
		return TSS_E_BAD_PARAMETER;
		break;
	}

	if (fOwnerAuth) {
		/* do an owner authorized get capability call */
		UINT32ToArray(TPM_ORD_GetCapabilityOwner, hashBlob);
		Trspi_Hash(TSS_HASH_SHA1, sizeof(UINT32), hashBlob, digest.digest);

		if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hPolicy)))
			return result;

		if ((result = secret_PerformAuth_OIAP(hPolicy, digest, &auth)))
			return result;

		if ((result = TCSP_GetCapabilityOwner(tcsContext,       /*  in */
						&auth,     /*  out */
						&version,  /*  out */
						&nonVolFlags,      /*  out */
						&volFlags  /*  out */
						)))
			return result;

		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, TPM_ORD_GetCapabilityOwner, hashBlob);
		Trspi_LoadBlob_TCPA_VERSION(&offset, hashBlob, version);
		Trspi_LoadBlob_UINT32(&offset, nonVolFlags, hashBlob);
		Trspi_LoadBlob_UINT32(&offset, volFlags, hashBlob);

		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

		if ((result = secret_ValidateAuth_OIAP(hPolicy, digest, &auth)))
			return result;

		respLen = 2 * sizeof(UINT32);
		respData = calloc_tspi(tspContext, respLen);
		if (respData == NULL) {
			LogError("malloc of %d bytes failed.", respLen);
			return TSS_E_OUTOFMEMORY;
		}

		offset = 0;
		Trspi_LoadBlob_UINT32(&offset, nonVolFlags, respData);
		Trspi_LoadBlob_UINT32(&offset, volFlags, respData);

		*pulRespDataLength = respLen;
		*prgbRespData = respData;
	} else {
		tcsSubCap = endian32(tcsSubCap);

		result = TCSP_GetCapability(tcsContext, tcsCapArea, ulSubCapLength, (BYTE *)&tcsSubCap,
				pulRespDataLength, prgbRespData);

		if (*pulRespDataLength == sizeof(UINT32)) {
			*((UINT32 *)(*prgbRespData)) = endian32(*((UINT32 *)(*prgbRespData)));
		}
	}

	return result;
}

TSS_RESULT
Tspi_TPM_GetCapabilitySigned(TSS_HTPM hTPM,	/*  in */
			     TSS_HTPM hKey,	/*  in */
			     TSS_FLAG capArea,	/*  in */
			     UINT32 ulSubCapLength,	/*  in */
			     BYTE * rgbSubCap,	/*  in */
			     TSS_VALIDATION * pValidationData,	/*  in, out */
			     UINT32 * pulRespDataLength,	/*  out */
			     BYTE ** prgbRespData	/*  out */
    )
{
#if 1
	/*
	 * Function was found to have a vulnerability, so implementation is not
	 * required by the TSS 1.1b spec.
	 */
	return TSS_E_NOTIMPL;
#else
	TCS_AUTH auth;
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_RESULT result;
	BYTE *hashBlob;
	UINT16 offset;
	TCPA_DIGEST hashDigest;
	TCPA_VERSION version;
	TSS_HPOLICY hPolicy;
	TCPA_NONCE antiReplay;
	TCS_KEY_HANDLE tcsKeyHandle;
	TCPA_CAPABILITY_AREA tcsCapArea;
	UINT32 tcsSubCapContainer;
	BYTE tcsSubCap[4];
	BYTE verifyInternally = 0;
	UINT32 sigSize;
	BYTE *sig = NULL;
	UINT32 keyDataSize;
	BYTE *keyData;
	TCPA_KEY keyContainer;

	if (pulRespDataLength == NULL || prgbRespData == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_2(hTPM, TSS_OBJECT_TYPE_TPM, hKey, TSS_OBJECT_TYPE_RSAKEY)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	tcsKeyHandle = getTCSKeyHandle(hKey);
	if (tcsKeyHandle == NULL_HKEY)
		return TSS_E_KEY_NOT_LOADED;

	if ((result = Tspi_GetPolicyObject(hKey, TSS_POLICY_USAGE, &hPolicy)))
		return result;

	/* ---          Verify the caps and subcaps */
	switch (capArea) {

	case TSS_TPMCAP_ALG:	/*  Queries whether an algorithm is supported. */
		tcsCapArea = TCPA_CAP_ALG;
		break;
/* 	case TSS_TPMCAP_FLAG://	Queries whether a flag is on or off. */
		/* ---  this is getCapOwner */
		/*      ---             This flag is removed */
/* 		return TSS_SUCCESS; */
/* 		break; */
	case TSS_TPMCAP_PROPERTY:	/*     Determines a physical property of the TPM. */
		tcsCapArea = TCPA_CAP_PROPERTY;
		tcsSubCapContainer = Decode_UINT32(rgbSubCap);
		if (tcsSubCapContainer == TSS_TPMCAP_PROP_PCR) {
			UINT32ToArray(TCPA_CAP_PROP_PCR, tcsSubCap);
		} else if (tcsSubCapContainer == TSS_TPMCAP_PROP_DIR) {
			UINT32ToArray(TCPA_CAP_PROP_DIR, tcsSubCap);
		} else if (tcsSubCapContainer == TSS_TPMCAP_PROP_SLOTS) {
			UINT32ToArray(TCPA_CAP_PROP_SLOTS, tcsSubCap);
		} else if (tcsSubCapContainer == TSS_TPMCAP_PROP_MANUFACTURER) {
			UINT32ToArray(TCPA_CAP_PROP_MANUFACTURER, tcsSubCap);
		} else
			return TSS_E_BAD_PARAMETER;
		break;
	case TSS_TPMCAP_VERSION:	/*      Queries the current TPM version. */
		tcsCapArea = TCPA_CAP_VERSION;
		break;

	default:

		return TSS_E_BAD_PARAMETER;

	}

	/***************************************
	 *	If we get to this point, then neither getCapOwner nor
	 *		an internal getCap was called.
	 ****************************************/
	if (pValidationData == NULL)
		verifyInternally = 1;

	if (verifyInternally) {
/* 		memset( antiReplay.nonce, 0xBB, 20 );		//change to random */
		if ((result = internal_GetRandomNonce(tcsContext, &antiReplay))) {
			LogError1("Failed creating random nonce");
			return TSS_E_INTERNAL_ERROR;
		}
	} else
		memcpy(antiReplay.nonce, pValidationData->rgbData, sizeof(TCPA_NONCE));

	/* ===  Now do some Hash'ing */
	offset = 0;
	hashBlob = malloc((3 * sizeof(UINT32)) + sizeof(TCPA_NONCE) + ulSubCapLength);
	if (hashBlob == NULL) {
		LogError("malloc of %d bytes failed.", (3 * sizeof(UINT32)) + sizeof(TCPA_NONCE)
				+ ulSubCapLength);
		return TSS_E_OUTOFMEMORY;
	}
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_GetCapabilitySigned, hashBlob);
	Trspi_LoadBlob(&offset, sizeof(TCPA_NONCE), hashBlob, antiReplay.nonce);
	Trspi_LoadBlob_UINT32(&offset, tcsCapArea, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, ulSubCapLength, hashBlob);
	Trspi_LoadBlob(&offset, ulSubCapLength, hashBlob, rgbSubCap);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
	free(hashBlob);
	/* ===  hashDigest now has the hash result       */
	/* ===  HMAC */
	if ((result = secret_PerformAuth_OIAP(hPolicy, hashDigest, &auth)))
		return result;

	if ((result = TCSP_GetCapabilitySigned(tcsContext,	/*  in */
					      tcsKeyHandle, antiReplay,	/*  in */
					      tcsCapArea,	/* capArea,                 // in */
					      ulSubCapLength,	/*  in */
					      rgbSubCap,	/*  in */
					      &auth, &version, pulRespDataLength,	/*  out */
					      prgbRespData,	/*  out */
					      &sigSize,	/* &pValidationData->ulValidationLength, */
					      &sig))) {	/* &pValidationData->rgbValidationData )) */
		TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
		return result;
	}

	/* ============validate return auth */
	offset = 0;
	hashBlob = malloc(20 + *pulRespDataLength + sigSize);
	if (hashBlob == NULL) {
		LogError("malloc of %d bytes failed.", 20 + *pulRespDataLength + sigSize);
		TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
		free(sig);
		return TSS_E_OUTOFMEMORY;
	}
	Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_GetCapabilitySigned, hashBlob);
	Trspi_LoadBlob_TCPA_VERSION(&offset, hashBlob, version);
	Trspi_LoadBlob_UINT32(&offset, *pulRespDataLength, hashBlob);
	Trspi_LoadBlob(&offset, *pulRespDataLength, hashBlob, *prgbRespData);
	Trspi_LoadBlob_UINT32(&offset, sigSize, hashBlob);
	Trspi_LoadBlob(&offset, sigSize, hashBlob, sig);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
	free(hashBlob);

	if ((result = secret_ValidateAuth_OIAP(hPolicy, hashDigest, &auth))) {
		TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
		free(sig);
		return result;
	}

	if (verifyInternally) {
		if ((result = Tspi_GetAttribData(hKey, TSS_TSPATTRIB_KEY_BLOB,
				       TSS_TSPATTRIB_KEYBLOB_BLOB, &keyDataSize, &keyData))) {
			TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
			free(sig);
			LogError1("Failed call to GetAttribData to get key blob");
			return TSS_E_INTERNAL_ERROR;
		}

		offset = 0;
		Trspi_UnloadBlob_KEY(tspContext, &offset, keyData, &keyContainer);

		offset = 0;
		hashBlob = malloc(*pulRespDataLength + sizeof(TCPA_NONCE));
		if (hashBlob == NULL) {
			LogError("malloc of %d bytes failed.", *pulRespDataLength + sizeof(TCPA_NONCE));
			TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
			free(sig);
			return TSS_E_OUTOFMEMORY;
		}
		Trspi_LoadBlob(&offset, *pulRespDataLength, hashBlob, *prgbRespData);
		Trspi_LoadBlob(&offset, sizeof(TCPA_NONCE), hashBlob, antiReplay.nonce);

		Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);
		free(hashBlob);

		if ((result = Trspi_Verify(TSS_HASH_SHA1, hashDigest.digest, 20,
					 keyContainer.pubKey.key, keyContainer.pubKey.keyLength,
					 sig, sigSize))) {
			TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
			free(sig);
			return TSS_E_VERIFICATION_FAILED;
		}

	} else {
		pValidationData->ulDataLength = *pulRespDataLength + 20;
		pValidationData->rgbData = calloc_tspi(tspContext, *pulRespDataLength);
		if (pValidationData->rgbData == NULL) {
			LogError("malloc of %d bytes failed.", *pulRespDataLength);
			TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
			free(sig);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbData, *prgbRespData, *pulRespDataLength);
		memcpy(&pValidationData->rgbData[(*pulRespDataLength)], antiReplay.nonce, 20);
		pValidationData->ulValidationLength = sigSize;
		pValidationData->rgbValidationData = calloc_tspi(tspContext, sigSize);
		if (pValidationData->rgbValidationData == NULL) {
			LogError("malloc of %d bytes failed.", sigSize);
			TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
			free(sig);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbValidationData, sig, sigSize);
	}

	return TSS_SUCCESS;
#endif
}

TSS_RESULT
Tspi_TPM_CreateMaintenanceArchive(TSS_HTPM hTPM,	/*  in */
				  BOOL fGenerateRndNumber,	/*  in */
				  UINT32 * pulRndNumberLength,	/*  out */
				  BYTE ** prgbRndNumber,	/*  out */
				  UINT32 * pulArchiveDataLength,	/*  out */
				  BYTE ** prgbArchiveData	/*  out */
    )
{
	if (pulRndNumberLength == NULL || prgbRndNumber == NULL ||
	    pulArchiveDataLength == NULL || prgbArchiveData == NULL)
		return TSS_E_BAD_PARAMETER;

	return TSS_E_NOTIMPL;
}

TSS_RESULT
Tspi_TPM_KillMaintenanceFeature(TSS_HTPM hTPM	/*  in */
    )
{
	return TSS_E_NOTIMPL;
}

TSS_RESULT
Tspi_TPM_LoadMaintenancePubKey(TSS_HTPM hTPM,	/*  in */
			       TSS_HKEY hMaintenanceKey,	/*  in */
			       TSS_VALIDATION * pValidationData	/*  in, out */
    )
{
	if (pValidationData == NULL)
		return TSS_E_BAD_PARAMETER;

	return TSS_E_NOTIMPL;
}

TSS_RESULT
Tspi_TPM_CheckMaintenancePubKey(TSS_HTPM hTPM,	/*  in */
				TSS_HKEY hMaintenanceKey,	/*  in */
				TSS_VALIDATION * pValidationData	/*  in, out */
    )
{
	if (pValidationData == NULL)
		return TSS_E_BAD_PARAMETER;

	return TSS_E_NOTIMPL;
}

TSS_RESULT
Tspi_TPM_GetRandom(TSS_HTPM hTPM,	/*  in */
		   UINT32 ulRandomDataLength,	/*  in */
		   BYTE ** prgbRandomData	/*  out */
    )
{
	UINT32 length;
	UINT32 index;
	TSS_RESULT status;
	BYTE *data;
	BYTE *holdData = NULL;
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_RESULT result;
	TSS_HCONTEXT tspContext;

	if (ulRandomDataLength == 0)
		return TSS_SUCCESS;

	if (prgbRandomData == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	index = 0;
	while (index < ulRandomDataLength) {
		length = ulRandomDataLength - index;
		status = TCSP_GetRandom(tcsContext, &length, &data);
		if (status) {
			if (holdData)
				free(holdData);
			return status;
		}

		if (holdData == NULL) {
			holdData = malloc(length);
			if (holdData == NULL) {
				LogError("malloc of %d bytes failed.", length);
				return TSS_E_OUTOFMEMORY;
			}
		} else {
			holdData = realloc(holdData, index + length);
			if (holdData == NULL) {
				LogError("malloc of %d bytes failed.", index + length);
				return TSS_E_OUTOFMEMORY;
			}
		}

		memcpy(&holdData[index], data, length);
		index += length;

		free(data);
	}

	*prgbRandomData = calloc_tspi(tspContext, index);
	if (*prgbRandomData == NULL) {
		LogError("malloc of %d bytes failed.", index);
		result = TSS_E_OUTOFMEMORY;
	} else {
		memcpy(*prgbRandomData, holdData, index);
	}

	free(holdData);
	return 0;
}

TSS_RESULT
Tspi_TPM_StirRandom(TSS_HTPM hTPM,	/*  in */
		    UINT32 ulEntropyDataLength,	/*  in */
		    BYTE * rgbEntropyData	/*  in */
    )
{
	TSS_RESULT result;
	TCS_CONTEXT_HANDLE tcsContext;

	if (ulEntropyDataLength > 0 && rgbEntropyData == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = TCSP_StirRandom(tcsContext, ulEntropyDataLength, rgbEntropyData)))
		return result;

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_AuthorizeMigrationTicket(TSS_HTPM hTPM,	/*  in */
				  TSS_HKEY hMigrationKey,	/*  in */
				  TSS_MIGRATION_SCHEME migrationScheme,	/*  in */
				  UINT32 * pulMigTicketLength,	/*  out */
				  BYTE ** prgbMigTicket	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	UINT16 offset;
	BYTE hashblob[0x1000];
	TCPA_DIGEST digest;
	TCPA_RESULT result;
	TSS_HPOLICY hOwnerPolicy;
	UINT32 migrationKeySize;
	BYTE *migrationKeyBlob;
	TCPA_KEY tcpaKey;
	BYTE pubKeyBlob[0x1000];
	TCS_AUTH ownerAuth;
	UINT32 pubKeySize;
	TSS_HCONTEXT tspContext;

	if (pulMigTicketLength == NULL || prgbMigTicket == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_2(hTPM, TSS_OBJECT_TYPE_TPM, hMigrationKey,
				       TSS_OBJECT_TYPE_RSAKEY)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	/*  get the tpm Policy */
	if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hOwnerPolicy)))
		return result;

	/*  Get the migration key blob */
	if ((result = Tspi_GetAttribData(hMigrationKey,
					TSS_TSPATTRIB_KEY_BLOB,
					TSS_TSPATTRIB_KEYBLOB_BLOB,
					&migrationKeySize, &migrationKeyBlob)))
		return result;

	/* ---  First, turn the keyBlob into a TCPA_KEY structure */
	offset = 0;
	Trspi_UnloadBlob_KEY(tspContext, &offset, migrationKeyBlob, &tcpaKey);
	free_tspi(tspContext, migrationKeyBlob);

	/* ---  Then pull the _PUBKEY portion out of that struct into a blob */
	offset = 0;
	Trspi_LoadBlob_KEY_PARMS(&offset, pubKeyBlob, &tcpaKey.algorithmParms);
	Trspi_LoadBlob_STORE_PUBKEY(&offset, pubKeyBlob, &tcpaKey.pubKey);
	pubKeySize = offset;

	/* ---  Auth */
	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_AuthorizeMigrationKey, hashblob);
	Trspi_LoadBlob_UINT16(&offset, migrationScheme, hashblob);
	Trspi_LoadBlob(&offset, pubKeySize, hashblob, pubKeyBlob);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	if ((result = secret_PerformAuth_OIAP(hOwnerPolicy, digest, &ownerAuth)))
		return result;

	/* ---  Send command */
	if ((result = TCSP_AuthorizeMigrationKey(tcsContext,	/*  in */
						migrationScheme,	/*  in */
						pubKeySize,	/*  in */
						pubKeyBlob,	/*  in */
						&ownerAuth,	/*  in, out */
						pulMigTicketLength,	/*  out */
						prgbMigTicket	/*  out */
	    ))) {
		TCSP_TerminateHandle(tcsContext, ownerAuth.AuthHandle);
		return result;
	}

	/* ---  Validate Auth */
	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashblob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_AuthorizeMigrationKey, hashblob);
	Trspi_LoadBlob(&offset, *pulMigTicketLength, hashblob, *prgbMigTicket);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashblob, digest.digest);

	if ((result = secret_ValidateAuth_OIAP(hOwnerPolicy, digest, &ownerAuth))) {
		TCSP_TerminateHandle(tcsContext, ownerAuth.AuthHandle);
		return result;
	}

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_GetEvent(TSS_HTPM hTPM,	/*  in */
		  UINT32 ulPcrIndex,	/*  in */
		  UINT32 ulEventNumber,	/*  in */
		  TSS_PCR_EVENT * pPcrEvent	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_RESULT result;
	TSS_PCR_EVENT *event = NULL;

	if (pPcrEvent == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = TCS_GetPcrEvent(tcsContext, ulPcrIndex, &ulEventNumber, &event)))
		return result;

	memcpy(pPcrEvent, event, sizeof(TSS_PCR_EVENT));
	free(event);

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_GetEvents(TSS_HTPM hTPM,	/*  in */
		   UINT32 ulPcrIndex,	/*  in */
		   UINT32 ulStartNumber,	/*  in */
		   UINT32 * pulEventNumber,	/*  in, out */
		   TSS_PCR_EVENT ** prgbPcrEvents	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_RESULT result;
	TSS_PCR_EVENT *events = NULL;
	UINT32 i;

	if (pulEventNumber == NULL || prgbPcrEvents == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = TCS_GetPcrEventsByPcr(tcsContext, ulPcrIndex, ulStartNumber, pulEventNumber, &events)))
		return result;

	*prgbPcrEvents = events;

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_GetEventLog(TSS_HTPM hTPM,	/*  in */
		     UINT32 * pulEventNumber,	/*  out */
		     TSS_PCR_EVENT ** prgbPcrEvents	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TSS_RESULT result;

	if (pulEventNumber == NULL || prgbPcrEvents == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	return TCS_GetPcrEventLog(tcsContext, pulEventNumber, prgbPcrEvents);
}

TSS_RESULT
Tspi_TPM_Quote(TSS_HTPM hTPM,	/*  in */
	       TSS_HKEY hIdentKey,	/*  in */
	       TSS_HPCRS hPcrComposite,	/*  in */
	       TSS_VALIDATION * pValidationData	/*  in, out */
    )
{
	TCPA_RESULT result;
	TCS_AUTH privAuth;
	TCS_AUTH *pPrivAuth = &privAuth;
	UINT16 offset;
	BYTE hashBlob[1000];
	TCPA_DIGEST digest;
	BYTE mySecret[20] = { 0 };
	TCS_CONTEXT_HANDLE tcsContext;
	TCS_KEY_HANDLE tcsKeyHandle;
	TSS_HPOLICY hPolicy;
	TCPA_NONCE antiReplay;
	AnObject *anObject;
	TCPA_PCR_OBJECT *pcrObject;
	UINT32 pcrDataSize;
	BYTE *pcrData;
	BOOL verifyInternally = 0;
	UINT32 validationLength = 0;
	BYTE *validationData = NULL;
	UINT32 pcrDataOutSize;
	BYTE *pcrDataOut;

	/*  james */
	UINT32 keyDataSize;
	BYTE *keyData;
	TCPA_KEY keyContainer;
	BYTE quoteinfo[1024];
	UINT32 usesAuth;
	TSS_HCONTEXT tspContext;

	if (hPcrComposite == NULL_HPCRS) {
		if ((result = obj_checkType_2(hTPM, TSS_OBJECT_TYPE_TPM,
					       hIdentKey, TSS_OBJECT_TYPE_RSAKEY)))
			return result;
		if ((result = obj_isConnected_2(hTPM, hIdentKey, &tcsContext)))
			return result;
	} else {
		if ((result = obj_checkType_3(hTPM, TSS_OBJECT_TYPE_TPM,
					       hIdentKey,
					       TSS_OBJECT_TYPE_RSAKEY,
					       hPcrComposite, TSS_OBJECT_TYPE_PCRS)))
			return result;
		if ((result = obj_isConnected_3(hTPM, hIdentKey, hPcrComposite, &tcsContext)))
			return result;
	}

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if (pValidationData == NULL)
		verifyInternally = TRUE;

	/*  get the Identity TCS keyHandle */
	tcsKeyHandle = getTCSKeyHandle(hIdentKey);
	if (tcsKeyHandle == NULL_HKEY)
		return TSS_E_KEY_NOT_LOADED;

	/*  get the PCR data */

	/*  get the identKey Policy */
	if ((result = Tspi_GetPolicyObject(hIdentKey, TSS_POLICY_USAGE, &hPolicy)))
		return result;

	anObject = getAnObjectByHandle(hPcrComposite);
	if (anObject == NULL || anObject->memPointer == NULL)
		return TSS_E_INVALID_HANDLE;
	pcrObject = anObject->memPointer;

	if (verifyInternally) {
		if ((result = internal_GetRandomNonce(tcsContext, &antiReplay)))
			return result;
	} else {
		memcpy(antiReplay.nonce, pValidationData->rgbExternalData, 20);
	}

	pcrDataSize = 0;
	if (hPcrComposite != 0) {
		offset = 0;
		pcrData = calloc_tspi(tspContext, 512);
		if (pcrData == NULL) {
			LogError("malloc of %d bytes failed.", 512);
			return TSS_E_OUTOFMEMORY;
		}
/* 		Trspi_LoadBlob_PCR_SELECTION( &offset, pcrData, pcrObject->pcrComposite.select ); */
		Trspi_LoadBlob_PCR_SELECTION(&offset, pcrData, pcrObject->select);
		pcrDataSize = offset;
	}

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_Quote, hashBlob);
	Trspi_LoadBlob(&offset, 20, hashBlob, antiReplay.nonce);
	Trspi_LoadBlob(&offset, pcrDataSize, hashBlob, pcrData);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

	if ((result = policy_UsesAuth(hPolicy, &usesAuth))) {
		return result;
	}

	if (usesAuth == FALSE) {
		pPrivAuth = NULL;
	} else {
		if ((result = secret_PerformAuth_OIAP(hPolicy, digest, &privAuth))) {
			return result;
		}
		pPrivAuth = &privAuth;
	}

	if ((result = TCSP_Quote(tcsContext,	/*  in */
				tcsKeyHandle,	/*  in */
				antiReplay,	/*  in */
				pcrDataSize,	/*  in, out */
				pcrData,	/*  in, out */
				pPrivAuth,	/*  in, out */
				&pcrDataOutSize, &pcrDataOut, &validationLength,	/*  out */
				&validationData	/*  out */
	    )))
		return result;

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_Quote, hashBlob);
	Trspi_LoadBlob(&offset, pcrDataOutSize, hashBlob, pcrDataOut);
	Trspi_LoadBlob_UINT32(&offset, validationLength, hashBlob);
	Trspi_LoadBlob(&offset, validationLength, hashBlob, validationData);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, digest.digest);

	if (usesAuth) {
		if ((result = secret_ValidateAuth_OIAP(hPolicy, digest, &privAuth)))
			return result;
	}
	if (verifyInternally) {
		/* validate the data here */
		if ((result = Tspi_GetAttribData(hIdentKey, TSS_TSPATTRIB_KEY_BLOB,
				       TSS_TSPATTRIB_KEYBLOB_BLOB, &keyDataSize, &keyData)))
			return result;

		offset = 0;
		Trspi_UnloadBlob_KEY(tspContext, &offset, keyData, &keyContainer);

		/*  creating pcrCompositeHash */
		Trspi_Hash(TSS_HASH_SHA1, pcrDataOutSize, pcrDataOut, digest.digest);

		/* generate Quote_info struct */
		/* 1. add version */
		offset = 0;
		Trspi_LoadBlob_TCPA_VERSION(&offset, quoteinfo, keyContainer.ver);
		/* 2. add "QUOT" */
		quoteinfo[offset++] = 'Q';
		quoteinfo[offset++] = 'U';
		quoteinfo[offset++] = 'O';
		quoteinfo[offset++] = 'T';
		/* 3. Composite Hash */
		Trspi_LoadBlob(&offset, 20, quoteinfo, digest.digest);
		/* 4. AntiReplay Nonce */
		Trspi_LoadBlob(&offset, 20, quoteinfo, antiReplay.nonce);

		/*  Hash 'em up good */
		Trspi_Hash(TSS_HASH_SHA1, offset, quoteinfo, digest.digest);

		if ((result = Trspi_Verify(TSS_HASH_SHA1, digest.digest, 20,
					 keyContainer.pubKey.key, keyContainer.pubKey.keyLength,
					 validationData, validationLength)))
			return result;

	} else {
		pValidationData->ulValidationLength = validationLength;
		pValidationData->rgbValidationData = calloc_tspi(tspContext, validationLength);
		if (pValidationData->rgbValidationData == NULL) {
			LogError("malloc of %d bytes failed.", validationLength);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbValidationData, validationData,
		       pValidationData->ulValidationLength);
		pValidationData->ulDataLength = pcrDataOutSize;
		pValidationData->rgbData = calloc_tspi(tspContext, pcrDataOutSize);
		if (pValidationData->rgbData == NULL) {
			LogError("malloc of %d bytes failed.", pcrDataOutSize);
			return TSS_E_OUTOFMEMORY;
		}
		memcpy(pValidationData->rgbData, pcrDataOut, pcrDataOutSize);
		pValidationData->ulExternalDataLength = 20;
		memcpy(pValidationData->rgbExternalData, antiReplay.nonce, 20);
		memcpy(&pValidationData->versionInfo,
		       getCurrentVersion(tspContext), sizeof (TCPA_VERSION));

	}
#if 0
	//create sig validation structure
	memcpy(SigValid->antiReplay.nonce, privAuth.NonceEven.nonce, 20);
	SigValid->SignatureLength = sigSize;
	memcpy(SigValid->Signature, sig, sigSize);
	offset = 0;
	Trspi_LoadBlob_PCR_COMPOSITE(&offset, pcrBlob, pcrComposite);
	LogArray("PCRBlob", pcrBlob, offset);

	Trspi_Hash(TSS_HASH_SHA1, offset, pcrBlob, digest.digest);

	offset = 0;
	Create_QUOTE_INFO_BLOB(&offset, keyObject->tcpa_key.ver, digest,
			       privAuth.NonceEven, quoteInfoBlob);

	Trspi_Hash(TSS_HASH_SHA1, offset, quoteInfoBlob, digest.digest);

	SigValid->DataLength = 20;
	memcpy(SigValid->Data, digest.digest, SigValid->DataLength);
#endif
	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_PcrExtend(TSS_HTPM hTPM,    /* in */
			UINT32 ulPcrIndex,        /* in */
			UINT32 ulPcrDataLength,   /* in */
			BYTE *pbPcrData,          /* in */
			TSS_PCR_EVENT *pPcrEvent, /* in */
			UINT32 * pulPcrValueLength,       /* out */
			BYTE ** prgbPcrValue      /* out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_PCRVALUE outDigest;
	TSS_RESULT result;
	BYTE *inDigest;
	UINT32 number;
	TSS_HCONTEXT tspContext;

	if (pulPcrValueLength == NULL || prgbPcrValue == NULL)
		return TSS_E_BAD_PARAMETER;

	if (ulPcrDataLength > 0 && pbPcrData == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	inDigest = malloc(TPM_DIGEST_SIZE);
	if (inDigest == NULL) {
		LogError("malloc of %d bytes failed.", TPM_DIGEST_SIZE);
		return TSS_E_OUTOFMEMORY;
	}

	if ((result = Trspi_Hash(TSS_HASH_SHA1, ulPcrDataLength, pbPcrData, inDigest)))
		return result;

	if ((result = TCSP_Extend(tcsContext, ulPcrIndex, *(TCPA_DIGEST *)inDigest, &outDigest)))
		return result;

	*prgbPcrValue = calloc_tspi(tspContext, sizeof(TCPA_PCRVALUE));
	if (*prgbPcrValue == NULL) {
		LogError("malloc of %d bytes failed.", sizeof(TCPA_PCRVALUE));
		return TSS_E_OUTOFMEMORY;
	}

	memcpy(*prgbPcrValue, &outDigest, sizeof(TCPA_PCRVALUE));
	*pulPcrValueLength = sizeof(TCPA_PCRVALUE);

	/* fill out the event structure and log it if its passed in */
	if (pPcrEvent != NULL) {
		TCPA_VERSION *ver = getCurrentVersion(hTPM);
		if (ver == NULL) {
			free(inDigest);
			return TSS_E_INTERNAL_ERROR;
		}

		memcpy(&(pPcrEvent->versionInfo), ver, sizeof(TSS_VERSION));
		pPcrEvent->ulPcrIndex = ulPcrIndex;
		/* leave eventType untouched */
		pPcrEvent->ulPcrValueLength = TPM_DIGEST_SIZE;
		pPcrEvent->rgbPcrValue = inDigest;
		/* leave ulEventLength untouched */
		/* leave rgbEvent untouched */

		if ((result = TCS_LogPcrEvent(tcsContext, *pPcrEvent, &number))) {
			pPcrEvent->rgbPcrValue = NULL;
			pPcrEvent->ulPcrValueLength = 0;
			free(inDigest);
		}
	}

	return result;
}

TSS_RESULT
Tspi_TPM_PcrRead(TSS_HTPM hTPM,	/*  in */
		 UINT32 ulPcrIndex,	/*  in */
		 UINT32 * pulPcrValueLength,	/*  out */
		 BYTE ** prgbPcrValue	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_PCRVALUE outDigest;
	TSS_RESULT result;
	TSS_HCONTEXT tspContext;

	if (pulPcrValueLength == NULL || prgbPcrValue == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = TCSP_PcrRead(tcsContext, ulPcrIndex, &outDigest)))
		return result;

	*prgbPcrValue = calloc_tspi(tspContext, sizeof(TCPA_PCRVALUE));
	if (*prgbPcrValue == NULL) {
		LogError("malloc of %d bytes failed.", sizeof(TCPA_PCRVALUE));
		return TSS_E_OUTOFMEMORY;
	}
	memcpy(*prgbPcrValue, outDigest.digest, sizeof(TCPA_PCRVALUE));
	*pulPcrValueLength = sizeof(TCPA_PCRVALUE);

	return TSS_SUCCESS;
}

TSS_RESULT
Tspi_TPM_DirWrite(TSS_HTPM hTPM,	/*  in */
		  UINT32 ulDirIndex,	/*  in */
		  UINT32 ulDirDataLength,	/*  in */
		  BYTE * rgbDirData	/*  in  */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_RESULT result;
	TCS_AUTH auth;
	TCPA_DIGEST hashDigest;
	UINT16 offset;
	BYTE hashBlob[32];
	TSS_HPOLICY hPolicy;
	TCPA_DIRVALUE dirValue = { { 0 } };

	if (rgbDirData == NULL && ulDirDataLength != 0)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = Tspi_GetPolicyObject(hTPM, TSS_POLICY_USAGE, &hPolicy)))
		return result;

#if 0
	memcpy(dirValue.digest, rgbDirData, ulDirDataLength);
#else
	/* hash the input data */
	if ((result = Trspi_Hash(TSS_HASH_SHA1, ulDirDataLength, rgbDirData, dirValue.digest)))
		return result;
#endif
	/* hash to be used for the OIAP calc */
	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_DirWriteAuth, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, ulDirIndex, hashBlob);
	Trspi_LoadBlob(&offset, sizeof(TCPA_DIGEST), hashBlob, (BYTE *)(&dirValue));
	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);

	/*  hashDigest now has the hash result       */
	if ((result = secret_PerformAuth_OIAP(hPolicy, hashDigest, &auth)))
		return result;

	if ((result = TCSP_DirWriteAuth(tcsContext,	/*  in */
				       ulDirIndex,	/*  in */
				       dirValue,	/*  in */
				       &auth))) {
		TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
		return result;
	}

	offset = 0;
	Trspi_LoadBlob_UINT32(&offset, result, hashBlob);
	Trspi_LoadBlob_UINT32(&offset, TPM_ORD_DirWriteAuth, hashBlob);
	Trspi_Hash(TSS_HASH_SHA1, offset, hashBlob, hashDigest.digest);

	if ((result = secret_ValidateAuth_OIAP(hPolicy, hashDigest, &auth))) {
		TCSP_TerminateHandle(tcsContext, auth.AuthHandle);
		return result;
	}
	return result;
}

TSS_RESULT
Tspi_TPM_DirRead(TSS_HTPM hTPM,	/*  in */
		 UINT32 ulDirIndex,	/*  in */
		 UINT32 * pulDirDataLength,	/*  out */
		 BYTE ** prgbDirData	/*  out */
    )
{
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_DIRVALUE dirValue;
	TSS_RESULT result;
	TSS_HCONTEXT tspContext;

	if (pulDirDataLength == NULL || prgbDirData == NULL)
		return TSS_E_BAD_PARAMETER;

	if ((result = obj_checkType_1(hTPM, TSS_OBJECT_TYPE_TPM)))
		return result;

	if ((tspContext = obj_getTspContext(hTPM)) == NULL_HCONTEXT)
		return TSS_E_INTERNAL_ERROR;

	if ((result = obj_isConnected_1(hTPM, &tcsContext)))
		return result;

	if ((result = TCSP_DirRead(tcsContext,	/*  in */
				  ulDirIndex,	/*  in */
				  &dirValue)))	/*  out */
		return result;

	*pulDirDataLength = 20;
	*prgbDirData = calloc_tspi(tspContext, *pulDirDataLength);
	if (*prgbDirData == NULL) {
		LogError("malloc of %d bytes failed.", *pulDirDataLength);
		return TSS_E_OUTOFMEMORY;
	}
	memcpy(*prgbDirData, dirValue.digest, *pulDirDataLength);
	return TSS_SUCCESS;
}
