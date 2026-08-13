#include <plugin-shared.h>

#include "../../../tests/test-check.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void* InstalledGuardTarget{};
std::size_t PatchBytesCallCount{};

int __fastcall OriginalGuardTarget(int value) noexcept
{
	return value + 1;
}

int __fastcall ActiveGuardTarget(int value) noexcept
{
	return value + 10;
}

bool __cdecl CaptureInlineGuard(
	const D2RL::PluginContext*,
	const D2RL::InlineHookRegistration* hook) noexcept
{
	if (!hook || !hook->target || !hook->original) {
		return false;
	}
	InstalledGuardTarget = hook->target;
	*hook->original = reinterpret_cast<void*>(&OriginalGuardTarget);
	return true;
}

bool __cdecl CheckTestBytes(
	const D2RL::PluginContext* context,
	std::uint64_t rva,
	const void* expected,
	std::uint32_t expectedSize) noexcept
{
	if (!context || context->exeBase == 0 || !expected || expectedSize == 0) {
		return false;
	}
	return std::memcmp(
		reinterpret_cast<const void*>(context->exeBase + rva),
		expected,
		expectedSize) == 0;
}

bool __cdecl PatchTestBytes(
	const D2RL::PluginContext* context,
	std::uint64_t rva,
	const void* expected,
	std::uint32_t expectedSize,
	const void* bytes,
	std::uint32_t size) noexcept
{
	if (!CheckTestBytes(context, rva, expected, expectedSize)
		|| !bytes || size == 0) {
		return false;
	}
	std::memcpy(
		reinterpret_cast<void*>(context->exeBase + rva),
		bytes,
		size);
	++PatchBytesCallCount;
	return true;
}

} // namespace

