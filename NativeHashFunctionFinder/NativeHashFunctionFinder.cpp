/**
 * @file NativeHashFunctionFinder.cpp
 * @brief Locates Address of Native Function in Process Memory
 * Tested with GTA5 757.4
 * Based on a concept by Bucho
 * @author sfinktah
 * @version 0.0.3
 * @date 2016-07-04
 */
/* Copyright (c) 2016 - Sfinktah Bungholio LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#pragma warning(disable:4996)
#include <algorithm>
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <TlHelp32.h>
#include <vector>
#include <stdlib.h>
#include <string>
#include <thread>
#include <tchar.h>
#include <strsafe.h>
#include <time.h>
#include <SDKDDKVer.h>
#define SUPPORT_64BIT_OFFSET
#include "../../../Libraries/distorm/examples/win32/dis64.h"
#include "natives.h"
#pragma comment(lib, "../../../Libraries/distorm/distorm.lib")
// Useful references: http://atom0s.com/forums/viewtopic.php?f=5&t=4&sid=4c99acd92ec8836e72d6740c9dad02ca

HANDLE hProcess;

// Retrieve the system error message for the last-error code
void ErrorExit(LPTSTR lpszFunction)
{
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	// Display the error message and exit the process

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
		(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("\n%s failed with error %d: %s"),
		lpszFunction, dw, lpMsgBuf);
	_tprintf(TEXT("%s\n"), (LPCTSTR)lpDisplayBuf);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
	ExitProcess(dw);
}


DWORD GetProcessByName(WCHAR* name)
{
	DWORD pid = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 process;
	ZeroMemory(&process, sizeof(process));
	process.dwSize = sizeof(process);

	if (Process32First(snapshot, &process))
	{
		do
		{
			if (wcsstr(process.szExeFile, name) != NULL)
			{
				pid = process.th32ProcessID;
				break;
			}
		} while (Process32Next(snapshot, &process));
	}

	CloseHandle(snapshot);

	if (pid != 0)
	{
		return pid;
	}

	return NULL;
}


void* ReadMemory(LPCVOID lpBaseAddress, SIZE_T bufLen = 1024) {
	static unsigned char* buf = new unsigned char[bufLen];
	SIZE_T nRead = 0;
	// SIZE_T bufLen = 0x32;
	if (!ReadProcessMemory(hProcess, lpBaseAddress, buf, bufLen, &nRead))
		ErrorExit(TEXT("ReadProcessMemory"));

	return buf;
}

// Link the library into our project.

// LPVOID RecurseJumps(LPVOID buf, DWORD64 &address)
DWORD64 RecurseJumps(unsigned char *memory, int len, _OffsetType offset)
{
	/*
	Call Instructions
	Hex	Mnemonic	Enc	LongMd	LegacyM	Description
	FF /3	CALL m16:32	B	Valid	Valid	In 64-bit mode: If selector points to a gate, then RIP = 64-bit displacement taken from gate; else RIP = zero extended 32-bit offset from far pointer referenced in the instruction.
	FF /3	CALL m16:16	B	Valid	Valid	Call far, absolute indirect address given in m16:16. In 32-bit mode: if selector points to a gate, then RIP = 32-bit zero extended displacement taken from gate; else RIP = zero extended 16-bit offset from far pointer referenced in the instruction.
	9A cp	CALL ptr16:32	A	Invalid	Valid	Call far, absolute, address given in operand.
	9A cd	CALL ptr16:16	A	Invalid	Valid	Call far, absolute, address given in operand.
	FF /2	CALL r/m64	B	Valid	N.E.	Call near, absolute indirect, address given in r/m64.
	FF /2	CALL r/m32	B	N.E.	Valid	Call near, absolute indirect, address given in r/m32.
	FF /2	CALL r/m16	B	N.E.	Valid	Call near, absolute indirect, address given in r/m16.
	E8 cd	CALL rel32	B	Valid	Valid	Call near, relative, displacement relative to next instruction. 32-bit displacement sign extended to 64-bits in 64-bit mode.
	E8 cw	CALL rel16	B	N.S.	Valid	Call near, relative, displacement relative to next instruction.
	*/
	/* Jump Instructions
	EB       cb JMP rel8     D Valid Valid Jump short, relative,          RIP = RIP + 8-bit displacement sign extended to 64-bits
	E9 XX    cw JMP rel16    D N.S.  Valid Jump near,  relative,          displacement relative to next instruction. Not supported in 64-bit mode.
	E9       cd JMP rel32    D Valid Valid Jump near,  relative,          RIP = RIP + 32-bit displacement sign extended to 64-bits
	FF XX    /4 JMP r/m16    M N.S.  Valid Jump near,  absolute indirect, address = zeroextended r/m16. Not supported in 64-bit mode.
	FF XX    /4 JMP r/m32    M N.S.  Valid Jump near,  absolute indirect, address given in r/m32. Not supported in 64-bit mode.
	FF       /4 JMP r/m64    M Valid N.E.  Jump near,  absolute indirect, RIP = 64-Bit offset from register or memory
	EA       cd JMP ptr16:16 D Inv.  Valid Jump far,   absolute,          address given in operand
	EA       cp JMP ptr16:32 D Inv.  Valid Jump far,   absolute,          address given in operand
	FF       /5 JMP m16:16   D Valid Valid Jump far,   absolute indirect, address given in m16:16
	FF       /5 JMP m16:32   D Valid Valid Jump far,   absolute indirect, address given in m16:32.
	REX.W+FF /5 JMP m16:64   D Valid N.E.  Jump far,   absolute indirect, address given in m16:64.
	*/

	/*
	Table 2-4. REX Prefix Fields [BITS: 0100WRXB]
	=============================================

	Field_Name Bit_Position Definition
	---------- ------------ ----------
	-          7:4          0100
	W          3            0 = Operand size determined by CS.D
							1 = 64 Bit Operand Size
	R          2            Extension of the ModR/M reg field
	X          1            Extension of the SIB index field
	B          0            Extension of the ModR/M r/m field, SIB base field, or Opcode reg field
	*/
	LPBYTE pByte = reinterpret_cast<LPBYTE>(memory);

	// TODO
	// 00007ff7498fcd8e (04) ff6424f8                 JMP QWORD [RSP-0x8]

	// Absolute
	if(*pByte == 0xFF && *(pByte + 1) == 0x25)
	{
		LPVOID pDest = nullptr;

		#ifdef _M_IX86
			pDest = **reinterpret_cast<LPVOID**>( pByte + 2 );
		#else
		if(*reinterpret_cast<DWORD*>(pByte + 2) != 0)
		{
			pDest = *reinterpret_cast<LPVOID**>( pByte + *reinterpret_cast<PDWORD>(pByte + 2) + 6 );
		}
		else
		{
			pDest = *reinterpret_cast<LPVOID**>( pByte + 6 );
		}
		#endif

		// return RecurseJumps(pDest);
		return 1; // TODO
	}

	
	else if(*pByte == 0x48 && *(pByte + 1) == 0xFF)
	{
		// Absolute Indirect
		return 1;
	}
	else if(*pByte == 0xE9)
	{
		LPVOID pDest = nullptr;

	#ifdef _M_IX86
		pDest = reinterpret_cast<LPVOID>( *reinterpret_cast<PDWORD>(pByte + 1) + reinterpret_cast<DWORD>(pByte) + relativeJmpSize );
	#else
		// DWORD_PTR base = (DWORD_PTR)address & 0xffffffff00000000;
		auto jmpOffset = *reinterpret_cast<long near *>(pByte + 1); 		// jump offset (JMP 0x0FF5ET00) 
		jmpOffset += 5; // 0xE9 ?? ?? ?? ??
		_OffsetType dest = offset;
		dest += jmpOffset;

		return dest;


		/*
		 * pDest = reinterpret_cast<LPVOID>( 
		 *             * reinterpret_cast<PDWORD>(pByte + 1) 		// jump offset (JMP 0x0FF5ET00) 
		 *             + reinterpret_cast<DWORD>(pByte)      		// + memory location of jmp instruction 
		 *             + 5  	// add bytes to get to RIP of next instruction (where offset is calculated from)
		 *             + (reinterpret_cast<DWORD_PTR>(GetModuleHandle(NULL)) & 0xFFFFFFFF00000000) 
		 *                     // giving us an absolute memory location for the jmp target
		 *         );
		 */
	#endif

		// return RecurseJumps(pDest);
	}

	// Relative Short (invalid for x64)
	else if(*pByte == 0xEB)
	{
		BYTE offset = *(pByte + 1);

		// Jmp forwards
		if(offset > 0x00 && offset <= 0x7F)
		{
			LPVOID pDest = reinterpret_cast<LPVOID>( pByte + 2 + offset );
			// return RecurseJumps(pDest);
		}

		// Jmp backwards
		else if(offset > 0x80 && offset <= 0xFF) // tbh none should be > FD
		{
			offset = -abs(offset);
			LPVOID pDest = reinterpret_cast<LPVOID>( pByte + 2 - offset );
			// return RecurseJumps(pDest);
		}
	}

	return 0;
}

