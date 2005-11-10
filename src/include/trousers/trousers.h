
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004, 2005
 *
 */

#ifndef _TROUSERS_H_
#define _TROUSERS_H_

#include <openssl/rsa.h>

/*
 * Utility functions offered by trousers for use in your TSS app.
 *
 * All functions listed here are specific to the trousers TSS and should not be
 * used in applications that are intended to be portable.
 *
 */

/* Blob unloading functions */
void       Trspi_UnloadBlob(UINT16 *offset, UINT32 size, BYTE *container, BYTE *object);
void       Trspi_UnloadBlob_BYTE(UINT16 *offset, BYTE *dataOut, BYTE *blob);
void       Trspi_UnloadBlob_BOOL(UINT16 *offset, TSS_BOOL *dataOut, BYTE *blob);
void       Trspi_UnloadBlob_UINT32(UINT16 *offset, UINT32 *out, BYTE *blob);
void       Trspi_UnloadBlob_UINT16(UINT16 *offset, UINT16 *out, BYTE *blob);
void       Trspi_UnloadBlob_TSS_VERSION(UINT16 *offset, BYTE *blob, TSS_VERSION *out);
void       Trspi_UnloadBlob_TCPA_VERSION(UINT16 *offset, BYTE *blob, TCPA_VERSION *out);
TSS_RESULT Trspi_UnloadBlob_PCR_INFO(UINT16 *offset, BYTE *blob, TCPA_PCR_INFO *pcr);
TSS_RESULT Trspi_UnloadBlob_PCR_SELECTION(UINT16 *offset, BYTE *blob, TCPA_PCR_SELECTION *pcr);
TSS_RESULT Trspi_UnloadBlob_STORED_DATA(UINT16 *offset, BYTE *blob, TCPA_STORED_DATA *data);
void       Trspi_UnloadBlob_KEY_FLAGS(UINT16 *offset, BYTE *blob, TCPA_KEY_FLAGS *flags);
TSS_RESULT Trspi_UnloadBlob_KEY_PARMS(UINT16 *offset, BYTE *blob, TCPA_KEY_PARMS *keyParms);
void       Trspi_UnloadBlob_UUID(UINT16 *offset, BYTE *blob, TSS_UUID *uuid);
TSS_RESULT Trspi_UnloadBlob_STORE_PUBKEY(UINT16 *, BYTE *, TCPA_STORE_PUBKEY *);
void       Trspi_UnloadBlob_DIGEST(UINT16 *offset, BYTE *blob, TCPA_DIGEST digest);
TSS_RESULT Trspi_UnloadBlob_PUBKEY(UINT16 *offset, BYTE *blob, TCPA_PUBKEY *pubKey);
TSS_RESULT Trspi_UnloadBlob_KEY(UINT16 * offset, BYTE * blob, TCPA_KEY * key);
TSS_RESULT Trspi_UnloadBlob_MigrationKeyAuth(UINT16 *offset, BYTE *blob, TCPA_MIGRATIONKEYAUTH *migAuth);
TSS_RESULT Trspi_UnloadBlob_PCR_EVENT(UINT16 *offset, BYTE *blob, TSS_PCR_EVENT *event);
void       Trspi_UnloadBlob_KM_KEYINFO(UINT16 *offset, BYTE *blob, TSS_KM_KEYINFO *info);
TSS_RESULT Trspi_UnloadBlob_SYMMETRIC_KEY(UINT16 *offset, BYTE *blob, TCPA_SYMMETRIC_KEY *key);
TSS_RESULT Trspi_UnloadBlob_SYM_CA_ATTESTATION(UINT16 *offset, BYTE *blob, TCPA_SYM_CA_ATTESTATION *sym);
TSS_RESULT Trspi_UnloadBlob_ASYM_CA_CONTENTS(UINT16 *offset, BYTE *blob, TCPA_ASYM_CA_CONTENTS *asym);
TSS_RESULT Trspi_UnloadBlob_IDENTITY_REQ(UINT16 *offset, BYTE *blob, TCPA_IDENTITY_REQ *req);
TSS_RESULT Trspi_UnloadBlob_IDENTITY_PROOF(UINT16 *offset, BYTE *blob, TCPA_IDENTITY_PROOF *proof);

