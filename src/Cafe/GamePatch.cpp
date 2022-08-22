#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "CafeSystem.h"

void hleExport_breathOfTheWild_busyLoop(PPCInterpreter_t* hCPU)
{
	uint32 queue7C = memory_readU32(hCPU->gpr[24] + 0x7C);
	uint32 queue80b = memory_readU8(hCPU->gpr[24] + 0x80);

	if (!(queue80b == 0 || hCPU->gpr[22] != 0 || queue7C > 0))
	{
		PPCInterpreter_relinquishTimeslice();
	}

	hCPU->gpr[6] = hCPU->gpr[29];
	hCPU->instructionPointer += 4;
}

void hleExport_breathOfTheWild_busyLoop2(PPCInterpreter_t* hCPU)
{
	uint32 queue7C = memory_readU32(hCPU->gpr[24] + 0x7C);
	uint32 queue80b = memory_readU8(hCPU->gpr[24] + 0x80);

	if (!(queue80b == 0 || hCPU->gpr[22] != 0 || queue7C > 0))
	{
		PPCInterpreter_relinquishTimeslice();
	}

	hCPU->gpr[12] = hCPU->gpr[29];
	hCPU->instructionPointer += 4;
}

void hleExport_ffl_swapEndianFloatArray(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(valueArray, uint32, 0);
	ppcDefineParamS32(valueCount, 1);
	for (sint32 i = 0; i < valueCount; i++)
	{
		valueArray[i] = _swapEndianU32(valueArray[i]);
	}
	osLib_returnFromFunction(hCPU, 0);
}

typedef struct  
{
	std::atomic<uint32be> count;
	uint32be ownerThreadId;
	uint32 ukn08;
}xcxCS_t;

void hleExport_xcx_enterCriticalSection(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(xcxCS, xcxCS_t, 0);
	uint32 threadId = coreinitThread_getCurrentThreadMPTRDepr(hCPU);
	cemu_assert_debug(xcxCS->ukn08 != 0);
	cemu_assert_debug(threadId);
	if (xcxCS->ownerThreadId == (uint32be)threadId)
	{
		xcxCS->count.store(xcxCS->count.load() + 1);
		osLib_returnFromFunction(hCPU, 0);
		return;
	}

	// quick check
	uint32be newCount = xcxCS->count.load() + 1;
	uint32be expectedCount = 0;
	if(xcxCS->count.compare_exchange_strong(expectedCount, newCount))
	{
		xcxCS->ownerThreadId = threadId;
		osLib_returnFromFunction(hCPU, 0);
		return;
	}

	// spinloop for a bit to reduce the time we occupy the scheduler lock (via PPCCore_switchToScheduler)
	while (true)
	{
		for (sint32 i = 0; i < 50; i++)
		{
			if (xcxCS->count.compare_exchange_strong(expectedCount, newCount))
			{
				xcxCS->ownerThreadId = threadId;
				osLib_returnFromFunction(hCPU, 0);
				return;
			}
			_mm_pause();
		}
		PPCCore_switchToScheduler();
	}
	osLib_returnFromFunction(hCPU, 0);
}

bool mh3u_raceConditionWorkaround = true;

void hleExport_mh3u_raceConditionWorkaround(PPCInterpreter_t* hCPU) // new style HLE method, does not need entry in hle_load (but can only be reached via BL and not the usual HLE instruction)
{
	uint8 b = memory_readU8(hCPU->gpr[3] + 0x3E5);
	b ^= 1;
	if (mh3u_raceConditionWorkaround)
	{
		b = 0;
		mh3u_raceConditionWorkaround = false;
	}
	osLib_returnFromFunction(hCPU, b);
}

void hleExport_pmcs_yellowPaintStarCrashWorkaround(PPCInterpreter_t* hCPU)
{
	hCPU->gpr[7] = hCPU->gpr[3] * 4;
	MPTR parentLR = memory_readU32(hCPU->gpr[1] + 0x4C);
	if (hCPU->gpr[3] >= 0x00800000)
	{
		hCPU->instructionPointer = parentLR;
		hCPU->gpr[1] += 0x48;
		hCPU->gpr[3] = 0;
		return;
	}
	hCPU->instructionPointer = hCPU->spr.LR;
}