// The number of the array of instructions the decoder function will use to return the disassembled instructions.
// Play with this value for performance...
#define MAX_INSTRUCTIONS (1000)

int dis64(unsigned char *memory, int len, _OffsetType offset)
{
	// Version of used compiled library.
	// Holds the result of the decoding.
	_DecodeResult res;
	// Decoded instruction information.
	_DecodedInst decodedInstructions[MAX_INSTRUCTIONS];
	// next is used for instruction's offset synchronization.
	// decodedInstructionsCount holds the count of filled instructions' array by the decoder.
	unsigned int decodedInstructionsCount = 0, i, next;

	// Default decoding mode is 32 bits, could be set by command line.
	_DecodeType dt = Decode64Bits;

	// Default offset for buffer is 0, could be set in command line.
	// _OffsetType offset = 0;
	char* errch = NULL;

	// Index to file name in argv.
	int param = 1;

	// Handling file.
	DWORD filesize, bytesread;

	// Buffer to disassemble.
	unsigned char *buf, *buf2;

	// Disassembler version.
	// offset = strtoul(argv[param + 1], &errch, 16);

	buf2 = buf = (unsigned char*)memory;
	filesize = bytesread = len;
	// printf("bits: %d\nfilename: %s\norigin: ", dt == Decode16Bits ? 16 : dt == Decode32Bits ? 32 : 64, "memory");
#ifdef SUPPORT_64BIT_OFFSET
	// if (dt != Decode64Bits) printf("%08I64x\n", offset);
	// else printf("%016I64x\n", offset);
#else
	printf("%08x\n", offset);
#endif
	// 00007ff7863a39f4 (05) e9e82e9a02               JMP 0x7ff788d468e1
	// 00007ff7863a39f9 (05) 488d642408               LEA RSP, [RSP + 0x8]
	// Decode the buffer at given offset (virtual address).
	while (1) {
		// If you get an unresolved external symbol linker error for the following line,
		// change the SUPPORT_64BIT_OFFSET in distorm.h.
		res = distorm_decode(offset, (const unsigned char*)buf, filesize, dt, decodedInstructions, MAX_INSTRUCTIONS, &decodedInstructionsCount);
		if (res == DECRES_INPUTERR) {
			// Null buffer? Decode type not 16/32/64?
			printf("Input error, halting!");

			return -4;
		}

		for (i = 0; i < decodedInstructionsCount; i++) {
#ifdef SUPPORT_64BIT_OFFSET
			printf("%0*I64x (%02d) %-24s %s%s%s\n", dt != Decode64Bits ? 8 : 16, decodedInstructions[i].offset, decodedInstructions[i].size, (char*)decodedInstructions[i].instructionHex.p, (char*)decodedInstructions[i].mnemonic.p, decodedInstructions[i].operands.length != 0 ? " " : "", (char*)decodedInstructions[i].operands.p);
#else
			printf("%08x (%02d) %-24s %s%s%s\n", decodedInstructions[i].offset, decodedInstructions[i].size, (char*)decodedInstructions[i].instructionHex.p, (char*)decodedInstructions[i].mnemonic.p, decodedInstructions[i].operands.length != 0 ? " " : "", (char*)decodedInstructions[i].operands.p);
#endif
			if (i > 0 && !strcmp((char*)decodedInstructions[i].mnemonic.p, "JMP")) {
				break;
			}
		}



		if (res == DECRES_SUCCESS) break; // All instructions were decoded.
		else if (decodedInstructionsCount == 0) break;

		// Synchronize:
		next = (unsigned long)(decodedInstructions[decodedInstructionsCount - 1].offset - offset);
		next += decodedInstructions[decodedInstructionsCount - 1].size;
		// Advance ptr and recalc offset.
		buf += next;
		filesize -= next;
		offset += next;
	}

	// Release buffer


	return 0;
}


