#include "SkeletalCreature.h"

#include "Creature.h"
#include "Engine.h"
#include "common/Exception.h"
#include "common/NumericCast.h"
#include "common/creaturesImage.h"
#include "fileformats/sfc/C2CreatureData.h"

#include <algorithm>
#include <array>

void SkeletalCreature::restoreC2Body(const sfc::CreatureV2& saved,
	const std::function<std::shared_ptr<creaturesImage>(const std::string&)>& load_image) {
	if (engine.version != 2 || !saved.body || saved.direction > 3 ||
		saved.downfoot_right > 1 || saved.expression > 5 || saved.current_pose.size() != 15)
		throw Exception("Invalid saved C2 skeleton state");

	// Runtime order is body, head, left/right thighs, left/right upper arms,
	// tail root, left/right shins, left/right forearms, left/right feet, tail tip.
	const std::array<std::vector<unsigned int>, 6> chains = {{
		{1}, {2, 7, 11}, {3, 8, 12}, {4, 9}, {5, 10}, {6, 13}}};
	std::array<const sfc::BodyPartV2*, 14> parts{};
	parts[0] = saved.body.get();
	std::array<attFile, 14> attachments;
	for (unsigned int view = 0; view < 10; ++view) {
		attachments[0].noattachments[view] = 6;
		for (unsigned int point = 0; point < 6; ++point)
			for (unsigned int axis = 0; axis < 2; ++axis)
				attachments[0].attachments[view][point * 2 + axis] = saved.body->attachments[point][view][axis];
	}
	attachments[0].nolines = 10;
	for (unsigned int chain = 0; chain < chains.size(); ++chain) {
		auto limb = saved.limbs[chain];
		// Some C2 creatures have no tail; a partially saved chain is still invalid.
		if (chain == 5 && !limb) continue;
		for (const auto index : chains[chain]) {
			if (!limb)
				throw Exception("Saved C2 skeleton has a missing limb");
			parts[index] = limb.get();
			attachments[index].nolines = 10;
			for (unsigned int view = 0; view < 10; ++view) {
				attachments[index].noattachments[view] = 2;
				std::copy(limb->attachments[view].begin(), limb->attachments[view].end(), attachments[index].attachments[view].begin());
			}
			limb = limb->next;
		}
		if (limb)
			throw Exception("Saved C2 skeleton has an unexpected extra limb");
	}

	std::array<std::shared_ptr<creaturesImage>, 14> restored_images;
	std::map<std::string, std::shared_ptr<creaturesImage>> galleries;
	std::array<int, 14> relative_x{}, relative_y{};
	int min_x = 0, min_y = 0;
	for (unsigned int i = 0; i < parts.size(); ++i) {
		if (!parts[i]) continue;
		const auto& part = *parts[i];
		if (!part.gallery || part.gallery->absolute_base < 0 || part.view < 0 || part.view >= 10)
			throw Exception("Invalid saved C2 body-part gallery or view");
		auto& gallery = galleries[part.gallery->filename];
		if (!gallery)
			gallery = load_image(part.gallery->filename);
		const uint64_t start = static_cast<uint64_t>(part.gallery->absolute_base) + part.base;
		const unsigned int count = i == 1 ? 120 : 10;
		if (!gallery || start + count > gallery->numframes())
			throw Exception("Saved C2 body-part frames are missing from its composite sprite");
		auto image = std::make_shared<creaturesImage>(part.gallery->filename + ":" + std::to_string(part.base));
		image->images.assign(gallery->images.begin() + start, gallery->images.begin() + start + count);
		restored_images[i] = std::move(image);
		relative_x[i] = numeric_cast<int>(int64_t(part.x) - saved.body->x);
		relative_y[i] = numeric_cast<int>(int64_t(part.y) - saved.body->y);
		min_x = std::min(min_x, relative_x[i]);
		min_y = std::min(min_y, relative_y[i]);
	}
	const int restored_adjust_x = numeric_cast<int>(-int64_t(min_x));
	const int restored_adjust_y = numeric_cast<int>(-int64_t(min_y));
	const int restored_x = numeric_cast<int>(int64_t(saved.body->x) + min_x);
	const int restored_y = numeric_cast<int>(int64_t(saved.body->y) + min_y);
	// The rendered head can be mid-blink even when the saved eye-state flag
	// requests open eyes. Restore the visible frame, not just that request.
	const int head_offset = int(parts[1]->pose_plus_base) - parts[1]->base - parts[1]->view;
	if (head_offset < 0 || head_offset > 110 || head_offset % 10)
		throw Exception("Invalid saved C2 facial frame");

	std::vector<std::unique_ptr<creaturePoseGene>> new_poses;
	std::vector<std::unique_ptr<creatureGaitGene>> new_gaits;
	for (unsigned int i = 0; i < saved.poses.size(); ++i) {
		if (saved.poses[i].empty()) continue;
		if (saved.poses[i].size() != 15)
			throw Exception("Invalid saved C2 pose-table entry");
		auto pose_gene = std::make_unique<creaturePoseGene>(2);
		pose_gene->poseno = i;
		std::fill_n(pose_gene->pose, 16, 0);
		std::copy(saved.poses[i].begin(), saved.poses[i].end(), pose_gene->pose);
		new_poses.push_back(std::move(pose_gene));
	}
	for (unsigned int i = 0; i < saved.gait_animations.size(); ++i) {
		const auto& gait = saved.gait_animations[i];
		if (gait.empty()) continue;
		if (gait.back() != 'R' || (gait.size() - 1) % 3 || gait.size() > 25)
			throw Exception("Unsupported saved C2 gait sequence");
		auto gait_gene = std::make_unique<creatureGaitGene>(2);
		gait_gene->drive = i;
		std::fill_n(gait_gene->pose, 8, 0);
		for (unsigned int step = 0; step < (gait.size() - 1) / 3; ++step) {
			unsigned int number = 0;
			for (unsigned int digit = 0; digit < 3; ++digit) {
				const char c = gait[step * 3 + digit];
				if (c < '0' || c > '9') throw Exception("Invalid saved C2 gait pose number");
				number = number * 10 + c - '0';
			}
			if (number == 0 || number > 255) throw Exception("Unsupported saved C2 gait pose number");
			gait_gene->pose[step] = number;
		}
		new_gaits.push_back(std::move(gait_gene));
	}

	// Commit only after validating the whole record and resolving its images.
	posegenes.clear();
	gaitgenes.clear();
	restored_pose_genes = std::move(new_poses);
	restored_gait_genes = std::move(new_gaits);
	for (const auto& gene : restored_pose_genes) posegenes[gene->poseno] = gene.get();
	for (const auto& gene : restored_gait_genes) gaitgenes[gene->drive] = gene.get();
	gaitgene = nullptr;
	gaiti = 0;
	walking = approaching = false;
	for (unsigned int i = 0; i < parts.size(); ++i) {
		images[i] = std::move(restored_images[i]);
		att[i] = std::move(attachments[i]);
		pose[i] = parts[i] ? parts[i]->view : 0;
		partx[i] = relative_x[i];
		party[i] = relative_y[i];
	}
	direction = saved.direction;
	const unsigned int directions[] = {3, 2, 0, 1};
	posedirection = directions[direction];
	facialexpression = head_offset / 20;
	eyesclosed = head_offset % 20 == 10;
	pregnancy = 0; // The saved composite already contains its current body shape.
	c2_cached_body = true;
	c2_has_tail = saved.limbs[5] != nullptr;
	adjustx = restored_adjust_x;
	adjusty = restored_adjust_y;
	x = restored_x;
	y = restored_y;
	downfoot_left = saved.downfoot_right == 0;
	const unsigned int foot = downfoot_left ? 11 : 12;
	oldfootx = attachmentX(foot, 1);
	oldfooty = attachmentY(foot, 1);
	lastgoodfootx = saved.foot_x;
	lastgoodfooty = saved.foot_y;
	downfootroom.reset();
	height = oldfooty;
	width = 50; // Retain the runtime's collision width until its C2 shape is modeled.
	calculated = true;
	setZOrder(saved.z_order);
}
