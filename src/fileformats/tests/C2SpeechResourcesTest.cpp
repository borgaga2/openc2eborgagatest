#include "fileformats/C2SpeechResources.h"
#include "common/Exception.h"
#include <gtest/gtest.h>

TEST(C2SpeechResources, ReadsVerifiedTablesAndRejectsTruncatedOrUnknownLayouts) {
	std::vector<uint8_t> image(0xd0400);
	auto put = [&](size_t p, uint32_t v) { for(unsigned i=0;i<4;++i) image[p+i]=v>>(8*i); };
	put(0,0x5a4d); put(0x3c,0x80); put(0x80,0x4550);
	put(0x84,0x0001014c); put(0x94,0xe0); put(0x98,0x10b); put(0xb4,0x400000);
	put(0x184,0); put(0x188,0xd0000); put(0x18c,0x400);
	const uint8_t signature[]{0x51,0x56,0x8b,0x74,0x24,0x0c};
	std::copy(std::begin(signature),std::end(signature),image.begin()+0x225c0);
	for(unsigned i=0;i<72;++i) put(0xb2620+i*4,0x4b3000);
	image[0xb3400]='x';
	put(0x2020b,0x4b2220); put(0x209aa,0x4cd700);
	for(unsigned i=0;i<18;++i) {image[0xcdb00+i*8]='x'; image[0xcdb04+i*8]='y';}
	auto resources = readC2SpeechResources(image);
	EXPECT_EQ(resources.babble[11][5],"x");
	ASSERT_EQ(resources.pronunciation_rules.size(),18u);
	EXPECT_EQ(resources.pronunciation_rules[17].second,"y");
	image[0x225c0]=0;
	EXPECT_THROW(readC2SpeechResources(image),Exception);
	image[0x225c0]=0x51;
	image.resize(0xcdb02);
	EXPECT_THROW(readC2SpeechResources(image),Exception);
}