int main()
{
	std::vector<int> calls;

	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_HookTransactionEnqueue("first", true, [&] {
		calls.push_back(1);
		return true;
	}));
	TEST_REQUIRE(PSh_HookTransactionEnqueue("second", true, [&] {
		calls.push_back(2);
		return true;
	}));
	TEST_REQUIRE(calls.empty());
	PSh_HookTransactionAbort();
	TEST_REQUIRE(calls.empty());
	TEST_REQUIRE(!PSh_HookTransactionIsOperational());

	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_HookTransactionEnqueue("first", true, [&] {
		calls.push_back(1);
		return true;
	}));
	TEST_REQUIRE(PSh_HookTransactionEnqueue("second", true, [&] {
		calls.push_back(2);
		return true;
	}));
	const auto success = PSh_HookTransactionCommit(nullptr);
	TEST_REQUIRE(success.success);
	TEST_REQUIRE(success.completedOperations == 2);
	TEST_REQUIRE(success.totalOperations == 2);
	TEST_REQUIRE((calls == std::vector<int>{ 1, 2 }));
	TEST_REQUIRE(PSh_HookTransactionIsOperational());

	calls.clear();
	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_HookTransactionEnqueue("optional", false, [&] {
		calls.push_back(1);
		return false;
	}));
	TEST_REQUIRE(PSh_HookTransactionEnqueue("required", true, [&] {
		calls.push_back(2);
		return true;
	}));
	const auto optionalFailure = PSh_HookTransactionCommit(nullptr);
	TEST_REQUIRE(optionalFailure.success);
	TEST_REQUIRE(optionalFailure.completedOperations == 1);
	TEST_REQUIRE(optionalFailure.totalOperations == 2);
	TEST_REQUIRE((calls == std::vector<int>{ 2, 1 }));
	TEST_REQUIRE(PSh_HookTransactionIsOperational());

	calls.clear();
	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_HookTransactionEnqueue("installed", true, [&] {
		calls.push_back(1);
		return true;
	}));
	TEST_REQUIRE(PSh_HookTransactionEnqueue("refused", true, [&] {
		calls.push_back(2);
		return false;
	}));
	TEST_REQUIRE(PSh_HookTransactionEnqueue("must-not-run", true, [&] {
		calls.push_back(3);
		return true;
	}));
	const auto requiredFailure = PSh_HookTransactionCommit(nullptr);
	TEST_REQUIRE(!requiredFailure.success);
	TEST_REQUIRE(requiredFailure.completedOperations == 1);
	TEST_REQUIRE(requiredFailure.totalOperations == 3);
	TEST_REQUIRE((calls == std::vector<int>{ 1, 2 }));
	TEST_REQUIRE(!PSh_HookTransactionIsOperational());

	constexpr std::size_t OperationCount = 5;
	for (std::size_t failureIndex = 0;
		failureIndex < OperationCount; ++failureIndex) {
		calls.clear();
		TEST_REQUIRE(PSh_HookTransactionBegin());
		for (std::size_t operationIndex = 0;
			operationIndex < OperationCount; ++operationIndex) {
			const auto marker = static_cast<int>(operationIndex + 1);
			TEST_REQUIRE(PSh_HookTransactionEnqueue(
				"failure-injection",
				true,
				[&, operationIndex, marker] {
					calls.push_back(marker);
					return operationIndex != failureIndex;
				},
				[&, marker] {
					calls.push_back(-marker);
					return true;
				}));
		}
		const auto injectedFailure = PSh_HookTransactionCommit(nullptr);
		TEST_REQUIRE(!injectedFailure.success);
		TEST_REQUIRE(injectedFailure.completedOperations == failureIndex);
		TEST_REQUIRE(injectedFailure.totalOperations == OperationCount);
		TEST_REQUIRE(injectedFailure.rolledBackOperations == failureIndex);
		TEST_REQUIRE(injectedFailure.rollbackFailures == 0);
		TEST_REQUIRE(!PSh_HookTransactionIsOperational());
		TEST_REQUIRE(calls.size() == (failureIndex + 1) + failureIndex);
		for (std::size_t actionIndex = 0;
			actionIndex <= failureIndex; ++actionIndex) {
			TEST_REQUIRE(calls[actionIndex] == static_cast<int>(actionIndex + 1));
		}
		for (std::size_t rollbackIndex = 0;
			rollbackIndex < failureIndex; ++rollbackIndex) {
			TEST_REQUIRE(calls[failureIndex + 1 + rollbackIndex]
				== -static_cast<int>(failureIndex - rollbackIndex));
		}
	}

	calls.clear();
	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_HookTransactionEnqueue(
		"rollback-failure",
		true,
		[&] {
			calls.push_back(1);
			return true;
		},
		[&] {
			calls.push_back(-1);
			return false;
		}));
	TEST_REQUIRE(PSh_HookTransactionEnqueue("required-failure", true, [&] {
		calls.push_back(2);
		return false;
	}));
	const auto rollbackFailure = PSh_HookTransactionCommit(nullptr);
	TEST_REQUIRE(!rollbackFailure.success);
	TEST_REQUIRE(rollbackFailure.rolledBackOperations == 0);
	TEST_REQUIRE(rollbackFailure.rollbackFailures == 1);
	TEST_REQUIRE((calls == std::vector<int>{ 1, 2, -1 }));

	auto* executableByte = static_cast<unsigned char*>(VirtualAlloc(
		nullptr,
		1,
		MEM_COMMIT | MEM_RESERVE,
		PAGE_EXECUTE_READWRITE));
	TEST_REQUIRE(executableByte != nullptr);
	*executableByte = 0xCC;
	D2RL::PluginContext restoreContext{};
	restoreContext.exeBase = reinterpret_cast<uintptr_t>(executableByte);
	const unsigned char appliedByte = 0xCC;
	const unsigned char originalByte = 0x90;
	TEST_REQUIRE(PSh_RestoreExecutableBytes(
		&restoreContext,
		0,
		&appliedByte,
		&originalByte,
		1));
	TEST_REQUIRE(*executableByte == originalByte);
	TEST_REQUIRE(!PSh_RestoreExecutableBytes(
		&restoreContext,
		0,
		&appliedByte,
		&originalByte,
		1));
	TEST_REQUIRE(VirtualFree(executableByte, 0, MEM_RELEASE) != FALSE);

	auto* transactionByte = static_cast<unsigned char*>(VirtualAlloc(
		nullptr,
		1,
		MEM_COMMIT | MEM_RESERVE,
		PAGE_EXECUTE_READWRITE));
	TEST_REQUIRE(transactionByte != nullptr);
	*transactionByte = 0x74;
	D2RL::PluginApi patchApi{};
	patchApi.apiSize = D2RL::PluginApiSize;
	patchApi.patchBytes = &PatchTestBytes;
	patchApi.checkExpectedBytes = &CheckTestBytes;
	D2RL::PluginContext patchContext{};
	patchContext.contextSize = D2RL::PluginContextSize;
	patchContext.api = &patchApi;
	patchContext.exeBase = reinterpret_cast<std::uintptr_t>(transactionByte);
	const unsigned char expectedByte = 0x74;
	const unsigned char replacementByte = 0xEB;
	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_ManifestPatchBytes(
		&patchContext,
		"test.transaction.patch",
		0,
		&expectedByte,
		1,
		&replacementByte,
		1));
	TEST_REQUIRE(*transactionByte == expectedByte);
	TEST_REQUIRE(PSh_HookTransactionEnqueue(
		"force-rollback",
		true,
		[] { return false; }));
	const auto patchedRollback = PSh_HookTransactionCommit(nullptr);
	TEST_REQUIRE(!patchedRollback.success);
	TEST_REQUIRE(patchedRollback.completedOperations == 1);
	TEST_REQUIRE(patchedRollback.rolledBackOperations == 1);
	TEST_REQUIRE(patchedRollback.rollbackFailures == 0);
	TEST_REQUIRE(*transactionByte == expectedByte);
	TEST_REQUIRE(VirtualFree(transactionByte, 0, MEM_RELEASE) != FALSE);

	D2RL::PluginApi api{};
	api.apiSize = D2RL::PluginApiSize;
	api.installInlineHook = &CaptureInlineGuard;
	D2RL::PluginContext context{};
	context.contextSize = D2RL::PluginContextSize;
	context.api = &api;
	void* reportedOriginal{};
	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_InstallGuardedInlineHook(
		&context,
		0x1234,
		nullptr,
		0,
		reinterpret_cast<void*>(&ActiveGuardTarget),
		&reportedOriginal));
	TEST_REQUIRE(reportedOriginal == reinterpret_cast<void*>(&OriginalGuardTarget));
	TEST_REQUIRE(InstalledGuardTarget != nullptr);
	const auto guardedCall = reinterpret_cast<int(__fastcall*)(int)>(
		InstalledGuardTarget);
	TEST_REQUIRE(guardedCall(5) == 6);
	const auto emptyCommit = PSh_HookTransactionCommit(nullptr);
	TEST_REQUIRE(emptyCommit.success);
	TEST_REQUIRE(guardedCall(5) == 15);
	PSh_HookTransactionDeactivate();
	TEST_REQUIRE(guardedCall(5) == 6);

	// Call-site relays must use the loader's byte-patch path rather than its
	// target-RVA API. A near allocation is allowed to land below D2R.exe, and a
	// negative relay RVA is not representable by PatchRel32.
	auto* callBuffer = static_cast<unsigned char*>(PSh_AllocNear(
		reinterpret_cast<void*>(&OriginalGuardTarget),
		16));
	TEST_REQUIRE(callBuffer != nullptr);
	callBuffer[0] = 0xE8;
	const auto originalDisplacement = static_cast<std::int64_t>(
		reinterpret_cast<std::uintptr_t>(&OriginalGuardTarget))
		- static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(callBuffer) + 5);
	TEST_REQUIRE(originalDisplacement >= INT32_MIN);
	TEST_REQUIRE(originalDisplacement <= INT32_MAX);
	const auto originalRelative = static_cast<std::int32_t>(originalDisplacement);
	std::memcpy(callBuffer + 1, &originalRelative, sizeof(originalRelative));
	callBuffer[5] = 0xC3;
	unsigned char expectedCall[5]{};
	std::memcpy(expectedCall, callBuffer, sizeof(expectedCall));

	D2RL::PluginApi callSiteApi{};
	callSiteApi.apiSize = D2RL::PluginApiSize;
	callSiteApi.patchBytes = &PatchTestBytes;
	callSiteApi.checkExpectedBytes = &CheckTestBytes;
	D2RL::PluginContext callSiteContext{};
	callSiteContext.contextSize = D2RL::PluginContextSize;
	callSiteContext.api = &callSiteApi;
	callSiteContext.exeBase = reinterpret_cast<std::uintptr_t>(callBuffer);
	const auto patchBytesCallsBeforeCommit = PatchBytesCallCount;
	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(PSh_ManifestPatchCallSite(
		&callSiteContext,
		"test.transaction.call-site",
		0,
		expectedCall,
		static_cast<std::uint32_t>(sizeof(expectedCall)),
		reinterpret_cast<void*>(&ActiveGuardTarget)));
	TEST_REQUIRE(std::memcmp(callBuffer, expectedCall, sizeof(expectedCall)) == 0);
	const auto callSiteCommit = PSh_HookTransactionCommit(&callSiteContext);
	TEST_REQUIRE(callSiteCommit.success);
	TEST_REQUIRE(callSiteCommit.completedOperations == 1);
	TEST_REQUIRE(PatchBytesCallCount == patchBytesCallsBeforeCommit + 1);
	TEST_REQUIRE(callBuffer[0] == 0xE8);
	std::int32_t guardedRelative{};
	std::memcpy(&guardedRelative, callBuffer + 1, sizeof(guardedRelative));
	auto* guardedRelay = reinterpret_cast<void*>(
		reinterpret_cast<std::uintptr_t>(callBuffer) + 5 + guardedRelative);
	MEMORY_BASIC_INFORMATION guardedRelayMemory{};
	TEST_REQUIRE(VirtualQuery(
		guardedRelay,
		&guardedRelayMemory,
		sizeof(guardedRelayMemory)) == sizeof(guardedRelayMemory));
	TEST_REQUIRE(guardedRelayMemory.State == MEM_COMMIT);
	TEST_REQUIRE(guardedRelayMemory.Protect == PAGE_EXECUTE_READ);
	PSh_HookTransactionDeactivate();
	TEST_REQUIRE(VirtualFree(callBuffer, 0, MEM_RELEASE) != FALSE);

	TEST_REQUIRE(PSh_HookTransactionBegin());
	TEST_REQUIRE(!PSh_HookTransactionBegin());
	PSh_HookTransactionAbort();
	TEST_REQUIRE(!PSh_HookTransactionIsOperational());

	return 0;
}
