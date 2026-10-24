#include "fileformats/sfc/C2BodyData.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCReader.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

TEST(C2BodyData, PreservesLinkedLimbsGalleryReferencesAndAnimationPadding) {
	auto body = std::make_shared<sfc::BodyV2>();
	auto limb = std::make_shared<sfc::LimbV2>();
	body->gallery = std::make_shared<sfc::CGalleryV1>();
	body->gallery->filename = "test";
	body->gallery->absolute_base = 0;
	body->gallery->refcount = 3;
	limb->gallery = body->gallery;
	limb->next = std::make_shared<sfc::LimbV2>();
	limb->next->gallery = body->gallery;
	body->x = 4940; body->y = 1112;
	body->angle = 2; body->view = 2;
	body->attachments[5][9] = {-5, 41};
	limb->angle = 5; limb->view = 9;
	limb->has_animation = 1;
	limb->animation_frame = 1;
	limb->animation[0] = '0'; limb->animation[1] = '1';
	limb->animation[98] = 0xa5;
	limb->next->attachments[9] = {1, 2, 3, 4};

	VectorWriter encoded;
	MFCWriter writer(encoded);
	writer.register_class<sfc::CGalleryV1>("CGallery", 2);
	sfc::register_c2_body_classes(writer);
	writer(body); writer(limb);
	SpanReader input(encoded.vector());
	MFCReader reader(input);
	reader.register_class<sfc::CGalleryV1>("CGallery", 2);
	sfc::register_c2_body_classes(reader);
	std::shared_ptr<sfc::BodyV2> decoded_body;
	std::shared_ptr<sfc::LimbV2> decoded_limb;
	reader(decoded_body); reader(decoded_limb);
	ASSERT_TRUE(decoded_body);
	ASSERT_TRUE(decoded_limb);
	ASSERT_TRUE(decoded_limb->next);
	EXPECT_EQ(decoded_body->gallery, decoded_limb->gallery);
	EXPECT_EQ(decoded_body->gallery, decoded_limb->next->gallery);
	EXPECT_EQ(decoded_body->attachments[5][9][0], -5);
	EXPECT_EQ(decoded_limb->animation[98], 0xa5);
	EXPECT_EQ(decoded_limb->next->attachments[9][3], 4);
	EXPECT_EQ(input.tell(), encoded.vector().size());
	VectorWriter reencoded;
	MFCWriter roundtrip_writer(reencoded);
	roundtrip_writer.register_class<sfc::CGalleryV1>("CGallery", 2);
	sfc::register_c2_body_classes(roundtrip_writer);
	roundtrip_writer(decoded_body); roundtrip_writer(decoded_limb);
	EXPECT_EQ(reencoded.vector(), encoded.vector());
}
