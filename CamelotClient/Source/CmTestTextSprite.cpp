#include "CmTestTextSprite.h"
#include "CmSceneObject.h"
#include "BsRenderable.h"
#include "CmMesh.h"
#include "CmVector2.h"
#include "BsTextSprite.h"
#include "CmFont.h"
#include "CmMaterial.h"
#include "BsGUILabel.h"
#include "BsGUIListBox.h"
#include "BsGUISkin.h"
#include "BsOverlayManager.h"
#include "BsSpriteTexture.h"
#include "BsEngineGUI.h"
#include "BsGUITexture.h"
#include "BsGUIArea.h"
#include "BsGUILayout.h"
#include "BsGUISpace.h"
#include "BsGUIViewport.h"
#include "BsGUIButton.h"
#include "BsCamera.h"
#include "CmInput.h"
#include "CmPlatform.h"
#include "BsGUIContent.h"
#include "CmStringTable.h"

using namespace BansheeEngine;

namespace CamelotFramework
{
	TestTextSprite::TestTextSprite(const HSceneObject& parent, CM::Viewport* target, CM::RenderWindow* ownerWindow)
		:GUIWidget(parent, target, ownerWindow)
	{
	}

	TestTextSprite::~TestTextSprite()
	{
	}

	void TestTextSprite::init(const HCamera& camera, const String& text, CM::RenderTexturePtr sceneView)
	{
		setSkin(EngineGUI::instance().getSkin());
		setDepth(128);

		GUIArea* area = GUIArea::createStretchedXY(*this, 0, 0, 0, 0);

		SpriteTexturePtr spriteTex = std::make_shared<SpriteTexture>(sceneView->getBindableColorTexture());
		area->getLayout().addElement(GUITexture::create(*this, GUILayoutOptions::fixed(800, 600), spriteTex));
		mLabel = GUILabel::create(*this, HString(L""));
		area->getLayout().addElement(mLabel);

		Vector<HString>::type dropDownElements;
		dropDownElements.push_back(HString(L"Ejlement #1"));
		dropDownElements.push_back(HString(L"Element #2"));
		dropDownElements.push_back(HString(L"Element #3"));
		area->getLayout().addElement(GUIListBox::create(*this, dropDownElements, GUILayoutOptions::fixed(50, 13)));

		GUIButton* button = GUIButton::create(*this, HString(L"dbgBtn"));
		button->onClick.connect(boost::bind(&TestTextSprite::dbgBtn, this));
		area->getLayout().addElement(button);

		area->getLayout().addFlexibleSpace();
	}

	void TestTextSprite::update()
	{
		WString value = toWString(toString(Input::instance().getCursorPosition().x) + " - " + toString(Input::instance().getCursorPosition().y));

		mLabel->setContent(GUIContent(HString(L"")));
	}

	void TestTextSprite::dbgBtn()
	{
		StringTable::instance().setString(L"dbgBtn", Language::Abkhazian, L"ALOALO");
		StringTable::instance().setActiveLanguage(Language::Abkhazian);
	}
}