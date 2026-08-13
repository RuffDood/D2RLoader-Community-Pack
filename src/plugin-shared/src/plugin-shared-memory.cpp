#include <plugin-shared.h>
#include <D2RLPlugin/logging.h>
#include <Windows.h>
#include <cstring>
#include <utility>

namespace {

struct DeferredHookOperation {
	std::string label;
	bool required{};
	std::function<bool()> action;
	std::function<bool()> rollback;
};

struct HookTransactionState {
	bool collecting{};
	std::vector<DeferredHookOperation> operations;
};

HookTransactionState Transaction;
volatile uint8_t* OperationalFlag{};

volatile uint8_t* EnsureOperationalFlag() noexcept
{
	if (OperationalFlag != nullptr) {
		return OperationalFlag;
	}
	OperationalFlag = static_cast<volatile uint8_t*>(VirtualAlloc(
		nullptr,
		1,
		MEM_COMMIT | MEM_RESERVE,
		PAGE_READWRITE));
	return OperationalFlag;
}

void SetOperational(bool operational) noexcept
{
	auto* flag = EnsureOperationalFlag();
	if (flag != nullptr) {
		InterlockedExchange8(
			reinterpret_cast<volatile char*>(flag),
			operational ? 1 : 0);
	}
}

void EmitMoveR11Immediate(uint8_t*& cursor, const void* address) noexcept
{
	*cursor++ = 0x49;
	*cursor++ = 0xBB;
	const auto value = reinterpret_cast<uint64_t>(address);
	std::memcpy(cursor, &value, sizeof(value));
	cursor += sizeof(value);
}

void EmitJumpR11(uint8_t*& cursor) noexcept
{
	*cursor++ = 0x41;
	*cursor++ = 0xFF;
	*cursor++ = 0xE3;
}

bool ProtectGuardStub(void* stub, size_t size) noexcept
{
	FlushInstructionCache(GetCurrentProcess(), stub, size);
	DWORD oldProtection{};
	return VirtualProtect(stub, size, PAGE_EXECUTE_READ, &oldProtection) != FALSE;
}

void* CreateDirectGuardStub(
	void* allocation,
	void* activeTarget,
	void* inactiveTarget) noexcept
{
	auto* flag = EnsureOperationalFlag();
	if (!allocation || !flag || !activeTarget || !inactiveTarget) {
		return nullptr;
	}

	auto* cursor = static_cast<uint8_t*>(allocation);
	EmitMoveR11Immediate(cursor, const_cast<uint8_t*>(flag));
	*cursor++ = 0x41;
	*cursor++ = 0x80;
	*cursor++ = 0x3B;
	*cursor++ = 0x00;
	*cursor++ = 0x74;
	*cursor++ = 0x0D;
	EmitMoveR11Immediate(cursor, activeTarget);
	EmitJumpR11(cursor);
	EmitMoveR11Immediate(cursor, inactiveTarget);
	EmitJumpR11(cursor);
	return allocation;
}

bool BuildCallRel32Bytes(
	const void* callSite,
	const void* target,
	uint8_t (&bytes)[5]) noexcept
{
	if (!callSite || !target) {
		return false;
	}

	const auto nextInstruction = reinterpret_cast<uintptr_t>(callSite) + 5;
	const auto targetAddress = reinterpret_cast<uintptr_t>(target);
	int64_t displacement{};
	if (targetAddress >= nextInstruction) {
		const auto distance = targetAddress - nextInstruction;
		if (distance > 0x7FFFFFFFu) {
			return false;
		}
		displacement = static_cast<int64_t>(distance);
	} else {
		const auto distance = nextInstruction - targetAddress;
		if (distance > 0x80000000u) {
			return false;
		}
		displacement = -static_cast<int64_t>(distance);
	}

	bytes[0] = 0xE8;
	const auto relative = static_cast<int32_t>(displacement);
	std::memcpy(bytes + 1, &relative, sizeof(relative));
	return true;
}

struct InlineGuardStub {
	void* entry{};
	void** originalSlot{};
};

InlineGuardStub CreateInlineGuardStub(void* activeTarget) noexcept
{
	constexpr size_t AllocationSize = 64;
	constexpr size_t OriginalSlotOffset = 48;
	auto* flag = EnsureOperationalFlag();
	auto* allocation = static_cast<uint8_t*>(VirtualAlloc(
		nullptr,
		AllocationSize,
		MEM_COMMIT | MEM_RESERVE,
		PAGE_EXECUTE_READWRITE));
	if (!allocation || !flag || !activeTarget) {
		if (allocation) {
			VirtualFree(allocation, 0, MEM_RELEASE);
		}
		return {};
	}

	auto** originalSlot = reinterpret_cast<void**>(
		allocation + OriginalSlotOffset);
	*originalSlot = nullptr;
	auto* cursor = allocation;
	EmitMoveR11Immediate(cursor, const_cast<uint8_t*>(flag));
	*cursor++ = 0x41;
	*cursor++ = 0x80;
	*cursor++ = 0x3B;
	*cursor++ = 0x00;
	*cursor++ = 0x74;
	*cursor++ = 0x0D;
	EmitMoveR11Immediate(cursor, activeTarget);
	EmitJumpR11(cursor);
	EmitMoveR11Immediate(cursor, originalSlot);
	*cursor++ = 0x4D;
	*cursor++ = 0x8B;
	*cursor++ = 0x1B;
	EmitJumpR11(cursor);
	return { allocation, originalSlot };
}

} // namespace

