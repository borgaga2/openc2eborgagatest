#include "openc2e/Engine.h"
#include "openc2e/Agent.h"
#include "openc2e/CompoundPart.h"
#include "openc2e/World.h"
#include "common/backend/BackendEvent.h"
#include <gtest/gtest.h>

namespace {
class TextEngine : public Engine {
  public:
	using Engine::handleTextInput;
};
class TextPart : public CompoundPart {
  public:
	std::string received;
	TextPart(Agent* owner) : CompoundPart(owner,0,0,0,0) {}
	void partRender(RenderTarget*,int,int) override {}
	unsigned int getWidth() override { return 0; }
	unsigned int getHeight() override { return 0; }
	bool canGainFocus() override { return true; }
	void handleTranslatedChar(unsigned char c) override { received += static_cast<char>(c); }
};
class TextAgent : public Agent {
  public:
	TextPart editor;
	TextAgent() : Agent(2,1,1,0), editor(this) {}
	CompoundPart* part(unsigned int) override { return &editor; }
};
class TextInputEnvironment {
	AgentRef old_focus = world.focusagent;
	unsigned old_part = world.focuspart;
  public:
	TextEngine engine;
	std::shared_ptr<TextAgent> agent = std::make_shared<TextAgent>();
	TextInputEnvironment() { engine.version=2; world.focusagent=agent.get(); world.focuspart=0; }
	~TextInputEnvironment() { world.focusagent=old_focus; world.focuspart=old_part; }
	void input(std::string text) {
		BackendEvent event{}; event.type=eventtextinput; event.text=std::move(text);
		engine.handleTextInput(event);
	}
};
}

TEST(EngineTextInput, DeliversEveryCharacterFromOneBackendEventInOrder) {
	TextInputEnvironment env;
	env.input("push food");
	EXPECT_EQ(env.agent->editor.received,"push food");
}

TEST(EngineTextInput, EmptyAndUnsupportedInputDoNotInsertNulCharacters) {
	TextInputEnvironment env;
	env.input("");
	env.input("\xf0\x9f\x98\x80");
	EXPECT_TRUE(env.agent->editor.received.empty());
	env.input("eat");
	EXPECT_EQ(env.agent->editor.received,"eat");
}

TEST(EngineTextInput, ConvertsTheWholeEventIncludingAccentsAndCombiningMarks) {
	TextInputEnvironment env;
	env.input("caf\xc3\xa9 e\xcc\x81");
	EXPECT_EQ(env.agent->editor.received,"caf\xe9 \xe9");
}
