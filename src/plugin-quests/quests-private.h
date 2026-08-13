#pragma once

#include <plugin-shared.h>
#include <plugin-shared-json.h>

enum class RewardType : uint8_t {
	Disabled,
	SamePerDifficulty,
	DifferentPerDifficulty,
};

template <typename T>
struct ItemQuestReward {
	T Reward;
	T RewardPerDifficulty[3];
};

struct QuestPluginOptions {
	RewardType DenOfEvilRewardEnabled;
	uint8_t    DenOfEvilSkillPointReward;
	RewardType IzualRewardEnabled;
	uint8_t    IzualSkillPointReward;
	RewardType BlackBookRewardEnabled;
	uint8_t    BlackBookStatPointReward;
	RewardType GoldenBirdRewardEnabled;
	uint8_t    GoldenBirdRewardStat;
	uint32_t   GoldenBirdRewardAmount;
	RewardType SkillBookRewardEnabled;
	uint8_t    SkillBookRewardStat;
	uint8_t    SkillBookRewardAmount;

	RewardType                AkaraCainRingRewardEnabled;
	ItemQuestReward<uint32_t> AkaraCainRingItem;
	ItemQuestReward<uint8_t>  AkaraCainRingQuality;
	RewardType                OrmusGidbinnRingRewardEnabled;
	ItemQuestReward<uint32_t> OrmusGidbinnRingItem;
	ItemQuestReward<uint8_t>  OrmusGidbinnRingQuality;
	RewardType                QualKehkRuneRewardEnabled;
	ItemQuestReward<uint32_t> QualKehkRuneItems[3];

	uint8_t ImbueAllowSockets;	// NOP two bytes at 0x14036b123 and 0x14036b61b


	void Load(const D2RL::PluginContext* context, const nlohmann::json& cfg);
};