uint8 hleSignature_wwhd_0173B2A0[] = {0x8D,0x43,0x00,0x01,0x7C,0xC9,0x52,0x78,0x55,0x2C,0x15,0xBA,0x7C,0x0C,0x28,0x2E,0x54,0xC8,0xC2,0x3E,0x7D,0x06,0x02,0x78,0x42,0x00,0xFF,0xE8,0x7C,0xC3,0x30,0xF8};

void hle_scan(uint8* data, sint32 dataLength, char* hleFunctionName)
{
	sint32 functionIndex = osLib_getFunctionIndex("hle", hleFunctionName);
	if( functionIndex < 0 )
	{
		debug_printf("HLE function unknown\n");
		return;
	}

	uint8* scanStart = memory_getPointerFromVirtualOffset(0x01000000);
	uint8* scanEnd = scanStart + 0x0F000000 - dataLength;
	uint8* scanCurrent = scanStart;
	while( scanCurrent < scanEnd )
	{
		if( memcmp(scanCurrent, data, dataLength) == 0 )
		{
			uint32 offset = (uint32)(scanCurrent - scanStart) + 0x01000000;
			debug_printf("HLE signature for '%s' found at 0x%08x\n", hleFunctionName, offset);
			uint32 opcode = (1<<26)|(functionIndex+0x1000); // opcode for HLE: 0x1000 + FunctionIndex
			memory_writeU32Direct(offset, opcode);
			break;
		}
		scanCurrent += 4;
	}
}

MPTR hle_locate(uint8* data, sint32 dataLength)
{
	uint8* scanStart = memory_getPointerFromVirtualOffset(0x01000000);
	uint8* scanEnd = scanStart + 0x0F000000 - dataLength;
	uint8* scanCurrent = scanStart;
	while( scanCurrent < scanEnd )
	{
		if( memcmp(scanCurrent, data, dataLength) == 0 )
		{
			return memory_getVirtualOffsetFromPointer(scanCurrent);
		}
		scanCurrent += 4;
	}
	return MPTR_NULL;
}

bool compareMasked(uint8* mem, uint8* compare, uint8* mask, sint32 length)
{
	while( length )
	{
		uint8 m = *mask;
		if( (*mem&m) != (*compare&m) )
			return false;
		mem++;
		compare++;
		mask++;
		length--;
	}
	return true;
}

MPTR hle_locate(uint8* data, uint8* mask, sint32 dataLength)
{
	uint8* scanStart = memory_getPointerFromVirtualOffset(MEMORY_CODEAREA_ADDR);
	uint8* scanEnd = memory_getPointerFromVirtualOffset(RPLLoader_GetMaxCodeOffset() - dataLength);
	uint8* scanCurrent = scanStart;
	if( mask )
	{
		if (dataLength >= 4 && *(uint32*)mask == 0xFFFFFFFF)
		{
			// fast path
			uint32 firstDword = *(uint32*)data;
			while (scanCurrent < scanEnd)
			{
				if (*(uint32*)scanCurrent == firstDword && compareMasked(scanCurrent, data, mask, dataLength))
				{
					return memory_getVirtualOffsetFromPointer(scanCurrent);
				}
				scanCurrent += 4;
			}
		}
		else
		{
#ifndef PUBLIC_RELEASE
			if (mask[0] != 0xFF)
				assert_dbg();
#endif
			uint8 firstByte = data[0];
			while (scanCurrent < scanEnd)
			{
				if (scanCurrent[0] == firstByte && compareMasked(scanCurrent, data, mask, dataLength))
				{
					return memory_getVirtualOffsetFromPointer(scanCurrent);
				}
				scanCurrent += 4;
			}
		}
	}
	else
	{
		while( scanCurrent < scanEnd )
		{
			if( memcmp(scanCurrent, data, dataLength) == 0 )
			{
				return memory_getVirtualOffsetFromPointer(scanCurrent);
			}
			scanCurrent += 4;
		}
	}
	return MPTR_NULL;
}

uint8 xcx_gpuHangDetection_degradeFramebuffer[] = {0x3B,0x39,0x00,0x01,0x28,0x19,0x4E,0x20,0x40,0x81,0x00,0x44};

uint8 xcx_framebufferReductionSignature[] = {0x80,0xC9,0x00,0x1C,0x38,0xA0,0x00,0x01,0x80,0x7E,0x00,0x80,0x80,0x9E,0x02,0xEC,0x80,0xE9,0x00,0x20,0x48,0x06,0x1E,0xD1,0x7E,0x73,0x1B,0x78};
uint8 xcx_framebufferReductionMask[] =      {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF};