/* Blob loading functions */
void Trspi_LoadBlob(UINT16 *offset, UINT32 size, BYTE *container, BYTE *object);
void Trspi_LoadBlob_UINT32(UINT16 *offset, UINT32 in, BYTE *blob);
void Trspi_LoadBlob_UINT16(UINT16 *offset, UINT16 in, BYTE *blob);
void Trspi_LoadBlob_BYTE(UINT16 *offset, BYTE data, BYTE *blob);
void Trspi_LoadBlob_BOOL(UINT16 *offset, TSS_BOOL data, BYTE *blob);
void Trspi_LoadBlob_RSA_KEY_PARMS(UINT16 *offset, BYTE *blob, TCPA_RSA_KEY_PARMS *parms);
void Trspi_LoadBlob_TSS_VERSION(UINT16 *offset, BYTE *blob, TSS_VERSION version);
void Trspi_LoadBlob_TCPA_VERSION(UINT16 *offset, BYTE *blob, TCPA_VERSION version);
void Trspi_LoadBlob_PCR_INFO(UINT16 *offset, BYTE *blob, TCPA_PCR_INFO *pcr);
void Trspi_LoadBlob_PCR_SELECTION(UINT16 *offset, BYTE *blob, TCPA_PCR_SELECTION *pcr);
void Trspi_LoadBlob_STORED_DATA(UINT16 *offset, BYTE *blob, TCPA_STORED_DATA *data);
void Trspi_LoadBlob_PUBKEY(UINT16 *offset, BYTE *blob, TCPA_PUBKEY *pubKey);
void Trspi_LoadBlob_KEY(UINT16 *offset, BYTE *blob, TCPA_KEY *key);
void Trspi_LoadBlob_KEY_FLAGS(UINT16 *offset, BYTE *blob, TCPA_KEY_FLAGS *flags);
void Trspi_LoadBlob_KEY_PARMS(UINT16 *offset, BYTE *blob, TCPA_KEY_PARMS *keyInfo);
void Trspi_LoadBlob_STORE_PUBKEY(UINT16 *offset, BYTE *blob, TCPA_STORE_PUBKEY *store);
void Trspi_LoadBlob_UUID(UINT16 *offset, BYTE *blob, TSS_UUID uuid);
void Trspi_LoadBlob_CERTIFY_INFO(UINT16 *offset, BYTE *blob, TCPA_CERTIFY_INFO *certify);
void Trspi_LoadBlob_STORE_ASYMKEY(UINT16 *offset, BYTE *blob, TCPA_STORE_ASYMKEY *store);
void Trspi_LoadBlob_PCR_EVENT(UINT16 *offset, BYTE *blob, TSS_PCR_EVENT *event);
void Trspi_LoadBlob_PRIVKEY_DIGEST(UINT16 *offset, BYTE *blob, TCPA_KEY *key);
void Trspi_LoadBlob_SYMMETRIC_KEY(UINT16 *offset, BYTE *blob, TCPA_SYMMETRIC_KEY *key);
void Trspi_LoadBlob_SYM_CA_ATTESTATION(UINT16 *offset, BYTE *blob, TCPA_SYM_CA_ATTESTATION *sym);
void Trspi_LoadBlob_ASYM_CA_CONTENTS(UINT16 *offset, BYTE *blob, TCPA_ASYM_CA_CONTENTS *asym);
void Trspi_LoadBlob_IDENTITY_REQ(UINT16 *offset, BYTE *blob, TCPA_IDENTITY_REQ *req);

/* Cryptographic Functions */

/* Hash @BufSize bytes at location @Buf using the algorithm @HashType.  Currently only
 * TSS_HASH_SHA1 is a suported type, so 20 bytes will be written to @Digest */
TCPA_RESULT Trspi_Hash(UINT32 HashType, UINT32 BufSize, BYTE *Buf, BYTE *Digest);

