/******************************************************************************
*
* Copyright (C) 2014 - 2019 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
*
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xsecure_rsa.c
*
* This file contains the implementation of the interface functions for RSA
* driver. Refer to the header file xsecure_sha.h for more detailed information.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.0   ba   10/13/14 Initial release
* 1.1   ba   12/11/15 Added support for NIST approved SHA-3 in 2.0 silicon
* 2.0   vns  03/15/17 Fixed compilation warning, and corrected SHA2 padding
*                     verification for silicon version other than 1.0
* 2.2   vns  07/06/17 Added doxygen tags
*       vns  17/08/17 Added APIs XSecure_RsaPublicEncrypt and
*                     XSecure_RsaPrivateDecrypt.As per functionality
*                     XSecure_RsaPublicEncrypt is same as XSecure_RsaDecrypt.
* 3.1   vns  11/04/18 Added support for 512, 576, 704, 768, 992, 1024, 1152,
*                     1408, 1536, 1984, 3072 key sizes, where previous verision
*                     has support only 2048 and 4096 key sizes.
* 3.2   vns  04/30/18 Added check for private RSA key decryption, such that only
*                     data to be decrypted should always be lesser than modulus
* 4.0 	arc  18/12/18 Fixed MISRA-C violations.
*       vns  21/12/18 Added RSA key zeroization after RSA operation
*       arc  03/06/19 Added input validations
*       vns  03/12/19 Modified as part of XilSecure code re-arch.
*       psl  03/26/19 Fixed MISRA-C violation
* 4.1   kal  05/20/19 Updated doxygen tags
* </pre>
*
* @note
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xsecure_rsa.h"
#include "xplatform_info.h"
/************************** Constant Definitions *****************************/
#ifdef XSECURE_ZYNQMP
/* PKCS padding for SHA-3 in 1.0 Silicon */
static const u8 XSecure_Silicon1_TPadSha3[] = {0x30U, 0x41U, 0x30U, 0x0DU,
			0x06U, 0x09U, 0x60U, 0x86U, 0x48U, 0x01U, 0x65U, 0x03U, 0x04U,
			0x02U, 0x02U, 0x05U, 0x00U, 0x04U, 0x30U };
#endif
/* PKCS padding for SHA-3 in 2.0 Silicon and onwards */
static const u8 XSecure_Silicon2_TPadSha3[] = {0x30U, 0x41U, 0x30U, 0x0DU,
			0x06U, 0x09U, 0x60U, 0x86U, 0x48U, 0x01U, 0x65U, 0x03U, 0x04U,
			0x02U, 0x09U, 0x05U, 0x00U, 0x04U, 0x30U };

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
 * @brief
 * This function initializes a a XSecure_Rsa structure with the default values
 * required for operating the RSA cryptographic engine.
 *
 *
 * @param	InstancePtr 	Pointer to the XSecure_Rsa instance.
 * @param	Mod		A character Pointer which contains the key
 *		Modulus of key size.
 * @param	ModExt		A Pointer to the pre-calculated exponential
 *		(R^2 Mod N) value.
 *		- NULL - if user doesn't have pre-calculated R^2 Mod N value,
 *		control will take care of this calculation internally.
 * @param	ModExpo		Pointer to the buffer which contains key
 *		exponent.
 *
 * @return	XST_SUCCESS if initialization was successful.
 *
 * @note	`Modulus`, `ModExt` and `ModExpo` are part of prtition signature
 * 		when authenticated boot image is generated by bootgen, else the
 *		all of them should be extracted from the key.
 *
 ******************************************************************************/