// This is going to be terribly ineffecient, and just start the whole thing off from the start every time.
int getNativeFunction(__int64 hash, char* name)
{
	// hash = 0xC834A7C58DEB59B4;

	DWORD PPID = GetProcessByName(TEXT("GTA5"));
	if (!PPID) {
		// printf("Failed to GetProcessByName(GTA5)\n");
		exit(1);
	}
	else {
		printf("Found GTA5.exe, PID: %lu\n", PPID);
	}

	SYSTEM_INFO si;
	ZeroMemory(&si, sizeof(SYSTEM_INFO));
	GetSystemInfo(&si);
	hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, PPID);
	if (!hProcess) 
		ErrorExit(TEXT("OpenProcess"));
	
	printf("Scanning process for %s hash 0x%016llx\n\n", name, hash);
	auto addr_min = (__int64)si.lpMinimumApplicationAddress;
	auto addr_max = (__int64)si.lpMaximumApplicationAddress;

	auto found = 0;

	// Loop the pages of memory of the application.. 
	while (addr_min < addr_max)
	{
		MEMORY_BASIC_INFORMATION mbi = { 0 };
		if (!VirtualQueryEx(hProcess, (LPCVOID)addr_min, &mbi, sizeof(mbi)))
			ErrorExit(TEXT("VirtualQueryEx"));

		// Determine if we have access to the page.. 
		if (mbi.State == MEM_COMMIT && ((mbi.Protect & PAGE_GUARD) == 0) && ((mbi.Protect & PAGE_NOACCESS) == 0))
		{
			// 
			// Below are flags about the current region of memory. If you want to specifically scan for only 
			// certain things like if the area is writable, executable, etc. you can use these flags to prevent 
			// reading non-desired protection types. 
			// 

			auto isCopyOnWrite = ((mbi.Protect & PAGE_WRITECOPY) != 0 || (mbi.Protect & PAGE_EXECUTE_WRITECOPY) != 0);
			auto isExecutable = ((mbi.Protect & PAGE_EXECUTE) != 0 || (mbi.Protect & PAGE_EXECUTE_READ) != 0 || (mbi.Protect & PAGE_EXECUTE_READWRITE) != 0 || (mbi.Protect & PAGE_EXECUTE_WRITECOPY) != 0);
			auto isWritable = ((mbi.Protect & PAGE_READWRITE) != 0 || (mbi.Protect & PAGE_WRITECOPY) != 0 || (mbi.Protect & PAGE_EXECUTE_READWRITE) != 0 || (mbi.Protect & PAGE_EXECUTE_WRITECOPY) != 0);

			// Dump the region into a memory block.. 
			auto dump = new unsigned char[mbi.RegionSize + 1];
			memset(dump, 0x00, mbi.RegionSize + 1);

			printf("\r0x%llx: %04x", (__int64)mbi.BaseAddress, mbi.Protect); // mbi.Protect & 0x100);
			if (!ReadProcessMemory(hProcess, mbi.BaseAddress, dump, mbi.RegionSize, NULL))
				ErrorExit(TEXT("ReadProcessMemory")); // "Failed to read memory of location : %08X\n", mbi.BaseAddress);

			__int64 Address = (__int64)mbi.BaseAddress;
			for (SIZE_T x = 0; x < mbi.RegionSize - 8; x += 4, Address += 4)
			{
				if (*(__int64*)(dump + x) == hash) 
				{
					// Address == ((__int64)mbi.BaseAddress + x)
					printf_s("\nFound hash at address: 0x%12llx\n", Address);
					if (x >= 0x40) {
						__int64 result = *(__int64*)(dump + x - 0x40);
						printf_s("Pointer to Native Function is at: 0x%12llx\n", Address - 0x40);
						printf_s("Native Function Address: 0x %12llx\n", result);
						/*
						BOOL WINAPI ReadProcessMemory(
						  _In_  HANDLE  hProcess,
						  _In_  LPCVOID lpBaseAddress,
						  _Out_ LPVOID  lpBuffer,
						  _In_  SIZE_T  nSize,
						  _Out_ SIZE_T  *lpNumberOfBytesRead
						);	*/

						/*
						 * SIZE_T nRead = 0;
                         * x32;
						 * unsigned char* buf = new unsigned char[bufLen];
						 * printf("Attempting to read native function memory...\n");
						 * if (!ReadProcessMemory(hProcess, (void *)result, buf, bufLen, &nRead))
						 *     ErrorExit(TEXT("ReadProcessMemory"));
						 */

						int bufLen = 128;
						DWORD64 jmpLocation = 0;
						unsigned char *buf;
						
						while (true) {
							buf = (unsigned char *)ReadMemory((void *)result, bufLen);
							jmpLocation = RecurseJumps(buf, (int)bufLen, result);
							if (jmpLocation > 0xff) {
								dis64(buf, 5, result);
								result = jmpLocation;
								continue;
							}
							dis64(buf, (int)bufLen, result);
							break;
						}
					}
					else {
						printf("\nNative Function Address is on previous page... woops!");
					}
					found++;
				}
			}

			// Cleanup the memory dump.. 
			delete[] dump;
		}

		// Step the current address by this regions size.. 
		if (found) break;
		addr_min += mbi.RegionSize;
	}

	printf("\n\n");
	return 0;
}

int main(int argc, char **argv) {
	unsigned long dver = 0;
	dver = distorm_version();
	printf("Disassembled with diStorm version: %d.%d.%d\n\n", (dver >> 16), ((dver) >> 8) & 0xff, dver & 0xff);
	for_each(ALLNATIVES.begin(), ALLNATIVES.end(), [](nativeStruct n) { getNativeFunction(n.hash, n.name); });
}