bool PSh_HookTransactionBegin() noexcept
{
	if (Transaction.collecting) {
		return false;
	}
	if (EnsureOperationalFlag() == nullptr) {
		return false;
	}
	SetOperational(false);
	try {
		Transaction.operations.clear();
		Transaction.operations.reserve(256);
		Transaction.collecting = true;
		return true;
	}
	catch (...) {
		Transaction.operations.clear();
		Transaction.collecting = false;
		return false;
	}
}

void PSh_HookTransactionAbort() noexcept
{
	SetOperational(false);
	Transaction.collecting = false;
	Transaction.operations.clear();
}

void PSh_HookTransactionDeactivate() noexcept
{
	SetOperational(false);
}

bool PSh_HookTransactionIsCollecting() noexcept
{
	return Transaction.collecting;
}

bool PSh_HookTransactionIsOperational() noexcept
{
	return OperationalFlag != nullptr && *OperationalFlag != 0;
}

bool PSh_HookTransactionEnqueue(
	const char* label,
	bool required,
	std::function<bool()> action,
	std::function<bool()> rollback) noexcept
{
	if (!Transaction.collecting || !label || *label == '\0' || !action) {
		return false;
	}
	try {
		Transaction.operations.push_back({
			label,
			required,
			std::move(action),
			std::move(rollback) });
		return true;
	}
	catch (...) {
		return false;
	}
}

PSh_HookTransactionCommitResult PSh_HookTransactionCommit(
	const D2RL::PluginContext* context) noexcept
{
	PSh_HookTransactionCommitResult result{};
	if (!Transaction.collecting) {
		return result;
	}

	Transaction.collecting = false;
	auto operations = std::move(Transaction.operations);
	Transaction.operations.clear();
	result.totalOperations = operations.size();
	std::vector<size_t> installedOperations;
	installedOperations.reserve(operations.size());

	const auto applyOperation = [&](size_t index) noexcept {
		const auto& operation = operations[index];
		bool installed{};
		try {
			installed = operation.action();
		}
		catch (...) {
			installed = false;
		}
		if (installed) {
			++result.completedOperations;
			installedOperations.push_back(index);
			return true;
		}
		if (!operation.required) {
			D2RL::LogWarnF(
				context,
				"PluginPack: optional deferred operation '%s' was refused.",
				operation.label.c_str());
			return true;
		}

		SetOperational(false);
		for (auto installed = installedOperations.rbegin();
			installed != installedOperations.rend(); ++installed) {
			const auto& installedOperation = operations[*installed];
			if (!installedOperation.rollback) {
				continue;
			}
			bool rolledBack{};
			try {
				rolledBack = installedOperation.rollback();
			}
			catch (...) {
				rolledBack = false;
			}
			if (rolledBack) {
				++result.rolledBackOperations;
			} else {
				++result.rollbackFailures;
				D2RL::LogErrorF(
					context,
					"PluginPack: rollback of deferred operation '%s' failed.",
					installedOperation.label.c_str());
			}
		}
		D2RL::LogErrorF(
			context,
			"PluginPack: deferred hook operation '%s' was refused after %zu/%zu operations; %zu direct writes were restored and all guarded detours remain inactive.",
			operation.label.c_str(),
			result.completedOperations,
			result.totalOperations,
			result.rolledBackOperations);
		return false;
	};

	for (size_t index = 0; index < operations.size(); ++index) {
		if (operations[index].required && !applyOperation(index)) {
			return result;
		}
	}
	for (size_t index = 0; index < operations.size(); ++index) {
		if (!operations[index].required) {
			(void)applyOperation(index);
		}
	}

	SetOperational(true);
	result.success = true;
	D2RL::LogInfoF(
		context,
		"PluginPack: deferred load committed %zu/%zu operations.",
		result.completedOperations,
		result.totalOperations);
	return result;
}

