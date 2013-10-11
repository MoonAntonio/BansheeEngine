#include "BsPrerequisites.h"
#include "BsGUIWidget.h"

namespace CamelotFramework
{
	class TestTextSprite : public BansheeEngine::GUIWidget
	{
	protected:
		friend class CM::SceneObject;

		TestTextSprite(const HSceneObject& parent, CM::Viewport* target, CM::RenderWindow* ownerWindow);
	public:
		~TestTextSprite();

		virtual void update();

		void init(const BS::HCamera& camera, const String& text, CM::RenderTexturePtr sceneView);

	private:
		BS::GUILabel* mLabel;

		void dbgBtn();
	};
}