s32 XSecure_RsaInitialize(XSecure_Rsa *InstancePtr, u8 *Mod, u8 *ModExt,
							u8 *ModExpo)
{
	u32 Status = XST_FAILURE;

	/* Assert validates the input arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Mod != NULL);
	Xil_AssertNonvoid(ModExpo != NULL);

	Status = XSecure_RsaCfgInitialize(InstancePtr);
	if (Status != (u32)XST_SUCCESS) {
		goto END;
	}

	InstancePtr->Mod = Mod;
	InstancePtr->ModExt = ModExt;
	InstancePtr->ModExpo = ModExpo;
	InstancePtr->SizeInWords = XSECURE_RSA_4096_SIZE_WORDS;
	InstancePtr->RsaState = XSECURE_RSA_INITIALIZED;

	Status = XST_SUCCESS;
END:
	return (s32)Status;
}

/*****************************************************************************/
/**
 * @brief
 * This function verifies the RSA decrypted data provided is either matching
 *  with the provided expected hash by taking care of PKCS padding.
 *
 * @param	Signature	Pointer to the buffer which holds the decrypted
 *		RSA signature
 * @param	Hash		Pointer to the buffer which has the hash
 *		calculated on the data to be authenticated.
 * @param	HashLen		Length of Hash used.
 *		- For SHA3 it should be 48 bytes
 *		- For SHA2 it should be 32 bytes
 *
 * @return	XST_SUCCESS if decryption was successful.
 *		XST_FAILURE in case of mismatch.
 *
 ******************************************************************************/
u32 XSecure_RsaSignVerification(u8 *Signature, u8 *Hash, u32 HashLen)
{
	u8 * Tpadding = (u8 *)XNULL;
	u32 Pad;
	u8 * PadPtr = (u8 *)XNULL;
	u32 sign_index;
	u32 Status = (u32)XST_FAILURE;

	/* Assert validates the input arguments */
	Xil_AssertNonvoid(Signature != NULL);
	Xil_AssertNonvoid(Hash != NULL);
	Xil_AssertNonvoid(HashLen == XSECURE_HASH_TYPE_SHA3);

	Pad = XSECURE_FSBL_SIG_SIZE - 3U - 19U - HashLen;
	PadPtr = Signature;

#ifdef XSECURE_ZYNQMP
	/* If Silicon version is not 1.0 then use the latest NIST approved SHA-3
	 * id for padding
	 */
	if (XGetPSVersion_Info() != (u32)XPS_VERSION_1)
	{
		if(XSECURE_HASH_TYPE_SHA3 == HashLen)
		{
			Tpadding = (u8 *)XSecure_Silicon2_TPadSha3;
		}
		else
		{
			goto ENDF;
		}
	}
	else
	{
		if(XSECURE_HASH_TYPE_SHA3 == HashLen)
		{
			Tpadding = (u8 *)XSecure_Silicon1_TPadSha3;
		}
		else
		{
			goto ENDF;
		}
	}
#else
	Tpadding = (u8 *)XSecure_Silicon2_TPadSha3;
#endif

	/*
	 * Re-Create PKCS#1v1.5 Padding
	* MSB  ------------------------------------------------------------LSB
	* 0x0 || 0x1 || 0xFF(for 202 bytes) || 0x0 || T_padding || SHA384 Hash
	*/

	if (0x00U != *PadPtr)
	{
		goto ENDF;
	}
	PadPtr++;

	if (0x01U != *PadPtr)
	{
		goto ENDF;
	}
	PadPtr++;

	for (sign_index = 0U; sign_index < Pad; sign_index++)
	{
		if (0xFFU != *PadPtr)
		{
			goto ENDF;
		}
		PadPtr++;
	}

	if (0x00U != *PadPtr)
	{
		goto ENDF;
	}
	PadPtr++;

	for (sign_index = 0U; sign_index < 19U; sign_index++)
	{
		if (*PadPtr != Tpadding[sign_index])
		{
			goto ENDF;
		}
		PadPtr++;
	}

	for (sign_index = 0U; sign_index < HashLen; sign_index++)
	{
		if (*PadPtr != Hash[sign_index])
		{
			goto ENDF;
		}
		PadPtr++;
	}
	Status = (u32)XST_SUCCESS;
ENDF:
	return Status;
}

/*****************************************************************************/
/**
 * @brief
* This function handles the RSA encryption with the public key components
* provided when initializing the RSA cryptographic core with the
* XSecure_RsaInitialize function.
*
* @param	InstancePtr	Pointer to the XSecure_Rsa instance.
* @param	Input		Pointer to the buffer which contains the input
*		data to be encrypted.
* @param	Size		Key size in bytes, Input size also should be
* 		same as Key size mentioned.Inputs supported are
* 		- XSECURE_RSA_4096_KEY_SIZE
* 		- XSECURE_RSA_2048_KEY_SIZE
* 		- XSECURE_RSA_3072_KEY_SIZE
* @param	Result		Pointer to the buffer where resultant decrypted
*		data to be stored		.
*
* @return	XST_SUCCESS if encryption was successful.
*
* @note		The Size passed here needs to match the key size used in the
* 		XSecure_RsaInitialize function.
*
******************************************************************************/
s32 XSecure_RsaPublicEncrypt(XSecure_Rsa *InstancePtr, u8 *Input, u32 Size,
					u8 *Result)
{
	s32 ErrorCode;

	/* Assert validates the input arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Result != NULL);
	Xil_AssertNonvoid(Input != NULL);
	Xil_AssertNonvoid(Size != 0x00U);
	Xil_AssertNonvoid(InstancePtr->RsaState == XSECURE_RSA_INITIALIZED);

	ErrorCode = (s32)XSecure_RsaOperation(InstancePtr, Input, Result,
					XSECURE_RSA_SIGN_ENC, Size);

	return ErrorCode;

}

/*****************************************************************************/
/**
 * @brief
* This function handles the RSA decryption with the private key components
* provided when initializing the RSA cryptographic core with the
* XSecure_RsaInitialize function.
*
* @param	InstancePtr	Pointer to the XSecure_Rsa instance.
* @param	Input		Pointer to the buffer which contains the input
*		data to be decrypted.
* @param	Size		Key size in bytes, Input size also should be
* 		same as	Key size mentioned. Inputs supported are
* 		- XSECURE_RSA_4096_KEY_SIZE,
* 		- XSECURE_RSA_2048_KEY_SIZE
* 		- XSECURE_RSA_3072_KEY_SIZE
* @param	Result		Pointer to the buffer where resultant decrypted
*		data to be stored		.
*
* @return	XST_SUCCESS if decryption was successful.
*
*		XSECURE_RSA_DATA_VALUE_ERROR - if input data is
*		greater than modulus.
*		XST_FAILURE - on RSA operation failure.
*
* @note		The Size passed in needs to match the key size used in the
* 		XSecure_RsaInitialize function..
*
******************************************************************************/
s32 XSecure_RsaPrivateDecrypt(XSecure_Rsa *InstancePtr, u8 *Input, u32 Size,
				u8 *Result)
{
	s32 Status = (s32)XSECURE_RSA_DATA_VALUE_ERROR;
	u32 idx;

	/* Assert validates the input arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Result != NULL);
	Xil_AssertNonvoid(Input != NULL);
	Xil_AssertNonvoid(Size != 0x00U);
	Xil_AssertNonvoid(InstancePtr->RsaState == XSECURE_RSA_INITIALIZED);

	/*
	 * Input data should always be smaller than modulus
	 * here we are checking only MSB byte
	 */
	for (idx = 0U; idx < Size; idx++) {
		if ((*(u8 *)(InstancePtr->Mod + idx)) > (*(u8 *)(Input + idx))) {
			Status = (s32)XSecure_RsaOperation(InstancePtr, Input, Result,
						XSECURE_RSA_SIGN_DEC, Size);
			break;
		}

		if ((*(u8 *)(InstancePtr->Mod + idx)) < (*(u8 *)(Input + idx))) {
			break;
		}
	}

	return Status;
}
