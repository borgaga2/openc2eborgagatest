#include "fileformats/sfc/C2BiochemistryData.h"
#include "fileformats/sfc/C2BrainData.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

TEST(C2BiochemistryData, PreservesChemistryOrganHolesAndSharedReceptors) {
	auto owner = std::make_shared<sfc::SimpleObjectV2>();
	owner->unid = 42;
	auto receptor = std::make_shared<sfc::ReceptorV2>();
	receptor->state = {2, 0, 2, 248, 32, 0, 50, 2};
	auto brain = std::make_shared<sfc::CBrainV2>();
	brain->receptor_groups = {{receptor}};
	brain->emitters.push_back({1, 4, 1, 11, 0, 25, 8, 6});
	brain->reactions.push_back({1, 2, 3, 4, 5, 6, 7, 8, 9});
	auto biology = std::make_shared<sfc::CBiochemistryV2>();
	biology->owner = owner.get();
	biology->chemicals[90] = {143, 7};
	biology->organs[0] = brain;
	biology->organs[127] = std::make_shared<sfc::COrganV2>();
	biology->organs[127]->receptor_groups = {{receptor, receptor}};
	biology->organs[127]->state[33] = 0xa5;
	biology->stored_organ_count = 2;
	biology->reserved_after_organs = 112;
	VectorWriter encoded;
	MFCWriter writer(encoded);
	sfc::register_c2_object_classes(writer);
	sfc::register_c2_brain_class(writer);
	sfc::register_c2_biochemistry_classes(writer);
	writer(owner); writer(brain); writer(biology);

	SpanReader input(encoded.vector());
	MFCReader reader(input);
	sfc::register_c2_object_classes(reader);
	sfc::register_c2_brain_class(reader);
	sfc::register_c2_biochemistry_classes(reader);
	std::shared_ptr<sfc::SimpleObjectV2> decoded_owner;
	std::shared_ptr<sfc::CBrainV2> decoded_brain;
	std::shared_ptr<sfc::CBiochemistryV2> decoded;
	reader(decoded_owner); reader(decoded_brain); reader(decoded);
	ASSERT_TRUE(decoded);
	ASSERT_TRUE(decoded_brain);
	ASSERT_TRUE(decoded->organs[127]);
	ASSERT_EQ(decoded->organs[127]->receptor_groups.size(), 1u);
	ASSERT_EQ(decoded->organs[127]->receptor_groups[0].size(), 2u);
	ASSERT_EQ(decoded_brain->receptor_groups.size(), 1u);
	ASSERT_EQ(decoded_brain->receptor_groups[0].size(), 1u);
	ASSERT_EQ(decoded_brain->emitters.size(), 1u);
	ASSERT_EQ(decoded_brain->reactions.size(), 1u);
	EXPECT_EQ(decoded->owner, decoded_owner.get());
	EXPECT_EQ(decoded->chemicals[90].concentration, 143);
	EXPECT_EQ(decoded->organs[0], decoded_brain);
	EXPECT_FALSE(decoded->organs[1]);
	EXPECT_EQ(decoded->organs[127]->state[33], 0xa5);
	EXPECT_EQ(decoded->organs[127]->receptor_groups[0][0], decoded_brain->receptor_groups[0][0]);
	EXPECT_EQ(decoded->organs[127]->receptor_groups[0][1], decoded_brain->receptor_groups[0][0]);
	EXPECT_EQ(decoded_brain->emitters[0][7], 6);
	EXPECT_EQ(decoded_brain->reactions[0][8], 9);
	EXPECT_EQ(decoded->reserved_after_organs, 112);
	EXPECT_EQ(input.tell(), encoded.vector().size());
	VectorWriter output;
	MFCWriter roundtrip(output);
	sfc::register_c2_object_classes(roundtrip);
	sfc::register_c2_brain_class(roundtrip);
	sfc::register_c2_biochemistry_classes(roundtrip);
	roundtrip(decoded_owner); roundtrip(decoded_brain); roundtrip(decoded);
	EXPECT_EQ(output.vector(), encoded.vector());
}

TEST(C2OrganData, RejectsInconsistentReceptorCountsAndTruncatedChemistry) {
	auto organ = std::make_shared<sfc::COrganV2>();
	organ->receptor_groups = {{std::make_shared<sfc::ReceptorV2>()}};
	VectorWriter output;
	MFCWriter writer(output);
	sfc::register_c2_organ_classes(writer);
	writer(organ);
	auto bytes = output.vector();
	bytes[50] = 2; // COrgan header (12) + organ state (34) + reserved field (4)
	SpanReader inconsistent_input(bytes);
	MFCReader inconsistent_reader(inconsistent_input);
	sfc::register_c2_organ_classes(inconsistent_reader);
	std::shared_ptr<sfc::COrganV2> decoded;
	EXPECT_THROW(inconsistent_reader(decoded), Exception);
	bytes = output.vector();
	bytes.pop_back();
	SpanReader truncated_input(bytes);
	MFCReader truncated_reader(truncated_input);
	sfc::register_c2_organ_classes(truncated_reader);
	EXPECT_THROW(truncated_reader(decoded), std::exception);
}

TEST(C2BiochemistryData, DoesNotConsumeFollowingCreatureSexOrAge) {
	// Owner pointer, 256 chemical pairs, 128 null organ pointers, count, BYTE.
	std::vector<uint8_t> bytes(775, 0);
	bytes[774] = 112;
	bytes.push_back(2);
	bytes.push_back(3);
	SpanReader input(bytes);
	MFCReader reader(input);
	sfc::CBiochemistryV2 biology;
	biology.serialize(reader);
	EXPECT_EQ(input.tell(), 775u);
	EXPECT_EQ(biology.reserved_after_organs, 112);
	uint8_t sex = 0, age = 0;
	reader(sex); reader(age);
	EXPECT_EQ(sex, 2);
	EXPECT_EQ(age, 3);
	EXPECT_FALSE(input.has_data_left());
}