UINT32 Trspi_HMAC(UINT32 HashType, UINT32 SecretSize, BYTE*Secret, UINT32 BufSize, BYTE*Buf, BYTE*hmacOut);

/* RSA encrypt @dataToEncryptLen bytes at location @dataToEncrypt using public key
 * @publicKey of size @keysize. This data will be encrypted using OAEP padding in
 * the openssl library using the OAEP padding parameter "TCPA".  This will allow
 * data encrypted with this function to be decrypted by a TPM using non-legacy keys */
int Trspi_RSA_Encrypt(unsigned char *dataToEncrypt,
		unsigned int dataToEncryptLen,
		unsigned char *encryptedData,
		unsigned int *encryptedDataLen,
		unsigned char *publicKey,
		unsigned int keysize);

int Trspi_Verify( UINT32 HashType, BYTE *pHash, UINT32 iHashLength,
		unsigned char *pModulus, int iKeyLength,
		BYTE *pSignature, UINT32 sig_len);

int Trspi_RSA_Public_Encrypt(unsigned char *in, unsigned int inlen,
			     unsigned char *out, unsigned int *outlen,
			     unsigned char *pubkey, unsigned int pubsize,
			     unsigned int e, int padding);

#define Trspi_RSA_PKCS15_Encrypt(in,inlen,out,outlen,pubKey,pubSize) \
        Trspi_RSA_Public_Encrypt(in,inlen,out,outlen,pubKey,pubSize,65537,RSA_PKCS1_PADDING)

#define Trspi_RSA_OAEP_Encrypt(in,inlen,out,outlen,pubKey,pubSize) \
        Trspi_RSA_Public_Encrypt(in,inlen,out,outlen,pubKey,pubSize,65537, \
				 RSA_PKCS1_OAEP_PADDING)

#define Trspi_TPM_RSA_OAEP_Encrypt(in,inlen,out,outlen,pubKey,pubSize) \
        Trspi_RSA_Encrypt(in,inlen,out,outlen,pubKey,pubSize)

/* Symmetric Encryption */

TSS_RESULT Trspi_Encrypt_ECB(UINT16 alg, BYTE *key, BYTE *in, UINT32 in_len,
			     BYTE *out, UINT32 *out_len);
TSS_RESULT Trspi_Decrypt_ECB(UINT16 alg, BYTE *key, BYTE *in, UINT32 in_len,
			     BYTE *out, UINT32 *out_len);


/* String Functions */

/* Below UNICODE is in reference to the TSS type of that name, which is
 * actually UTF-16. */

/* Convert @string to a UNICODE string. On error, NULL is returned. If len
 * is non-NULL, *len will be set to the size of the returned buffer. */
BYTE *Trspi_Native_To_UNICODE(BYTE *string, unsigned *len);

/* convert UNICODE @string to a string from the current codeset. If there is
 * If len is non-NULL, *len will be set to the size of the returned buffer. */
BYTE *Trspi_UNICODE_To_Native(BYTE *string, unsigned *len);

/* Error Functions */

/* return a human readable string based on the result */
char *Trspi_Error_String(TSS_RESULT);

/* return a human readable error layer ( "tpm", "tddl", etc...) */
char *Trspi_Error_Layer(TSS_RESULT);

/* return just the error code bits of the result */
TSS_RESULT Trspi_Error_Code(TSS_RESULT);

/* masks */
#define TSS_KEY_SIZE_MASK	0x00000F00
#define TSS_KEY_TYPE_MASK	0x000000F0
#define TSS_ENCDATA_TYPE_MASK	0x0000000F

/* These should be passed an TSS_FLAG parameter as to
 * Tspi_Context_CreateObject
 */
#define TSS_KEY_SIZE(x)		(x & TSS_KEY_SIZE_MASK)
#define TSS_KEY_TYPE(x)		(x & TSS_KEY_TYPE_MASK)
#define TSS_ENCDATA_TYPE(x)	(x & TSS_ENCDATA_TYPE_MASK)

#endif
