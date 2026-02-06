//
// Created by vastrakai on 7/7/2024.
//

#include "StackWalker.hpp"
#define _AMD64_
#include <DbgHelp.h>
#include <Psapi.h>
#include <spdlog/spdlog.h>
#include <Utils/MemUtils.hpp>
#include <Utils/ProcUtils.hpp>

StackWalker::StackWalker()
{
    std::vector<std::wstring> modulePaths = ProcUtils::getModulePaths();
    std::string modulePathStr = MemUtils::getModulePath(Solstice::mModule);
    std::string symPath;
    if (!modulePathStr.empty())
    {
        auto pos = modulePathStr.find_last_of("\\/");
        if (pos != std::string::npos) symPath = modulePathStr.substr(0, pos);
    }
    char envPath[4096] = { 0 };
    DWORD envLen = GetEnvironmentVariableA("_NT_SYMBOL_PATH", envPath, static_cast<DWORD>(sizeof(envPath)));
    if (envLen > 0)
    {
        if (!symPath.empty()) symPath += ";";
        symPath += std::string(envPath, envLen);
    }
    if (symPath.empty()) symPath = ".";

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(GetCurrentProcess(), symPath.c_str(), FALSE))
    {
        spdlog::error("Failed to initialize symbol handler");
    }
    else
    {
        LoadModuleSymbols(modulePaths);
    }
}

StackWalker::~StackWalker()
{
    // Cleanup the symbol handler
    UnloadModuleSymbols();
}

// Stored vector of loaded module paths
static std::vector<std::wstring> gModulePaths;

void StackWalker::LoadModuleSymbols(const std::vector<std::wstring>& modulePaths) {
    HMODULE mainModule = Solstice::mModule;
    std::string modulePathStr = MemUtils::getModulePath(mainModule);
    if (!modulePathStr.empty()) {
        gModulePaths.emplace_back(modulePathStr.begin(), modulePathStr.end());

        DWORD symOptions = SymGetOptions();
        symOptions |= SYMOPT_LOAD_LINES;
        symOptions |= SYMOPT_UNDNAME;
        SymSetOptions(symOptions);

        MODULEINFO info{};
        DWORD64 base = 0;
        DWORD size = 0;
        if (GetModuleInformation(GetCurrentProcess(), mainModule, &info, sizeof(info)))
        {
            base = reinterpret_cast<DWORD64>(info.lpBaseOfDll);
            size = info.SizeOfImage;
        }
        DWORD64 result = SymLoadModuleEx(GetCurrentProcess(), Solstice::mModule, modulePathStr.c_str(), nullptr, base, size,
                                         nullptr, 0);

        if (result == 0) {
            spdlog::error("Failed to load symbols for the main module.");
        } else {
            spdlog::info("Loaded symbols for Solstice.dll.");
        }
    } else {
        spdlog::error("Could not get module path for the main module.");
    }
}

void StackWalker::UnloadModuleSymbols()
{
    if (Solstice::mModule)
    {
        SymUnloadModule64(GetCurrentProcess(), reinterpret_cast<DWORD64>(Solstice::mModule));
    }
    SymCleanup(GetCurrentProcess());
}


std::vector<std::string> StackWalker::ShowCallstack(HANDLE hThread, PCONTEXT pContext) {
    auto stackTrace = std::vector<std::string>();
    // Initialize the stack frame
    STACKFRAME64 stackFrame;
    memset(&stackFrame, 0, sizeof(STACKFRAME64));
    stackFrame.AddrPC.Offset = pContext ? pContext->Rip : 0;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = pContext ? pContext->Rbp : 0;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = pContext ? pContext->Rsp : 0;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    // Initialize the context record
    CONTEXT contextRecord;
    memset(&contextRecord, 0, sizeof(CONTEXT));
    contextRecord.ContextFlags = CONTEXT_FULL;
    if (pContext)
    {
        contextRecord = *pContext;
    }
    else
    {
        RtlCaptureContext(&contextRecord);
    }

    // Initialize the symbol info
    SYMBOL_INFO* symbolInfo = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
    memset(symbolInfo, 0, sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
    symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbolInfo->MaxNameLen = MAX_SYM_NAME;

    // Initialize the line info
    IMAGEHLP_LINE64 lineInfo;
    memset(&lineInfo, 0, sizeof(IMAGEHLP_LINE64));
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    // Walk the stack
    while (StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), hThread, &stackFrame, &contextRecord, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
    {
        if (SymFromAddr(GetCurrentProcess(), stackFrame.AddrPC.Offset, nullptr, symbolInfo))
        {
            DWORD displacement = 0;
            if (SymGetLineFromAddr64(GetCurrentProcess(), stackFrame.AddrPC.Offset, &displacement, &lineInfo))
            {
                const char* fileName = lineInfo.FileName ? lineInfo.FileName : "unknown";
                spdlog::info("{} {} - {}:{}", MemUtils::getMbMemoryString(stackFrame.AddrPC.Offset), symbolInfo->Name, fileName, lineInfo.LineNumber);
                stackTrace.push_back(fmt::format("{} {} - {}:{}", MemUtils::getMbMemoryString(stackFrame.AddrPC.Offset), symbolInfo->Name, fileName, lineInfo.LineNumber));
            }
            else
            {
                spdlog::info("{} {}", MemUtils::getMbMemoryString(stackFrame.AddrPC.Offset), symbolInfo->Name);
                stackTrace.push_back(fmt::format("{} {}", MemUtils::getMbMemoryString(stackFrame.AddrPC.Offset), symbolInfo->Name));
            }
        }
        else
        {
            DWORD err = GetLastError();
            spdlog::info("{} [sym err {}]", MemUtils::getMbMemoryString(stackFrame.AddrPC.Offset), err);
            stackTrace.push_back(fmt::format("{} [sym err {}]", MemUtils::getMbMemoryString(stackFrame.AddrPC.Offset), err));
        }
    }

    // Cleanup
    free(symbolInfo);

    return stackTrace;
}