uint8 botw_busyLoopSignature[] = {0x80,0xE6,0x00,0x00,0x2C,0x07,0x00,0x01,0x41,0x82,0xFF,0xF8,0x7D,0x00,0x30,0x28,0x2C,0x08,0x00,0x00,0x40,0x82,0xFF,0xF8,0x7C,0x00,0x30,0x6C,0x39,0x00,0x00,0x01,0x7D,0x00,0x31,0x2D,0x40,0x82,0xFF,0xE8 };
uint8 botw_busyLoopMask[] =      {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

uint8 botw_busyLoopSignature2[] = {0x80,0x0C,0x00,0x00,0x2C,0x00,0x00,0x01,0x41,0x82,0xFF,0xF8,0x7C,0xA0,0x60,0x28,0x2C,0x05,0x00,0x00,0x40,0x82,0xFF,0xF8,0x7C,0x00,0x60,0x6C,0x38,0x80,0x00,0x01,0x7C,0x80,0x61,0x2D,0x40,0x82,0xFF,0xE8};
uint8 botw_busyLoopMask2[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

uint8 botw_crashFuncSignature[] = { 0x94,0x21,0xFF,0xD8,0x7C,0x08,0x02,0xA6,0xBF,0x41,0x00,0x10,0x7C,0xC7,0x33,0x78,0x7C,0xBE,0x2B,0x78,0x90,0x01,0x00,0x2C,0x7C,0x9D,0x23,0x78,0x38,0x00,0x00,0x00,0x7F,0xC6,0xF3,0x78,0x38,0x81,0x00,0x0C,0x90,0x01,0x00,0x0C,0x38,0xA1,0x00,0x08 };
uint8 botw_crashFuncMask[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

uint8 ffl_floatArrayEndianSwap[] = { 0x7C,0x08,0x02,0xA6,0x94,0x21,0xFF,0xE8,0x93,0xC1,0x00,0x10,0x7C,0x7E,0x1B,0x78,0x93,0xE1,0x00,0x14,0x93,0x81,0x00,0x08,0x7C,0x9F,0x23,0x78,0x93,0xA1,0x00,0x0C,0x90,0x01,0x00,0x1C,0x3B,0xA0,0x00,0x00,0x7C,0x1D,0xF8,0x40,0x40,0x80,0x00,0x20,0x57,0xBC,0x10,0x3A,0x7C,0x3E,0xE4,0x2E };

uint8 xcx_enterCriticalSectionSignature[] = { 0x94,0x21,0xFF,0xE0,0xBF,0x41,0x00,0x08,0x7C,0x08,0x02,0xA6,0x90,0x01,0x00,0x24,0x7C,0x7E,0x1B,0x78,0x80,0x1E,0x00,0x08,0x2C,0x00,0x00,0x00,0x41,0x82,0x00,0xC0,0x48,0x01,0xD7,0xA1,0x7C,0x7A,0x1B,0x79,0x41,0x82,0x00,0xB4,0x81,0x3E,0x00,0x04,0x7C,0x09,0xD0,0x40,0x40,0x82,0x00,0x2C,0x7D,0x20,0xF0,0x28,0x7C,0x00,0xF0,0x6C };
uint8 xcx_enterCriticalSectionMask[] =      { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

uint8 smash4_softlockFixV0Signature[] = { 0x2C,0x03,0x00,0x00,0x41,0x82,0x00,0x20,0x38,0x60,0x00,0x0A,0x48,0x33,0xB8,0xAD,0x7F,0xA3,0xEB,0x78,0x7F,0xC4,0xF3,0x78,0x4B,0xFF,0xFF,0x09,0x2C,0x03,0x00,0x00 };
uint8 smash4_softlockFixV0Mask[] =      { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF };

uint8 mh3u_raceConditionWorkaroundV0Signature[] = { 0x38,0x21,0x00,0x40,0x38,0x60,0x00,0x00,0x4E,0x80,0x00,0x20,0x80,0x7B,0xDB,0x9C,0x48,0x11,0x6B,0x81,0x2C,0x03,0x00,0x00 };
uint8 mh3u_raceConditionWorkaroundV0Mask[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF };

uint8 pmcs_yellowPaintStarCrashV0Signature[] = { 0x94,0x21,0xFF,0xB8,0xBE,0x61,0x00,0x14,0x7C,0x08,0x02,0xA6,0x7C,0xB3,0x2B,0x78,0x90,0x01,0x00,0x4C,0x7C,0x9D,0x23,0x78,0x83,0x3D,0x00,0x0C,0x81,0x39,0x04,0xA8,0x54,0x67,0x10,0x3A,0x7F,0xC7,0x48,0x2E,0x83,0x1E,0x00,0xDC,0x82,0xF8,0x00,0x08,0x2C,0x17,0x00,0x00 };
//uint8 mh3u_raceConditionWorkaroundV0Mask[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF };

uint8 bayo2_audioQueueFixSignature[] = { 0x80,0x03,0x00,0x3C,0x81,0x43,0x00,0x5C,0x81,0x83,0x00,0x40,0x55,0x48,0xB2,0xBE,0x3D,0x40,0x10,0x1D,0x7D,0x6C,0x42,0x14,0x39,0x4A,0x46,0xF0,0x7D,0x8B,0x00,0x50 };
uint8 bayo2_audioQueueFixMask[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0xFF };

uint8 sm3dw_dynFrameBufferResScale[] = { 0x94,0x21,0xFF,0xB8,0xBF,0x21,0x00,0x2C,0x7C,0x08,0x02,0xA6,0x90,0x01,0x00,0x4C,0x7C,0x7E,0x1B,0x78,0x81,0x7E,0x07,0xD8,0x38,0x80,0x00,0x02,0x38,0x6B,0x00,0x03 };

uint8 tww_waitFunc[] = { 0x7C,0x08,0x02,0xA6,0x94,0x21,0xFF,0xF0,0x93,0xE1,0x00,0x0C,0x7C,0x7F,0x1B,0x78,0x90,0x01,0x00,0x14,0x80,0x7F,0x02,0xE0,0x81,0x83,0x00,0x0C,0x80,0x0C,0x00,0x1C,0x7C,0x09,0x03,0xA6,0x38,0xA0,0x00,0x00,0x38,0x9F,0x03,0x68 };

static_assert(sizeof(xcx_enterCriticalSectionSignature) == sizeof(xcx_enterCriticalSectionMask), "xcx_enterCriticalSection signature and size mismatch");
static_assert(sizeof(bayo2_audioQueueFixSignature) == sizeof(bayo2_audioQueueFixMask), "bayo2_audioQueueFix signature and size mismatch");

uint8 cars3_avro_schema_incref[] = { 0x2C,0x03,0x00,0x00,0x94,0x21,0xFF,0xE8,0x41,0x82,0x00,0x40,0x39,0x03,0x00,0x08,0x39,0x41,0x00,0x08,0x91,0x01,0x00,0x08,0x7D,0x80,0x50,0x28,0x2C,0x0C,0xFF,0xFF,0x41,0x82,0x00,0x28,0x39,0x21,0x00,0x0C,0x38,0x0C,0x00,0x01,0x38,0xE0,0x00,0x01,0x91,0x01,0x00,0x0C,0x7C,0x00,0x49,0x2D };


sint32 hleIndex_h000000001 = -1;
sint32 hleIndex_h000000002 = -1;
sint32 hleIndex_h000000003 = -1;
sint32 hleIndex_h000000004 = -1;

/*
 * Returns true for all HLE functions that do not jump to LR
 * Used by recompiler to determine function code flow
 */
bool GamePatch_IsNonReturnFunction(uint32 hleIndex)
{
	if (hleIndex == hleIndex_h000000001)
		return true;
	if (hleIndex == hleIndex_h000000002)
		return true;
	if (hleIndex == hleIndex_h000000003)
		return false;
	if (hleIndex == hleIndex_h000000004)
		return false;
	return false;
}

void GamePatch_scan()
{
	MPTR hleAddr;
	uint32 hleInstallStart = GetTickCount();

	hleAddr = hle_locate(xcx_gpuHangDetection_degradeFramebuffer, NULL, sizeof(xcx_gpuHangDetection_degradeFramebuffer));
	if( hleAddr )
	{
#ifndef PUBLIC_RELEASE
		forceLog_printf("HLE: XCX GPU hang detection");
#endif
		// remove the ADDI r25, r25, 1 instruction
		memory_writeU32(hleAddr, memory_readU32(hleAddr+4));
	}

	hleAddr = hle_locate(xcx_framebufferReductionSignature, xcx_framebufferReductionMask, sizeof(xcx_framebufferReductionSignature));
	if( hleAddr )
	{
#ifndef PUBLIC_RELEASE
		forceLog_printf("HLE: Prevent XCX rendertarget reduction");
#endif
		uint32 bl = memory_readU32(hleAddr+0x14);
		uint32 func_isReductionBuffer = hleAddr + 0x14 + (bl&0x3FFFFFC);

		// patch isReductionBuffer
		memory_writeU32(func_isReductionBuffer, 0x38600000); // LI R3, 0
		memory_writeU32(func_isReductionBuffer+4, 0x4E800020); // BLR

	}

	hleIndex_h000000001 = osLib_getFunctionIndex("hle", "h000000001");
	hleAddr = hle_locate(botw_busyLoopSignature, botw_busyLoopMask, sizeof(botw_busyLoopSignature));
	if (hleAddr)
	{
#ifndef PUBLIC_RELEASE
		forceLog_printf("HLE: Patch BotW busy loop 1 at 0x%08x", hleAddr);
#endif
		sint32 functionIndex = hleIndex_h000000001;
		uint32 opcode = (1 << 26) | (functionIndex); // opcode for HLE: 0x1000 + FunctionIndex
		memory_writeU32Direct(hleAddr - 4, opcode);
	}
	hleIndex_h000000002 = osLib_getFunctionIndex("hle", "h000000002");
	hleAddr = hle_locate(botw_busyLoopSignature2, botw_busyLoopMask2, sizeof(botw_busyLoopSignature2));
	if (hleAddr)
	{
#ifndef PUBLIC_RELEASE
		forceLog_printf("HLE: Patch BotW busy loop 2 at 0x%08x", hleAddr);
#endif
		sint32 functionIndex = hleIndex_h000000002;
		uint32 opcode = (1 << 26) | (functionIndex); // opcode for HLE: 0x1000 + FunctionIndex
		memory_writeU32Direct(hleAddr - 4, opcode);
	}

	// FFL library float array endian conversion
	// original function needs invalid float values to remain intact between LFSX -> STFSX, which is not supported in recompiler mode
	hleIndex_h000000003 = osLib_getFunctionIndex("hle", "h000000003");
	hleAddr = hle_locate(ffl_floatArrayEndianSwap, NULL, sizeof(ffl_floatArrayEndianSwap));
	if (hleAddr)
	{
		forceLogDebug_printf("HLE: Hook FFL float array endian swap function at 0x%08x", hleAddr);
		sint32 functionIndex = hleIndex_h000000003;
		uint32 opcode = (1 << 26) | (functionIndex); // opcode for HLE: 0x1000 + FunctionIndex
		memory_writeU32Direct(hleAddr, opcode);
	}

	// XCX freeze workaround
	//hleAddr = hle_locate(xcx_enterCriticalSectionSignature, xcx_enterCriticalSectionMask, sizeof(xcx_enterCriticalSectionSignature));
	//if (hleAddr)
	//{
	//	forceLogDebug_printf("HLE: Hook XCX enterCriticalSection function at 0x%08x", hleAddr);
	//	hleIndex_h000000004 = osLib_getFunctionIndex("hle", "h000000004");
	//	sint32 functionIndex = hleIndex_h000000004;
	//	uint32 opcode = (1 << 26) | (functionIndex); // opcode for HLE: 0x1000 + FunctionIndex
	//	memory_writeU32Direct(hleAddr, opcode);
	//}

	// MH3U race condition (tested for EU+US 1.2)
	hleAddr = hle_locate(mh3u_raceConditionWorkaroundV0Signature, mh3u_raceConditionWorkaroundV0Mask, sizeof(mh3u_raceConditionWorkaroundV0Mask));
	if (hleAddr)
	{
		uint32 patchAddr = hleAddr + 0x10;
		forceLog_printf("HLE: Patch MH3U race condition candidate at 0x%08x", patchAddr);
		uint32 funcAddr = PPCInterpreter_makeCallableExportDepr(hleExport_mh3u_raceConditionWorkaround);
		// set absolute jump
		uint32 opc = 0x48000000;
		opc |= PPC_OPC_LK;
		opc |= PPC_OPC_AA;
		opc |= funcAddr;
		memory_writeU32(patchAddr, opc);
	}

	// Super Smash Bros softlock fix
	// fixes random softlocks that can occur after matches
	hleAddr = hle_locate(smash4_softlockFixV0Signature, smash4_softlockFixV0Mask, sizeof(smash4_softlockFixV0Signature));
	if (hleAddr)
	{
		forceLogDebug_printf("Smash softlock fix: 0x%08x", hleAddr);
		memory_writeU32(hleAddr+0x20, memory_readU32(hleAddr+0x1C));
	}

	// Color Splash Yellow paint star crash workaround
	// fixes the crash at the beginning of the dream sequence cutscene after collecting the yellow paint star
	hleAddr = hle_locate(pmcs_yellowPaintStarCrashV0Signature, nullptr, sizeof(pmcs_yellowPaintStarCrashV0Signature));
	if (hleAddr)
	{
		forceLogDebug_printf("Color Splash crash fix: 0x%08x", hleAddr);
		uint32 funcAddr = PPCInterpreter_makeCallableExportDepr(hleExport_pmcs_yellowPaintStarCrashWorkaround);
		// set absolute jump
		uint32 opc = 0x48000000;
		opc |= PPC_OPC_LK;
		opc |= PPC_OPC_AA;
		opc |= funcAddr;
		memory_writeU32(hleAddr+0x20, opc);
	}

	// Bayonetta 2 sound queue patch (fixes audio starting to loop infinitely when there is stutter)
	hleAddr = hle_locate(bayo2_audioQueueFixSignature, bayo2_audioQueueFixMask, sizeof(bayo2_audioQueueFixSignature));
	if (hleAddr)
	{
		// replace CMPL with CMP
		forceLog_printf("Patching Bayonetta 2 audio bug at: 0x%08x", hleAddr+0x34);
		uint32 opc = memory_readU32(hleAddr + 0x34);
		opc &= ~(0x3FF << 1); // turn CMPL to CMP
		memory_writeU32(hleAddr + 0x34, opc);
	}

	if (CafeSystem::GetRPXHashUpdated() == 0xb1c033dd) // Wind Waker US
	{
		uint32 p = memory_readU32(0x02813878);
		if (p == 0x40800018)
		{
			debug_printf("HLE: TWW US dsp kill channel patch\n");
			uint32 li = 0x18;
			uint32 opcode = (li & 0x3FFFFFC) | (18 << 26); // replace BGE with B instruction
			memory_writeU32(0x02813878, opcode);
		}
	}
	else if (CafeSystem::GetRPXHashUpdated() == 0xCDC68ACD) // Wind Waker EU
	{
		uint32 p = memory_readU32(0x2814138);
		if (p == 0x40800018)
		{
			debug_printf("HLE: TWW EU dsp kill channel patch\n");
			uint32 li = 0x18;
			uint32 opcode = (li & 0x3FFFFFC) | (18 << 26); // replace BGE with B instruction
			memory_writeU32(0x02814138, opcode);
		}
	}

	// disable SM3DW dynamic resolution scaling (fixes level 1-5 spamming lots of texture creates when gradually resizing framebuffer)
	hleAddr = hle_locate(sm3dw_dynFrameBufferResScale, nullptr, sizeof(sm3dw_dynFrameBufferResScale));
	if (hleAddr)
	{
		forceLog_printf("Patching SM3DW dynamic resolution scaling at: 0x%08x", hleAddr);
		memory_writeU32(hleAddr, 0x4E800020); // BLR
	}

	// remove unnecessary lock from a wait function in TWW
	// this resolves a deadlock in singlecore mode
	hleAddr = hle_locate(tww_waitFunc, nullptr, sizeof(tww_waitFunc));
	if (hleAddr)
	{
		forceLog_printf("Patching TWW race conditon at: 0x%08x", hleAddr);
		// NOP calls to Lock/Unlock mutex
		memory_writeU32(hleAddr + 0x34, 0x60000000);
		memory_writeU32(hleAddr + 0x48, 0x60000000);
		memory_writeU32(hleAddr + 0x50, 0x60000000);
		memory_writeU32(hleAddr + 0x64, 0x60000000);
	}

	uint32 hleInstallEnd = GetTickCount();
	forceLog_printf("HLE scan time: %dms", hleInstallEnd-hleInstallStart);
}

RunAtCemuBoot _loadGamePatchAPI([]()
	{
		osLib_addFunction("hle", "h000000001", hleExport_breathOfTheWild_busyLoop);
		osLib_addFunction("hle", "h000000002", hleExport_breathOfTheWild_busyLoop2);
		osLib_addFunction("hle", "h000000003", hleExport_ffl_swapEndianFloatArray);
		osLib_addFunction("hle", "h000000004", hleExport_xcx_enterCriticalSection);
	});
