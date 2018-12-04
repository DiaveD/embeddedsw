/******************************************************************************
*
* Copyright (C) 2014 - 17 Xilinx, Inc.  All rights reserved.
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
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
*******************************************************************************/
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
*                     verfication for silicon version other than 1.0
*
* </pre>
*
* @note
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xsecure_rsa.h"

/************************** Constant Definitions *****************************/
u8 HashMgf[463] = {0};
u8 Db[463];

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
extern void XSecure_Sha3Padd(XSecure_Sha3 *InstancePtr, u8 *Dst, u32 MsgLen);
void *XPmcr_MemSetbsp(void *SrcPtr, u32 Char, u32 Len);
u8 XSecure_RsaSha3Array[512];
static inline u32  XSecure_MaskGenFunc(XSecure_Sha3 *Sha3InstancePtr, u8 * Out, u32 OutLen,
				u8 *Input);
/************************** Variable Definitions *****************************/
XSecure_Vars Xsecure_Varsocm __attribute__ ((aligned(32)));
extern XCsuDma CsuDma0;
/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
 *
 * Initializes a specific Xsecure_Rsa instance so that it is ready to be used.
 *
 * @param	InstancePtr is a pointer to the XSecure_Rsa instance.
 * @param	Mod is the pointer to Modulus used for authentication
 * @param	ModExt is the pointer to precalculated R^2 Mod N value used for
 * 			authentication
 *		Pass NULL - if user doesn't have pre-calculated R^2 Mod N value,
 *		control will take care of this calculation internally.
 * @param	ModExpo is the pointer to the exponent(public key) used for
 * 			authentication
 *
 * @return	XST_SUCCESS if initialization was successful.
 *
 * @note	Modulus, ModExt and ModExpo are part of signature generated by
 * 			bootgen after authentication
 *
 ******************************************************************************/