bool PSh_RestoreExecutableBytes(
	const D2RL::PluginContext* context,
	uint64_t rva,
	const void* applied,
	const void* original,
	uint32_t size) noexcept
{
	if (!context || context->exeBase == 0 || !applied || !original || size == 0) {
		return false;
	}
	auto* destination = reinterpret_cast<void*>(context->exeBase + rva);
	if (std::memcmp(destination, applied, size) != 0) {
		return false;
	}

	DWORD oldProtection{};
	if (!VirtualProtect(
			destination,
			size,
			PAGE_EXECUTE_READWRITE,
			&oldProtection)) {
		return false;
	}
	std::memcpy(destination, original, size);
	FlushInstructionCache(GetCurrentProcess(), destination, size);
	DWORD ignoredProtection{};
	return VirtualProtect(
		destination,
		size,
		oldProtection,
		&ignoredProtection) != FALSE;
}

bool PSh_InstallGuardedInlineHook(
	const D2RL::PluginContext* context,
	uint64_t rva,
	const void* expected,
	uint32_t expectedSize,
	void* target,
	void** original) noexcept
{
	if (!context || !target || EnsureOperationalFlag() == nullptr) {
		return false;
	}
	auto stub = CreateInlineGuardStub(target);
	if (!stub.entry || !stub.originalSlot) {
		return false;
	}
	if (!context->InstallInlineHook(
			rva,
			expected,
			expectedSize,
			stub.entry,
			stub.originalSlot)) {
		VirtualFree(stub.entry, 0, MEM_RELEASE);
		return false;
	}
	if (*stub.originalSlot == nullptr) {
		D2RL::LogErrorF(
			context,
			"PluginPack: D2RLoader installed a guarded inline hook without returning its trampoline at RVA 0x%llX.",
			static_cast<unsigned long long>(rva));
		return false;
	}
	if (original) {
		*original = *stub.originalSlot;
	}
	if (!ProtectGuardStub(stub.entry, 64)) {
		D2RL::LogWarnF(
			context,
			"PluginPack: guarded inline relay at RVA 0x%llX remains executable-writable.",
			static_cast<unsigned long long>(rva));
	}
	return true;
}

extern "C" void* PSh_AllocNear(void* hint, size_t size) noexcept
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	uintptr_t gran = si.dwAllocationGranularity; // typically 65536
	uintptr_t base = reinterpret_cast<uintptr_t>(hint) & ~(gran - 1);

	for (uintptr_t delta = gran; delta < 0x70000000u; delta += gran)
	{
		void* p = VirtualAlloc(reinterpret_cast<void*>(base + delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (p) return p;

		if (base > delta)
		{
			p = VirtualAlloc(reinterpret_cast<void*>(base - delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (p) return p;
		}
	}
	return nullptr;
}

static bool PSh_ManifestPatchCallSiteImmediate(
	const D2RL::PluginContext* context,
	uint64_t callOffset,
	const void* expected,
	uint32_t expectedSize,
	void* hookFn,
	bool guarded) noexcept
{
	if (!PSh_ValidatePluginTarget(context) || hookFn == nullptr)
		return false;

	void* callSite = reinterpret_cast<void*>(context->exeBase + callOffset);

	if (expected && expectedSize > 0 && memcmp(callSite, expected, expectedSize) != 0)
	{
		D2RL::LogErrorF(context, "PSh_PatchCallSite: expected bytes mismatch at %p", callSite);
		return false;
	}

	// FF 25 00000000 <imm64> — absolute indirect jmp through the 8 bytes that follow it.
	constexpr size_t kDirectStubSize = 14;
	constexpr size_t kGuardedStubSize = 42;
	const auto stubSize = guarded ? kGuardedStubSize : kDirectStubSize;
	void* stub = PSh_AllocNear(callSite, stubSize);
	if (!stub)
	{
		D2RL::LogErrorF(context, "PSh_PatchCallSite: PSh_AllocNear failed for call site at %p: %lu",
			callSite, GetLastError());
		return false;
	}

	if (guarded) {
		if (expected == nullptr || expectedSize < 5
			|| static_cast<const uint8_t*>(expected)[0] != 0xE8) {
			VirtualFree(stub, 0, MEM_RELEASE);
			return false;
		}
		int32_t relative{};
		std::memcpy(
			&relative,
			static_cast<const uint8_t*>(expected) + 1,
			sizeof(relative));
		auto* originalTarget = reinterpret_cast<void*>(
			reinterpret_cast<uintptr_t>(callSite) + 5 + relative);
		if (!CreateDirectGuardStub(stub, hookFn, originalTarget)) {
			VirtualFree(stub, 0, MEM_RELEASE);
			return false;
		}
	} else {
		uint8_t* s = static_cast<uint8_t*>(stub);
		s[0] = 0xFF; s[1] = 0x25;
		s[2] = 0x00; s[3] = 0x00; s[4] = 0x00; s[5] = 0x00;
		*reinterpret_cast<uint64_t*>(s + 6) = reinterpret_cast<uint64_t>(hookFn);
	}

	if (!ProtectGuardStub(stub, stubSize)) {
		VirtualFree(stub, 0, MEM_RELEASE);
		return false;
	}

	// Submit the complete CALL instruction through D2RLoader so collisions are
	// still rejected consistently. PatchRel32 accepts only a target RVA relative
	// to D2R.exe; a valid near relay can live below exeBase, which cannot be
	// represented by that API. Encoding the signed displacement here supports
	// relays on either side of the executable without bypassing the loader's
	// governed patch path. The relay remains inactive until the DLL transaction
	// commits.
	uint8_t callBytes[5]{};
	if (!BuildCallRel32Bytes(callSite, stub, callBytes)
		|| !context->PatchBytes(
			callOffset,
			expected,
			expectedSize,
			callBytes,
			static_cast<uint32_t>(sizeof(callBytes))))
	{
		D2RL::LogErrorF(context, "PSh_PatchCallSite: D2RLoader rejected call site at %p", callSite);
		DWORD oldProtection{};
		VirtualProtect(stub, stubSize, PAGE_EXECUTE_READWRITE, &oldProtection);
		VirtualFree(stub, 0, MEM_RELEASE);
		return false;
	}

	// The relay intentionally remains allocated for the process lifetime. If the
	// loader does not restore a successful CALL after a later failure, the
	// inactive relay still delegates to the original target without entering the
	// plugin DLL.
	return true;
}

extern "C" bool PSh_ManifestPatchCallSite(
	const D2RL::PluginContext* context,
	const char* manifestId,
	uint64_t callOffset,
	const void* expected,
	uint32_t expectedSize,
	void* hookFn) noexcept
{
	if (!PSh_ManifestSiteIsValid(
			context,
			manifestId,
			callOffset,
			expected,
			expectedSize)
		|| hookFn == nullptr) {
		return false;
	}
	if (!PSh_HookTransactionIsCollecting()) {
		return PSh_ManifestPatchCallSiteImmediate(
			context,
			callOffset,
			expected,
			expectedSize,
			hookFn,
			false);
	}

	try {
		auto expectedCopy = PSh_CopyHookBytes(expected, expectedSize);
		return PSh_HookTransactionEnqueue(manifestId, true,
			[context, callOffset, hookFn,
				expectedCopy = std::move(expectedCopy)]() noexcept {
				return PSh_ManifestPatchCallSiteImmediate(
					context,
					callOffset,
					expectedCopy.empty() ? nullptr : expectedCopy.data(),
					static_cast<uint32_t>(expectedCopy.size()),
					hookFn,
					true);
			});
	}
	catch (...) {
		return false;
	}
}