s32 XSecure_RsaInitialize(XSecure_Rsa *InstancePtr, u8 *Mod, u8 *ModExt,
							u8 *ModExpo)
{
	/* Assert validates the input arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Mod != NULL);
	Xil_AssertNonvoid(ModExpo != NULL);

	InstancePtr->BaseAddress = XSECURE_CSU_RSA_BASE;
	InstancePtr->Mod = Mod;
	InstancePtr->ModExt = ModExt;
	InstancePtr->ModExpo = ModExpo;

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 *
 * Write Data to RSA RAM at a given offset
 *
 * @param	InstancePtr is a pointer to the XSecure_Aes instance.
 * @param	WrData is pointer to the data to be written to RSA RAM
 * @param	RamOffset is the offset for the data to be written in RSA RAM
 *
 * @return	None
 *
 * @note	None
 *
 ******************************************************************************/
static void XSecure_RsaWriteMem(XSecure_Rsa *InstancePtr, u32* WrData,
							u8 RamOffset)
{
	/* Assert validates the input arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	u32 Index = 0U;
	u32 DataOffset = 0U;
	u32 TmpIndex = 0U;
	u32 Data = 0U;

	/** Each of this loop will write 192 bits of data*/
	for (DataOffset = 0U; DataOffset < 22U; DataOffset++)
	{
		for (Index = 0U; Index < 6U; Index++)
		{
			TmpIndex = (DataOffset*6U) + Index;
			/**
			* Exponent size is only 4 bytes
			* and rest of the data needs to be 0
			*/
			if(XSECURE_CSU_RSA_RAM_EXPO == RamOffset)
			{
				if(0U == TmpIndex )
				{
					Data = Xil_Htonl(*WrData);
				}
				else
				{
					Data = 0x0U;
				}
			}
			else
			{
				if(TmpIndex >=128U)
				{
					Data = 0x0U;
				}
				else
				{
				/**
				* The RSA data in Image is in Big Endian.
				* So reverse it before putting in RSA memory,
				* becasue RSA h/w expects it in Little endian.
				*/

				Data = (u32)Xil_Htonl(WrData[(u32)127 - TmpIndex]);
				}
			}
			XSecure_WriteReg(InstancePtr->BaseAddress,
			(XSECURE_CSU_RSA_WRITE_DATA_OFFSET),
							Data);
		}
		XSecure_WriteReg(InstancePtr->BaseAddress,
				XSECURE_CSU_RSA_READ_ADDR_OFFSET,
				((RamOffset * (u8)22) + DataOffset) | ((u32)1 << 31));
	}
}

/*****************************************************************************/
/**
 *
 * Read back the resulting data from RSA RAM
 *
 * @param	InstancePtr is a pointer to the XSecure_Rsa instance.
 * @param	RdData is the pointer to location where the decrypted data will
 * 			be written
 *
 * @return	None
 *
 * @note	None
 *
 ******************************************************************************/
static void XSecure_RsaGetData(XSecure_Rsa *InstancePtr, u32 *RdData)
{
	/* Assert validates the input arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	u32 Index = 0U;
	u32 DataOffset = 0U;
	s32 TmpIndex = 0;

	/* Each of this loop will write 192 bits of data */
	for (DataOffset = 0U; DataOffset < 22U; DataOffset++)
	{
		XSecure_WriteReg(InstancePtr->BaseAddress,
				XSECURE_CSU_RSA_READ_ADDR_OFFSET,
				(XSECURE_CSU_RSA_RAM_RES_Y * 22U) + DataOffset);

		for (Index = 0U; Index < 6U; Index++)
		{

		TmpIndex = (s32)127 - ((DataOffset*6U) + Index);
				if(TmpIndex < 0)
				{
					break;
				}
		/*
		 * The Signature digest is compared in Big endian.
		 * So because RSA h/w results in Little endian,
		 * reverse it after reading it from RSA memory,
		 */
			RdData[TmpIndex] = Xil_Htonl(XSecure_ReadReg(
					InstancePtr->BaseAddress,
					XSECURE_CSU_RSA_READ_DATA_OFFSET));
		}

	}

}

/*****************************************************************************
 * Calculate the MINV value and put it into RSA core registers
 *
 * @param	InstancePtr is the pointer to XSeure_Rsa instance
 *
 * @return	None
 *
 * @note	MINV is the 32-bit value of "-M mod 2**32"
 *			where M is LSB 32 bits of the original modulus
 *
 ******************************************************************************/

static void XSecure_RsaMod32Inverse(XSecure_Rsa *InstancePtr)
{
	/* Assert validates the input arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Calculate the MINV */
	u8 Count = (u8)0;
	u32 *ModPtr = (u32 *)InstancePtr->Mod;
	u32 ModVal = Xil_Htonl(ModPtr[127]);
	u32 Inv = (u32)2 - ModVal;

	for (Count = (u8)0; Count < (u8)4; ++Count)
	{
		Inv = (Inv * (2U - ( ModVal * Inv ) ) );
	}

	Inv = -Inv;

	/* Put the value in MINV registers */
	XSecure_WriteReg(InstancePtr->BaseAddress, XSECURE_CSU_RSA_MINV_OFFSET,
						(Inv));

}

/*****************************************************************************/
/**
 *
 * Write all the RSA data used for decryption (Modulus, Exponent etc.) at
 * corresponding offsets in RSA RAM.
 *
 * @param	InstancePtr is a pointer to the XSecure_Rsa instance.
 *
 * @return	None.
 *
 * @note	None
 *
 ******************************************************************************/
static void XSecure_RsaPutData(XSecure_Rsa *InstancePtr)
{
	u8 *ModExpoVal = InstancePtr->ModExpo;
	u8 *ModVal = InstancePtr->Mod;
	u8 *ModExtVal = InstancePtr->ModExt;
	/* Assert validates the input arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	/* Initialize Modular exponentiation */
	XSecure_RsaWriteMem(InstancePtr, (u32 *)ModExpoVal,
					XSECURE_CSU_RSA_RAM_EXPO);

	/* Initialize Modular. */
	XSecure_RsaWriteMem(InstancePtr, (u32 *)ModVal,
					XSECURE_CSU_RSA_RAM_MOD);

	if (InstancePtr->ModExt != NULL) {
		/* Initialize Modular extension (R*R Mod M) */
		XSecure_RsaWriteMem(InstancePtr, (u32 *)ModExtVal,
					XSECURE_CSU_RSA_RAM_RES_Y);
	}

}

/*****************************************************************************/
/**
 *
 * This function handles the RSA decryption from end to end
 *
 * @param	InstancePtr is a pointer to the XSecure_Rsa instance.
 * @param	Result is the pointer to decrypted data generated by RSA.
 * @param	EncText is the pointer to the data(hash) to be decrypted
 *
 * @return	XST_SUCCESS if decryption was successful.
 *
 * @note	None
 *
 ******************************************************************************/
s32 XSecure_RsaPublicEncrypt(XSecure_Rsa *InstancePtr, u8 *EncText, u8 *Result)
{
	u64 TStart, TEnd;
	/* Assert validates the input arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Result != NULL);

	volatile u32 Status = 0x0U;
	s32 ErrorCode = XST_SUCCESS;


	XSecure_WriteReg(InstancePtr->BaseAddress,
				0x40U, 0x1U);
	XSecure_WriteReg(InstancePtr->BaseAddress,
					0x40U, 0x0U);
	/* Setting Key length */
	XSecure_WriteReg(InstancePtr->BaseAddress,
			XSECURE_CSU_RSA_KEY_LEN, 0x1000U);

	/* Initialize MINV values from Mod. */
	XSecure_RsaMod32Inverse(InstancePtr);

	/* Put Modulus, exponent, Mod extension in RSA RAM */
	XSecure_RsaPutData(InstancePtr);

	/* Initialize Digest */
	XSecure_RsaWriteMem(InstancePtr, (u32 *)EncText,
				XSECURE_CSU_RSA_RAM_DIGEST);




	/* CFG0*/
	XSecure_WriteReg(InstancePtr->BaseAddress,
				0x28U, 0x6BU);
	XSecure_WriteReg(InstancePtr->BaseAddress,
					0x2CU, 0x81U);
	XSecure_WriteReg(InstancePtr->BaseAddress,
					0x30U, 0x16U);
	XSecure_WriteReg(InstancePtr->BaseAddress,
					0x3CU, 0x15U);

	/* Start the RSA operation. */
	if (InstancePtr->ModExt != NULL) {
		XSecure_WriteReg(InstancePtr->BaseAddress,
			XSECURE_CSU_RSA_CONTROL_OFFSET, XSECURE_CSU_RSA_CONTROL_MASK);
	}
	else {
		XSecure_WriteReg(InstancePtr->BaseAddress,
				XSECURE_CSU_RSA_CONTROL_OFFSET,
		XSECURE_CSU_RSA_CONTROL_EXP);

	}

	/* Check and wait for status */
	do
	{
		Status = XSecure_ReadReg(InstancePtr->BaseAddress,
					XSECURE_CSU_RSA_STATUS_OFFSET);

		if(XSECURE_CSU_RSA_STATUS_ERROR ==
				((u32)Status & XSECURE_CSU_RSA_STATUS_ERROR))
		{
			Status = XST_FAILURE;
			/*LDRA_INSPECTED 13 S */
			goto END;
		}

	}while(XSECURE_CSU_RSA_STATUS_DONE !=
				((u32)Status & XSECURE_CSU_RSA_STATUS_DONE));


	/* Copy the result */
	XSecure_RsaGetData(InstancePtr, (u32 *)Result);

END:
	return ErrorCode;
}

/*****************************************************************************/
/**
 * This function converts the integer provided to secified length.
 *
 * @param	Integer is the variable in which input should be provided.
 * @param	Size holds the required size.
 * @param	Convert is a pointer in which ouput will be updated.
 *
 * @return	None.
 *
 * @note	None
 *
 ******************************************************************************/
static void XSecure_I2Osp(u32 Integer, u32 Size, u8 *Convert)
{
   if (Integer < 256U) {
				  Convert[Size - 1] = (u8)Integer;
   }
}

/*****************************************************************************/
/**
 * Mask generation function with SHA3.
 *
 * @param	Sha3InstancePtr is a pointer to the XSecure_Sha3 instance.
 * @param	Out is a pointer in which output of this function will be stored.
 * @param	Outlen should specify the required length.
 * @param	Input is pointer which holds the input data for which mask
 * 			should be calculated which should be 48 bytes length.
 *
 * @return	None.
 *
 * @note	None
 *
 ******************************************************************************/
static inline u32  XSecure_MaskGenFunc(XSecure_Sha3 *Sha3InstancePtr, u8 * Out, u32 OutLen,
				u8 *Input)
{
	u32 Counter = 0;
	u32 HashLen = 48;
	u8 Hashstore[48]= {0};
	u32 Index1 = 0;
	u32 Size = 48;
	u32 Status = XST_SUCCESS;

	/* coverity[aray_null] */
	/* coverity[remediation] */
	if(Hashstore != 0U){;}
	while (Counter <= (OutLen/HashLen)) {
	  XSecure_I2Osp(Counter, 4, Xsecure_Varsocm.Convert);
	  XSecure_Sha3Start(Sha3InstancePtr);
	  (void)XSecure_Sha3Update(Sha3InstancePtr, Input, HashLen, 0);
	  (void)XSecure_Sha3Update(Sha3InstancePtr, Xsecure_Varsocm.Convert, 4, 0);
	  /* Padding for SHA3 */
	  /* 01 and 10*1 padding */
	  Status = XSecure_Sha3FinishPad(Sha3InstancePtr, Hashstore);
	  if ((Counter + 1U) > (OutLen/HashLen)) {
			 /*By the time this if loop is true counter value is > 1
			  * OutLen is fixed here to 463, HashLen is 48*/
			  /* coverity[overflow_const] */
			 Size = (OutLen % HashLen);
	  }
	  (void)memcpy(Out + Index1, Hashstore, Size);
	  Index1 = Index1 + 48U;
	  Counter = Counter + 1U;
	  (void)XSecure_Sha3Initialize(Sha3InstancePtr, Sha3InstancePtr->CsuDmaPtr);
	  if(Status != (u32)XST_SUCCESS){;}
	     }
	return Status;

}

/*****************************************************************************/
/**
 *
 * This function encrypts the RSA signature provided and performs required
 * PSS operations to extract salt and calculates M prime hash and compares
 * with hash obtained from EM.
 *
 * @param	RsaInstancePtr is a pointer to the XSecure_Rsa instance.
 * @param	Sha3InstancePtr is a pointer to the XSecure_Sha3 instance.
 * @param	Signature is the pointer to RSA signature for data to be
 *          authenticated
 * @param	Hash should hold the data hash which needs to authenticated.
 *
 * @return	XST_SUCCESS if verification was successful.
 *
 * @note	Prior to this API, XSecure_RsaInitialize() API should be called.
 *
 ******************************************************************************/
u32 XSecure_RsaPssSignatureverification(XSecure_Rsa *RsaInstancePtr,
			XSecure_Sha3 *Sha3InstancePtr, u8 *Signature, u8 *MsgHash)
{

	u32 Status;
	u8 MPrimeHash[48] = {0};

	u32 Index;
	/* coverity[assigned_value] */
	Status = XST_FAILURE;
	XSecure_Sha3 Sha3Instance;
	u8 *DataHash = (u8 *)MsgHash;

	/* coverity[aray_null] */
	/* coverity[remediation] */
	if(MPrimeHash != 0U){;}

	(void)memset(XSecure_RsaSha3Array, 0U, XSECURE_FSBL_SIG_SIZE);
	(void)memset(&Xsecure_Varsocm, 0U, sizeof(Xsecure_Varsocm));

	/* RSA signature encryption with public key components */
	/* coverity[value_overwrite] */
	Status = (u32)XSecure_RsaPublicEncrypt(RsaInstancePtr, Signature,
						XSecure_RsaSha3Array);
	if (Status != (u32)XST_SUCCESS) {
		/*LDRA_INSPECTED 13 S */
		goto END;
	}

	/* Checks for signature encrypted message */
	if (XSecure_RsaSha3Array[511] != 0xBCU) {
		Status = XST_FAILURE;
		goto END;
	}

	/* As CSUDMA can't accept unaligned addresses */
	(void)memcpy(Xsecure_Varsocm.EmHash, XSecure_RsaSha3Array + 463, 48);

			/* Salt extraction */
	/* Generate DB from masked DB and Hash */
	(void)XSecure_MaskGenFunc(Sha3InstancePtr, HashMgf, 463, Xsecure_Varsocm.EmHash);


	/* XOR MGF output with masked DB from EM to get DB */
	for (Index = 0U; Index < 463U; Index++) {
		Db[Index] = HashMgf[Index] ^ XSecure_RsaSha3Array[Index];
	}

	/* As CSUDMA can't accept unaligned addresses */
	(void)memcpy(Xsecure_Varsocm.Salt, Db+415, 48);


	XSecure_Sha3Initialize(&Sha3Instance, &CsuDma0);
	/* Hash on M prime */
	XSecure_Sha3Start(&Sha3Instance);

	(void)XSecure_Sha3Update(&Sha3Instance, Xsecure_Varsocm.Padding1, 8, 0); /* Padding 1 */

	(void)XSecure_Sha3Update(&Sha3Instance, DataHash, 48,0); /* Message hash */

	(void)XSecure_Sha3Update(&Sha3Instance, Xsecure_Varsocm.Salt, 48, 0);  /* salt */

	(void)XSecure_Sha3FinishPad(&Sha3Instance, MPrimeHash);

	/* Compare MPrime Hash with Hash from EM */
	for (Index = 0U; Index < 48U; Index++) {
		if (MPrimeHash[Index] != XSecure_RsaSha3Array[463+Index]) {
			Status = XST_FAILURE;
				/*LDRA_INSPECTED 13 S */
				goto END;
		}
	}

	Status = XST_SUCCESS;

END:
	return Status;
}